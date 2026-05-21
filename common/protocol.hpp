#pragma once
#include <cstdint>

namespace tvproto {

constexpr uint32_t MAGIC        = 0x54564552u; // 'TVER'
constexpr uint16_t DEFAULT_PORT = 7788;

enum class PacketType : uint8_t {
    Handshake  = 0x01,
    FrameData  = 0x02,
    Ack        = 0x03,
    Ping       = 0x04,
    Disconnect = 0x05,
};

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;        // 4
    uint8_t  type;         // 1
    uint32_t payload_size; // 4
    uint32_t frame_width;  // 4
    uint32_t frame_height; // 4
    uint32_t frame_number; // 4  → total 21 bytes
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 21, "Packed header must be 21 bytes");

} // namespace tvproto
