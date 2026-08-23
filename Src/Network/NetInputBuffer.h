#ifndef INCLUDED_NET_INPUT_BUFFER_H
#define INCLUDED_NET_INPUT_BUFFER_H

#include <cstdint>
#include <deque>
#include <vector>
#include <cstddef>

class NetInputBuffer
{
public:
    struct Frame
    {
        uint32_t frame = 0;
        std::vector<uint8_t> data;
        bool confirmed = false;
    };

    NetInputBuffer(
        uint32_t minDelayFrames = 1,
        uint32_t maxDelayFrames = 6,
        uint32_t targetBufferFrames = 2
    );

    void Reset();

    void Push(
        uint32_t frame,
        const uint8_t* data,
        size_t size
    );

    bool Has(uint32_t frame) const;

    bool PopExact(
        uint32_t frame,
        std::vector<uint8_t>& data
    );

    bool GetPredicted(
        uint32_t frame,
        std::vector<uint8_t>& data
    ) const;

    uint32_t UpdateTargetDelay(
        uint32_t currentFrame
    );

    uint32_t GetDelayFrames() const;
    uint32_t GetMinDelayFrames() const;
    uint32_t GetMaxDelayFrames() const;

    uint32_t GetHighestReceivedFrame() const;
    uint32_t GetLowestReceivedFrame() const;

private:
    uint32_t m_minDelayFrames;
    uint32_t m_maxDelayFrames;
    uint32_t m_targetBufferFrames;

    uint32_t m_delayFrames;

    uint32_t m_highestReceivedFrame;
    uint32_t m_lowestReceivedFrame;

    std::deque<Frame> m_frames;

    std::vector<uint8_t> m_lastConfirmedInput;

    void Trim();
};

#endif
