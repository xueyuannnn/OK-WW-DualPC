param(
    [Parameter(Mandatory=$true)][string]$Plan,
    [ValidateSet('Prepare','Install')][string]$Mode = 'Prepare',
    [switch]$NoRestart
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[AppContext]::SetSwitch('Switch.System.IO.UseLegacyPathHandling', $false)
[AppContext]::SetSwitch('Switch.System.IO.BlockLongPaths', $false)
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$job = $null
$log = $null
$changed = New-Object 'System.Collections.Generic.List[object]'
$committed = $false
$profileRelative = ''
$installLock = $null

function Join-File([string]$base, [string]$relative) { return [IO.Path]::Combine($base, $relative) }
function Exists([string]$path) { return [IO.File]::Exists($path) -or [IO.Directory]::Exists($path) }
function Write-Log([string]$message) {
    $line = (Get-Date -Format o) + ' ' + $message
    [IO.File]::AppendAllText($log, $line + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
    [Console]::WriteLine($message)
}
function Hash([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
function Full([string]$path) {
    $path = $path.Replace('/','\')
    if (-not $path.StartsWith('\\?\')) {
        if ($path -match '^[A-Za-z]:\\') { $path = '\\?\' + $path }
        elseif ($path.StartsWith('\\')) { $path = '\\?\UNC\' + $path.Substring(2) }
        else { throw 'Installation path must be absolute' }
    }
    return [IO.Path]::GetFullPath($path).TrimEnd('\')
}
# Normalize the command-line path and JSON paths identically. Calling
# GetFullPath before adding the extended prefix can expand 8.3 components.
$job = Full ([IO.Path]::GetDirectoryName($Plan))
$log = [IO.Path]::Combine($job, 'update.log')
function Inside([string]$base, [string]$relative) {
    if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or $relative.Contains('\') -or
        $relative -match '(^|/)\.\.?(/|$)' -or $relative -match '[\x00-\x1f]' -or
        $relative -match '(^|/)(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])([./]|$)' -or
        $relative -match '[. ](/|$)') { throw "Unsafe package path: $relative" }
    $target = Full (Join-File $base $relative)
    $prefix = (Full $base) + '\'
    if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path outside update scope' }
    # Refuse junctions, symbolic links and other reparse points on every ancestor.
    $current = $target
    while ($current.Length -ge (Full $base).Length) {
        if (Exists $current) {
            $attributes = [IO.File]::GetAttributes($current)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point in update path: $current" }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if (-not $parent -or $parent -eq $current) { break }
        $current = $parent
    }
    return $target
}
function Protected([string]$relative) {
    if ($profileRelative -and ($relative -eq $profileRelative -or
        $relative.StartsWith($profileRelative+'/', [StringComparison]::OrdinalIgnoreCase))) { return $true }
    return $relative -match '^(native-full-data|native-teams|configs-dualpc|configs|ok_tasks|ok_import|ok_templates|workshop-library|\.updates)(/|$)' -or
        $relative -match '^(dualpc\.json|native-device-settings\.json|native-ui\.json)$' -or
        $relative -match '^native/personal(/|$)' -or
        $relative -match '^workshop-engine/(configs|logs|cache|screenshots|ok_tasks)(/|$)'
}
function Save-Json([string]$path, $value) {
    [IO.File]::WriteAllText($path, ($value | ConvertTo-Json -Depth 30), (New-Object Text.UTF8Encoding($false)))
}
function Read-Manifest([string]$stage) {
    $manifestFile = Inside $stage 'PACKAGE.json'
    if ((New-Object IO.FileInfo $manifestFile).Length -gt 16777216) { throw 'Package manifest too large' }
    $manifest = [IO.File]::ReadAllText($manifestFile) | ConvertFrom-Json
    if ($manifest.variant -ne 'full' -or $manifest.runtime -ne 'C++ + Python workshop') { throw 'Not an ordinary complete package' }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $manifest.files) {
        $relative = [string]$entry.path
        if ((Protected $relative) -or $relative -eq 'PACKAGE.json' -or -not $seen.Add($relative)) { throw "Forbidden or duplicate manifest path: $relative" }
        $file = Inside $stage $relative
        if ($entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or -not [IO.File]::Exists($file) -or
            (New-Object IO.FileInfo $file).Length -ne $entry.bytes -or (Hash $file) -ne $entry.sha256) {
            throw "Package file verification failed: $relative"
        }
    }
    if (-not $seen.Contains('OKWW-DualPC-Native-Dev.exe') -or
        -not $seen.Contains('workshop-engine/runtime/python.exe') -or
        (Hash (Inside $stage 'OKWW-DualPC-Native-Dev.exe')) -ne $manifest.exe_sha256) { throw 'Incomplete application package' }
    foreach ($file in [IO.Directory]::EnumerateFiles($stage, '*', [IO.SearchOption]::AllDirectories)) {
        $relative = $file.Substring($stage.Length + 1).Replace('\','/')
        if ($relative -ne 'PACKAGE.json' -and -not $seen.Contains($relative)) { throw "Unlisted package file: $relative" }
    }
    return $manifest
}
try {
    $spec = [IO.File]::ReadAllText($Plan) | ConvertFrom-Json
    $root = Full ([string]$spec.root)
    $profile = Full ([string]$spec.data)
    if ($profile -eq $root) { throw 'Profile must have a separate directory' }
    if ($profile.StartsWith($root+'\', [StringComparison]::OrdinalIgnoreCase)) {
        $profileRelative = $profile.Substring($root.Length+1).Replace('\','/')
    }
    if ($spec.schema -ne 1 -or $spec.sha256 -notmatch '^[0-9a-f]{64}$' -or
        (Full $spec.job) -ne $job -or $job -ne (Full (Join-File $root ('.updates/' + [IO.Path]::GetFileName($job)))) -or
        -not [IO.File]::Exists((Join-File $root 'OKWW-DualPC-Native-Dev.exe'))) { throw 'Invalid installation plan' }
    $null = Inside $root ('.updates/' + [IO.Path]::GetFileName($job) + '/plan.json')
    $stage = Join-File $job 'stage'
    $package = Join-File $job 'package.zip'
    if ($Mode -eq 'Prepare') {
        Write-Log 'Verifying download'
        if ((Hash $package) -ne $spec.sha256) { throw 'Archive checksum mismatch' }
        if (Exists $stage) { throw 'Stage already exists; retry with a new update directory' }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($package)
        try {
            $total = [long]0
            $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
            if ($archive.Entries.Count -gt 100000) { throw 'Too many archive entries' }
            foreach ($entry in $archive.Entries) {
                if (-not $entry.FullName.StartsWith('OKWW-DualPC-full/', [StringComparison]::Ordinal)) { throw 'Unexpected archive root' }
                $relative = $entry.FullName.Substring('OKWW-DualPC-full/'.Length)
                if (-not $relative) { continue }
                $isDirectory = $relative.EndsWith('/')
                $relative = $relative.TrimEnd('/')
                $null = Inside $stage $relative
                if ((Protected $relative) -or -not $seen.Add($relative) -or
                    (($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960) { throw "Forbidden archive entry: $relative" }
                $total += $entry.Length
                if ($total -gt 12GB -or $entry.Length -gt 4GB) { throw 'Expanded package too large' }
            }
            $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot([string]$spec.root))
            if ($drive.AvailableFreeSpace -lt ($total + 512MB)) { throw 'Not enough free disk space for extraction' }
            $null = [IO.Directory]::CreateDirectory($stage)
            Write-Log 'Extracting complete package'
            foreach ($entry in $archive.Entries) {
                $relative = $entry.FullName.Substring('OKWW-DualPC-full/'.Length)
                if (-not $relative -or $relative.EndsWith('/')) { continue }
                $destination = Inside $stage $relative
                $null = [IO.Directory]::CreateDirectory(([IO.Path]::GetDirectoryName($destination)))
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $false)
            }
        } finally { $archive.Dispose() }
        Write-Log 'Verifying package files'
        $manifest = Read-Manifest $stage
        if ($manifest.release_version.TrimStart('v') -ne $spec.release.TrimStart('v')) { throw 'Package version does not match selected release' }
        Write-Log 'READY'
        exit 0
    }
    # Wait for this exact parent process; never stop a process or act on a reused PID.
    $parentProcess = Get-Process -Id $spec.pid -ErrorAction SilentlyContinue
    if ($parentProcess -and $parentProcess.StartTime.ToUniversalTime().Ticks.ToString() -eq [string]$spec.parent_start_ticks) {
        if (-not $parentProcess.WaitForExit(60000)) { throw 'Application has not exited; installation cancelled' }
    }
    $installLock = [IO.File]::Open((Inside $root '.updates/install.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $manifest = Read-Manifest $stage
    if ($manifest.release_version.TrimStart('v') -ne $spec.release.TrimStart('v')) { throw 'Package version does not match selected release' }
    $oldManifest = [IO.File]::ReadAllText((Inside $root 'PACKAGE.json')) | ConvertFrom-Json
    $files = New-Object 'System.Collections.Generic.List[object]'
    $newPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $manifest.files) {
        $relative = [string]$entry.path
        $null = $newPaths.Add($relative)
        $destination = Inside $root $relative
        if ([IO.File]::Exists($destination) -and (Hash $destination) -eq $entry.sha256) { continue }
        $files.Add([pscustomobject]@{ path=$relative; source=(Inside $stage $relative); remove=$false })
    }
    foreach ($entry in $oldManifest.files) {
        $relative = [string]$entry.path
        if ((Protected $relative) -or $relative -eq 'PACKAGE.json' -or $newPaths.Contains($relative)) { continue }
        $destination = Inside $root $relative
        if ([IO.File]::Exists($destination)) {
            # Unrecognized local modifications belong to the user, not the updater.
            if ((Hash $destination) -eq $entry.sha256) { $files.Add([pscustomobject]@{ path=$relative; source=''; remove=$true }) }
        }
    }
    if ($oldManifest.variant -eq 'personal') {
        $manifest.variant = 'personal'
        foreach ($entry in $oldManifest.files) { if (Protected ([string]$entry.path)) { $manifest.files += $entry } }
    }
    $manifest | Add-Member -NotePropertyName release_version -NotePropertyValue $spec.release -Force
    Save-Json (Join-File $job 'installed-manifest.json') $manifest
    $files.Add([pscustomobject]@{ path='PACKAGE.json'; source=(Join-File $job 'installed-manifest.json'); remove=$false })
    $backup = Join-File $job 'rollback'
    $null = [IO.Directory]::CreateDirectory($backup)
    $required = [long]512MB
    foreach ($item in $files) {
        $destination = Inside $root $item.path
        if ([IO.File]::Exists($destination)) { $required += (New-Object IO.FileInfo $destination).Length }
        if (-not $item.remove) { $required += (New-Object IO.FileInfo $item.source).Length }
    }
    $drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot([string]$spec.root))
    if ($drive.AvailableFreeSpace -lt $required) { throw 'Not enough disk space for update rollback' }
    # All backups and destination access checks finish before replacing the first file.
    foreach ($item in $files) {
        $destination = Inside $root $item.path
        $saved = Inside $backup $item.path
        $hadFile = [IO.File]::Exists($destination)
        if ($hadFile) {
            $probe = [IO.File]::Open($destination, 'Open', 'ReadWrite', 'None')
            $probe.Dispose()
            $null = [IO.Directory]::CreateDirectory(([IO.Path]::GetDirectoryName($saved)))
            [IO.File]::Copy($destination, $saved, $false)
        } elseif (Exists $destination) { throw "Destination is a directory: $destination" }
        $item | Add-Member -NotePropertyName existed -NotePropertyValue $hadFile
    }
    Save-Json (Join-File $job 'journal.json') $files.ToArray()
    Write-Log 'Installing update'
    foreach ($item in $files) {
        $destination = Inside $root $item.path
        $changed.Add($item)
        if ($item.remove) { [IO.File]::Delete($destination) }
        else {
            $null = [IO.Directory]::CreateDirectory(([IO.Path]::GetDirectoryName($destination)))
            [IO.File]::Copy($item.source, $destination, $true)
        }
    }
    $committed = $true
    Save-Json (Join-File $job 'result.json') @{ ok=$true; release=$spec.release; changed=$files.Count }
    Write-Log 'Update installed successfully'
    # The relaunch is stopped and disconnected; never carry over --run-task.
    $executable = [IO.Path]::Combine([string]$spec.root, 'OKWW-DualPC-Native-Dev.exe')
    $data = [string]$spec.data
    if ($data.Contains('"')) { throw 'Invalid profile directory' }
    # Only remove verified tool-owned stage/rollback paths under this exact job.
    foreach ($relative in @('stage','rollback')) {
        $target = Inside $job $relative
        if ((Full ([IO.Path]::GetDirectoryName($target))) -ne $job) { throw 'Cleanup scope changed' }
        [IO.Directory]::Delete($target, $true)
    }
    [IO.File]::Delete($package)
    $installLock.Dispose()
    $installLock = $null
    if (-not $NoRestart) { Start-Process -FilePath $executable -WorkingDirectory ([string]$spec.root) -ArgumentList @('--root', ('"'+$spec.root+'"'), '--data', ('"'+$data.TrimEnd('\')+'"')) }
    exit 0
} catch {
    $message = $_.Exception.Message
    Write-Log ("FAILED: " + $message)
    Write-Log $_.ScriptStackTrace
    if (-not $committed -and $changed.Count) {
        for ($index = $changed.Count - 1; $index -ge 0; $index--) {
            $item = $changed[$index]
            try {
                $destination = Inside $root $item.path
                if ($item.existed) { [IO.File]::Copy((Inside $backup $item.path), $destination, $true) }
                else { [IO.File]::Delete($destination) }
            } catch { Write-Log ('ROLLBACK FAILED: ' + $_.Exception.Message) }
        }
    }
    Save-Json (Join-File $job 'result.json') @{ ok=$false; error=$message; committed=$committed }
    if ($installLock) { $installLock.Dispose(); $installLock = $null }
    if ($Mode -eq 'Install' -and -not $NoRestart) {
        Add-Type -AssemblyName System.Windows.Forms
        $null = [Windows.Forms.MessageBox]::Show("更新未完成，请重新打开软件或前往 GitHub 下载完整包。`n诊断记录：$log", 'OK-WW-DualPC 更新')
    }
    exit 1
}
