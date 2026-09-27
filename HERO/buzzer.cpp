#include "buzzer.h"
#include "config.h"
#include "settings.h"

Buzzer buzzer;

// Terminator is {0, 0}. {0, n} with n > 0 is a rest.

static const Buzzer::Step kBoot[]     = {{523, 80}, {659, 80}, {784, 140}, {0, 0}};
static const Buzzer::Step kCamOn[]    = {{1200, 50}, {0, 0}};
static const Buzzer::Step kCamOff[]   = {{400, 70}, {0, 0}};
static const Buzzer::Step kHappy[]    = {{784, 70}, {988, 110}, {0, 0}};
static const Buzzer::Step kAngry[]    = {{175, 50}, {0, 20}, {175, 50}, {0, 0}};
static const Buzzer::Step kCurious[]  = {{880, 40}, {1320, 80}, {0, 0}};
static const Buzzer::Step kDead[]     = {{698, 60}, {523, 60}, {392, 70}, {262, 120}, {0, 0}};
static const Buzzer::Step kSleepy[]   = {{330, 120}, {262, 180}, {0, 0}};
static const Buzzer::Step kWink[]     = {{1400, 30}, {0, 0}};
static const Buzzer::Step kWake[]     = {{392, 80}, {523, 120}, {0, 0}};
static const Buzzer::Step kMessage[]  = {{880, 70}, {1175, 110}, {0, 0}};
static const Buzzer::Step kSettings[] = {{1000, 25}, {0, 0}};
static const Buzzer::Step kWatchdog[] = {{250, 50}, {0, 40}, {200, 70}, {0, 0}};

void Buzzer::lock() {
    if (!_mux) _mux = xSemaphoreCreateMutex();
    if (_mux) xSemaphoreTake(_mux, portMAX_DELAY);
}

void Buzzer::unlock() {
    if (_mux) xSemaphoreGive(_mux);
}

void Buzzer::begin() {
    lock();
    unlock();
    ledcAttachChannel(PIN_BUZZER, 2000, 8, BUZZER_LEDC_CH);
    silence();
}

void Buzzer::play(BuzzerCue cue) {
    if (!settings.data.soundEnabled) return;
    const Step* seq = sequence(cue);
    if (!seq) return;
    lock();
    _seq = seq;
    _index = 0;
    _playing = true;
    applyStep();
    unlock();
}

void Buzzer::stopLocked() {
    _playing = false;
    _seq = nullptr;
    _index = 0;
    _stepUntil = 0;
    silence();
}

void Buzzer::stop() {
    lock();
    stopLocked();
    unlock();
}

void Buzzer::update() {
    if (!settings.data.soundEnabled) {
        if (_playing) stop();
        return;
    }
    lock();
    if (!_playing || !_seq) {
        unlock();
        return;
    }
    if ((int32_t)(millis() - _stepUntil) < 0) {
        unlock();
        return;
    }
    _index++;
    applyStep();
    unlock();
}

void Buzzer::applyStep() {
    if (!_seq) {
        stopLocked();
        return;
    }
    const Step& s = _seq[_index];
    if (s.hz == 0 && s.ms == 0) {
        stopLocked();
        return;
    }
    if (s.hz == 0) silence();
    else ledcWriteTone(PIN_BUZZER, s.hz);
    _stepUntil = millis() + s.ms;
}

void Buzzer::silence() {
    ledcWriteTone(PIN_BUZZER, 0);
    ledcWrite(PIN_BUZZER, 0);
}

const Buzzer::Step* Buzzer::sequence(BuzzerCue cue) const {
    switch (cue) {
        case BuzzerCue::BOOT:      return kBoot;
        case BuzzerCue::CAM_ON:    return kCamOn;
        case BuzzerCue::CAM_OFF:   return kCamOff;
        case BuzzerCue::HAPPY:     return kHappy;
        case BuzzerCue::ANGRY:     return kAngry;
        case BuzzerCue::CURIOUS:   return kCurious;
        case BuzzerCue::DEAD:      return kDead;
        case BuzzerCue::SLEEPY:    return kSleepy;
        case BuzzerCue::WINK:      return kWink;
        case BuzzerCue::WAKE:      return kWake;
        case BuzzerCue::MESSAGE:   return kMessage;
        case BuzzerCue::SETTINGS:  return kSettings;
        case BuzzerCue::WATCHDOG:  return kWatchdog;
    }
    return nullptr;
}
