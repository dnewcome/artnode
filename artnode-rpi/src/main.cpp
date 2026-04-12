#include <cstdio>
#include <csignal>
#include <mutex>

#include "platform.h"
#include "runtime_config.h"
#include "led_controller.h"
#include "pattern_engine.h"
#include "artnet.h"
#include "web_config.h"

#if ENABLE_HUB75
#include "hub75_controller.h"
#endif

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static volatile bool s_running = true;

static void onSignal(int) { s_running = false; }

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    signal(SIGINT,  onSignal);
    signal(SIGTERM, onSignal);

    printf("[artnode] starting\n");

    // ---- Config ------------------------------------------------------------
    RuntimeConfig cfg;
    loadConfig(cfg);

    printf("[artnode] hostname=%s brightness=%u mode=%d\n",
           cfg.hostname, cfg.brightness, (int)cfg.node_mode);

    std::mutex cfgMutex;

    // ---- LED strips --------------------------------------------------------
    LedController leds;
    leds.begin(cfg);

    // ---- HUB75 panel -------------------------------------------------------
#if ENABLE_HUB75
    Hub75Controller hub75;
    // Force spatial into panel/2D mode so patterns render correctly on the matrix
    cfg.spatial.panel_w = HUB75_W * HUB75_CHAIN;
    cfg.spatial.virt_w  = HUB75_W * HUB75_CHAIN;
    cfg.spatial.virt_h  = HUB75_H;
    hub75.begin(cfg);
    printf("[artnode] HUB75 %dx%d chain=%d\n",
           HUB75_W * HUB75_CHAIN, HUB75_H, HUB75_CHAIN);
#endif

    // ---- Pattern engine ----------------------------------------------------
    PatternEngine patterns;
    patterns.setSpatial(cfg.spatial);

    // ---- Art-Net -----------------------------------------------------------
    ArtNet artnet;
    uint32_t lastFrameMs = 0;

    if (cfg.node_mode != NodeMode::STANDALONE) {
        if (!artnet.begin()) {
            fprintf(stderr, "[artnode] Art-Net init failed — running in STANDALONE mode\n");
            cfg.node_mode = NodeMode::STANDALONE;
        }
    }

    // ---- Web config --------------------------------------------------------
    WebConfig webConfig(cfg, cfgMutex);
    webConfig.onSave([&](const RuntimeConfig& newCfg) {
        // Re-apply pattern spatial when config changes
        patterns.setSpatial(newCfg.spatial);
    });
    webConfig.begin(80);

    // ---- Main loop ---------------------------------------------------------
    printf("[artnode] running\n");

#if ENABLE_HUB75
    // HUB75 show() calls SwapOnVSync which blocks until the matrix background
    // thread completes a scan.  Calling it once per Art-Net universe (24× per
    // frame) serialises the entire receive loop behind 24 blocking waits and
    // can starve the socket buffer.  Rate-limit to one blit per frame instead.
    uint32_t hub75LastShowMs = 0;
    constexpr uint32_t HUB75_FRAME_MS = 33;  // ~30 fps
    bool hub75Dirty = false;
#endif

    while (s_running) {
        // -- Art-Net receive -------------------------------------------------
        if (cfg.node_mode != NodeMode::STANDALONE) {
            artnet.poll([&](uint16_t universe, uint8_t* data, uint16_t len) {
                webConfig.countFrame();
                lastFrameMs = millis();

                {
                    std::lock_guard<std::mutex> lock(cfgMutex);
                    leds.handleUniverse(static_cast<uint8_t>(universe), data, len, cfg);
                }
                leds.show();

#if ENABLE_HUB75
                hub75.handleUniverse(static_cast<uint8_t>(universe), data, len);
                hub75Dirty = true;
#endif
            });
        }

        // -- HUB75 blit (rate-limited) ---------------------------------------
#if ENABLE_HUB75
        {
            uint32_t now = millis();
            if (hub75Dirty && (now - hub75LastShowMs >= HUB75_FRAME_MS)) {
                hub75.show();
                hub75LastShowMs = now;
                hub75Dirty = false;
            }
        }
#endif

        // -- Pattern engine --------------------------------------------------
        bool runPatterns = (cfg.node_mode == NodeMode::STANDALONE) ||
                           (millis() - lastFrameMs > IDLE_TIMEOUT_MS);

        if (runPatterns) {
            std::lock_guard<std::mutex> lock(cfgMutex);
            // HUB75 ticks first so it captures the frame-gate firing.
            // tick() always renders to the buffer; returns true only once per
            // FRAME_MS so subsequent callers (strips) still know whether to show().
#if ENABLE_HUB75
            if (patterns.tick(hub75.getLeds(), HUB75_TOTAL_LEDS)) {
                hub75.show();
                hub75LastShowMs = millis();
                hub75Dirty = false;
            }
#endif
            for (int i = 0; i < NUM_STRIPS; i++) {
                if (patterns.tick(leds.getLeds(i), cfg.strips[i].num_leds)) {
                    leds.show();
                }
            }
        }
    }

    // ---- Cleanup -----------------------------------------------------------
    printf("[artnode] shutting down\n");
    artnet.end();
    leds.cleanup();
    return 0;
}
