#pragma once

#include "platform/windows/WasapiLoopbackCapture.h"
#include "recording/IRecordingBackend.h"

#include <QByteArray>
#include <QSize>

#include <d3d11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Media.Transcoding.h>
#include <winrt/Windows.Storage.Streams.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

struct IDXGIDevice;
struct IDXGISurface;
struct ID2D1Factory1;
struct ID2D1Bitmap1;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct AsyncCallbackState;

class WgcVideoRecorderBackend : public IRecordingBackend
{
public:
    WgcVideoRecorderBackend();
    ~WgcVideoRecorderBackend() override;

    void setLogCallback(LogCallback callback) override;
    void setStartedCallback(StartedCallback callback) override;
    void setFinishedCallback(FinishedCallback callback) override;

    OperationResult start(const RecordingOptions& options) override;
    OperationResult requestStop() override;

private:
    struct QueuedFrame
    {
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::Windows::Foundation::TimeSpan timestamp {};
    };

    struct QueuedAudioPacket
    {
        QByteArray pcm;
        uint32_t frameCount {0};
        winrt::Windows::Foundation::TimeSpan timestamp {};
        winrt::Windows::Foundation::TimeSpan duration {};
    };

    struct OutputTextureSlot
    {
        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::com_ptr<IDXGISurface> surface;
        winrt::com_ptr<ID2D1Bitmap1> targetBitmap;
    };

    void run();
    void initializeD3D();
    void initializeAudioCapture();
    void initializeCapture();
    void initializeTranscode();
    void cleanup();
    void finish(const OperationResult& result);
    void requestStopInternal(const QString& reason);
    void beginSourceShutdown();
    void setFatalError(const QString& message);
    void log(const QString& message) const;
    void initializeScaleContext();
    void initializeOutputTexturePool();
    bool shouldScaleOutput() const;

    QSize initialCaptureSize() const;
    QSize outputSizeForCapture(const QSize& captureSize) const;
    std::chrono::milliseconds frameWaitTimeout() const;
    std::chrono::milliseconds audioWaitTimeout() const;
    void finalizeOutputFile();
    void stopAcceptingAsyncCallbacks();
    void waitForAsyncCallbacks();

    winrt::Windows::Media::MediaProperties::MediaEncodingProfile createEncodingProfile() const;
    winrt::Windows::Media::Core::MediaStreamSource createMediaStreamSource();
    winrt::Windows::Storage::Streams::IRandomAccessStream createOutputStream(const std::wstring& filePath) const;
    winrt::com_ptr<ID3D11Texture2D> cloneFrameTexture(ID3D11Texture2D* sourceTexture);
    winrt::Windows::Storage::Streams::IBuffer createAudioBuffer(const QByteArray& pcmBytes) const;
    QueuedAudioPacket createSilenceAudioPacket(std::int64_t timestamp100ns, uint32_t frameCount) const;
    void trimAudioPacketFront(QueuedAudioPacket& packet, uint32_t frameCount) const;
    std::int64_t audioFramesToHundredNs(uint32_t frameCount) const;
    uint32_t hundredNsToAudioFramesCeil(std::int64_t duration100ns) const;

    std::optional<QueuedFrame> waitForFrame(std::chrono::milliseconds timeout);
    std::optional<QueuedFrame> dequeueFrameLocked();
    void waitForAudioPacket(std::chrono::milliseconds timeout);
    std::optional<QueuedAudioPacket> dequeueAudioPacketLocked();
    void onAudioPacketCaptured(const QByteArray& pcmFrames, uint32_t frameCount, std::int64_t timestamp100ns);

    void onFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const&);
    void onCaptureClosed(
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& sender,
        winrt::Windows::Foundation::IInspectable const&);
    void onStarting(
        winrt::Windows::Media::Core::MediaStreamSource const& sender,
        winrt::Windows::Media::Core::MediaStreamSourceStartingEventArgs const& args);
    void onSampleRequested(
        winrt::Windows::Media::Core::MediaStreamSource const& sender,
        winrt::Windows::Media::Core::MediaStreamSourceSampleRequestedEventArgs const& args);
    void onVideoSampleRequested(
        winrt::Windows::Media::Core::MediaStreamSourceSampleRequestedEventArgs const& args,
        winrt::Windows::Media::Core::IMediaStreamDescriptor const& descriptor);
    void onAudioSampleRequested(
        winrt::Windows::Media::Core::MediaStreamSourceSampleRequestedEventArgs const& args,
        winrt::Windows::Media::Core::IMediaStreamDescriptor const& descriptor);

    mutable std::mutex callbackMutex_;
    LogCallback logCallback_;
    StartedCallback startedCallback_;
    FinishedCallback finishedCallback_;
    std::shared_ptr<AsyncCallbackState> callbackState_;


    RecordingOptions options_;
    std::filesystem::path finalOutputPath_;
    std::filesystem::path tempOutputPath_;

    std::thread worker_;
    std::atomic<bool> started_ {false};
    std::atomic<bool> stopRequested_ {false};
    std::atomic<bool> finished_ {false};

    mutable std::mutex mutex_;
    std::condition_variable frameCv_;
    std::condition_variable audioCv_;
    std::deque<QueuedFrame> frameQueue_;
    std::deque<QueuedAudioPacket> audioQueue_;
    std::optional<QueuedFrame> firstFrame_;
    std::optional<QueuedFrame> lastDeliveredFrame_;
    std::int64_t emittedFrameCount_ {0};
    std::int64_t firstVideoCaptureTimestamp100ns_ {0};
    std::int64_t lastVideoSampleTimestamp100ns_ {0};
    std::int64_t requiredAudioEnd100ns_ {0};
    std::int64_t lastEmittedAudioEnd100ns_ {0};
    std::chrono::steady_clock::time_point sampleClockStart_ {};
    bool firstVideoCaptureTimestampSeen_ {false};
    bool hasVideoSampleTimestamp_ {false};
    bool audioPacketObserved_ {false};
    bool audioSampleRequestedObserved_ {false};
    bool sourceShutdownInitiated_ {false};
    bool videoStreamEnded_ {false};
    QString stopReason_;
    QString fatalErrorMessage_;

    QSize captureSize_;
    QSize outputSize_;
    winrt::Windows::Foundation::TimeSpan frameDuration_ {};
    winrt::Windows::Foundation::TimeSpan lastVideoSampleEnd_ {};
    LoopbackAudioFormat audioFormat_ {};

    winrt::com_ptr<ID3D11Device> d3dDevice_;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext_;
    winrt::com_ptr<IDXGIDevice> dxgiDevice_;
    winrt::com_ptr<ID2D1Factory1> d2dFactory_;
    winrt::com_ptr<ID2D1Device> d2dDevice_;
    winrt::com_ptr<ID2D1DeviceContext> d2dContext_;
    std::array<OutputTextureSlot, 4> outputTextureSlots_ {};
    std::size_t nextOutputTextureSlot_ {0};
    WasapiLoopbackCapture loopbackCapture_;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice direct3DDevice_ {nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem captureItem_ {nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_ {nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession captureSession_ {nullptr};
    winrt::event_token frameArrivedToken_ {};
    winrt::event_token captureClosedToken_ {};
    winrt::Windows::Media::Core::VideoStreamDescriptor videoStreamDescriptor_ {nullptr};
    winrt::Windows::Media::Core::AudioStreamDescriptor audioStreamDescriptor_ {nullptr};
    winrt::Windows::Media::Core::MediaStreamSource mediaStreamSource_ {nullptr};
    winrt::Windows::Media::Transcoding::MediaTranscoder transcoder_ {nullptr};
    winrt::Windows::Storage::Streams::IRandomAccessStream outputStream_ {nullptr};
    winrt::event_token startingToken_ {};
    winrt::event_token sampleRequestedToken_ {};
};
