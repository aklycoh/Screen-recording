param(
    [string]$SourceDir = "D:\game\mp4",
    [string]$BuildDir = "D:\game\mp4\build",
    [string]$QtPrefix = "D:\Qt\6.8.2\msvc2022_64",
    [string]$CMakeExe = "C:\Program Files\CMake\bin\cmake.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $CMakeExe)) {
    throw "CMake not found at $CMakeExe"
}

if (-not (Test-Path $QtPrefix)) {
    throw "Qt not found at $QtPrefix"
}

$windeployqt = Join-Path $QtPrefix "bin\windeployqt.exe"
$exe = Join-Path $BuildDir "Debug\mp4_recorder.exe"

& $CMakeExe -S $SourceDir -B $BuildDir -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=$QtPrefix
& $CMakeExe --build $BuildDir --config Debug

if (-not (Test-Path $windeployqt)) {
    throw "windeployqt not found at $windeployqt"
}

if (-not (Test-Path $exe)) {
    throw "Executable not found at $exe after build"
}

& $windeployqt --debug --force $exe
