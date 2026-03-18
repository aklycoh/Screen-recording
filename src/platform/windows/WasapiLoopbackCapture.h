#pragma once

#include "common/OperationResult.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

struct LoopbackAudioFormat
{
    uint32_t sampleRate {48000};
    uint16_t channels {2};
    uint16_t bitsPerSample {16};
    uint16_t blockAlign {4};
};

class WasapiLoopbackCapture
{
public:
    using PacketCallback = std::function<void(const QByteArray& pcmFrames, uint32_t frameCount, std::int64_t timestamp100ns)>;
    using LogCallback = std::function<void(const QString&)>;

    WasapiLoopbackCapture();
    ~WasapiLoopbackCapture();

    OperationResult start(PacketCallback packetCallback, LogCallback logCallback = {});
    void stop();

    bool isRunning() const;
    LoopbackAudioFormat format() const;

private:
    void captureThreadMain();
    void notifyStartupResult(const OperationResult& result);
    void log(const QString& message) const;

    PacketCallback packetCallback_;
    LogCallback logCallback_;

    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable startupCv_;

    LoopbackAudioFormat format_;
    OperationResult startupResult_;
    bool startupCompleted_ {false};

    std::atomic<bool> running_ {false};
    std::atomic<bool> stopRequested_ {false};
};
