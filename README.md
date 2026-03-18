# MP4 Recorder

Windows desktop recorder focused on one job: capture a single window and export an MP4 that is easy to share to a phone.

## Current state

This repository currently contains:

- an engineering architecture document in `docs/architecture.md`
- a Qt 6 + CMake runnable shell
- a Win32 window enumerator
- a real `Windows.Graphics.Capture -> MediaTranscoder -> MP4` video path
- recording pipeline interfaces for capture, audio, encoding, and MP4 muxing

Current implementation status:

- video MP4 recording is wired
- `WASAPI loopback` system-audio recording is wired
- microphone capture is intentionally still disabled
- window resize currently stops the session to protect output integrity
- minimizing the recorded window currently stops the session

## Recommended stack

- Visual Studio 2022
- CMake 3.24+
- Qt 6.5+
- Windows 10/11 SDK

## Configure

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.2\msvc2022_64"
cmake --build build --config Debug
```

## Run

```powershell
.\build\Debug\mp4_recorder.exe
```

## One-command build in the current shell

If your current PowerShell session has not refreshed PATH after installation, use:

```powershell
.\scripts\build-debug.ps1
.\scripts\run-debug.ps1
```

`build-debug.ps1` will also run `windeployqt` so the Debug executable can find the required Qt DLLs and plugins.

For recording quality validation, prefer Release:

```powershell
.\scripts\build-release.ps1
.\scripts\run-release.ps1
```

## Next implementation step

1. Add microphone capture.
2. Mix system audio and microphone into a single AAC track for phone playback.
3. Add resize-safe scaling instead of stop-on-resize.
4. Add disk-space checks and crash recovery metadata.
