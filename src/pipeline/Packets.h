#pragma once

#include <QByteArray>
#include <QSize>
#include <cstdint>

struct VideoFramePacket
{
    std::int64_t timestampUs {0};
    QSize frameSize;
};

struct AudioFramePacket
{
    std::int64_t timestampUs {0};
    QByteArray interleavedPcm;
    int sampleRate {48000};
    int channels {2};
};

struct EncodedPacket
{
    std::int64_t timestampUs {0};
    std::int64_t durationUs {0};
    QByteArray bytes;
    bool keyFrame {false};
};

