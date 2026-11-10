param(
    [string]$QtRoot = $env:OKWW_QT_ROOT,
    [string]$OpenCvRoot = $env:OKWW_OPENCV_ROOT,
    [string]$OpenVinoRoot = $env:OKWW_OPENVINO_ROOT,
    [switch]$Test,
    [switch]$Full,
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if (!$OpenCvRoot) { $OpenCvRoot = Join-Path $projectRoot 'native/deps/opencv/build' }
if (!$OpenVinoRoot) { $OpenVinoRoot = Join-Path $projectRoot '.venv/Lib/site-packages/openvino' }
if (!$QtRoot) { throw '请传入 -QtRoot，或设置 OKWW_QT_ROOT。详见 docs/development.md。' }

# Keep -Full for existing build commands; the standard target is now the C++ runtime.
$arguments = @{
    QtRoot = $QtRoot
    OpenCvRoot = $OpenCvRoot
    OpenVinoRoot = $OpenVinoRoot
    Test = $Test
}
if ($BuildDirectory) { $arguments.BuildDirectory = $BuildDirectory }
& (Join-Path $projectRoot 'native/tools/Build-PortableSource.ps1') @arguments
