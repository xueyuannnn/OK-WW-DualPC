$ErrorActionPreference = 'Stop'
$dependencyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../deps'))
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
$archive = Join-Path $dependencyRoot 'opencv-4.12.0-windows.exe'
$expected = 'b753b14d880b9bc8d89d6acd3b665c040baec0211078435432fcae117db707af'
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://github.com/opencv/opencv/releases/download/4.12.0/opencv-4.12.0-windows.exe' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'OpenCV archive checksum differs from the official release asset digest'
}
$config = Join-Path $dependencyRoot 'opencv/build/OpenCVConfig.cmake'
if (!(Test-Path -LiteralPath $config)) {
    $process = Start-Process -FilePath $archive -ArgumentList @('-y', "-o`"$dependencyRoot`"") -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "OpenCV extraction failed: $($process.ExitCode)" }
}
if (!(Test-Path -LiteralPath $config)) { throw 'OpenCV SDK is incomplete' }
Write-Output $config
