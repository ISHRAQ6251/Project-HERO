#include "web_server.h"
#include "config.h"
#include "settings.h"
#include <string.h>
#include "motor_control.h"
#include "hero_eyes.h"
#include "display_manager.h"
#include "wifi_helper.h"
#include "camera_server.h"
#include "web_assets.h"
#include "buzzer.h"
#include <limits.h>
#include <stdlib.h>

WebServerMgr webServerMgr;

static bool authorized(AsyncWebServerRequest* request) {
    const String& h = request->header("X-Auth-Token");
    if (h.length() > 0 && h == settings.data.authToken) return true;
    if (request->hasParam("token")) {
        return request->getParam("token")->value() == settings.data.authToken;
    }
    return false;
}

static bool hasValueParam(AsyncWebServerRequest* request, const char* name) {
    return request->hasParam(name, true) || request->hasParam(name);
}

static void unauthorized(AsyncWebServerRequest* request) {
    request->send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
}

static bool queryInt(AsyncWebServerRequest* request, const char* name, int fallback, int& value) {
    const AsyncWebParameter* param = nullptr;
    if (request->hasParam(name, true)) param = request->getParam(name, true);
    else if (request->hasParam(name)) param = request->getParam(name);
    if (!param) {
        value = fallback;
        return true;
    }

    String textValue = param->value();
    const char* text = textValue.c_str();
    char* end = nullptr;
    long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) return false;
    value = (int)parsed;
    return true;
}

static bool queryBool(AsyncWebServerRequest* request, const char* name, bool fallback, bool& value) {
    if (!hasValueParam(request, name)) {
        value = fallback;
        return true;
    }
    const AsyncWebParameter* param = request->hasParam(name, true)
        ? request->getParam(name, true)
        : request->getParam(name);
    const String& text = param->value();
    if (text == "1") {
        value = true;
        return true;
    }
    if (text == "0") {
        value = false;
        return true;
    }
    return false;
}

static HeroMood parseMood(const String& s) {
    if (s == "happy") return HeroMood::HAPPY;
    if (s == "angry") return HeroMood::ANGRY;
    if (s == "shock") return HeroMood::SHOCK;
    if (s == "dead") return HeroMood::DEAD;
    if (s == "sleepy") return HeroMood::SLEEPY;
    if (s == "wink") return HeroMood::WINK;
    return HeroMood::IDLE;
}

// Escapes s into dst (quotes/backslashes preserved, control chars -> space).
// Bounded write, no heap: /api/state is polled by every client on a timer.
static void jsonEscape(char* dst, size_t len, const char* s) {
    if (!dst || len == 0) return;
    size_t o = 0;
    for (const char* p = s; p && *p; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            if (o + 2 >= len) break;
            dst[o++] = '\\';
        } else if (c < 0x20) {
            c = ' ';
        }
        if (o + 1 >= len) break;
        dst[o++] = c;
    }
    dst[o] = 0;
}

static void sendState(AsyncWebServerRequest* request) {
    char ip[16];
    wifiHelper.ip(ip, sizeof(ip));
    char message[140];
    jsonEscape(message, sizeof(message), displayMgr.message());
    char buf[640];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"throttle\":%d,\"steering\":%d,\"mood\":\"%s\","
             "\"sleeping\":%s,\"flashlightOn\":%s,\"oledAnim\":%s,"
              "\"camResolution\":%d,\"camQuality\":%d,\"camFlip\":%s,\"camRunning\":%s,"
              "\"reverseLeft\":%s,\"reverseRight\":%s,\"trimLeft\":%u,\"trimRight\":%u,"
              "\"soundEnabled\":%s,\"defaultToken\":%s,"
              "\"connected\":true,\"apMode\":true,\"ip\":\"%s\","
              "\"messageActive\":%s,\"message\":\"%s\"}",
             (int)motors.throttle(), (int)motors.steering(), eyes.moodName(),
             displayMgr.sleeping() ? "true" : "false",
             settings.data.flashlightOn ? "true" : "false",
             settings.data.oledAnim ? "true" : "false",
             settings.data.camResolution, settings.data.camQuality,
             settings.data.camFlip ? "true" : "false",
             cameraServer.wanted() ? "true" : "false",
             settings.data.reverseLeft ? "true" : "false",
             settings.data.reverseRight ? "true" : "false",
              (unsigned)settings.data.trimLeft, (unsigned)settings.data.trimRight,
              settings.data.soundEnabled ? "true" : "false",
              (strcmp(settings.data.authToken, AUTH_DEFAULT_TOKEN) == 0) ? "true" : "false",
              ip,
             displayMgr.messageActive() ? "true" : "false",
             message);
    // Copy into a String so the async response owns the bytes (buf is stack).
    request->send(200, "application/json", String(buf));
}

// Serial-free bring-up test: GET /api/pintest?token=hero&pin=1&speed=768
// drives one motor input (0=L_IN1/GPIO47, 1=L_IN2/GPIO14, 2=R_IN3/GPIO21,
// 3=R_IN4/GPIO42) for MOTOR_TEST_MS. `duty` in the reply is the PWM value read
// back from the pin: a non-zero value means the pin is attached and driven, so
// a wheel that still does not turn points at wiring/DRV8833 rather than code.
static void handlePinTest(AsyncWebServerRequest* request) {
    if (!authorized(request)) return unauthorized(request);
    int index;
    int value;
    if (!queryInt(request, "pin", -1, index) ||
        !queryInt(request, "speed", (int)MOTOR_PWM_MAX * 3 / 4, value) ||
        value < -(int)MOTOR_PWM_MAX || value > (int)MOTOR_PWM_MAX) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"pin must be 0..3 and speed within PWM range\"}");
        return;
    }
    if (index < 0 || index > 3) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"pin must be 0..3\"}");
        return;
    }
    uint32_t duty = motors.testPin((uint8_t)index, (int16_t)value);
    String s = "{\"ok\":true,\"pin\":" + String(index) + ",\"duty\":" + String(duty) + "}";
    request->send(200, "application/json", s);
}

void WebServerMgr::begin() {
    pinMode(PIN_FLASH_LED, OUTPUT);
    digitalWrite(PIN_FLASH_LED, settings.data.flashlightOn ? HIGH : LOW);

    // ---------- static assets ----------
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/css; charset=utf-8", STYLE_CSS);
    });
    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "application/javascript; charset=utf-8", APP_JS);
    });

    // ---------- camera ----------
    cameraServer.setupHandlers(_server);

    // ---------- open endpoints ----------
    _server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* request) {
        char ip[16];
        wifiHelper.ip(ip, sizeof(ip));
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"apMode\":true,\"connected\":true,"
                 "\"ip\":\"%s\",\"hostname\":\"" HOSTNAME "\"}",
                 ip);
        request->send(200, "application/json", String(buf));
    });

    _server.on("/api/auth", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (request->hasParam("token", true) &&
            request->getParam("token", true)->value() == settings.data.authToken) {
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(401, "application/json", "{\"ok\":false}");
        }
    });

    // ---------- protected endpoints ----------
    _server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        sendState(request);
    });

    _server.on("/api/drive", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        int throttleValue;
        int steeringValue;
        if (!queryInt(request, "throttle", 0, throttleValue) ||
            !queryInt(request, "steering", 0, steeringValue)) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"drive values must be integers\"}");
            return;
        }
        int16_t throttle = (int16_t)constrain(throttleValue, -(int)MOTOR_PWM_MAX, (int)MOTOR_PWM_MAX);
        int16_t steering = (int16_t)constrain(steeringValue, -(int)MOTOR_PWM_MAX, (int)MOTOR_PWM_MAX);
        motors.drive(throttle, steering);
        eyes.setDriving(throttle, steering);
        displayMgr.notifyActivity();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        motors.stop();
        eyes.setDriving(0, 0);
        displayMgr.notifyActivity();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // Bring-up motor test, invokable from a phone browser (serial unusable on
    // battery): /api/pintest?token=hero&pin=1&speed=768
    _server.on("/api/pintest", HTTP_GET, handlePinTest);
    _server.on("/api/pintest", HTTP_POST, handlePinTest);

    _server.on("/api/mood", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        String m = request->hasParam("mood", true) ? request->getParam("mood", true)->value() : "idle";
        displayMgr.setMood(parseMood(m));
        request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/message", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        // empty text == clear the persistent message box
        if (request->hasParam("text", true) && request->getParam("text", true)->value().length() > 0) {
            String text = request->getParam("text", true)->value();
            if (text.length() > 63) text = text.substring(0, 63);
            displayMgr.showMessage(text.c_str());
        } else {
            displayMgr.clearMessage();
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/flash", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        bool on = request->hasParam("on", true) && request->getParam("on", true)->value() == "1";
        settings.setFlashlight(on);
        digitalWrite(PIN_FLASH_LED, on ? HIGH : LOW);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        bool oledAnim = settings.data.oledAnim;
        bool soundEnabled = settings.data.soundEnabled;
        bool reverseLeft = settings.data.reverseLeft;
        bool reverseRight = settings.data.reverseRight;
        int trimLeft = settings.data.trimLeft;
        int trimRight = settings.data.trimRight;
        if (!queryBool(request, "oledAnim", oledAnim, oledAnim) ||
            !queryBool(request, "soundEnabled", soundEnabled, soundEnabled) ||
            !queryBool(request, "reverseLeft", reverseLeft, reverseLeft) ||
            !queryBool(request, "reverseRight", reverseRight, reverseRight) ||
            !queryInt(request, "trimLeft", trimLeft, trimLeft) ||
            !queryInt(request, "trimRight", trimRight, trimRight) ||
            (hasValueParam(request, "trimLeft") &&
             (trimLeft < MOTOR_TRIM_MIN || trimLeft > MOTOR_TRIM_MAX)) ||
            (hasValueParam(request, "trimRight") &&
             (trimRight < MOTOR_TRIM_MIN || trimRight > MOTOR_TRIM_MAX))) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid settings value\"}");
            return;
        }
        if (hasValueParam(request, "oledAnim")) {
            settings.setOledAnim(oledAnim);
            eyes.setAnimEnabled(settings.data.oledAnim);
        }
        if (hasValueParam(request, "soundEnabled")) {
            settings.setSoundEnabled(soundEnabled);
            if (!settings.data.soundEnabled) buzzer.stop();
        }
        bool motorSave = hasValueParam(request, "reverseLeft") || hasValueParam(request, "reverseRight") ||
            hasValueParam(request, "trimLeft") || hasValueParam(request, "trimRight");
        if (motorSave) {
            settings.setMotorCal(reverseLeft, reverseRight, (uint8_t)trimLeft, (uint8_t)trimRight);
            motors.reapply();
        }
        if (settings.data.soundEnabled) buzzer.play(BuzzerCue::SETTINGS);
        request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/camera", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        if (request->hasParam("on", true)) {
            bool on = request->getParam("on", true)->value() == "1";
            if (on) cameraServer.start();
            else cameraServer.stop();
            request->send(200, "application/json",
                          cameraServer.wanted()
                              ? "{\"ok\":true,\"camRunning\":true}"
                              : "{\"ok\":true,\"camRunning\":false}");
            return;
        }
        if (request->hasParam("flip", true)) {
            settings.setCamFlip(request->getParam("flip", true)->value() == "1");
            cameraServer.applySettings();
            request->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad param\"}");
    });

    _server.on("/api/token", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authorized(request)) return unauthorized(request);
        String t = request->hasParam("token", true) ? request->getParam("token", true)->value() : "";
        if (t.length() >= 4 && t.length() <= sizeof(settings.data.authToken) - 1) {
            settings.setToken(t.c_str());
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"token must be 4..32 bytes\"}");
        }
    });

    _server.begin();
}
