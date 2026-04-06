#include "status_led.h"

void StatusLed::begin() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
}

void StatusLed::setState(NodeState s) {
    _state = s;
    _phase = 0;
    if (s == NodeState::DMX_ACTIVE) {
        _on   = true;
        _last = millis();
    }
}

void StatusLed::tick() {
    uint32_t now = millis();

    switch (_state) {
        case NodeState::WIFI_CONNECTING: {
            // 100ms on / 100ms off
            if (now - _last >= 100) {
                _on = !_on;
                _last = now;
            }
            break;
        }
        case NodeState::WIFI_CONNECTED: {
            // 50ms flash, then 2950ms off (once per 3s)
            uint32_t cycle = (now % 3000);
            _on = (cycle < 50);
            break;
        }
        case NodeState::DMX_ACTIVE: {
            // 30ms flash triggered externally via setState
            if (_on && now - _last >= 30) {
                _on = false;
            }
            break;
        }
    }

    digitalWrite(STATUS_LED_PIN, _on ? HIGH : LOW);
}

// Call this each time a DMX frame arrives to trigger the flash
void dmxFlash(StatusLed& led) {
    led.setState(NodeState::DMX_ACTIVE);
}
