#include "platform/windows/WgcVideoRecorderBackend.h"

#include "platform/windows/WinRtHelpers.h"

#include <Windows.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <ShCore.h>
#include <Shlwapi.h>
#include <dxgi1_2.h>
#include <robuffer.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

using namespace std::chrono_literals;

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Media::MediaProperties;
using namespace winrt::Windows::Media::Transcoding;
using namespace winrt::Windows::Storage::Streams;

namespace
{
constexpr std::size_t kMaxQueuedFrames = 1;
constexpr std::size_t kMaxQueuedAudioPackets = 256;
constexpr uint32_t kDefaultAudioBitrate = 128000;
constexpr std::int64_t kAudioGapTolerance100ns = 2 * 10'000;
constexpr std::int64_t kDefaultSilenceChunk100ns = 10 * 10'000;

int evenFloor(int value)
{
    if (value <= 2) {
        return 2;
    }
    return value & ~1;
}

TimeSpan fpsToFrameDuration(int fps)
{
    return std::chrono::duration_cast<TimeSpan>(std::chrono::microseconds(1'000'000 / fps));
}

QString hresultMessage(const hresult_error& error)
{
    const std::wstring message = error.message().c_str();
    if (!message.empty()) {
        return QString::fromStdWString(message);
    }

    return QStringLiteral("HRESULT 0x%1")
        .arg(static_cast<unsigned long>(error.code()), 8, 16, QLatin1Char('0'));
}

VideoEncodingQuality chooseEncodingQuality(const QSize& outputSize)
{
    if (outputSize.width() > 1280 || outputSize.height() > 720) {
        return VideoEncodingQuality::HD1080p;
    }
    return VideoEncodingQuality::HD720p;
}
}

WgcVideoRecorderBackend::WgcVideoRecorderBackend() = default;

WgcVideoRecorderBackend::~WgcVideoRecorderBackend()
{
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void WgcVideoRecorderBackend::setLogCallback(LogCallback callback)
{
    logCallback_ = std::move(callback);
}

void WgcVideoRecorderBackend::setStartedCallback(StartedCallback callback)
{
    startedCallback_ = std::move(callback);
}

void WgcVideoRecorderBackend::setFinishedCallback(FinishedCallback callback)
{
    finishedCallback_ = std::move(callback);
}

OperationResult WgcVideoRecorderBackend::start(const RecordingOptions& options)
{
    if (worker_.joinable()) {
        return OperationResult::failure(QStringLiteral("Recording backend is already running."));
    }

    if (options.audio.captureMicrophone) {
        return OperationResult::failure(QStringLiteral("Microphone capture is not connected yet. Please use video-only or system-audio-only recording."));
    }

    options_ = options;
    finalOutputPath_ = std::filesystem::path(options.output.outputFilePath.toStdWString());
    tempOutputPath_ = finalOutputPath_;
    tempOutputPath_ += L".part";

    stopRequested_.store(false);
    started_.store(false);
    finished_.store(false);

    worker_ = std::thread([this]() { run(); });

    return OperationResult::success(QStringLiteral("Preparing Windows.Graphics.Capture session."));
}

OperationResult WgcVideoRecorderBackend::requestStop()
{
    if (!worker_.joinable()) {
        return OperationResult::failure(QStringLiteral("Recording backend is not running."));
    }

    requestStopInternal(QStringLiteral("Stop requested by user."));
    return OperationResult::success(QStringLiteral("Stop requested. Finalizing MP4..."));
}

void WgcVideoRecorderBackend::run()
{
    try {
        init_apartment(apartment_type::multi_threaded);

        if (!GraphicsCaptureSession::IsSupported()) {
            throw hresult_error(E_NOTIMPL, L"Windows.Graphics.Capture is not supported on this system.");
        }

        if (finalOutputPath_.empty()) {
            throw hresult_error(E_INVALIDARG, L"Output path is empty.");
        }

        if (!finalOutputPath_.parent_path().empty()) {
            std::filesystem::create_directories(finalOutputPath_.parent_path());
        }

        if (std::filesystem::exists(tempOutputPath_)) {
            std::filesystem::remove(tempOutputPath_);
        }

        initializeD3D();
        if (options_.audio.captureSystemAudio) {
            initializeAudioCapture();
        }
        initializeCapture();
        initializeTranscode();

        captureSession_.StartCapture();
        started_.store(true);
        if (startedCallback_) {
            startedCallback_();
        }

        log(QStringLiteral("Video capture started."));

        auto prepareResult = transcoder_.PrepareMediaStreamSourceTranscodeAsync(
            mediaStreamSource_,
            outputStream_,
            createEncodingProfile()).get();

        if (!prepareResult.CanTranscode()) {
            throw hresult_error(E_FAIL, L"MediaTranscoder refused the selected MP4/H.264 settings.");
        }

        prepareResult.TranscodeAsync().get();

        finalizeOutputFile();
        finish(OperationResult::success(QStringLiteral("Recording finished and MP4 file finalized.")));
    } catch (const hresult_error& error) {
        if (std::filesystem::exists(tempOutputPath_)) {
            std::error_code removeError;
            std::filesystem::remove(tempOutputPath_, removeError);
        }
        finish(OperationResult::failure(hresultMessage(error)));
    } catch (const std::exception& error) {
        if (std::filesystem::exists(tempOutputPath_)) {
            std::error_code removeError;
            std::filesystem::remove(tempOutputPath_, removeError);
        }
        finish(OperationResult::failure(QString::fromUtf8(error.what())));
    }

    cleanup();
    uninit_apartment();
}

void WgcVideoRecorderBackend::initializeD3D()
{
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel {};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        d3dDevice_.put(),
        &featureLevel,
        d3dContext_.put());

#ifndef NDEBUG
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        d3dDevice_ = nullptr;
        d3dContext_ = nullptr;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            d3dDevice_.put(),
            &featureLevel,
            d3dContext_.put());
    }
#endif

    if (hr == E_INVALIDARG) {
        constexpr D3D_FEATURE_LEVEL fallbackLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        d3dDevice_ = nullptr;
        d3dContext_ = nullptr;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags & ~D3D11_CREATE_DEVICE_DEBUG,
            fallbackLevels,
            ARRAYSIZE(fallbackLevels),
            D3D11_SDK_VERSION,
            d3dDevice_.put(),
            &featureLevel,
            d3dContext_.put());
    }

    check_hresult(hr);

    direct3DDevice_ = createDirect3DDevice(d3dDevice_.get());
}

void WgcVideoRecorderBackend::initializeAudioCapture()
{
    {
        std::lock_guard lock(mutex_);
        audioQueue_.clear();
        lastEmittedAudioEnd100ns_ = 0;
        audioPacketObserved_ = false;
        audioSampleRequestedObserved_ = false;
    }

    const OperationResult result = loopbackCapture_.start(
        [this](const QByteArray& pcmFrames, uint32_t frameCount, std::int64_t timestamp100ns) {
            onAudioPacketCaptured(pcmFrames, frameCount, timestamp100ns);
        },
        [this](const QString& message) {
            log(message);
        });

    if (!result.ok) {
        throw hresult_error(E_FAIL, result.message.toStdWString().c_str());
    }

    audioFormat_ = loopbackCapture_.format();
    log(QStringLiteral("Audio input format: %1 Hz, %2 ch, %3-bit PCM.")
            .arg(audioFormat_.sampleRate)
            .arg(audioFormat_.channels)
            .arg(audioFormat_.bitsPerSample));
}

void WgcVideoRecorderBackend::initializeCapture()
{
    captureItem_ = createCaptureItemForWindow(options_.window.nativeHandle);
    captureSize_ = initialCaptureSize();
    outputSize_ = outputSizeForCapture(captureSize_);
    frameDuration_ = fpsToFrameDuration(options_.video.targetFps);

    log(QStringLiteral("Capture size: %1x%2").arg(captureSize_.width()).arg(captureSize_.height()));

    if (captureSize_ != outputSize_) {
        log(QStringLiteral("Output size adjusted to %1x%2 for even H.264 dimensions.")
                .arg(outputSize_.width())
                .arg(outputSize_.height()));
    }

    if (shouldScaleOutput()) {
        initializeScaleContext();
        log(QStringLiteral("Scaling output to %1x%2 to honor max width %3.")
                .arg(outputSize_.width())
                .arg(outputSize_.height())
                .arg(options_.video.maxOutputWidth));
    }

    initializeOutputTexturePool();

    framePool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
        direct3DDevice_,
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        captureItem_.Size());

    frameArrivedToken_ = framePool_.FrameArrived({this, &WgcVideoRecorderBackend::onFrameArrived});
    captureSession_ = framePool_.CreateCaptureSession(captureItem_);
    captureClosedToken_ = captureItem_.Closed({this, &WgcVideoRecorderBackend::onCaptureClosed});
}

void WgcVideoRecorderBackend::initializeTranscode()
{
    mediaStreamSource_ = createMediaStreamSource();
    transcoder_ = MediaTranscoder();
    transcoder_.HardwareAccelerationEnabled(false);
    log(QStringLiteral("Using software transcode for stability-first recording."));
    outputStream_ = createOutputStream(tempOutputPath_.wstring());
}

void WgcVideoRecorderBackend::cleanup()
{
    loopbackCapture_.stop();

    {
        std::lock_guard lock(mutex_);
        frameQueue_.clear();
        audioQueue_.clear();
        firstFrame_.reset();
        lastDeliveredFrame_.reset();
        emittedFrameCount_ = 0;
        lastEmittedAudioEnd100ns_ = 0;
        sampleClockStart_ = {};
        lastVideoSampleEnd_ = {};
        audioPacketObserved_ = false;
        audioSampleRequestedObserved_ = false;
        videoStreamEnded_ = false;
    }
    frameCv_.notify_all();
    audioCv_.notify_all();

    if (framePool_) {
        if (frameArrivedToken_.value != 0) {
            framePool_.FrameArrived(frameArrivedToken_);
        }
        framePool_.Close();
        framePool_ = nullptr;
    }

    if (captureItem_ && captureClosedToken_.value != 0) {
        captureItem_.Closed(captureClosedToken_);
    }

    if (captureSession_) {
        captureSession_.Close();
        captureSession_ = nullptr;
    }

    if (mediaStreamSource_) {
        if (startingToken_.value != 0) {
            mediaStreamSource_.Starting(startingToken_);
        }
        if (sampleRequestedToken_.value != 0) {
            mediaStreamSource_.SampleRequested(sampleRequestedToken_);
        }
        mediaStreamSource_ = nullptr;
    }

    if (outputStream_) {
        outputStream_.Close();
        outputStream_ = nullptr;
    }

    audioStreamDescriptor_ = nullptr;
    videoStreamDescriptor_ = nullptr;
    captureItem_ = nullptr;
    direct3DDevice_ = nullptr;
    d2dContext_ = nullptr;
    d2dDevice_ = nullptr;
    d2dFactory_ = nullptr;
    dxgiDevice_ = nullptr;
    for (auto& slot : outputTextureSlots_) {
        slot.targetBitmap = nullptr;
        slot.surface = nullptr;
        slot.texture = nullptr;
    }
    nextOutputTextureSlot_ = 0;
    d3dContext_ = nullptr;
    d3dDevice_ = nullptr;
    transcoder_ = nullptr;
}

void WgcVideoRecorderBackend::finish(const OperationResult& result)
{
    if (finished_.exchange(true)) {
        return;
    }

    if (finishedCallback_) {
        finishedCallback_(result);
    }
}

void WgcVideoRecorderBackend::requestStopInternal(const QString& reason)
{
    {
        std::lock_guard lock(mutex_);
        stopRequested_.store(true);
        stopReason_ = reason;
    }
    frameCv_.notify_all();
    audioCv_.notify_all();
}

void WgcVideoRecorderBackend::log(const QString& message) const
{
    if (logCallback_) {
        logCallback_(message);
    }
}

QSize WgcVideoRecorderBackend::initialCaptureSize() const
{
    return toQSize(captureItem_.Size());
}

void WgcVideoRecorderBackend::initializeScaleContext()
{
    if (d2dContext_) {
        return;
    }

    D2D1_FACTORY_OPTIONS factoryOptions {};
#ifndef NDEBUG
    factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    check_hresult(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_MULTI_THREADED,
        __uuidof(ID2D1Factory1),
        &factoryOptions,
        reinterpret_cast<void**>(d2dFactory_.put_void())));

    dxgiDevice_ = d3dDevice_.as<IDXGIDevice>();
    check_hresult(d2dFactory_->CreateDevice(dxgiDevice_.get(), d2dDevice_.put()));
    check_hresult(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext_.put()));
    d2dContext_->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
    d2dContext_->SetDpi(96.0f, 96.0f);
}

void WgcVideoRecorderBackend::initializeOutputTexturePool()
{
    D3D11_TEXTURE2D_DESC textureDesc {};
    textureDesc.Width = static_cast<UINT>(outputSize_.width());
    textureDesc.Height = static_cast<UINT>(outputSize_.height());
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    const auto pixelFormat = D2D1::PixelFormat(textureDesc.Format, D2D1_ALPHA_MODE_IGNORE);
    const auto targetBitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        pixelFormat);

    for (auto& slot : outputTextureSlots_) {
        slot.texture = nullptr;
        slot.surface = nullptr;
        slot.targetBitmap = nullptr;

        check_hresult(d3dDevice_->CreateTexture2D(&textureDesc, nullptr, slot.texture.put()));
        check_hresult(slot.texture->QueryInterface(__uuidof(IDXGISurface), slot.surface.put_void()));

        if (shouldScaleOutput()) {
            check_hresult(d2dContext_->CreateBitmapFromDxgiSurface(
                slot.surface.get(),
                &targetBitmapProperties,
                slot.targetBitmap.put()));
        }
    }

    nextOutputTextureSlot_ = 0;
}

bool WgcVideoRecorderBackend::shouldScaleOutput() const
{
    return options_.video.maxOutputWidth > 0
        && captureSize_.width() > options_.video.maxOutputWidth
        && outputSize_.width() < evenFloor(captureSize_.width());
}

QSize WgcVideoRecorderBackend::outputSizeForCapture(const QSize& captureSize) const
{
    const QSize sourceSize {evenFloor(captureSize.width()), evenFloor(captureSize.height())};
    if (options_.video.maxOutputWidth <= 0 || sourceSize.width() <= options_.video.maxOutputWidth) {
        return sourceSize;
    }

    const double scale = static_cast<double>(options_.video.maxOutputWidth)
        / static_cast<double>(captureSize.width());
    const int scaledWidth = evenFloor(static_cast<int>(std::lround(captureSize.width() * scale)));
    const int scaledHeight = evenFloor(static_cast<int>(std::lround(captureSize.height() * scale)));
    return {scaledWidth, scaledHeight};
}

std::chrono::milliseconds WgcVideoRecorderBackend::frameWaitTimeout() const
{
    const int fps = std::max(1, options_.video.targetFps);
    const int timeoutMs = std::max(5, 1000 / fps / 4);
    return std::chrono::milliseconds(timeoutMs);
}

std::chrono::milliseconds WgcVideoRecorderBackend::audioWaitTimeout() const
{
    return 100ms;
}

void WgcVideoRecorderBackend::finalizeOutputFile()
{
    if (outputStream_) {
        outputStream_.FlushAsync().get();
        outputStream_.Close();
        outputStream_ = nullptr;
    }

    std::error_code error;
    if (std::filesystem::exists(finalOutputPath_)) {
        std::filesystem::remove(finalOutputPath_, error);
        error.clear();
    }

    std::filesystem::rename(tempOutputPath_, finalOutputPath_, error);
    if (error) {
        throw std::runtime_error("Failed to rename finalized MP4 file.");
    }
}

MediaEncodingProfile WgcVideoRecorderBackend::createEncodingProfile() const
{
    auto profile = MediaEncodingProfile::CreateMp4(chooseEncodingQuality(outputSize_));
    if (options_.audio.captureSystemAudio) {
        profile.Audio(AudioEncodingProperties::CreateAac(audioFormat_.sampleRate, audioFormat_.channels, kDefaultAudioBitrate));
    } else {
        profile.Audio(nullptr);
    }
    profile.Video().Width(outputSize_.width());
    profile.Video().Height(outputSize_.height());
    profile.Video().Bitrate(options_.video.videoBitrateKbps * 1000);
    profile.Video().FrameRate().Numerator(options_.video.targetFps);
    profile.Video().FrameRate().Denominator(1);
    profile.Video().PixelAspectRatio().Numerator(1);
    profile.Video().PixelAspectRatio().Denominator(1);
    return profile;
}

MediaStreamSource WgcVideoRecorderBackend::createMediaStreamSource()
{
    auto videoProperties = VideoEncodingProperties::CreateUncompressed(MediaEncodingSubtypes::Bgra8(), outputSize_.width(), outputSize_.height());
    videoProperties.FrameRate().Numerator(options_.video.targetFps);
    videoProperties.FrameRate().Denominator(1);
    videoProperties.PixelAspectRatio().Numerator(1);
    videoProperties.PixelAspectRatio().Denominator(1);

    videoStreamDescriptor_ = VideoStreamDescriptor(videoProperties);
    videoStreamDescriptor_.Name(L"video");

    MediaStreamSource source {nullptr};
    if (options_.audio.captureSystemAudio) {
        auto audioProperties = AudioEncodingProperties::CreatePcm(audioFormat_.sampleRate, audioFormat_.channels, audioFormat_.bitsPerSample);
        audioStreamDescriptor_ = AudioStreamDescriptor(audioProperties);
        audioStreamDescriptor_.Name(L"audio");
        source = MediaStreamSource(videoStreamDescriptor_, audioStreamDescriptor_);
    } else {
        audioStreamDescriptor_ = nullptr;
        source = MediaStreamSource(videoStreamDescriptor_);
    }

    source.BufferTime(0s);
    startingToken_ = source.Starting({this, &WgcVideoRecorderBackend::onStarting});
    sampleRequestedToken_ = source.SampleRequested({this, &WgcVideoRecorderBackend::onSampleRequested});
    return source;
}

IRandomAccessStream WgcVideoRecorderBackend::createOutputStream(const std::wstring& filePath) const
{
    com_ptr<IStream> stream;
    check_hresult(SHCreateStreamOnFileEx(
        filePath.c_str(),
        STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
        FILE_ATTRIBUTE_NORMAL,
        TRUE,
        nullptr,
        stream.put()));

    IRandomAccessStream randomAccessStream {nullptr};
    check_hresult(CreateRandomAccessStreamOverStream(
        stream.get(),
        BSOS_PREFERDESTINATIONSTREAM,
        guid_of<IRandomAccessStream>(),
        reinterpret_cast<void**>(put_abi(randomAccessStream))));
    return randomAccessStream;
}

winrt::com_ptr<ID3D11Texture2D> WgcVideoRecorderBackend::cloneFrameTexture(ID3D11Texture2D* sourceTexture)
{
    D3D11_TEXTURE2D_DESC sourceDesc {};
    sourceTexture->GetDesc(&sourceDesc);
    auto& slot = outputTextureSlots_[nextOutputTextureSlot_];
    nextOutputTextureSlot_ = (nextOutputTextureSlot_ + 1) % outputTextureSlots_.size();
    auto targetTexture = slot.texture;

    if (!shouldScaleOutput()) {
        const D3D11_BOX sourceBox {
            0,
            0,
            0,
            static_cast<UINT>(outputSize_.width()),
            static_cast<UINT>(outputSize_.height()),
            1};

        d3dContext_->CopySubresourceRegion(targetTexture.get(), 0, 0, 0, 0, sourceTexture, 0, &sourceBox);
        return targetTexture;
    }

    winrt::com_ptr<IDXGISurface> sourceSurface;
    check_hresult(sourceTexture->QueryInterface(__uuidof(IDXGISurface), sourceSurface.put_void()));

    const auto pixelFormat = D2D1::PixelFormat(sourceDesc.Format, D2D1_ALPHA_MODE_IGNORE);
    const auto sourceBitmapProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, pixelFormat);

    winrt::com_ptr<ID2D1Bitmap1> sourceBitmap;
    check_hresult(d2dContext_->CreateBitmapFromDxgiSurface(sourceSurface.get(), &sourceBitmapProperties, sourceBitmap.put()));

    d2dContext_->SetTarget(slot.targetBitmap.get());
    d2dContext_->BeginDraw();
    d2dContext_->Clear();
    d2dContext_->DrawBitmap(
        sourceBitmap.get(),
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(outputSize_.width()), static_cast<float>(outputSize_.height())),
        1.0f,
        D2D1_INTERPOLATION_MODE_LINEAR,
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(captureSize_.width()), static_cast<float>(captureSize_.height())));
    check_hresult(d2dContext_->EndDraw());
    d2dContext_->SetTarget(nullptr);

    return targetTexture;
}

IBuffer WgcVideoRecorderBackend::createAudioBuffer(const QByteArray& pcmBytes) const
{
    Buffer buffer(static_cast<uint32_t>(pcmBytes.size()));
    buffer.Length(static_cast<uint32_t>(pcmBytes.size()));

    auto byteAccess = buffer.as<::Windows::Storage::Streams::IBufferByteAccess>();
    byte* destination = nullptr;
    check_hresult(byteAccess->Buffer(&destination));

    if (!pcmBytes.isEmpty()) {
        std::memcpy(destination, pcmBytes.constData(), static_cast<std::size_t>(pcmBytes.size()));
    }

    return buffer;
}

WgcVideoRecorderBackend::QueuedAudioPacket WgcVideoRecorderBackend::createSilenceAudioPacket(
    std::int64_t timestamp100ns,
    uint32_t frameCount) const
{
    QueuedAudioPacket packet;
    packet.frameCount = frameCount;
    packet.timestamp = TimeSpan {timestamp100ns};
    packet.duration = TimeSpan {audioFramesToHundredNs(frameCount)};
    packet.pcm = QByteArray(static_cast<int>(frameCount * audioFormat_.blockAlign), Qt::Uninitialized);
    packet.pcm.fill('\0');
    return packet;
}

void WgcVideoRecorderBackend::trimAudioPacketFront(QueuedAudioPacket& packet, uint32_t frameCount) const
{
    if (frameCount == 0 || packet.frameCount == 0) {
        return;
    }

    if (frameCount >= packet.frameCount) {
        packet.pcm.clear();
        packet.frameCount = 0;
        packet.duration = TimeSpan {};
        return;
    }

    const int bytesToTrim = static_cast<int>(frameCount * audioFormat_.blockAlign);
    packet.pcm.remove(0, bytesToTrim);
    packet.frameCount -= frameCount;
    packet.timestamp += TimeSpan {audioFramesToHundredNs(frameCount)};
    packet.duration = TimeSpan {audioFramesToHundredNs(packet.frameCount)};
}

std::int64_t WgcVideoRecorderBackend::audioFramesToHundredNs(uint32_t frameCount) const
{
    if (audioFormat_.sampleRate == 0) {
        return 0;
    }
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(frameCount) * 10'000'000ULL) / audioFormat_.sampleRate);
}

uint32_t WgcVideoRecorderBackend::hundredNsToAudioFramesCeil(std::int64_t duration100ns) const
{
    if (audioFormat_.sampleRate == 0 || duration100ns <= 0) {
        return 0;
    }

    const auto numerator = static_cast<std::uint64_t>(duration100ns) * audioFormat_.sampleRate + 9'999'999ULL;
    return static_cast<uint32_t>(numerator / 10'000'000ULL);
}

std::optional<WgcVideoRecorderBackend::QueuedFrame> WgcVideoRecorderBackend::waitForFrame(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    frameCv_.wait_for(lock, timeout, [this] {
        return stopRequested_.load() || firstFrame_.has_value() || !frameQueue_.empty();
    });

    if (firstFrame_) {
        QueuedFrame frame = std::move(*firstFrame_);
        firstFrame_.reset();
        return frame;
    }

    return dequeueFrameLocked();
}

std::optional<WgcVideoRecorderBackend::QueuedFrame> WgcVideoRecorderBackend::dequeueFrameLocked()
{
    if (frameQueue_.empty()) {
        return std::nullopt;
    }

    QueuedFrame frame = std::move(frameQueue_.front());
    frameQueue_.pop_front();
    return frame;
}

void WgcVideoRecorderBackend::waitForAudioPacket(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    audioCv_.wait_for(lock, timeout, [this] {
        return stopRequested_.load() || !audioQueue_.empty();
    });
}

std::optional<WgcVideoRecorderBackend::QueuedAudioPacket> WgcVideoRecorderBackend::dequeueAudioPacketLocked()
{
    if (audioQueue_.empty()) {
        return std::nullopt;
    }

    QueuedAudioPacket packet = std::move(audioQueue_.front());
    audioQueue_.pop_front();
    return packet;
}

void WgcVideoRecorderBackend::onAudioPacketCaptured(
    const QByteArray& pcmFrames,
    uint32_t frameCount,
    std::int64_t timestamp100ns)
{
    if (frameCount == 0 || stopRequested_.load()) {
        return;
    }

    QueuedAudioPacket packet;
    packet.pcm = pcmFrames;
    packet.frameCount = frameCount;
    packet.timestamp = TimeSpan {timestamp100ns};
    packet.duration = TimeSpan {audioFramesToHundredNs(frameCount)};

    bool shouldLogFirstPacket = false;
    {
        std::lock_guard lock(mutex_);
        shouldLogFirstPacket = !audioPacketObserved_;
        audioPacketObserved_ = true;
        audioQueue_.push_back(std::move(packet));
        while (audioQueue_.size() > kMaxQueuedAudioPackets) {
            audioQueue_.pop_front();
        }
    }

    if (shouldLogFirstPacket) {
        log(QStringLiteral("First system-audio packet captured: %1 frames at %2 ms.")
                .arg(frameCount)
                .arg(timestamp100ns / 10'000));
    }

    audioCv_.notify_all();
}

void WgcVideoRecorderBackend::onFrameArrived(
    const Direct3D11CaptureFramePool& sender,
    const winrt::Windows::Foundation::IInspectable&)
{
    if (stopRequested_.load()) {
        return;
    }

    auto frame = sender.TryGetNextFrame();
    if (!frame) {
        return;
    }

    const QSize currentSize = toQSize(frame.ContentSize());
    if (currentSize != captureSize_) {
        log(QStringLiteral("Window size changed from %1x%2 to %3x%4. Current build stops to protect MP4 integrity.")
                .arg(captureSize_.width())
                .arg(captureSize_.height())
                .arg(currentSize.width())
                .arg(currentSize.height()));
        requestStopInternal(QStringLiteral("Window size changed during recording."));
        return;
    }

    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> sourceTexture;
    check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), sourceTexture.put_void()));

    QueuedFrame queuedFrame;
    queuedFrame.texture = cloneFrameTexture(sourceTexture.get());
    queuedFrame.timestamp = frame.SystemRelativeTime();

    {
        std::lock_guard lock(mutex_);

        if (emittedFrameCount_ == 0 && !firstFrame_) {
            firstFrame_ = std::move(queuedFrame);
        } else {
            frameQueue_.clear();
            frameQueue_.push_back(std::move(queuedFrame));
            while (frameQueue_.size() > kMaxQueuedFrames) {
                frameQueue_.pop_front();
            }
        }
    }

    frameCv_.notify_all();
}

void WgcVideoRecorderBackend::onCaptureClosed(
    const GraphicsCaptureItem&,
    const winrt::Windows::Foundation::IInspectable&)
{
    log(QStringLiteral("The selected window was closed. Finalizing recording."));
    requestStopInternal(QStringLiteral("The selected window was closed."));
}

void WgcVideoRecorderBackend::onStarting(
    const MediaStreamSource&,
    const MediaStreamSourceStartingEventArgs& args)
{
    auto firstFrame = waitForFrame(5s);
    if (!firstFrame) {
        requestStopInternal(QStringLiteral("No video frame arrived before transcoding started."));
        args.Request().SetActualStartPosition(0s);
        return;
    }

    {
        std::lock_guard lock(mutex_);
        firstFrame_ = std::move(firstFrame);
        emittedFrameCount_ = 0;
        lastEmittedAudioEnd100ns_ = 0;
        sampleClockStart_ = std::chrono::steady_clock::now();
        lastVideoSampleEnd_ = {};
        videoStreamEnded_ = false;
    }

    args.Request().SetActualStartPosition(0s);
}

void WgcVideoRecorderBackend::onSampleRequested(
    const MediaStreamSource&,
    const MediaStreamSourceSampleRequestedEventArgs& args)
{
    const auto descriptor = args.Request().StreamDescriptor();
    if (options_.audio.captureSystemAudio && audioStreamDescriptor_ && descriptor == audioStreamDescriptor_) {
        onAudioSampleRequested(args, descriptor);
        return;
    }

    onVideoSampleRequested(args, descriptor);
}

void WgcVideoRecorderBackend::onVideoSampleRequested(
    const MediaStreamSourceSampleRequestedEventArgs& args,
    const IMediaStreamDescriptor&)
{
    auto deferral = args.Request().GetDeferral();

    std::chrono::steady_clock::time_point dueTime {};
    {
        std::lock_guard lock(mutex_);
        if (sampleClockStart_ != std::chrono::steady_clock::time_point {}) {
            dueTime = sampleClockStart_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(frameDuration_ * emittedFrameCount_);
        }
    }

    if (!stopRequested_.load() && dueTime != std::chrono::steady_clock::time_point {}) {
        std::this_thread::sleep_until(dueTime);
    }

    auto frame = waitForFrame(frameWaitTimeout());
    if (!frame && !stopRequested_.load()) {
        const auto hwnd = reinterpret_cast<HWND>(options_.window.nativeHandle);
        if (!IsWindow(hwnd)) {
            log(QStringLiteral("The selected window handle is no longer valid. Finalizing recording."));
            requestStopInternal(QStringLiteral("The selected window handle became invalid."));
        } else if (IsIconic(hwnd)) {
            log(QStringLiteral("The selected window was minimized. Current build stops recording to avoid invalid output."));
            requestStopInternal(QStringLiteral("The selected window was minimized."));
        }
    }

    if (!frame && !stopRequested_.load()) {
        std::lock_guard lock(mutex_);
        if (lastDeliveredFrame_) {
            frame = lastDeliveredFrame_;
        }
    }

    if (!frame) {
        {
            std::lock_guard lock(mutex_);
            videoStreamEnded_ = true;
        }
        args.Request().Sample(nullptr);
        deferral.Complete();
        return;
    }

    TimeSpan sampleTimestamp {};
    {
        std::lock_guard lock(mutex_);
        lastDeliveredFrame_ = frame;
        sampleTimestamp = frameDuration_ * emittedFrameCount_;
        lastVideoSampleEnd_ = sampleTimestamp + frameDuration_;
        videoStreamEnded_ = false;
        ++emittedFrameCount_;
    }

    auto surface = createDirect3DSurface(frame->texture.get());
    auto sample = MediaStreamSample::CreateFromDirect3D11Surface(surface, sampleTimestamp);
    sample.Duration(frameDuration_);
    args.Request().Sample(sample);

    deferral.Complete();
}

void WgcVideoRecorderBackend::onAudioSampleRequested(
    const MediaStreamSourceSampleRequestedEventArgs& args,
    const IMediaStreamDescriptor&)
{
    auto deferral = args.Request().GetDeferral();

    bool shouldLogFirstAudioRequest = false;
    {
        std::lock_guard lock(mutex_);
        shouldLogFirstAudioRequest = !audioSampleRequestedObserved_;
        audioSampleRequestedObserved_ = true;
    }

    if (shouldLogFirstAudioRequest) {
        log(QStringLiteral("MediaTranscoder requested the first audio sample."));
    }

    waitForAudioPacket(audioWaitTimeout());

    std::optional<QueuedAudioPacket> packet;
    {
        std::lock_guard lock(mutex_);

        while (!audioQueue_.empty()) {
            auto nextPacket = std::move(audioQueue_.front());
            audioQueue_.pop_front();

            const std::int64_t overlap100ns = lastEmittedAudioEnd100ns_ - nextPacket.timestamp.count();
            if (overlap100ns > kAudioGapTolerance100ns) {
                const uint32_t trimFrames = hundredNsToAudioFramesCeil(overlap100ns);
                trimAudioPacketFront(nextPacket, trimFrames);
            }

            if (nextPacket.frameCount == 0 || nextPacket.pcm.isEmpty()) {
                continue;
            }

            if (nextPacket.timestamp.count() > lastEmittedAudioEnd100ns_ + kAudioGapTolerance100ns) {
                const std::int64_t gap100ns = nextPacket.timestamp.count() - lastEmittedAudioEnd100ns_;
                const std::int64_t silenceDuration100ns = std::min(gap100ns, kDefaultSilenceChunk100ns);
                const uint32_t silenceFrames = std::max<uint32_t>(1, hundredNsToAudioFramesCeil(silenceDuration100ns));
                packet = createSilenceAudioPacket(lastEmittedAudioEnd100ns_, silenceFrames);
                lastEmittedAudioEnd100ns_ = packet->timestamp.count() + packet->duration.count();
                audioQueue_.push_front(std::move(nextPacket));
                break;
            }

            if (nextPacket.timestamp.count() < lastEmittedAudioEnd100ns_) {
                nextPacket.timestamp = TimeSpan {lastEmittedAudioEnd100ns_};
            }

            lastEmittedAudioEnd100ns_ = nextPacket.timestamp.count() + nextPacket.duration.count();
            packet = std::move(nextPacket);
            break;
        }

        if (!packet) {
            if (!stopRequested_.load()) {
                const uint32_t silenceFrames = std::max<uint32_t>(1, hundredNsToAudioFramesCeil(kDefaultSilenceChunk100ns));
                packet = createSilenceAudioPacket(lastEmittedAudioEnd100ns_, silenceFrames);
                lastEmittedAudioEnd100ns_ = packet->timestamp.count() + packet->duration.count();
            } else {
                const std::int64_t desiredAudioEnd100ns = lastVideoSampleEnd_.count();
                if (lastEmittedAudioEnd100ns_ < desiredAudioEnd100ns) {
                    const std::int64_t gap100ns = desiredAudioEnd100ns - lastEmittedAudioEnd100ns_;
                    const uint32_t silenceFrames = std::max<uint32_t>(
                        1,
                        hundredNsToAudioFramesCeil(std::min(gap100ns, kDefaultSilenceChunk100ns)));
                    packet = createSilenceAudioPacket(lastEmittedAudioEnd100ns_, silenceFrames);
                    lastEmittedAudioEnd100ns_ = packet->timestamp.count() + packet->duration.count();
                } else if (!videoStreamEnded_
                    && lastEmittedAudioEnd100ns_ <= desiredAudioEnd100ns + kDefaultSilenceChunk100ns) {
                    const uint32_t silenceFrames = std::max<uint32_t>(1, hundredNsToAudioFramesCeil(kDefaultSilenceChunk100ns));
                    packet = createSilenceAudioPacket(lastEmittedAudioEnd100ns_, silenceFrames);
                    lastEmittedAudioEnd100ns_ = packet->timestamp.count() + packet->duration.count();
                }
            }
        }
    }

    if (!packet) {
        args.Request().Sample(nullptr);
        deferral.Complete();
        return;
    }

    auto buffer = createAudioBuffer(packet->pcm);
    auto sample = MediaStreamSample::CreateFromBuffer(buffer, packet->timestamp);
    sample.Duration(packet->duration);
    args.Request().Sample(sample);

    deferral.Complete();
}
