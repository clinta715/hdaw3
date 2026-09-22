# crash-diag.ps1 -- exit-code forensics for the HDAW engine + WER LocalDumps setup.
#
# Usage:
#   crash-diag.ps1 report          exit codes + dump status of recent captures
#   crash-diag.ps1 wer-on          enable WER LocalDumps (FULL dumps) for engine + plugin host
#   crash-diag.ps1 wer-off         disable WER LocalDumps
#
# Why WER LocalDumps: procdump's attach-mode monitor only catches UNHANDLED
# exceptions. The 2026-09-22 crashes were silent exits (no unhandled exception
# surfaced to procdump) -- WER LocalDumps catches fail-fast/abort/terminate paths
# procdump can miss, writing FULL dumps to %TEMP%\hdaw_crash_captures\wer\.

param([Parameter(Mandatory=$true)][string]$Action)

$captureRoot = Join-Path $env:TEMP 'hdaw_crash_captures'
$werKey = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\HDAW_headless_mcp.exe'
$werKeyHost = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\hdaw_plugin_host.exe'

switch ($Action) {
    'report' {
        Get-ChildItem $captureRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 8 |
            ForEach-Object {
                Write-Host ('== ' + $_.Name + '  ' + $_.LastWriteTime)
                $log = Join-Path $_.FullName 'procdump.log'
                if (Test-Path $log) {
                    $txt = (Get-Content $log -Raw) -replace ([char]0), ''
                    $txt -split "`n" | Where-Object { $_ -match 'Exception:|Process Exit|Dump 1|READY|FAILED' } |
                        ForEach-Object { Write-Host ('   ' + $_.Trim()) }
                }
                $dmps = Get-ChildItem $_.FullName -Filter '*.dmp' -ErrorAction SilentlyContinue
                if ($dmps) { $dmps | ForEach-Object { Write-Host ('   DUMP: ' + $_.Name + ' (' + [math]::Round($_.Length/1MB) + ' MB)') } }
            }
        # WER dumps too
        $werDumps = Get-ChildItem (Join-Path $captureRoot 'wer') -Filter '*.dmp' -ErrorAction SilentlyContinue
        if ($werDumps) { $werDumps | ForEach-Object { Write-Host ('WER DUMP: ' + $_.Name + ' (' + [math]::Round($_.Length/1MB) + ' MB)') } }
    }
    'wer-on' {
        foreach ($key in @($werKey, $werKeyHost)) {
            New-Item -Path $key -Force | Out-Null
            Set-ItemProperty -Path $key -Name DumpFolder -Value "$captureRoot\wer" -Type ExpandString
            Set-ItemProperty -Path $key -Name DumpType -Value 2 -Type DWord   # 2 = FULL dump
            Set-ItemProperty -Path $key -Name DumpCount -Value 5 -Type DWord
        }
        Write-Host '[crash-diag] WER LocalDumps ENABLED (full dumps, count 5) for engine + plugin host.'
        Write-Host '[crash-diag] WER dumps land in' (Join-Path $captureRoot 'wer')
    }
    'wer-off' {
        foreach ($key in @($werKey, $werKeyHost)) {
            Remove-Item $key -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host '[crash-diag] WER LocalDumps DISABLED.'
    }
    default { Write-Error "unknown action: $Action (use report | wer-on | wer-off)"; exit 1 }
}
