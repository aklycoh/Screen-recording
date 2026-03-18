#include "recording/RecordingSession.h"

#include "platform/windows/WgcVideoRecorderBackend.h"
#include "recording/IRecordingBackend.h"

#include <QMetaObject>

RecordingSession::RecordingSession(QObject* parent)
    : QObject(parent)
{
}

RecordingSession::~RecordingSession() = default;

OperationResult RecordingSession::start(const RecordingOptions& options)
{
    if (state_ != RecordingState::Idle && state_ != RecordingState::Failed) {
        return OperationResult::failure(QStringLiteral("A recording session is already active."));
    }

    if (options.target.type == CaptureTargetType::Window && options.target.window.nativeHandle == 0) {
        return OperationResult::failure(QStringLiteral("No window selected."));
    }

    if (options.target.type == CaptureTargetType::Display && options.target.display.nativeHandle == 0) {
        return OperationResult::failure(QStringLiteral("No display selected."));
    }

    if (options.output.outputFilePath.trimmed().isEmpty()) {
        return OperationResult::failure(QStringLiteral("Output file path is empty."));
    }

    lastOptions_ = options;
    backend_ = createBackend();
    if (!backend_) {
        return OperationResult::failure(QStringLiteral("Unable to create a recording backend."));
    }

    backend_->setLogCallback([this](const QString& message) {
        QMetaObject::invokeMethod(this, [this, message]() {
            emit stateChanged(state_, message);
        }, Qt::QueuedConnection);
    });

    backend_->setStartedCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            setState(RecordingState::Recording, QStringLiteral("Capture and MP4 transcode are running."));
        }, Qt::QueuedConnection);
    });

    backend_->setFinishedCallback([this](const OperationResult& result) {
        QMetaObject::invokeMethod(this, [this, result]() {
            if (!result.ok) {
                setState(RecordingState::Failed, result.message);
                resetBackend();
                return;
            }

            setState(RecordingState::Idle, result.message);
            resetBackend();
        }, Qt::QueuedConnection);
    });

    setState(RecordingState::Preparing, QStringLiteral("Initializing native recording backend."));
    const OperationResult result = backend_->start(options);
    if (!result.ok) {
        resetBackend();
        setState(RecordingState::Failed, result.message);
        return result;
    }

    return result;
}

OperationResult RecordingSession::stop()
{
    if (state_ != RecordingState::Recording && state_ != RecordingState::Preparing) {
        return OperationResult::failure(QStringLiteral("No active recording session."));
    }

    if (!backend_) {
        return OperationResult::failure(QStringLiteral("Recording backend is unavailable."));
    }

    setState(RecordingState::Finalizing, QStringLiteral("Stop requested. Waiting for MP4 finalization."));
    return backend_->requestStop();
}

RecordingState RecordingSession::state() const
{
    return state_;
}

RecordingOptions RecordingSession::lastOptions() const
{
    return lastOptions_;
}

void RecordingSession::setState(RecordingState state, const QString& detail)
{
    state_ = state;
    emit stateChanged(state_, detail);
}

std::unique_ptr<IRecordingBackend> RecordingSession::createBackend()
{
    return std::make_unique<WgcVideoRecorderBackend>();
}

void RecordingSession::resetBackend()
{
    backend_.reset();
}
