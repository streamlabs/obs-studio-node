param([int]$Parent, [string]$Marker)
$ErrorActionPreference = 'Stop'
Start-Sleep -Seconds 6
if (!(Test-Path -LiteralPath $Marker)) { exit }
$expected = (Resolve-Path 'streamlabs-build.app/distribute/obs-studio-node/obs64.exe').Path
$servers = @(Get-CimInstance Win32_Process -Filter "Name = 'obs64.exe'" | Where-Object {
    $_.ExecutablePath -eq $expected -and $_.CommandLine -notmatch '(?:^|\s)--type='
})
foreach ($server in $servers) {
    Write-Host "Recording diagnostic: server $($server.ProcessId), parent $($server.ParentProcessId), test $Parent"
    & ./obs-stacks.exe $server.ProcessId $expected
}
$logRoot = 'tests/osn-tests/osnData/slobs-client/node-obs/logs'
$log = Get-ChildItem -LiteralPath $logRoot -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($log) {
    Select-String -LiteralPath $log.FullName -Pattern 'Total frames output|Total drawn frames|Writing file|Browser Source, INIT|Adapter [0-9]|maximum audio buffering|audio is lagging|video is lagging|GPU acceleration|CPU Name|rendering lag|output stopped|Failed to create texture|Device Removed|D3D11 device|encoder.*failed' |
        Select-Object -Last 45 | ForEach-Object { Write-Host $_.Line }
}
