#include "runtime_config.h"
#include <Preferences.h>

static Preferences prefs;

void loadConfig(RuntimeConfig& cfg) {
    prefs.begin("artnode", true);

    strlcpy(cfg.ssid,     prefs.getString("ssid",     WIFI_SSID).c_str(),     sizeof(cfg.ssid));
    strlcpy(cfg.password, prefs.getString("pass",     WIFI_PASSWORD).c_str(), sizeof(cfg.password));
    strlcpy(cfg.hostname, prefs.getString("hostname", "artnode").c_str(),      sizeof(cfg.hostname));
    cfg.brightness  = prefs.getUChar("brightness", 200);
    cfg.node_mode   = (NodeMode)prefs.getUChar("node_mode", (uint8_t)NodeMode::AUTO);

    for (int i = 0; i < NUM_STRIPS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "s%d_leds", i);
        cfg.strips[i].num_leds = prefs.getUShort(key, STRIPS[i].num_leds);

        snprintf(key, sizeof(key), "s%d_uni", i);
        cfg.strips[i].start_universe = prefs.getUChar(key, STRIPS[i].start_universe);

        snprintf(key, sizeof(key), "s%d_off", i);
        cfg.strips[i].channel_offset = prefs.getUShort(key, STRIPS[i].channel_offset);
    }

    prefs.end();
}

void saveConfig(const RuntimeConfig& cfg) {
    prefs.begin("artnode", false);

    prefs.putString("ssid",       cfg.ssid);
    prefs.putString("pass",       cfg.password);
    prefs.putString("hostname",   cfg.hostname);
    prefs.putUChar("brightness",  cfg.brightness);
    prefs.putUChar("node_mode",   (uint8_t)cfg.node_mode);

    for (int i = 0; i < NUM_STRIPS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "s%d_leds", i);
        prefs.putUShort(key, cfg.strips[i].num_leds);

        snprintf(key, sizeof(key), "s%d_uni", i);
        prefs.putUChar(key, cfg.strips[i].start_universe);

        snprintf(key, sizeof(key), "s%d_off", i);
        prefs.putUShort(key, cfg.strips[i].channel_offset);
    }

    prefs.end();
}
