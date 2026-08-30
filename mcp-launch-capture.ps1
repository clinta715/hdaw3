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
#   env HDAW_NO_CRASH_CAPTURE        = '1'   -> bypass handled by the bat (this script not run)

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

$engine.WaitForExit()
exit $engine.ExitCode