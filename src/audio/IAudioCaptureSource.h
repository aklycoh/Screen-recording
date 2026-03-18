#pragma once

#include "pipeline/Packets.h"

#include <functional>

class IAudioCaptureSource
{
public:
    using FrameCallback = std::function<void(const AudioFramePacket&)>;

    virtual ~IAudioCaptureSource() = default;

    virtual bool initialize() = 0;
    virtual void setFrameCallback(FrameCallback callback) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

