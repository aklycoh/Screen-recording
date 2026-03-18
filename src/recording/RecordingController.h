#pragma once

#include "common/OperationResult.h"
#include "core/RecordingTypes.h"
#include "platform/windows/MonitorEnumerator.h"
#include "platform/windows/WindowEnumerator.h"
#include "recording/RecordingSession.h"

#include <QObject>

class RecordingController : public QObject
{
    Q_OBJECT

public:
    explicit RecordingController(
        WindowEnumerator& windowEnumerator,
        MonitorEnumerator& monitorEnumerator,
        QObject* parent = nullptr);

    const QList<WindowInfo>& availableWindows() const;
    const QList<DisplayInfo>& availableDisplays() const;
    bool isRecording() const;

    OperationResult refreshCaptureTargets();
    OperationResult startRecording(const RecordingOptions& options);
    OperationResult stopRecording();

signals:
    void windowsChanged(const QList<WindowInfo>& windows);
    void displaysChanged(const QList<DisplayInfo>& displays);
    void stateChanged(RecordingState state, const QString& detail);
    void logMessage(const QString& message);

private:
    WindowEnumerator& windowEnumerator_;
    MonitorEnumerator& monitorEnumerator_;
    RecordingSession session_;
    QList<WindowInfo> availableWindows_;
    QList<DisplayInfo> availableDisplays_;
};
