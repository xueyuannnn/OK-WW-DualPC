$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$report=Join-Path $projectRoot 'validation-dualpc/portrait-20261109'
$cache=Join-Path $report 'portrait-sources'
New-Item -ItemType Directory -Path $cache -Force|Out-Null
$items=@(Get-Content -LiteralPath (Join-Path $report 'portrait-downloads.json') -Raw|ConvertFrom-Json)
$results=$items|ForEach-Object -ThrottleLimit 6 -Parallel {
    $target=Join-Path $using:cache ($_.class+'.source')
    if($_.class -notmatch '^[A-Za-z0-9]+$' -or $_.url -notmatch '^https://(pack\.skowt\.cc/asset/|wutheringwaves\.kurogames\.com/static4\.0/assets/)') {throw 'Unreviewed portrait URL or filename'}
    try {
        Invoke-WebRequest -Uri $_.url -OutFile $target -TimeoutSec 25 -ErrorAction Stop
        [pscustomobject]@{name=$_.class;ok=$true}
    } catch {
        [pscustomobject]@{name=$_.class;ok=$false;error=$_.Exception.Message}
    }
}
$results|ConvertTo-Json -Depth 3|Set-Content -LiteralPath (Join-Path $report 'download-result.json') -Encoding utf8
$failed=@($results|Where-Object {!$_.ok})
if($failed.Count){$failed|Format-Table;throw "Failed portrait downloads: $($failed.Count)"}
[pscustomobject]@{downloaded=$results.Count;tls_validation=$true}
