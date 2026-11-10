param(
    [switch]$Setup,
    [switch]$Check,
    [switch]$Diagnose,
    [switch]$Configure,
    [switch]$UpdateCheck,
    [switch]$PrepareUpdate
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$env:PYTHONIOENCODING = 'utf-8'
$taskRoot = Split-Path -Parent $PSScriptRoot
# The upstream OCR loader requires an ASCII application path. A directory
# junction keeps one source copy while giving the runtime an ASCII entrypoint.
$taskAliasBase = Join-Path $env:LOCALAPPDATA 'OKWW-DualPC'
if ($taskAliasBase -match '[^\x00-\x7F]') {
    $taskAliasBase = Join-Path $env:PUBLIC 'Documents\OKWW-DualPC'
    if ($taskAliasBase -match '[^\x00-\x7F]') { throw 'An ASCII runtime path is required by the upstream OCR loader' }
}
$taskHasher = [Security.Cryptography.SHA256]::Create()
try { $taskRootHash = [BitConverter]::ToString($taskHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($taskRoot.ToLowerInvariant()))).Replace('-','').Substring(0,12) }
finally { $taskHasher.Dispose() }
$taskAlias = Join-Path $taskAliasBase "app-$taskRootHash"
if (-not (Test-Path -LiteralPath $taskAliasBase)) { New-Item -ItemType Directory -Path $taskAliasBase | Out-Null }
if (-not (Test-Path -LiteralPath $taskAlias)) {
    New-Item -ItemType Junction -Path $taskAlias -Target $taskRoot | Out-Null
} else {
    $taskExisting = Get-Item -LiteralPath $taskAlias
    if ($taskExisting.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath(@($taskExisting.Target)[0]).TrimEnd('\') -ne [IO.Path]::GetFullPath($taskRoot).TrimEnd('\')) {
        throw "Runtime alias already exists with a different target: $taskAlias"
    }
}
Push-Location -LiteralPath $taskAlias
try {
    $taskPython = Join-Path $taskAlias 'runtime\python.exe'
    if (-not (Test-Path -LiteralPath $taskPython)) {
        $taskPython = Join-Path $taskAlias '.venv\Scripts\python.exe'
    }
    if (-not (Test-Path -LiteralPath $taskPython)) {
        & python -m venv (Join-Path $taskAlias '.venv')
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the Python environment' }
        $Setup = $true
    }
    if ($Setup) {
        $env:OK_SCRIPT_BUILD_VERSION = '2.0.8+dualpc.1'
        & $taskPython -m pip install -r (Join-Path $taskAlias 'requirements-dualpc.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
    }
    if ($UpdateCheck -or $PrepareUpdate) {
        $taskUpdateArgs = @((Join-Path $taskAlias 'tools_dualpc\prepare_update.py'))
        if ($PrepareUpdate) { $taskUpdateArgs += '--prepare' }
        & $taskPython @taskUpdateArgs
    } else {
        $taskLaunchArgs = @((Join-Path $taskAlias 'main_dualpc.py'))
        if ($Check) { $taskLaunchArgs += '--check' }
        if ($Diagnose) { $taskLaunchArgs += '--diagnose' }
        if ($Configure) { $taskLaunchArgs += '--setup' }
        & $taskPython @taskLaunchArgs
    }
    exit $LASTEXITCODE
} finally { Pop-Location }
