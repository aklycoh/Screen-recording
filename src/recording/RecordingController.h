#pragma once

#include "common/OperationResult.h"
#include "core/RecordingTypes.h"
#include "platform/windows/WindowEnumerator.h"
#include "recording/RecordingSession.h"

#include <QObject>

class RecordingController : public QObject
{
    Q_OBJECT

public:
    explicit RecordingController(WindowEnumerator& windowEnumerator, QObject* parent = nullptr);

    const QList<WindowInfo>& availableWindows() const;
    bool isRecording() const;

    OperationResult refreshWindows();
    OperationResult startRecording(const RecordingOptions& options);
    OperationResult stopRecording();

signals:
    void windowsChanged(const QList<WindowInfo>& windows);
    void stateChanged(RecordingState state, const QString& detail);
    void logMessage(const QString& message);

private:
    WindowEnumerator& windowEnumerator_;
    RecordingSession session_;
    QList<WindowInfo> availableWindows_;
};

