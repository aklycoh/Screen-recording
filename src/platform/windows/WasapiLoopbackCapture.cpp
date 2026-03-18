#include "platform/windows/WasapiLoopbackCapture.h"

#include <Windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace std::chrono_literals;

namespace
{
enum class SampleKind
{
    PcmInteger,
    IeeeFloat,
};

struct InputAudioFormat
{
    SampleKind sampleKind {SampleKind::PcmInteger};
    uint32_t sampleRate {0};
    uint16_t channels {0};
    uint16_t bitsPerSample {0};
    uint16_t validBitsPerSample {0};
    uint16_t blockAlign {0};
    bool hasSpeakerMap {false};
    std::array<DWORD, 18> speakerMaskByChannel {};
};

constexpr float kMinus3dBGain = 0.70710678f;
constexpr float kMinus6dBGain = 0.5f;
constexpr float kUnknownChannelGain = 0.25f;

QString hresultMessage(const winrt::hresult_error& error)
{
    const std::wstring message = error.message().c_str();
    if (!message.empty()) {
        return QString::fromStdWString(message);
    }

    return QStringLiteral("HRESULT 0x%1")
        .arg(static_cast<unsigned long>(error.code()), 8, 16, QLatin1Char('0'));
}

bool guidEquals(const GUID& left, const GUID& right)
{
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

QString describeWaveFormat(const WAVEFORMATEX& format)
{
    return QStringLiteral("%1 Hz, %2 ch, %3-bit, tag=%4")
        .arg(format.nSamplesPerSec)
        .arg(format.nChannels)
        .arg(format.wBitsPerSample)
        .arg(format.wFormatTag);
}

InputAudioFormat resolveInputFormat(const WAVEFORMATEX& format)
{
    InputAudioFormat result;
    result.sampleRate = format.nSamplesPerSec;
    result.channels = format.nChannels;
    result.bitsPerSample = format.wBitsPerSample;
    result.validBitsPerSample = format.wBitsPerSample;
    result.blockAlign = format.nBlockAlign;

    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        result.sampleKind = SampleKind::PcmInteger;
        if (result.channels == 1) {
            result.hasSpeakerMap = true;
            result.speakerMaskByChannel[0] = SPEAKER_FRONT_CENTER;
        } else if (result.channels == 2) {
            result.hasSpeakerMap = true;
            result.speakerMaskByChannel[0] = SPEAKER_FRONT_LEFT;
            result.speakerMaskByChannel[1] = SPEAKER_FRONT_RIGHT;
        }
        return result;
    }

    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        result.sampleKind = SampleKind::IeeeFloat;
        if (result.channels == 1) {
            result.hasSpeakerMap = true;
            result.speakerMaskByChannel[0] = SPEAKER_FRONT_CENTER;
        } else if (result.channels == 2) {
            result.hasSpeakerMap = true;
            result.speakerMaskByChannel[0] = SPEAKER_FRONT_LEFT;
            result.speakerMaskByChannel[1] = SPEAKER_FRONT_RIGHT;
        }
        return result;
    }

    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE
        && format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        if (extensible.Samples.wValidBitsPerSample != 0) {
            result.validBitsPerSample = extensible.Samples.wValidBitsPerSample;
        }

        DWORD remainingMask = extensible.dwChannelMask;
        for (uint16_t channelIndex = 0; channelIndex < result.channels && remainingMask != 0; ++channelIndex) {
            const DWORD speakerMask = remainingMask & (~remainingMask + 1);
            result.speakerMaskByChannel[channelIndex] = speakerMask;
            remainingMask &= ~speakerMask;
            result.hasSpeakerMap = true;
        }

        if (guidEquals(extensible.SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            result.sampleKind = SampleKind::PcmInteger;
            return result;
        }

        if (guidEquals(extensible.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            result.sampleKind = SampleKind::IeeeFloat;
            return result;
        }
    }

    throw std::runtime_error("Unsupported WASAPI loopback format.");
}

QString describeInputFormat(const InputAudioFormat& format)
{
    return QStringLiteral("%1 Hz, %2 ch, %3-bit (%4)")
        .arg(format.sampleRate)
        .arg(format.channels)
        .arg(format.validBitsPerSample)
        .arg(format.sampleKind == SampleKind::IeeeFloat ? QStringLiteral("float") : QStringLiteral("pcm"));
}

double qpcTicksToHundredNs(UINT64 ticks, LONGLONG frequency)
{
    if (frequency <= 0) {
        return 0.0;
    }
    return static_cast<double>(ticks) * 10'000'000.0 / static_cast<double>(frequency);
}

float clampUnit(float sample)
{
    return std::clamp(sample, -1.0f, 1.0f);
}

float readIntegerSample(const BYTE* sampleBytes, const InputAudioFormat& format)
{
    const int bytesPerSample = std::max<int>(1, format.bitsPerSample / 8);
    std::uint32_t raw = 0;
    for (int index = 0; index < bytesPerSample && index < 4; ++index) {
        raw |= static_cast<std::uint32_t>(sampleBytes[index]) << (index * 8);
    }

    const uint16_t validBits = std::clamp<uint16_t>(
        format.validBitsPerSample == 0 ? format.bitsPerSample : format.validBitsPerSample,
        1,
        32);
    const int shift = 32 - validBits;
    const std::int32_t signedValue = static_cast<std::int32_t>(raw << shift) >> shift;
    const double scale = static_cast<double>(std::uint64_t {1} << (validBits - 1));
    if (scale <= 0.0) {
        return 0.0f;
    }

    return clampUnit(static_cast<float>(signedValue / scale));
}

float readFloatSample(const BYTE* sampleBytes, const InputAudioFormat& format)
{
    if (format.bitsPerSample == 32) {
        float value = 0.0f;
        std::memcpy(&value, sampleBytes, sizeof(value));
        return clampUnit(value);
    }

    if (format.bitsPerSample == 64) {
        double value = 0.0;
        std::memcpy(&value, sampleBytes, sizeof(value));
        return clampUnit(static_cast<float>(value));
    }

    throw std::runtime_error("Unsupported IEEE float sample width.");
}

float readSampleAsFloat(const BYTE* frameBytes, uint16_t channelIndex, const InputAudioFormat& format)
{
    const int bytesPerSample = std::max<int>(1, format.bitsPerSample / 8);
    const BYTE* sampleBytes = frameBytes + (static_cast<int>(channelIndex) * bytesPerSample);
    if (format.sampleKind == SampleKind::IeeeFloat) {
        return readFloatSample(sampleBytes, format);
    }
    return readIntegerSample(sampleBytes, format);
}

std::int16_t floatToPcm16(float sample)
{
    const float clamped = std::clamp(sample, -1.0f, 32767.0f / 32768.0f);
    const int value = static_cast<int>(std::lround(clamped * 32768.0f));
    return static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
}

void mixToStereo(float sample, float leftGain, float rightGain, float& left, float& right)
{
    left += sample * leftGain;
    right += sample * rightGain;
}

void addSpeakerToStereo(float sample, DWORD speakerMask, float& left, float& right)
{
    switch (speakerMask) {
    case SPEAKER_FRONT_LEFT:
    case SPEAKER_SIDE_LEFT:
    case SPEAKER_BACK_LEFT:
    case SPEAKER_TOP_FRONT_LEFT:
    case SPEAKER_TOP_BACK_LEFT:
        mixToStereo(sample, 1.0f, 0.0f, left, right);
        break;
    case SPEAKER_FRONT_RIGHT:
    case SPEAKER_SIDE_RIGHT:
    case SPEAKER_BACK_RIGHT:
    case SPEAKER_TOP_FRONT_RIGHT:
    case SPEAKER_TOP_BACK_RIGHT:
        mixToStereo(sample, 0.0f, 1.0f, left, right);
        break;
    case SPEAKER_FRONT_CENTER:
    case SPEAKER_TOP_FRONT_CENTER:
        mixToStereo(sample, kMinus3dBGain, kMinus3dBGain, left, right);
        break;
    case SPEAKER_LOW_FREQUENCY:
        mixToStereo(sample, kMinus6dBGain, kMinus6dBGain, left, right);
        break;
    case SPEAKER_BACK_CENTER:
        mixToStereo(sample, kMinus6dBGain, kMinus6dBGain, left, right);
        break;
    case SPEAKER_FRONT_LEFT_OF_CENTER:
        mixToStereo(sample, 1.0f, kUnknownChannelGain, left, right);
        break;
    case SPEAKER_FRONT_RIGHT_OF_CENTER:
        mixToStereo(sample, kUnknownChannelGain, 1.0f, left, right);
        break;
    case SPEAKER_TOP_CENTER:
        mixToStereo(sample, kUnknownChannelGain, kUnknownChannelGain, left, right);
        break;
    default:
        mixToStereo(sample, kUnknownChannelGain, kUnknownChannelGain, left, right);
        break;
    }
}

QByteArray convertToPcm16Stereo(const BYTE* data, uint32_t frameCount, const InputAudioFormat& inputFormat)
{
    QByteArray converted(static_cast<int>(frameCount * 2 * sizeof(std::int16_t)), Qt::Initialization::Uninitialized);
    auto* destination = reinterpret_cast<std::int16_t*>(converted.data());

    for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const BYTE* frameBytes = data + static_cast<std::size_t>(frameIndex) * inputFormat.blockAlign;
        float left = 0.0f;
        float right = 0.0f;

        for (uint16_t channelIndex = 0; channelIndex < inputFormat.channels; ++channelIndex) {
            const float sample = readSampleAsFloat(frameBytes, channelIndex, inputFormat);
            if (inputFormat.hasSpeakerMap) {
                addSpeakerToStereo(sample, inputFormat.speakerMaskByChannel[channelIndex], left, right);
                continue;
            }

            if (inputFormat.channels == 1) {
                mixToStereo(sample, 1.0f, 1.0f, left, right);
            } else if (channelIndex == 0) {
                mixToStereo(sample, 1.0f, 0.0f, left, right);
            } else if (channelIndex == 1) {
                mixToStereo(sample, 0.0f, 1.0f, left, right);
            } else {
                mixToStereo(sample, kUnknownChannelGain, kUnknownChannelGain, left, right);
            }
        }

        destination[frameIndex * 2] = floatToPcm16(clampUnit(left));
        destination[frameIndex * 2 + 1] = floatToPcm16(clampUnit(right));
    }

    return converted;
}

struct CoTaskMemDeleter
{
    void operator()(WAVEFORMATEX* value) const
    {
        if (value != nullptr) {
            CoTaskMemFree(value);
        }
    }
};
}

WasapiLoopbackCapture::WasapiLoopbackCapture() = default;

WasapiLoopbackCapture::~WasapiLoopbackCapture()
{
    stop();
}

OperationResult WasapiLoopbackCapture::start(PacketCallback packetCallback, LogCallback logCallback)
{
    if (worker_.joinable()) {
        return OperationResult::failure(QStringLiteral("System audio capture is already running."));
    }

    packetCallback_ = std::move(packetCallback);
    logCallback_ = std::move(logCallback);

    {
        std::lock_guard lock(mutex_);
        startupResult_ = {};
        startupCompleted_ = false;
    }

    stopRequested_.store(false);
    running_.store(false);
    worker_ = std::thread([this]() { captureThreadMain(); });

    std::unique_lock lock(mutex_);
    startupCv_.wait(lock, [this] { return startupCompleted_; });
    const OperationResult result = startupResult_;
    lock.unlock();

    if (!result.ok && worker_.joinable()) {
        worker_.join();
    }

    return result;
}

void WasapiLoopbackCapture::stop()
{
    stopRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false);
}

bool WasapiLoopbackCapture::isRunning() const
{
    return running_.load();
}

LoopbackAudioFormat WasapiLoopbackCapture::format() const
{
    return format_;
}

void WasapiLoopbackCapture::captureThreadMain()
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        winrt::check_hresult(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            enumerator.put_void()));

        winrt::com_ptr<IMMDevice> device;
        winrt::check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()));

        winrt::com_ptr<IAudioClient> audioClient;
        winrt::check_hresult(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, audioClient.put_void()));

        WAVEFORMATEX* mixFormatRaw = nullptr;
        winrt::check_hresult(audioClient->GetMixFormat(&mixFormatRaw));
        std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> mixFormat(mixFormatRaw);
        const InputAudioFormat inputFormat = resolveInputFormat(*mixFormat);

        const DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        HRESULT hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            streamFlags,
            0,
            0,
            mixFormat.get(),
            nullptr);

        if (FAILED(hr)) {
            QString detail = QStringLiteral("Unable to initialize WASAPI loopback with mix format %1.")
                .arg(describeWaveFormat(*mixFormat));
            throw winrt::hresult_error(hr, detail.toStdWString().c_str());
        }

        format_.sampleRate = inputFormat.sampleRate;
        format_.channels = 2;
        format_.bitsPerSample = 16;
        format_.blockAlign = static_cast<uint16_t>(format_.channels * sizeof(std::int16_t));

        REFERENCE_TIME defaultPeriod = 0;
        winrt::check_hresult(audioClient->GetDevicePeriod(&defaultPeriod, nullptr));
        const std::int64_t periodFrames = std::max<std::int64_t>(
            1,
            (static_cast<std::int64_t>(format_.sampleRate) * defaultPeriod) / 10'000'000);
        const auto pollDuration = std::max(
            5ms,
            std::chrono::milliseconds(static_cast<int>((periodFrames * 1000) / format_.sampleRate)));

        winrt::com_ptr<IAudioCaptureClient> captureClient;
        winrt::check_hresult(audioClient->GetService(__uuidof(IAudioCaptureClient), captureClient.put_void()));
        winrt::check_hresult(audioClient->Start());

        LARGE_INTEGER qpcFrequency {};
        QueryPerformanceFrequency(&qpcFrequency);

        log(QStringLiteral("System audio loopback started. Device mix format: %1. Output format: %2 Hz, %3 channel(s), %4-bit PCM.")
                .arg(describeInputFormat(inputFormat))
                .arg(format_.sampleRate)
                .arg(format_.channels)
                .arg(format_.bitsPerSample));

        running_.store(true);
        notifyStartupResult(OperationResult::success(QStringLiteral("System audio loopback initialized.")));

        auto nextPollTime = std::chrono::steady_clock::now();
        bool firstQpcPositionSeen = false;
        UINT64 firstQpcPosition = 0;
        std::uint64_t fallbackCapturedFrames = 0;

        while (!stopRequested_.load()) {
            nextPollTime += pollDuration;

            UINT32 nextPacketFrames = 0;
            winrt::check_hresult(captureClient->GetNextPacketSize(&nextPacketFrames));

            while (nextPacketFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frameCount = 0;
                DWORD flags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;
                winrt::check_hresult(captureClient->GetBuffer(&data, &frameCount, &flags, &devicePosition, &qpcPosition));

                QByteArray packet;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
                    packet.fill('\0', static_cast<int>(frameCount * format_.blockAlign));
                } else {
                    packet = convertToPcm16Stereo(data, frameCount, inputFormat);
                }

                winrt::check_hresult(captureClient->ReleaseBuffer(frameCount));
                if (packetCallback_) {
                    std::int64_t timestamp100ns = static_cast<std::int64_t>(
                        (static_cast<std::uint64_t>(fallbackCapturedFrames) * 10'000'000ULL) / format_.sampleRate);

                    if (qpcPosition != 0 && qpcFrequency.QuadPart > 0) {
                        if (!firstQpcPositionSeen) {
                            firstQpcPosition = qpcPosition;
                            firstQpcPositionSeen = true;
                        }

                        if (qpcPosition >= firstQpcPosition) {
                            timestamp100ns = static_cast<std::int64_t>(
                                std::llround(qpcTicksToHundredNs(qpcPosition - firstQpcPosition, qpcFrequency.QuadPart)));
                        }
                    }

                    packetCallback_(packet, frameCount, timestamp100ns);
                }

                fallbackCapturedFrames += frameCount;
                winrt::check_hresult(captureClient->GetNextPacketSize(&nextPacketFrames));
            }

            std::this_thread::sleep_until(nextPollTime);
        }

        audioClient->Stop();
        running_.store(false);
        winrt::uninit_apartment();
    } catch (const winrt::hresult_error& error) {
        running_.store(false);
        bool shouldNotify = false;
        {
            std::lock_guard lock(mutex_);
            shouldNotify = !startupCompleted_;
        }

        if (shouldNotify) {
            notifyStartupResult(OperationResult::failure(hresultMessage(error)));
        } else {
            log(hresultMessage(error));
        }
        winrt::uninit_apartment();
    } catch (const std::exception& error) {
        running_.store(false);
        bool shouldNotify = false;
        {
            std::lock_guard lock(mutex_);
            shouldNotify = !startupCompleted_;
        }

        if (shouldNotify) {
            notifyStartupResult(OperationResult::failure(QString::fromUtf8(error.what())));
        } else {
            log(QString::fromUtf8(error.what()));
        }
        winrt::uninit_apartment();
    }
}

void WasapiLoopbackCapture::notifyStartupResult(const OperationResult& result)
{
    {
        std::lock_guard lock(mutex_);
        startupResult_ = result;
        startupCompleted_ = true;
    }
    startupCv_.notify_all();
}

void WasapiLoopbackCapture::log(const QString& message) const
{
    if (logCallback_) {
        logCallback_(message);
    }
}
