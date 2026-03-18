#pragma once

#include "pipeline/Packets.h"

#include <functional>

class IAudioEncoder
{
public:
    using PacketCallback = std::function<void(const EncodedPacket&)>;

    virtual ~IAudioEncoder() = default;

    virtual bool initialize(int sampleRate, int channels, int bitrateKbps) = 0;
    virtual void setPacketCallback(PacketCallback callback) = 0;
    virtual void encode(const AudioFramePacket& frame) = 0;
    virtual void finalize() = 0;
};

