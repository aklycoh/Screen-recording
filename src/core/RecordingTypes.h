#pragma once

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
    double dpiScale {1.0};
    bool primary {false};
};

enum class CaptureTargetType
{
    Window,
    Display
};

struct CaptureTarget
{
    CaptureTargetType type {CaptureTargetType::Window};
    WindowInfo window;
    DisplayInfo display;
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
