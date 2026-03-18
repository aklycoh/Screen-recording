#pragma once

#include "pipeline/Packets.h"

#include <QString>

class IMp4Muxer
{
public:
    virtual ~IMp4Muxer() = default;

    virtual bool open(const QString& tempFilePath) = 0;
    virtual void writeVideoPacket(const EncodedPacket& packet) = 0;
    virtual void writeAudioPacket(const EncodedPacket& packet) = 0;
    virtual bool finalize(const QString& finalFilePath) = 0;
    virtual void abort() = 0;
};

