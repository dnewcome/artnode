#pragma once
#include <cstdint>
#include <functional>

class ArtNet {
public:
    using DmxCallback = std::function<void(uint16_t universe, uint8_t* data, uint16_t len)>;

    // Open UDP socket on the given port (default Art-Net port 6454).
    // Returns true on success.
    bool begin(uint16_t port = 6454);

    // Poll for incoming packets.  Calls cb for each valid ArtDMX frame received.
    // Blocks for at most timeout_us microseconds using select().
    // Call this in a tight loop — it returns quickly when no packets arrive.
    void poll(DmxCallback cb, int timeout_us = 1000);

    // Close the socket.
    void end();

private:
    int _sock = -1;
};
