#include "recording/RecordingController.h"

#include <QString>

namespace
{
QString describeTarget(const CaptureTarget& target)
{
    if (target.type == CaptureTargetType::Region) {
        const QString displayName = !target.display.name.isEmpty()
            ? target.display.name
            : QStringLiteral("Display");
        return QStringLiteral("%1 region [%2]").arg(displayName, describeCaptureRegion(target.region));
    }

    if (target.type == CaptureTargetType::Display) {
        if (!target.display.name.isEmpty()) {
            return target.display.name;
        }
        return QStringLiteral("Display");
    }

    if (!target.window.title.isEmpty()) {
        return target.window.title;
    }
    return QStringLiteral("Window");
}
}

RecordingController::RecordingController(
    WindowEnumerator& windowEnumerator,
    MonitorEnumerator& monitorEnumerator,
    QObject* parent)
    : QObject(parent)
    , windowEnumerator_(windowEnumerator)
    , monitorEnumerator_(monitorEnumerator)
    , session_(this)
{
    connect(&session_, &RecordingSession::stateChanged, this, [this](RecordingState state, const QString& detail) {
        emit stateChanged(state, detail);
        emit logMessage(QStringLiteral("[%1] %2").arg(describeRecordingState(state), detail));
    });
}

const QList<WindowInfo>& RecordingController::availableWindows() const
{
    return availableWindows_;
}

const QList<DisplayInfo>& RecordingController::availableDisplays() const
{
    return availableDisplays_;
}

bool RecordingController::isRecording() const
{
    const RecordingState state = session_.state();
    return state == RecordingState::Recording
        || state == RecordingState::Preparing
        || state == RecordingState::Finalizing;
}

OperationResult RecordingController::refreshCaptureTargets()
{
    availableWindows_ = windowEnumerator_.enumerateWindows();
    availableDisplays_ = monitorEnumerator_.enumerateMonitors();
    emit windowsChanged(availableWindows_);
    emit displaysChanged(availableDisplays_);

    if (availableWindows_.isEmpty() && availableDisplays_.isEmpty()) {
        return OperationResult::failure(QStringLiteral("No capture targets were found."));
    }

    emit logMessage(QStringLiteral("Enumerated %1 windows and %2 displays.")
                        .arg(availableWindows_.size())
                        .arg(availableDisplays_.size()));
    return OperationResult::success(QStringLiteral("Capture targets refreshed."));
}

OperationResult RecordingController::startRecording(const RecordingOptions& options)
{
    emit logMessage(QStringLiteral("Starting session for \"%1\".").arg(describeTarget(options.target)));
    emit logMessage(QStringLiteral("Output file: %1").arg(options.output.outputFilePath));
    emit logMessage(QStringLiteral("Audio: system=%1 microphone=%2")
                        .arg(options.audio.captureSystemAudio ? QStringLiteral("on") : QStringLiteral("off"))
                        .arg(options.audio.captureMicrophone ? QStringLiteral("on") : QStringLiteral("off")));
    emit logMessage(QStringLiteral("Video: %1 fps, max width %2, bitrate %3 kbps")
                        .arg(options.video.targetFps)
                        .arg(options.video.maxOutputWidth)
                        .arg(options.video.videoBitrateKbps));
    return session_.start(options);
}

OperationResult RecordingController::stopRecording()
{
    return session_.stop();
}
