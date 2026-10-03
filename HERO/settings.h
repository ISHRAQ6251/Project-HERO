#pragma once

#include <Arduino.h>
#include <esp_camera.h>

struct RoverSettings {
    char authToken[33] = {0};

    int camResolution = (int)FRAMESIZE_VGA; // framesize_t enum value: VGA (640x480)
    int camQuality = 12;      // JPEG quality 10..63 (lower = better)
    bool camFlip = false;     // vflip only (upside-down mount); hmirror is always on

    bool flashlightOn = false;
    bool oledAnim = true;
    bool reverseLeft = true;   // this build's left motor is wired inverted
    bool reverseRight = true;  // this build's right motor is wired inverted
    uint8_t trimLeft = 100;
    uint8_t trimRight = 100;
    bool soundEnabled = true;
};

class Settings {
public:
    void begin();
    void load();
    void save();
    void reset();

    RoverSettings data;

    // convenience accessors used by the rest of the firmware
    void setToken(const char* token);
    void setCamFlip(bool flip);
    void setFlashlight(bool on);
    void setOledAnim(bool on);
    void setMotorCal(bool reverseLeft, bool reverseRight, uint8_t trimLeft, uint8_t trimRight);
    void setSoundEnabled(bool on);

private:
    void writeString(const char* key, const char* value);
    void readString(const char* key, char* dst, size_t len, const char* def);
};

extern Settings settings;
