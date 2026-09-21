<#
.SYNOPSIS
  Shard the HDAW gtest suite across N concurrent processes.

.DESCRIPTION
  Native Windows/PowerShell twin of scripts/run-tests-parallel.sh (which needs WSL bash
  and WSLENV forwarding). The suite is serial by default and takes ~44 min on a dev box
  (1768 tests / 264 suites, measured 2026-09-21); this splits the gtest LIST across N
  processes: small suites stay whole (one filter per suite), large ones are sharded per
  test, and the units are dealt round-robin into N buckets. Every bucket runs as its own
  hdaw_tests.exe with its own log; the script waits for all of them and reports an
  aggregate plus an exit code (non-zero when any test failed).

  Safe to run concurrently: every ProxyProcessManager gets a unique pipe/shm namespace
  per instance (AGENTS.md lesson 20) and render temp targets are pid-tagged (fixed
  2026-09-21), so the historic cross-shard collision on
  %TEMP%\hdaw_render_<trackIndex>_<counter>.wav cannot happen.

  NOT everything is parallel-safe. Each shard process opens the audio device and spawns
  its own isolated plugin children, so the device/plugin dependent suites are kept out of
  the shard set and run in ONE extra process, serially, after the shards (see
  -SerialSuites). Pass -SerialSuites "" to shard absolutely everything.

  MEASUREMENT CAVEAT (2026-09-21): the shard count has NOT been validated end-to-end. The
  runs used to calibrate it produced many failures of the *device-dependent* suites
  (InternalFx, MasterGain, MasterBusFx, AudioPoolDedup, AudioEngineReadFacadeTest,
  AutomationPidRouting, McpCoverageTest, ApplyPresetToolTest) with
  `getTrack() == nullptr` / `tr == nullptr` — the documented deviceless pattern (AGENTS.md
  lessons 9/17: no usable audio route -> rebuildRoutingGraph no-ops -> getTrack() nulls).
  Those same tests fail the SAME way when run alone in that state, and they PASS in a
  device-healthy serial run, so the calibration data was contaminated by an environmental
  device failure and cannot be used to prove a contention threshold. Re-measure on a box
  where the device works before trusting a high -Shards value; treat a sharded run whose
  failures are all `getTrack()/tr == nullptr` as ENVIRONMENTAL, not as contention.

  What is independently true and fixed: concurrent runs cannot collide on proxy
  pipe/shm names (unique namespace per manager instance, lesson 20) or on render temp
  targets (pid-tagged, fixed 2026-09-21).

  Real-plugin gates run only when HDAW_REAL_PLUGIN_TESTS=1 is set in THIS shell — the
  child processes inherit the environment directly (no WSL/WSLENV forwarding involved).

.EXAMPLE
  powershell -File run-tests-sharded.ps1                     # full suite, 4 shards
  powershell -File run-tests-sharded.ps1 -Shards 6           # faster
  powershell -File run-tests-sharded.ps1 -Filter "FxMidiInjection.*" -Shards 4
  $env:HDAW_REAL_PLUGIN_TESTS=1; powershell -File run-tests-sharded.ps1 -Shards 3
#>
param(
    [int]$Shards = 2,
    [string]$Filter = "*",
    [string]$Binary = "",
    [int]$WholeSuiteThreshold = 25,   # suites with <= N tests run as one unit
    # Device/plugin dependent suites: each shard process would open the audio device and
    # spawn its own isolated plugin children, so these are kept out of the shard set and
    # run in ONE serial process at the end. Matched as a REGEX against the suite name.
    # Pass -SerialSuites "" to shard everything (see the measurement caveat in the header).
    [string]$SerialSuites = "^(PluginIsolation|CrashRecovery|ProxyNamespace|RealtimeSafety|InternalFx|MasterGain|MasterBusFx|AudioPoolDedup|AudioEngineReadFacadeTest|AutomationPidRouting|RenderSequenceRelease|FileLibraryPatchTest|McpCoverageTest|ExportVolumeBypass|FxMidiInjection|OfflineMidiFxAutomation|ApplyPresetToolTest|Clap.*)$",
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Binary) { $Binary = Join-Path $root "build\hdaw_tests.exe" }
if (-not (Test-Path $Binary)) {
    throw "test binary not found: $Binary  (build it: build-fast.bat test)"
}
if ($Shards -lt 1) { $Shards = 1 }
$tmp = [System.IO.Path]::GetTempPath()
$stamp = Get-Date -Format "HHmmss"

# --- 1) enumerate units (suite whole, or per test for big suites) -------------
$list = & $Binary --gtest_list_tests "--gtest_filter=$Filter" 2>$null
$suites = [ordered]@{}
$current = $null
foreach ($line in $list) {
    if ($line -match '^(\S+)\.\s*$') { $current = $Matches[1]; $suites[$current] = @(); continue }
    if ($current -and $line -match '^\s+(\S+)') { $suites[$current] += $Matches[1] }
}
$units = New-Object System.Collections.Generic.List[string]
$serialUnits = New-Object System.Collections.Generic.List[string]
foreach ($suite in $suites.Keys) {
    $isSerial = $SerialSuites -and ($suite -match $SerialSuites)
    $tests = $suites[$suite]
    if ($isSerial) { $serialUnits.Add("$suite.*"); continue }
    if ($tests.Count -eq 0 -or $tests.Count -le $WholeSuiteThreshold) { $units.Add("$suite.*") }
    else { foreach ($t in $tests) { $units.Add("$suite.$t") } }
}
if (($units.Count + $serialUnits.Count) -eq 0) { throw "no tests matched filter '$Filter'" }

# --- 2) deal round-robin ------------------------------------------------------
$buckets = @()
for ($i = 0; $i -lt $Shards; $i++) { $buckets += ,(New-Object System.Collections.Generic.List[string]) }
for ($i = 0; $i -lt $units.Count; $i++) { $buckets[$i % $Shards].Add($units[$i]) }

$realPlugins = [bool]$env:HDAW_REAL_PLUGIN_TESTS
"hdaw sharded run: $($units.Count) shardable units from $($suites.Count) suites -> $Shards shards (+ $($serialUnits.Count) serial suites) (filter '$Filter', real plugins: $realPlugins)"

# --- 3) run ------------------------------------------------------------------
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$procs = @()
for ($s = 0; $s -lt $Shards; $s++) {
    if ($buckets[$s].Count -eq 0) { continue }
    $log = Join-Path $tmp "hdaw_shard_${stamp}_$s.log"
    # Chunk the patterns so one bucket can never exceed the Windows command-line limit.
    $chunks = @(); $cur = ""
    foreach ($p in $buckets[$s]) {
        if (($cur.Length + $p.Length + 1) -gt 6000) { $chunks += $cur; $cur = $p }
        else { $cur = if ($cur) { $cur + ":" + $p } else { $p } }
    }
    if ($cur) { $chunks += $cur }
    $args_ = @()
    foreach ($c in $chunks) { $args_ += "--gtest_filter=$c" }
    $procs += [pscustomobject]@{
        Index = $s; Log = $log; Count = $buckets[$s].Count
        Proc  = Start-Process -FilePath $Binary -ArgumentList $args_ `
                    -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
                    -PassThru -NoNewWindow
    }
}
foreach ($sh in $procs) { $sh.Proc.WaitForExit() }

# --- 3b) the contention-sensitive suites, serially ---------------------------
$serialProc = $null
if ($serialUnits.Count -gt 0) {
    $log = Join-Path $tmp "hdaw_shard_${stamp}_serial.log"
    $serialProc = [pscustomobject]@{
        Index = 'serial'; Log = $log; Count = $serialUnits.Count
        Proc = Start-Process -FilePath $Binary -ArgumentList @("--gtest_filter=$($serialUnits -join ':')") `
                   -RedirectStandardOutput $log -RedirectStandardError "$log.err" `
                   -PassThru -NoNewWindow
    }
    $serialProc.Proc.WaitForExit()
    $procs += $serialProc
}
$sw.Stop()

# --- 4) aggregate ------------------------------------------------------------
$passed = 0; $failed = 0; $failedTests = @()
foreach ($sh in $procs) {
    $ok  = (Select-String -Path $sh.Log -Pattern '^\[       OK \]'     -ErrorAction SilentlyContinue | Measure-Object).Count
    $bad = (Select-String -Path $sh.Log -Pattern '^\[  FAILED  \]'     -ErrorAction SilentlyContinue | Measure-Object).Count
    $passed += $ok; $failed += $bad
    foreach ($m in Select-String -Path $sh.Log -Pattern '^\[  FAILED  \] (\S+)' -ErrorAction SilentlyContinue) {
        $failedTests += $m.Matches[0].Groups[1].Value
    }
    if (-not $Quiet) { "  shard $($sh.Index): $ok passed, $bad failed ($($sh.Count) units) -> $($sh.Log)" }
}
"elapsed: {0:n1} min" -f ($sw.Elapsed.TotalMinutes)
"TOTAL: $passed passed, $failed failed"
if ($failedTests.Count -gt 0) {
    "FAILED TESTS (re-run solo before blaming the code):"
    $failedTests | Sort-Object -Unique | ForEach-Object { "  $_" }
}
exit ([int]($failed -gt 0))
