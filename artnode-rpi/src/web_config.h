#pragma once
#include <functional>
#include <mutex>
#include <atomic>

// cpp-httplib is a header-only library; include it here only where needed.
// Forward-declare the server type to keep compile times low for headers
// that include web_config.h.
namespace httplib { class Server; }

#include "runtime_config.h"

class WebConfig {
public:
    WebConfig(RuntimeConfig& cfg, std::mutex& cfgMutex)
        : _cfg(cfg), _mutex(cfgMutex) {}

    ~WebConfig();

    // Start the HTTP server in a background thread on the given port.
    void begin(int port = 80);

    // Increment the Art-Net frame counter shown on the status page.
    void countFrame() { _frames++; }

    // Callback fired after config is successfully saved.
    using SaveCallback = std::function<void(const RuntimeConfig&)>;
    void onSave(SaveCallback cb) { _onSave = cb; }

private:
    RuntimeConfig&        _cfg;
    std::mutex&           _mutex;
    SaveCallback          _onSave;
    std::atomic<uint32_t> _frames{0};
    std::atomic<uint32_t> _startMs{0};

    // Owned server instance; fully typed only in web_config.cpp.
    httplib::Server* _server = nullptr;
};
