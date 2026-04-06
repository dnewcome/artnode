#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArtnetWifi.h>
#include "runtime_config.h"
#include "led_controller.h"
#include "status_led.h"
#include "web_config.h"

RuntimeConfig cfg;
LedController leds;
StatusLed     statusLed;
ArtnetWifi    artnet;
WebConfig*    webConfig = nullptr;

extern void webConfigCountFrame();

void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
    webConfigCountFrame();
    statusLed.setState(NodeState::DMX_ACTIVE);
    leds.handleUniverse((uint8_t)universe, data, length, cfg);
    leds.show();
}

void setup() {
    Serial.begin(115200);

    statusLed.begin();
    statusLed.setState(NodeState::WIFI_CONNECTING);

    loadConfig(cfg);

    WiFi.setHostname(cfg.hostname);
    WiFi.begin(cfg.ssid, cfg.password);
    Serial.printf("Connecting to %s", cfg.ssid);
    while (WiFi.status() != WL_CONNECTED) {
        statusLed.tick();
        delay(250);
        Serial.print(".");
    }
    Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
    statusLed.setState(NodeState::WIFI_CONNECTED);

    // mDNS — accessible as hostname.local
    if (MDNS.begin(cfg.hostname)) {
        Serial.printf("mDNS: http://%s.local\n", cfg.hostname);
    }

    // OTA — flash via: pio run -t upload --upload-port hostname.local
    ArduinoOTA.setHostname(cfg.hostname);
    ArduinoOTA.onStart([]()  { Serial.println("OTA start"); });
    ArduinoOTA.onEnd([]()    { Serial.println("\nOTA done"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
    ArduinoOTA.begin();

    leds.begin(cfg);

    webConfig = new WebConfig(cfg);
    webConfig->begin();
    Serial.printf("Web config: http://%s.local\n", cfg.hostname);

    artnet.begin();
    artnet.setArtDmxCallback(onDmxFrame);
}

void loop() {
    ArduinoOTA.handle();
    webConfig->handle();
    artnet.read();
    statusLed.tick();
}
