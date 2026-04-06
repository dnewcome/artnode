#include "espnow_mesh.h"
#include <esp_now.h>
#include <WiFi.h>

static EspNowMesh* g_instance = nullptr;

static void recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (g_instance) g_instance->_onReceive(data, len);
}

void EspNowMesh::begin() {
    g_instance = this;

    if (esp_now_init() != ESP_OK) {
        Serial.println("[mesh] esp_now_init failed");
        return;
    }
    esp_now_register_recv_cb(recv_cb);

    // Register broadcast peer so we can send to all nodes
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast, 6);
    peer.channel = 0;       // 0 = use current WiFi channel
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(broadcast)) {
        esp_now_add_peer(&peer);
    }

    Serial.println("[mesh] ESP-NOW ready");
}

void EspNowMesh::broadcastUniverse(uint8_t universe, uint8_t* data, uint16_t len) {
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    uint8_t num_frags = (len + MESH_FRAG_SIZE - 1) / MESH_FRAG_SIZE;
    if (num_frags == 0) return;
    if (num_frags > MESH_MAX_FRAGS) num_frags = MESH_MAX_FRAGS;

    MeshDmxPacket pkt;
    pkt.magic     = MESH_MAGIC;
    pkt.type      = MESH_PKT_DMX;
    pkt.universe  = universe;
    pkt.seq       = _seq++;
    pkt.num_frags = num_frags;
    pkt.total_len = len;

    for (uint8_t f = 0; f < num_frags; f++) {
        uint16_t offset   = f * MESH_FRAG_SIZE;
        uint16_t frag_len = min((uint16_t)MESH_FRAG_SIZE, (uint16_t)(len - offset));

        pkt.frag = f;
        memcpy(pkt.data, data + offset, frag_len);
        if (frag_len < MESH_FRAG_SIZE) {
            memset(pkt.data + frag_len, 0, MESH_FRAG_SIZE - frag_len);
        }

        esp_now_send(broadcast, (uint8_t*)&pkt, sizeof(pkt));
    }
}

void EspNowMesh::_onReceive(const uint8_t* raw, int len) {
    if (len < 2) return;
    if (raw[0] != MESH_MAGIC) return;
    if (raw[1] != MESH_PKT_DMX) return;
    if (len < (int)sizeof(MeshDmxPacket)) return;

    const MeshDmxPacket* pkt = reinterpret_cast<const MeshDmxPacket*>(raw);
    if (!_dmxCb) return;

    // Single-fragment fast path (most strips fit in one packet)
    if (pkt->num_frags == 1) {
        uint16_t data_len = min(pkt->total_len, (uint16_t)MESH_FRAG_SIZE);
        uint8_t buf[MESH_FRAG_SIZE];
        memcpy(buf, pkt->data, data_len);
        _dmxCb(pkt->universe, buf, data_len);
        return;
    }

    // Multi-fragment reassembly
    ReassemblySlot& slot = slotFor(pkt->universe, pkt->seq, pkt->num_frags, pkt->total_len);

    uint16_t offset   = pkt->frag * MESH_FRAG_SIZE;
    uint16_t frag_len = min((uint16_t)MESH_FRAG_SIZE, (uint16_t)(pkt->total_len - offset));
    if (offset + frag_len <= sizeof(slot.buf)) {
        memcpy(slot.buf + offset, pkt->data, frag_len);
        slot.recv_mask |= (1 << pkt->frag);
    }

    uint8_t full_mask = (1 << pkt->num_frags) - 1;
    if (slot.recv_mask == full_mask) {
        _dmxCb(slot.universe, slot.buf, slot.total_len);
        slot.active = false;
    }
}

EspNowMesh::ReassemblySlot& EspNowMesh::slotFor(
    uint8_t universe, uint8_t seq, uint8_t num_frags, uint16_t total_len)
{
    // Find existing slot for this universe+seq, or claim the oldest inactive one
    int oldest = 0;
    for (int i = 0; i < 4; i++) {
        if (_slots[i].active && _slots[i].universe == universe) {
            if (_slots[i].seq == seq) return _slots[i];
            // Stale slot for same universe — evict it
            _slots[i].active = false;
        }
        if (!_slots[i].active) oldest = i;
    }
    ReassemblySlot& s = _slots[oldest];
    s.active    = true;
    s.universe  = universe;
    s.seq       = seq;
    s.num_frags = num_frags;
    s.total_len = total_len;
    s.recv_mask = 0;
    memset(s.buf, 0, sizeof(s.buf));
    return s;
}
