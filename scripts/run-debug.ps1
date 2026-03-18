param(
    [string]$BuildDir = "D:\game\mp4\build",
    [string]$QtPrefix = "D:\Qt\6.8.2\msvc2022_64"
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $BuildDir "Debug\mp4_recorder.exe"
if (-not (Test-Path $exe)) {
    throw "Executable not found at $exe. Run scripts\\build-debug.ps1 first."
}

$qtBin = Join-Path $QtPrefix "bin"
if (-not (Test-Path $qtBin)) {
    throw "Qt bin not found at $qtBin"
}

$env:Path = "$qtBin;$env:Path"
$env:QT_PLUGIN_PATH = Join-Path $QtPrefix "plugins"

Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
