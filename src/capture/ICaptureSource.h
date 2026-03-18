#pragma once

#include "pipeline/Packets.h"

#include <QSize>
#include <QtGlobal>
#include <functional>

class ICaptureSource
{
public:
    using FrameCallback = std::function<void(const VideoFramePacket&)>;

    virtual ~ICaptureSource() = default;

    virtual bool initialize(quintptr nativeHandle, int targetFps) = 0;
    virtual void setFrameCallback(FrameCallback callback) = 0;
    virtual QSize currentFrameSize() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

