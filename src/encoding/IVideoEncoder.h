#pragma once

#include "pipeline/Packets.h"

#include <QSize>
#include <functional>

class IVideoEncoder
{
public:
    using PacketCallback = std::function<void(const EncodedPacket&)>;

    virtual ~IVideoEncoder() = default;

    virtual bool initialize(const QSize& frameSize, int fps, int bitrateKbps) = 0;
    virtual void setPacketCallback(PacketCallback callback) = 0;
    virtual void encode(const VideoFramePacket& frame) = 0;
    virtual void finalize() = 0;
};

