#include "wifi_helper.h"
#include "config.h"
#include "buzzer.h"
#include <ESPmDNS.h>

WifiHelper wifiHelper;

static void onWifiEvent(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        buzzer.play(BuzzerCue::AP_CONNECT);
    }
}

void WifiHelper::begin() {
    WiFi.mode(WIFI_AP);
    WiFi.setHostname(HOSTNAME);
    WiFi.onEvent(onWifiEvent);
    if (!WiFi.softAP(AP_SSID, AP_PASS, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS)) {
        Serial.println("wifi: SoftAP start failed");
        return;
    }
    if (!WiFi.setTxPower(WIFI_POWER_20dBm)) {
        Serial.println("wifi: failed to request maximum 20 dBm TX power");
    } else {
        Serial.println("wifi: requested maximum 20 dBm TX power (regulatory/PHY limits apply)");
    }
    delay(200);

    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", WiFi.softAPIP());

    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
    }
}

void WifiHelper::update() {
    _dns.processNextRequest();
}

void WifiHelper::ip(char* dst, size_t len) const {
    if (!dst || len == 0) return;
    IPAddress addr = WiFi.softAPIP();
    snprintf(dst, len, "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
}
