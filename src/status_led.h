#pragma once
#include <Arduino.h>

// Built-in LED on most ESP32 devkits
#define STATUS_LED_PIN 2

enum class NodeState {
    WIFI_CONNECTING,   // fast blink
    WIFI_CONNECTED,    // slow heartbeat
    DMX_ACTIVE,        // brief flash
};

class StatusLed {
public:
    void begin();
    void setState(NodeState s);
    void tick();   // call from loop()

private:
    NodeState _state = NodeState::WIFI_CONNECTING;
    uint32_t  _last  = 0;
    bool      _on    = false;
    int       _phase = 0;
};
