#include "runtime_config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>

using json = nlohmann::json;

#ifndef ARTNODE_CONFIG_PATH
#define ARTNODE_CONFIG_PATH "/etc/artnode/config.json"
#endif
static const char* CONFIG_PATH = ARTNODE_CONFIG_PATH;

static void applyDefaults(RuntimeConfig& cfg) {
    snprintf(cfg.hostname, sizeof(cfg.hostname), "artnode");
    cfg.brightness = 200;
    cfg.node_mode  = NodeMode::DIRECT;
    for (int i = 0; i < NUM_STRIPS; i++) {
        cfg.strips[i].num_leds       = STRIPS[i].num_leds;
        cfg.strips[i].start_universe = STRIPS[i].start_universe;
        cfg.strips[i].channel_offset = STRIPS[i].channel_offset;
    }
    cfg.spatial = DEFAULT_SPATIAL;
}

void loadConfig(RuntimeConfig& cfg) {
    applyDefaults(cfg);

    std::ifstream f(CONFIG_PATH);
    if (!f.is_open()) return;

    try {
        auto j = json::parse(f);

        if (j.contains("hostname")) {
            auto s = j["hostname"].get<std::string>();
            snprintf(cfg.hostname, sizeof(cfg.hostname), "%s", s.c_str());
        }
        if (j.contains("brightness")) cfg.brightness = j["brightness"].get<uint8_t>();
        if (j.contains("node_mode"))  cfg.node_mode  = (NodeMode)j["node_mode"].get<int>();

        if (j.contains("strips") && j["strips"].is_array()) {
            auto& arr = j["strips"];
            for (int i = 0; i < NUM_STRIPS && i < (int)arr.size(); i++) {
                auto& s = arr[i];
                if (s.contains("num_leds"))       cfg.strips[i].num_leds       = s["num_leds"];
                if (s.contains("start_universe")) cfg.strips[i].start_universe = s["start_universe"];
                if (s.contains("channel_offset")) cfg.strips[i].channel_offset = s["channel_offset"];
            }
        }

        if (j.contains("spatial") && j["spatial"].is_object()) {
            auto& sp = j["spatial"];
            if (sp.contains("virt_w"))   cfg.spatial.virt_w   = sp["virt_w"];
            if (sp.contains("virt_h"))   cfg.spatial.virt_h   = sp["virt_h"];
            if (sp.contains("origin_x")) cfg.spatial.origin_x = sp["origin_x"];
            if (sp.contains("origin_y")) cfg.spatial.origin_y = sp["origin_y"];
            if (sp.contains("step_x"))   cfg.spatial.step_x   = sp["step_x"];
            if (sp.contains("step_y"))   cfg.spatial.step_y   = sp["step_y"];
            if (sp.contains("panel_w"))  cfg.spatial.panel_w  = sp["panel_w"];
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[artnode] config parse error: %s — using defaults\n", e.what());
    }
}

void saveConfig(const RuntimeConfig& cfg) {
    // Ensure the config directory exists
    std::error_code ec;
    auto dir = std::filesystem::path(CONFIG_PATH).parent_path();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        fprintf(stderr, "[artnode] cannot create %s: %s\n",
                dir.c_str(), ec.message().c_str());
        return;
    }

    json j;
    j["hostname"]   = cfg.hostname;
    j["brightness"] = cfg.brightness;
    j["node_mode"]  = (int)cfg.node_mode;

    json strips = json::array();
    for (int i = 0; i < NUM_STRIPS; i++) {
        strips.push_back({
            {"num_leds",       cfg.strips[i].num_leds},
            {"start_universe", cfg.strips[i].start_universe},
            {"channel_offset", cfg.strips[i].channel_offset},
        });
    }
    j["strips"] = strips;

    j["spatial"] = {
        {"virt_w",   cfg.spatial.virt_w},
        {"virt_h",   cfg.spatial.virt_h},
        {"origin_x", cfg.spatial.origin_x},
        {"origin_y", cfg.spatial.origin_y},
        {"step_x",   cfg.spatial.step_x},
        {"step_y",   cfg.spatial.step_y},
        {"panel_w",  cfg.spatial.panel_w},
    };

    std::ofstream f(CONFIG_PATH);
    if (!f.is_open()) {
        fprintf(stderr, "[artnode] cannot write %s\n", CONFIG_PATH);
        return;
    }
    f << j.dump(2) << '\n';
}
