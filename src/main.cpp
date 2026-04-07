#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArtnetWifi.h>
#include <esp_wifi.h>
#include "runtime_config.h"
#include "led_controller.h"
#include "status_led.h"
#include "web_config.h"
#include "espnow_mesh.h"
#include "pattern_engine.h"
#include "hub75_controller.h"

// ---- Global objects ----
RuntimeConfig  cfg;
LedController  leds;
#if ENABLE_HUB75
Hub75Controller hub75;
#endif
StatusLed      statusLed;
ArtnetWifi     artnet;
EspNowMesh     mesh;
PatternEngine  patterns;
WebConfig*     webConfig = nullptr;

NodeMode       activeMode = NodeMode::AUTO;
uint32_t       lastFrameMs = 0;

extern void webConfigCountFrame();

// ---- Helpers ----

static void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
    webConfigCountFrame();
    statusLed.setState(NodeState::DMX_ACTIVE);
    lastFrameMs = millis();
    leds.handleUniverse((uint8_t)universe, data, length, cfg);
    leds.show();
#if ENABLE_HUB75
    hub75.handleUniverse((uint8_t)universe, data, length);
    hub75.show();
#endif

    if (activeMode == NodeMode::BRIDGE || activeMode == NodeMode::AUTO) {
        mesh.broadcastUniverse((uint8_t)universe, data, length);
    }
}

static void initWifi() {
    WiFi.setHostname(cfg.hostname);
    WiFi.begin(cfg.ssid, cfg.password);
}

static bool waitForWifi(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) return false;
        statusLed.tick();
        delay(250);
        Serial.print(".");
    }
    return true;
}

static void startNetworkServices() {
    Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

    if (MDNS.begin(cfg.hostname)) {
        Serial.printf("mDNS: http://%s.local\n", cfg.hostname);
    }

    ArduinoOTA.setHostname(cfg.hostname);
    ArduinoOTA.onStart([]()  { Serial.println("OTA start"); });
    ArduinoOTA.onEnd([]()    { Serial.println("\nOTA done"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
    ArduinoOTA.begin();

    webConfig = new WebConfig(cfg);
    webConfig->begin();
    Serial.printf("Web config: http://%s.local\n", cfg.hostname);
}

// ---- Setup ----

void setup() {
    Serial.begin(115200);
    Serial.printf("\n[artnode] boot\n");

    statusLed.begin();
    statusLed.setState(NodeState::WIFI_CONNECTING);

    loadConfig(cfg);
    leds.begin(cfg);
#if ENABLE_HUB75
    // For HUB75 panels, override spatial to panel mode so patterns render in 2D.
    cfg.spatial.panel_w = HUB75_W * HUB75_CHAIN;
    cfg.spatial.virt_w  = HUB75_W * HUB75_CHAIN;
    cfg.spatial.virt_h  = HUB75_H;
    hub75.begin(cfg);
#endif
    patterns.setSpatial(cfg.spatial);

    NodeMode requestedMode = cfg.node_mode;

    // ---- STANDALONE: no network at all ----
    if (requestedMode == NodeMode::STANDALONE) {
        activeMode = NodeMode::STANDALONE;
        Serial.println("[artnode] mode: STANDALONE");
        return;
    }

    // ---- MESH: ESP-NOW only, skip WiFi ----
    if (requestedMode == NodeMode::MESH) {
        activeMode = NodeMode::MESH;
        Serial.println("[artnode] mode: MESH (ESP-NOW slave)");
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        esp_wifi_set_channel(MESH_CHANNEL, WIFI_SECOND_CHAN_NONE);
        mesh.begin();
        mesh.onDmx([](uint8_t universe, uint8_t* data, uint16_t len) {
            lastFrameMs = millis();
            statusLed.setState(NodeState::DMX_ACTIVE);
            leds.handleUniverse(universe, data, len, cfg);
            leds.show();
#if ENABLE_HUB75
            hub75.handleUniverse(universe, data, len);
            hub75.show();
#endif
        });
        mesh.onPattern([](uint8_t p, uint8_t p1, uint8_t p2, uint32_t t) {
            // Adopt bridge's pattern + time so spatial patterns stay in sync
            patterns.setPattern((Pattern)p, p1, p2);
            patterns.setTime(t);
        });
        statusLed.setState(NodeState::WIFI_CONNECTED);
        return;
    }

    // ---- All other modes: try WiFi first ----
    WiFi.mode(WIFI_STA);
    initWifi();
    Serial.printf("Connecting to %s", cfg.ssid);

    bool wifi_ok = waitForWifi(
        (requestedMode == NodeMode::AUTO) ? WIFI_TIMEOUT_MS : 30000
    );

    if (!wifi_ok) {
        if (requestedMode == NodeMode::AUTO) {
            // Fall back to mesh slave
            activeMode = NodeMode::MESH;
            Serial.println("\n[artnode] WiFi timeout — falling back to MESH mode");
            WiFi.disconnect();
            esp_wifi_set_channel(MESH_CHANNEL, WIFI_SECOND_CHAN_NONE);
            mesh.begin();
            mesh.onDmx([](uint8_t universe, uint8_t* data, uint16_t len) {
                lastFrameMs = millis();
                statusLed.setState(NodeState::DMX_ACTIVE);
                leds.handleUniverse(universe, data, len, cfg);
                leds.show();
#if ENABLE_HUB75
                hub75.handleUniverse(universe, data, len);
                hub75.show();
#endif
            });
            mesh.onPattern([](uint8_t p, uint8_t p1, uint8_t p2, uint32_t t) {
                patterns.setPattern((Pattern)p, p1, p2);
                patterns.setTime(t);
            });
            statusLed.setState(NodeState::WIFI_CONNECTED);
        } else {
            // DIRECT or BRIDGE forced — keep retrying in loop()
            Serial.println("\n[artnode] WiFi not connected (will retry)");
        }
        return;
    }

    // WiFi connected
    statusLed.setState(NodeState::WIFI_CONNECTED);
    startNetworkServices();

    if (requestedMode == NodeMode::DIRECT) {
        activeMode = NodeMode::DIRECT;
        Serial.println("[artnode] mode: DIRECT");
    } else {
        // AUTO or BRIDGE: enable mesh forwarding
        activeMode = NodeMode::BRIDGE;
        Serial.println("[artnode] mode: BRIDGE");
        mesh.begin();
    }

    artnet.begin();
    artnet.setArtDmxCallback(onDmxFrame);
}

// ---- Loop ----

void loop() {
    if (webConfig) {
        ArduinoOTA.handle();
        webConfig->handle();
    }

    if (activeMode == NodeMode::DIRECT || activeMode == NodeMode::BRIDGE) {
        artnet.read();
    }

    // Pattern engine: always runs in STANDALONE, otherwise kicks in after idle timeout
    bool runPatterns = (activeMode == NodeMode::STANDALONE) ||
                       (millis() - lastFrameMs > IDLE_TIMEOUT_MS);
    if (runPatterns) {
        for (int i = 0; i < NUM_STRIPS; i++) {
            if (patterns.tick(leds.getLeds(i), cfg.strips[i].num_leds)) {
                leds.show();
            }
        }
#if ENABLE_HUB75
        if (patterns.tick(hub75.getLeds(), HUB75_TOTAL_LEDS)) {
            hub75.show();
        }
#endif

        // Bridge: broadcast pattern state every 500 ms so mesh slaves stay in sync
        if (activeMode == NodeMode::BRIDGE || activeMode == NodeMode::AUTO) {
            static uint32_t lastSyncMs = 0;
            if (millis() - lastSyncMs > 500) {
                lastSyncMs = millis();
                mesh.broadcastPattern(
                    (uint8_t)patterns.getPattern(),
                    patterns.getParam1(),
                    patterns.getParam2(),
                    patterns.getTime()
                );
            }
        }
    }

    statusLed.tick();
}
