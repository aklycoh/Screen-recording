cmake_minimum_required(VERSION 3.24)

project(mp4_recorder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(mp4_recorder
    src/app/main.cpp
    src/app/ApplicationContext.h
    src/app/ApplicationContext.cpp
    src/common/OperationResult.h
    src/core/RecordingTypes.h
    src/platform/windows/WindowEnumerator.h
    src/platform/windows/WindowEnumerator.cpp
    src/platform/windows/MonitorEnumerator.h
    src/platform/windows/MonitorEnumerator.cpp
    src/platform/windows/WinRtHelpers.h
    src/platform/windows/WinRtHelpers.cpp
    src/platform/windows/WasapiLoopbackCapture.h
    src/platform/windows/WasapiLoopbackCapture.cpp
    src/platform/windows/WgcVideoRecorderBackend.h
    src/platform/windows/WgcVideoRecorderBackend.cpp
    src/recording/IRecordingBackend.h
    src/recording/RecordingController.h
    src/recording/RecordingController.cpp
    src/recording/RecordingSession.h
    src/recording/RecordingSession.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/pipeline/Packets.h
    src/capture/ICaptureSource.h
    src/audio/IAudioCaptureSource.h
    src/encoding/IVideoEncoder.h
    src/encoding/IAudioEncoder.h
    src/muxing/IMp4Muxer.h
)

target_include_directories(mp4_recorder PRIVATE src)

target_link_libraries(mp4_recorder
    PRIVATE
        Qt6::Widgets
        d2d1
        d3d11
        dwmapi
        dxgi
        ole32
        runtimeobject
        shcore
        shlwapi
        windowsapp
)

if(MSVC)
    target_compile_options(mp4_recorder PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

target_compile_definitions(mp4_recorder PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
