#pragma once

#include <Arduino.h>

// Non-blocking passive-buzzer cues. Sequences are advanced from update()
// in loop(); play() never delay()s.

enum class BuzzerCue : uint8_t {
    BOOT = 0,
    AP_CONNECT,
    CAM_ON,
    CAM_OFF,
    HAPPY,
    ANGRY,
    SHOCK,
    DEAD,
    SLEEPY,
    WINK,
    WAKE,
    MESSAGE,
    SETTINGS,
    WATCHDOG
};

class Buzzer {
public:
    struct Step {
        uint16_t hz;
        uint16_t ms;
    };

    void begin();
    void play(BuzzerCue cue);
    void update();
    void stop();

private:
    void applyStep();
    void silence();
    void stopLocked();
    const Step* sequence(BuzzerCue cue) const;

    void lock();
    void unlock();

    const Step* _seq = nullptr;
    uint8_t _index = 0;
    uint32_t _stepUntil = 0;
    bool _playing = false;
    SemaphoreHandle_t _mux = nullptr;
};

extern Buzzer buzzer;
