<#
.SYNOPSIS
  C2c engine helper: kill / launch / md5 / procs.

.DESCRIPTION
  Native Windows replacement for the retired probe-c2c/engine.sh — a WSL-only
  orchestrator that reached the build tree through /mnt/d/... paths and shelled
  out to cmd.exe. Pure PowerShell with native paths now; the repo root is
  derived from this script's location (D:\pdf\roo projects\hdaw3 on the dev box).

  Subcommands (first positional argument; default is `launch`, as in the .sh):

    kill     stop HDAW_headless.exe / hdaw_plugin_host.exe if they are running
    launch   kill, run C:\temp\launch_hdaw_diag.cmd, then poll the MCP endpoint
             (127.0.0.1:18765) once a second for up to 90 s and print
             ENGINE-UP or ENGINE-TIMEOUT
    md5      MD5 of build\HDAW_headless.exe, build\hdaw_plugin_host.exe and
             build\hdaw_tests.exe (md5sum format: "<hash>  <path>")
    procs    list the running engine / plugin-host processes (first 12)

.EXAMPLE
  powershell -File probe-c2c\engine.ps1 launch
  powershell -File probe-c2c\engine.ps1 md5
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('kill', 'launch', 'md5', 'procs')]
    [string]$Action = 'launch'
)

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot 'build'
$McpUrl = 'http://127.0.0.1:18765/mcp'
$Launcher = 'C:\temp\launch_hdaw_diag.cmd'

# Neither helper is an error when nothing matches: "no engine running" is the
# normal pre-launch state (the .sh swallowed taskkill's exit code the same way).
function Stop-HdawEngine {
    Get-Process -Name HDAW_headless, hdaw_plugin_host -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

function Get-HdawEngineProcess {
    Get-Process -Name HDAW_headless, hdaw_plugin_host -ErrorAction SilentlyContinue
}

switch ($Action) {
    'kill' {
        Stop-HdawEngine
        Start-Sleep -Seconds 1
        'KILLED'
    }
    'launch' {
        Stop-HdawEngine
        Start-Sleep -Seconds 2
        if (Test-Path -LiteralPath $Launcher) {
            & $Launcher
        } else {
            Write-Warning "launcher not found: $Launcher"
        }
        $up = $false
        $body = '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"engine_info","arguments":{}}}'
        for ($i = 0; $i -lt 90; $i++) {
            try {
                $resp = Invoke-WebRequest -Uri $McpUrl -Method Post -ContentType 'application/json' `
                    -Body $body -TimeoutSec 2 -UseBasicParsing
                if ($resp.Content -match '"result"') { $up = $true; break }
            } catch {
                # not up yet (connection refused / timeout) - keep polling
            }
            Start-Sleep -Seconds 1
        }
        if ($up) { 'ENGINE-UP' } else { 'ENGINE-TIMEOUT' }
    }
    'md5' {
        $files = @('HDAW_headless.exe', 'hdaw_plugin_host.exe', 'hdaw_tests.exe') |
            ForEach-Object { Join-Path $BuildDir $_ }
        $missing = @($files | Where-Object { -not (Test-Path -LiteralPath $_) })
        if ($missing.Count -gt 0) {
            # md5sum's contract: report every unreadable file, exit non-zero.
            $missing | ForEach-Object { [Console]::Error.WriteLine(('md5sum: {0}: No such file or directory' -f $_)) }
            exit 1
        }
        foreach ($f in $files) {
            $h = Get-FileHash -LiteralPath $f -Algorithm MD5
            '{0}  {1}' -f $h.Hash.ToLowerInvariant(), $f
        }
    }
    'procs' {
        Get-HdawEngineProcess | Select-Object -First 12 |
            Format-Table Id, ProcessName, StartTime -AutoSize
    }
}
