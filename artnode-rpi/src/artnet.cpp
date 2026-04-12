#include "artnet.h"

#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>

// Art-Net packet layout (Art-Net 4 / backwards-compatible with Art-Net 3):
//   [0..7]   "Art-Net\0"  ID string (8 bytes)
//   [8..9]   OpCode LE    0x00 0x50 = ArtDMX (0x5000)
//   [10..11] ProtVer      0x00 0x0E (version 14)
//   [12]     Sequence     0 = disabled
//   [13]     Physical
//   [14]     SubUni       low byte of universe (0-255)
//   [15]     Net          bits 14..8 of universe (0-127)
//   [16]     LengthHi     MSB of DMX data length
//   [17]     Length       LSB of DMX data length
//   [18+]    DMX data     up to 512 bytes

static const uint8_t ARTNET_ID[8] = {'A','r','t','-','N','e','t','\0'};
static const uint16_t ARTDMX_OPCODE = 0x5000;
static const size_t   ARTNET_HEADER = 18;

bool ArtNet::begin(uint16_t port) {
    _sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_sock < 0) {
        perror("[artnet] socket");
        return false;
    }

    // Reuse address so we can restart quickly
    int reuse = 1;
    setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Allow receiving broadcast Art-Net
    int bcast = 1;
    setsockopt(_sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    // Non-blocking (poll() uses select() so this is belt-and-suspenders)
    int flags = fcntl(_sock, F_GETFL, 0);
    fcntl(_sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("[artnet] bind");
        close(_sock);
        _sock = -1;
        return false;
    }

    printf("[artnet] listening on UDP port %u\n", port);
    return true;
}

void ArtNet::poll(DmxCallback cb, int timeout_us) {
    if (_sock < 0) return;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(_sock, &rfds);

    struct timeval tv{0, timeout_us};
    if (select(_sock + 1, &rfds, nullptr, nullptr, &tv) <= 0) return;

    // Buffer big enough for header + 512 bytes DMX
    uint8_t buf[ARTNET_HEADER + 512];
    ssize_t len = recv(_sock, buf, sizeof(buf), 0);
    if (len < static_cast<ssize_t>(ARTNET_HEADER)) return;

    // Validate Art-Net ID
    if (memcmp(buf, ARTNET_ID, 8) != 0) return;

    // Check OpCode (little-endian)
    uint16_t opcode = static_cast<uint16_t>(buf[8]) | (static_cast<uint16_t>(buf[9]) << 8);
    if (opcode != ARTDMX_OPCODE) return;

    // Universe: SubUni | (Net << 8)
    uint16_t universe = static_cast<uint16_t>(buf[14]) | (static_cast<uint16_t>(buf[15]) << 8);

    // Data length (big-endian, must be even per spec)
    uint16_t data_len = (static_cast<uint16_t>(buf[16]) << 8) | buf[17];

    // Clamp to what we actually received
    if (static_cast<ssize_t>(ARTNET_HEADER + data_len) > len) {
        data_len = static_cast<uint16_t>(len - ARTNET_HEADER);
    }
    if (data_len == 0) return;

    cb(universe, buf + ARTNET_HEADER, data_len);
}

void ArtNet::end() {
    if (_sock >= 0) {
        close(_sock);
        _sock = -1;
    }
}
