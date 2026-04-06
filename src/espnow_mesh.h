#pragma once
#include <Arduino.h>
#include <functional>

// ---- Packet types ----
#define MESH_MAGIC     0xAE
#define MESH_PKT_DMX   0x01

// Max bytes per ESP-NOW packet is 250. Header = 9 bytes → 241 payload.
// Round down to 240 for alignment.
#define MESH_FRAG_SIZE   240
#define MESH_MAX_FRAGS   3    // ceil(512 / 240) = 3

struct __attribute__((packed)) MeshDmxPacket {
    uint8_t  magic;       // MESH_MAGIC
    uint8_t  type;        // MESH_PKT_DMX
    uint8_t  universe;
    uint8_t  seq;         // rolling counter, used to discard stale fragments
    uint8_t  frag;        // this fragment index (0-based)
    uint8_t  num_frags;   // total fragments for this frame
    uint16_t total_len;   // total universe payload length
    uint8_t  data[MESH_FRAG_SIZE];
};
// 8 + 240 = 248 bytes ✓

// ---- Callback types ----
using MeshDmxCallback = std::function<void(uint8_t universe, uint8_t* data, uint16_t len)>;

// ---- EspNowMesh ----
class EspNowMesh {
public:
    // call after WiFi is up (or after wifi_set_channel for mesh-only nodes)
    void begin();

    // Bridge: fragment a 512-byte universe and broadcast to all nodes
    void broadcastUniverse(uint8_t universe, uint8_t* data, uint16_t len);

    // Slave: fired when a complete universe is reassembled from incoming fragments
    void onDmx(MeshDmxCallback cb) { _dmxCb = cb; }

    // Internal — called by the static ESP-NOW recv callback
    void _onReceive(const uint8_t* data, int len);

private:
    MeshDmxCallback _dmxCb;
    uint8_t         _seq = 0;

    struct ReassemblySlot {
        bool     active     = false;
        uint8_t  universe   = 0;
        uint8_t  seq        = 0;
        uint8_t  num_frags  = 0;
        uint16_t total_len  = 0;
        uint8_t  recv_mask  = 0;   // bit i = fragment i received
        uint8_t  buf[512]   = {};
    };
    ReassemblySlot _slots[4];  // concurrent reassembly for up to 4 universes

    ReassemblySlot& slotFor(uint8_t universe, uint8_t seq, uint8_t num_frags, uint16_t total_len);
};
