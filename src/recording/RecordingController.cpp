#include "recording/RecordingController.h"

#include <QString>

namespace
{
QString describeState(RecordingState state)
{
    switch (state) {
    case RecordingState::Idle:
        return QStringLiteral("Idle");
    case RecordingState::Preparing:
        return QStringLiteral("Preparing");
    case RecordingState::Recording:
        return QStringLiteral("Recording");
    case RecordingState::Finalizing:
        return QStringLiteral("Finalizing");
    case RecordingState::Failed:
        return QStringLiteral("Failed");
    }

    return QStringLiteral("Unknown");
}
}

RecordingController::RecordingController(WindowEnumerator& windowEnumerator, QObject* parent)
    : QObject(parent)
    , windowEnumerator_(windowEnumerator)
    , session_(this)
{
    connect(&session_, &RecordingSession::stateChanged, this, [this](RecordingState state, const QString& detail) {
        emit stateChanged(state, detail);
        emit logMessage(QStringLiteral("[%1] %2").arg(describeState(state), detail));
    });
}

const QList<WindowInfo>& RecordingController::availableWindows() const
{
    return availableWindows_;
}

bool RecordingController::isRecording() const
{
    return session_.state() == RecordingState::Recording
        || session_.state() == RecordingState::Preparing
        || session_.state() == RecordingState::Finalizing;
}

OperationResult RecordingController::refreshWindows()
{
    availableWindows_ = windowEnumerator_.enumerateWindows();
    emit windowsChanged(availableWindows_);

    if (availableWindows_.isEmpty()) {
        return OperationResult::failure(QStringLiteral("No visible top-level windows were found."));
    }

    emit logMessage(QStringLiteral("Enumerated %1 candidate windows.").arg(availableWindows_.size()));
    return OperationResult::success(QStringLiteral("Window list refreshed."));
}

OperationResult RecordingController::startRecording(const RecordingOptions& options)
{
    emit logMessage(QStringLiteral("Starting session for \"%1\".").arg(options.window.title));
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
