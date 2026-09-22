# mcp-launch-capture.ps1 -- procdump crash capture for HDAW MCP sessions (attach mode).
#
# Started by mcp-launch.bat with inherited stdio. Starts the engine normally so
# the MCP JSON-RPC contract on stdout stays clean, then ATTACHES procdump as a
# watcher whose UTF-16 banner goes to log files (procdump.log / procdump.err in
# the capture dir), NOT to the engine's stdout.
#
# This logic lives in a script file (not an inline `powershell -Command "..."`)
# because cmd's quote pairing cannot survive embedded double quotes like
# "$($engine.Id)" -- an inline string with those aborts the whole batch file with
# '... was unexpected at this time' and the engine never starts.
#
# Contract:
#   env ENGINE                       = temp copy of HDAW_headless.exe
#   env HDAW_CRASH_DUMP_TYPE (opt.)  = 'mini' -> -mm dump flag (default: -ma)
#   env HDAW_CRASH_NO_KILL (opt.)    = '1'   -> do NOT kill the engine after a
#                                              dump is written (legacy behavior;
#                                              the 2026-09-22 heap-corruption
#                                              session served a brain-damaged
#                                              engine because procdump detached
#                                              without killing)
#   env HDAW_NO_CRASH_CAPTURE        = '1'   -> bypass handled by the bat (this script not run)
#
# KILL-AFTER-DUMP (2026-09-22): procdump REJECTS '-k' in ATTACH mode ("only valid
# with AeDebug Just-in-Time support (-i)"), so the kill is implemented HERE: after
# starting the procdump watcher, poll the capture dir for a written *.dmp and
# Stop-Process the engine the moment one appears. Without this, a heap-corrupted
# engine keeps serving MCP sessions in a brain-damaged state (see
# docs/handoffs/2026-09-22-engine-heap-corruption-investigation.md).

$enginePath = $env:ENGINE
if ([string]::IsNullOrWhiteSpace($enginePath)) {
    [Console]::Error.WriteLine('[mcp-launch] ERROR: ENGINE env var not set -- cannot start HDAW engine')
    exit 1
}

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $enginePath
$psi.Arguments = '--mcp-stdio'
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $false
$psi.RedirectStandardError = $false
$psi.RedirectStandardInput = $false
$engine = [System.Diagnostics.Process]::Start($psi)

$dir = Join-Path $env:TEMP ('hdaw_crash_captures\engine_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$flags = '-ma'
if ($env:HDAW_CRASH_DUMP_TYPE -eq 'mini') { $flags = '-mm' }

$pd = (Get-Command procdump -ErrorAction SilentlyContinue).Source
if ($pd) {
    Start-Process -FilePath $pd -ArgumentList '-accepteula', $flags, '-e', '-g', "$($engine.Id)", $dir `
        -WindowStyle Hidden `
        -RedirectStandardOutput "$dir\procdump.log" `
        -RedirectStandardError "$dir\procdump.err"
    [Console]::Error.WriteLine('[mcp-launch] crash capture ON (attach pid=' + $engine.Id + ')')
}

# Wait loop with dump-kill. 250 ms poll is negligible next to a ~200 MB dump
# write (~1 s); the engine's own exit still ends the loop normally.
while (-not $engine.HasExited) {
    if ($pd -and $env:HDAW_CRASH_NO_KILL -ne '1' -and (Test-Path (Join-Path $dir '*.dmp'))) {
        [Console]::Error.WriteLine('[mcp-launch] dump captured -- killing corrupted engine pid=' + $engine.Id)
        Stop-Process -Id $engine.Id -Force -ErrorAction SilentlyContinue
        break
    }
    Start-Sleep -Milliseconds 250
}
if (-not $engine.HasExited) { $engine.WaitForExit() }
exit $engine.ExitCode
