#pragma once
#include <WebServer.h>
#include "runtime_config.h"

class WebConfig {
public:
    WebConfig(RuntimeConfig& cfg) : _cfg(cfg), _server(80) {}

    void begin();
    void handle();   // call from loop()

    // Callback fired after config is saved — use to re-apply brightness, restart, etc.
    using SaveCallback = std::function<void(const RuntimeConfig&)>;
    void onSave(SaveCallback cb) { _onSave = cb; }

private:
    RuntimeConfig& _cfg;
    WebServer      _server;
    SaveCallback   _onSave;

    void serveIndex();
    void serveConfig();
    void handleSaveConfig();
    void serveStatus();
};
