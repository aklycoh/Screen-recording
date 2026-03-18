# MP4 Recorder Architecture

## Product positioning

This project is a Windows-only local screen recorder optimized for:

- selecting one window
- recording stable video
- exporting MP4
- keeping phone playback compatibility higher than extreme fidelity

It is not a live streaming product and it does not prioritize complex editing.

## Core decisions

### Stable route

- Desktop framework: C++20 + Qt 6 Widgets
- Window capture: Windows.Graphics.Capture
- Audio capture: WASAPI loopback + optional microphone input
- Video encoding: Media Foundation H.264
- Audio encoding: Media Foundation AAC
- Container: MP4
- Reliability strategy: temp file, explicit finalize, session state machine, detailed logs

### Fast landing route

- Desktop framework: C++20 + Qt 6 Widgets
- Window capture: Windows.Graphics.Capture
- Audio/video encode and mux: FFmpeg
- Tradeoff: faster implementation, larger dependency surface, weaker native integration consistency

## Why this route

- `Windows.Graphics.Capture` is the best fit for single-window capture on modern Windows.
- `WASAPI` is the native choice for system audio and microphone capture.
- `Media Foundation` keeps MP4, H.264, and AAC on the native stack and is friendlier to phone compatibility goals.
- `Qt Widgets` is enough for an efficient control-oriented desktop UI and keeps future expansion manageable.

## MVP scope

- enumerate capturable windows
- select one window
- choose output path
- optional system audio
- optional microphone
- start recording
- stop recording
- export MP4 playable on Windows and phones
- handle window close, resize, minimized, low disk, encoder failure, and interrupted stop

## Recommended defaults

- FPS: 30
- max output width: 1280
- video codec: H.264 High Profile when supported, otherwise Main
- video bitrate:
  - 720p: 3.5 to 4.5 Mbps
  - 1080p: 5.5 to 8 Mbps
- audio codec: AAC LC
- audio sample rate: 48 kHz
- audio channels: stereo for system mix, mono or stereo for microphone path, output mixed to stereo
- audio bitrate: 128 kbps AAC

## Reliability rules

- every recording session owns a temp working directory
- write to temp `.mp4.part`
- finalize and atomically rename on success
- keep session metadata for recovery
- stop with a bounded shutdown timeout
- flush muxer before completing session
- use timestamp-driven A/V sync instead of frame-count sync

## Current implementation status

- Qt desktop shell
- window enumeration
- asynchronous recording state machine
- `Windows.Graphics.Capture` video capture backend
- `WASAPI loopback` system audio backend
- `MediaStreamSource + MediaTranscoder` based H.264 MP4 export
- temp file finalization with `.part` rename
- microphone controls intentionally disabled until the second audio phase is added

## Next implementation order

1. microphone capture and mixer
2. resize-safe scaling path
3. low-disk and recovery metadata
4. long-recording soak tests
