#include "NetFrame.h"

#include <cstring>

namespace NetFrame
{

    bool Encode(
        uint32_t frame,
        uint16_t flags,
        const void* payload,
        uint16_t payloadSize,
        std::vector<uint8_t>& packet
    )
    {
        if (payloadSize > 0 && payload == nullptr)
            return false;

        Header header{};

        header.magic = MAGIC;
        header.version = VERSION;
        header.flags = flags;
        header.frame = frame;
        header.payloadSize = payloadSize;
        header.reserved = 0;

        packet.resize(sizeof(Header) + payloadSize);

        std::memcpy(
            packet.data(),
                    &header,
                    sizeof(Header)
        );

        if (payloadSize > 0)
        {
            std::memcpy(
                packet.data() + sizeof(Header),
                        payload,
                        payloadSize
            );
        }

        return true;
    }

    bool Decode(
        const void* data,
        size_t size,
        Header& header,
        const uint8_t*& payload
    )
    {
        if (data == nullptr || size < sizeof(Header))
            return false;

        std::memcpy(
            &header,
            data,
            sizeof(Header)
        );

        if (header.magic != MAGIC)
            return false;

        if (header.version != VERSION)
            return false;

        if (
            sizeof(Header) + header.payloadSize != size
        )
        {
            return false;
        }

        payload =
        reinterpret_cast<const uint8_t*>(data) +
        sizeof(Header);

        return true;
    }

}
