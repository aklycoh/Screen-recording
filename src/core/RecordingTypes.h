#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QtGlobal>

struct WindowInfo
{
    quintptr nativeHandle {0};
    QString title;
    QString className;
    unsigned long processId {0};
    QSize windowSize;
    bool minimized {false};
};

struct DisplayInfo
{
    quintptr nativeHandle {0};
    QString name;
    QString deviceName;
    QSize pixelSize;
    QRect geometry;
    double dpiScale {1.0};
    bool primary {false};
};

enum class CaptureTargetType
{
    Window,
    Display,
    Region
};

struct CaptureRegion
{
    int x {0};
    int y {0};
    int width {0};
    int height {0};
};

struct CaptureTarget
{
    CaptureTargetType type {CaptureTargetType::Window};
    WindowInfo window;
    DisplayInfo display;
    CaptureRegion region;
};

struct AudioOptions
{
    bool captureSystemAudio {true};
    bool captureMicrophone {false};
};

struct VideoOptions
{
    int targetFps {30};
    int maxOutputWidth {1280};
    int videoBitrateKbps {4500};
};

struct OutputOptions
{
    QString outputFilePath;
};

struct RecordingOptions
{
    CaptureTarget target;
    AudioOptions audio;
    VideoOptions video;
    OutputOptions output;
};

enum class RecordingState
{
    Idle,
    Preparing,
    Recording,
    Finalizing,
    Failed
};

inline QString describeRecordingState(RecordingState state)
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

inline bool isValidCaptureRegion(const CaptureRegion& region)
{
    return region.width > 0 && region.height > 0;
}

inline QString describeCaptureRegion(const CaptureRegion& region)
{
    if (!isValidCaptureRegion(region)) {
        return QStringLiteral("No region selected");
    }

    return QStringLiteral("%1,%2  %3x%4")
        .arg(region.x)
        .arg(region.y)
        .arg(region.width)
        .arg(region.height);
}
