#ifndef INCLUDED_NET_FRAME_H
#define INCLUDED_NET_FRAME_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace NetFrame
{
    constexpr uint32_t MAGIC = 0x534D4E46;
    constexpr uint16_t VERSION = 1;

    enum Flags : uint16_t
    {
        FLAG_NONE = 0,
        FLAG_INPUT = 1 << 0,
        FLAG_ACK = 1 << 1
    };

    #pragma pack(push, 1)

    struct Header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t flags;

        uint32_t frame;
        uint16_t payloadSize;
        uint16_t reserved;
    };

    #pragma pack(pop)

    bool Encode(
        uint32_t frame,
        uint16_t flags,
        const void* payload,
        uint16_t payloadSize,
        std::vector<uint8_t>& packet
    );

    bool Decode(
        const void* data,
        size_t size,
        Header& header,
        const uint8_t*& payload
    );
}

#endif
