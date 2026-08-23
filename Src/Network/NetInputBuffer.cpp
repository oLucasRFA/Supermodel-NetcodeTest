#include "NetInputBuffer.h"

#include <algorithm>
#include <cstring>

NetInputBuffer::NetInputBuffer(
    uint32_t minDelayFrames,
    uint32_t maxDelayFrames,
    uint32_t targetBufferFrames
)
: m_minDelayFrames(minDelayFrames),
m_maxDelayFrames(std::max(minDelayFrames, maxDelayFrames)),
m_targetBufferFrames(targetBufferFrames),
m_delayFrames(minDelayFrames),
m_highestReceivedFrame(0),
m_lowestReceivedFrame(0)
{
}

void NetInputBuffer::Reset()
{
    m_frames.clear();
    m_lastConfirmedInput.clear();

    m_delayFrames = m_minDelayFrames;

    m_highestReceivedFrame = 0;
    m_lowestReceivedFrame = 0;
}

void NetInputBuffer::Push(
    uint32_t frame,
    const uint8_t* data,
    size_t size
)
{
    if (data == nullptr && size != 0)
        return;

    auto it = std::lower_bound(
        m_frames.begin(),
                               m_frames.end(),
                               frame,
                               [](const Frame& lhs, uint32_t rhs)
                               {
                                   return lhs.frame < rhs;
                               }
    );

    if (it != m_frames.end() && it->frame == frame)
    {
        if (size == 0)
        {
            it->data.clear();
        }
        else
        {
            it->data.assign(data, data + size);
        }

        it->confirmed = true;
    }
    else
    {
        Frame entry;
        entry.frame = frame;

        if (size == 0)
        {
            entry.data.clear();
        }
        else
        {
            entry.data.assign(data, data + size);
        }

        entry.confirmed = true;

        m_frames.insert(it, std::move(entry));
    }

    if (m_frames.size() == 1)
        m_lowestReceivedFrame = frame;

    m_highestReceivedFrame =
    std::max(m_highestReceivedFrame, frame);

    m_lowestReceivedFrame =
    m_frames.empty()
    ? frame
    : m_frames.front().frame;

    if (size == 0)
    {
        m_lastConfirmedInput.clear();
    }
    else
    {
        m_lastConfirmedInput.assign(data, data + size);
    }

    Trim();
}

bool NetInputBuffer::Has(uint32_t frame) const
{
    return std::find_if(
        m_frames.begin(),
                        m_frames.end(),
                        [frame](const Frame& entry)
                        {
                            return entry.frame == frame;
                        }
    ) != m_frames.end();
}

bool NetInputBuffer::PopExact(
    uint32_t frame,
    std::vector<uint8_t>& data
)
{
    auto it = std::find_if(
        m_frames.begin(),
                           m_frames.end(),
                           [frame](const Frame& entry)
                           {
                               return entry.frame == frame;
                           }
    );

    if (it == m_frames.end())
        return false;

    data = it->data;
    m_frames.erase(it);

    if (!m_frames.empty())
        m_lowestReceivedFrame = m_frames.front().frame;

    return true;
}

bool NetInputBuffer::GetPredicted(
    uint32_t frame,
    std::vector<uint8_t>& data
) const
{
    auto it = std::find_if(
        m_frames.begin(),
                           m_frames.end(),
                           [frame](const Frame& entry)
                           {
                               return entry.frame == frame;
                           }
    );

    if (it != m_frames.end())
    {
        data = it->data;
        return true;
    }

    if (m_lastConfirmedInput.empty())
        return false;

    data = m_lastConfirmedInput;
    return true;
}

uint32_t NetInputBuffer::UpdateTargetDelay(
    uint32_t currentFrame
)
{
    if (m_frames.empty())
        return m_delayFrames;

    const uint32_t bufferedFrames =
    m_highestReceivedFrame > currentFrame
    ? m_highestReceivedFrame - currentFrame
    : 0;

    if (bufferedFrames < m_targetBufferFrames)
    {
        if (m_delayFrames < m_maxDelayFrames)
            ++m_delayFrames;
    }
    else if (bufferedFrames > m_targetBufferFrames + 2)
    {
        if (m_delayFrames > m_minDelayFrames)
            --m_delayFrames;
    }

    return m_delayFrames;
}

uint32_t NetInputBuffer::GetDelayFrames() const
{
    return m_delayFrames;
}

uint32_t NetInputBuffer::GetMinDelayFrames() const
{
    return m_minDelayFrames;
}

uint32_t NetInputBuffer::GetMaxDelayFrames() const
{
    return m_maxDelayFrames;
}

uint32_t NetInputBuffer::GetHighestReceivedFrame() const
{
    return m_highestReceivedFrame;
}

uint32_t NetInputBuffer::GetLowestReceivedFrame() const
{
    return m_lowestReceivedFrame;
}

void NetInputBuffer::Trim()
{
    constexpr size_t MAX_BUFFERED_FRAMES = 120;

    while (m_frames.size() > MAX_BUFFERED_FRAMES)
        m_frames.pop_front();
}
