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
    WindowInfo window;
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

