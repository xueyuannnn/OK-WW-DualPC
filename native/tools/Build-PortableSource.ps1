param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$OpenCvRoot,
    [Parameter(Mandatory=$true)][string]$OpenVinoRoot,
    [switch]$Test,
    [switch]$Personal,
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw '请安装 Visual Studio 2022 C++ 桌面开发工具。' }
foreach ($sdk in @(
    (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'),
    (Join-Path $OpenCvRoot 'OpenCVConfig.cmake'),
    (Join-Path $OpenVinoRoot 'cmake/OpenVINOConfig.cmake')
)) {
    if (!(Test-Path -LiteralPath $sdk)) { throw "找不到 SDK 配置：$sdk。详见 docs/development.md。" }
}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw '请安装 Visual Studio 2022 的 C++ 桌面开发组件。' }
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build = if($BuildDirectory) {[IO.Path]::GetFullPath($BuildDirectory)} else {Join-Path $projectRoot 'build-native'}
$personalBuild = if ($Personal) { 'ON' } else { 'OFF' }
& $cmake -S (Join-Path $projectRoot 'native') -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot" "-DOpenCV_DIR=$OpenCvRoot" "-DOpenVINO_DIR=$OpenVinoRoot/cmake" '-DOKWW_FULL_NATIVE=ON' '-DOKWW_BUILD_COMPAT=OFF' "-DOKWW_BUILD_PERSONAL=$personalBuild"
if ($LASTEXITCODE) { throw 'CMake 配置失败。' }
if ($Test) { & $cmake --build $build --config Release --parallel 4 }
else {
    & $cmake --build $build --config Release --parallel 4 --target OKWWNativeDev
    if (!$LASTEXITCODE -and $Personal) {
        & $cmake --build $build --config Release --parallel 4 --target okww_team_lucy okww_team_cart okww_team_denia okww_team_aemeath okww_team_hsin
    }
}
if ($LASTEXITCODE) { throw 'C++ 编译失败。' }
if ($Test) {
    $oldPath=$env:PATH
    try {
        $env:PATH="$QtRoot/bin;$OpenCvRoot/x64/vc16/bin;$OpenVinoRoot/libs;$OpenVinoRoot/runtime/bin/intel64/Release;$projectRoot;$oldPath"
        & (Join-Path (Split-Path $cmake -Parent) 'ctest.exe') --test-dir $build -C Release --output-on-failure
        if ($LASTEXITCODE) { throw '原生测试未通过。' }
    } finally { $env:PATH=$oldPath }
}
