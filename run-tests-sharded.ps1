<#
.SYNOPSIS
  Shard the HDAW gtest suite across N concurrent processes.

.DESCRIPTION
  Native Windows/PowerShell shard runner - the canonical parallel test runner.
  It replaced scripts/run-tests-parallel.sh (retired: that one needed WSL bash and
  WSLENV forwarding; this one needs neither). The suite is serial by default and takes ~44 min on a dev box
  (1768 tests / 264 suites, measured 2026-09-21); this splits the gtest LIST across N
  processes: small suites stay whole (one filter per suite), large ones are sharded per
  test, and the units are dealt round-robin into N buckets. Every bucket runs as its own
  hdaw_tests.exe with its own log; the script waits for all of them and reports an
  aggregate plus an exit code (non-zero when any test failed). The aggregate's TOTAL
  counts UNIQUE failing test names - a gtest log names every failure twice (a timed
  `[  FAILED  ] Name (123 ms)` line and a bare `[  FAILED  ] Name` line), so summing
  the raw `^[  FAILED  ]` lines double-counts each failure. The per-shard detail lines
  still show the raw counts.

  INCOMPLETE-SHARD DETECTION (2026-09-25): a shard process that is killed (OOM, an
  external taskkill, a hard crash) leaves a log that stops mid-test with NO gtest
  completion summary line. Such a shard used to be counted as a clean pass - its log
  simply had fewer `[       OK ]` lines, so the aggregate under-reported and the run
  exited 0 (a false green; observed three times, e.g. shard 0 dying at
  ExportBakeTimeout.LargeProjectExportsWithDefaultTimeout while the runner still
  printed `TOTAL: 1821 passed, 0 unique failed`). Every shard log is now checked for
  the gtest banner `[==========] <n> tests from <m> test suites ran.`; a log missing
  it is reported as INCOMPLETE with the last `[ RUN      ]` line it reached, counted as
  a runner failure, and forces a non-zero exit. Unique-failure counting is unaffected.
  This is a subset of the per-shard evidence: capture it with
  `powershell -File run-tests-sharded.ps1 -Shards 4` and inspect the printed
  `INCOMPLETE SHARDS:` block; see docs/build-and-testing.md for the crash artifact
  locations to check next.

  Shard logs land in the first WRITABLE of `$env:HDAW_SHARD_LOG_DIR`,
  `<repo>/.tmp_tests/shard_logs`, or the system temp dir - printed at startup as
  `shard logs -> <dir>` - so a sandboxed shell whose %TEMP% is denied no longer needs
  the caller to pre-set TMP/TEMP just to get the runner started.

  ONE FILTER PER PROCESS (fixed 2026-09-25): gtest accepts exactly ONE --gtest_filter and
  OVERWRITES it when the flag is repeated. The old per-bucket chunking (split at 6000
  chars, one --gtest_filter per chunk) therefore ran only the LAST chunk and still exited
  0: `-Shards 2` over the full suite printed `Running 130 tests from 18 test suites` per
  shard (~72% of the suite never ran) yet reported `TOTAL: 550 passed, 0 unique failed` in
  347 s, while `-Shards 4` (buckets that happened to fit in one chunk) covered all 533
  units. A bucket is now planned as ONE process wherever possible - all units joined with
  ':' into a single --gtest_filter under a 24000-char guard (`-MaxFilterArgLength`), or, if
  the bucket is longer, a single --gtest_flagfile=<file> (gtest 1.14 supports it; probed at
  runtime, `-DisableFlagfile` forces the fallback), and only with no flagfile support as
  several SEQUENTIAL invocations whose logs are concatenated into the one shard log.
  The aggregate then verifies INTENDED-vs-EXECUTED per shard: the bucket's intended test
  count is derived from --gtest_list_tests (whole suite -> its test count, `Suite.Test` ->
  1) and must EQUAL the sum of the gtest `[==========] N tests from M test suites ran.`
  counts (one banner per launched invocation), with the per-invocation
  `[==========] Running N tests ...` totals as a second cross-check. Any shortfall is
  reported as `INCOMPLETE SHARDS: shard <n> (ran <x> of <y> intended tests)`, marks the
  per-shard line `[INCOMPLETE: ...]`, and forces a non-zero exit; the summary line prints
  `coverage: executed <x> of <y> intended tests`.

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
  child processes inherit the environment directly (no WSL/WSLENV forwarding involved,
  unlike the retired scripts/run-tests-parallel.sh).

.EXAMPLE
  powershell -File run-tests-sharded.ps1                     # full suite, 4 shards
  powershell -File run-tests-sharded.ps1 -Shards 6           # faster
  powershell -File run-tests-sharded.ps1 -Binaries build\hdaw_tests_mcp.exe  # one exe of the 2026-09-28 split
  powershell -File run-tests-sharded.ps1 -Filter "FxMidiInjection.*" -Shards 4
  $env:HDAW_REAL_PLUGIN_TESTS=1; powershell -File run-tests-sharded.ps1 -Shards 3
#>
param(
    [int]$Shards = 2,
    [string]$Filter = "*",
    [string]$Binary = "",
    # 2026-09-28 test-time split: one or more test exes. Empty = the four
    # per-seam exes under build\ (hdaw_tests_engine/_mcp/_frontend/_platform).
    # -Binary still works for a single explicit binary.
    [string[]]$Binaries = @(),
    [int]$WholeSuiteThreshold = 25,   # suites with <= N tests run as one unit
    # Device/plugin dependent suites: each shard process would open the audio device and
    # spawn its own isolated plugin children, so these are kept out of the shard set and
    # run in ONE serial process at the end. Matched as a REGEX against the suite name.
    # Pass -SerialSuites "" to shard everything (see the measurement caveat in the header).
    [string]$SerialSuites = "^(PluginIsolation|CrashRecovery|ProxyNamespace|RealtimeSafety|InternalFx|MasterGain|MasterBusFx|AudioPoolDedup|AudioEngineReadFacadeTest|AutomationPidRouting|RenderSequenceRelease|FileLibraryPatchTest|McpCoverageTest|ExportVolumeBypass|FxMidiInjection|OfflineMidiFxAutomation|ApplyPresetToolTest|Clap.*)$",
    # Hard guard for a SINGLE --gtest_filter argument: gtest takes one filter flag and
    # OVERWRITES it when repeated, so a bucket is never split into several
    # --gtest_filter args. 24000 is far below the 32767-char Windows command-line limit,
    # which must also carry the exe path and (for the flagfile route) the flag name.
    [int]$MaxFilterArgLength = 24000,
    # 2026-09-29: shardable units are dealt into Shards * BucketFactor buckets
    # that run through a greedy work pool (lanes = Shards). Finer buckets let the
    # pool re-pack work as lanes free up; fixed one-bucket-per-lane waves let a
    # single heavy bucket stall its whole lane (measured 2026-09-28: a 13.6-min
    # engine bucket held lane 2 while lane 1 idled - full-suite wall 23.1 min vs
    # the 19.7 min single-binary baseline).
    [int]$BucketFactor = 3,
    # Diagnostics only: force the `--gtest_flagfile` route off (it is probed at runtime;
    # see the route table in the run section). Used to exercise the sequential fallback.
    [switch]$DisableFlagfile,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Binaries -or $Binaries.Count -eq 0) {
    if ($Binary) { $Binaries = @($Binary) }
    else {
        # 2026-09-28 test-time split default: the four per-seam exes.
        $Binaries = @('hdaw_tests_engine', 'hdaw_tests_mcp', 'hdaw_tests_frontend', 'hdaw_tests_platform') |
            ForEach-Object { Join-Path $root "build\$_.exe" }
    }
}
foreach ($b in $Binaries) {
    if (-not (Test-Path $b)) {
        throw "test binary not found: $b  (build it: build-fast.bat test)"
    }
}
if ($Shards -lt 1) { $Shards = 1 }

# --- 0) shard-log directory --------------------------------------------------
# The runner writes one log per shard. [System.IO.Path]::GetTempPath() is the
# sandbox-denied %TEMP% on an agent box, which made a plain invocation die with a
# confusing "Cannot find path ...hdaw_shard_<stamp>_0.log" from Select-String.
# Resolve the first WRITABLE candidate in this order and print it:
#   $env:HDAW_SHARD_LOG_DIR  ->  <script dir>/.tmp_tests/shard_logs  ->  system temp
function Test-ShardLogDirWritable([string]$dir) {
    try {
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        $probe = Join-Path $dir ("hdaw_shard_probe_{0}.txt" -f $PID)
        Set-Content -LiteralPath $probe -Value "probe" -ErrorAction Stop
        $back = Get-Content -LiteralPath $probe -ErrorAction Stop
        Remove-Item -LiteralPath $probe -Force -ErrorAction Stop
        return ($back -eq "probe")
    } catch {
        return $false
    }
}

$logCandidates = @()
if ($env:HDAW_SHARD_LOG_DIR) { $logCandidates += $env:HDAW_SHARD_LOG_DIR }
$logCandidates += (Join-Path $root ".tmp_tests\shard_logs")
$logCandidates += [System.IO.Path]::GetTempPath()

$tmp = $null
foreach ($candidate in $logCandidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
    if (Test-ShardLogDirWritable $candidate) { $tmp = $candidate; break }
    "shard log dir '$candidate' is not writable - trying the next candidate"
}
if (-not $tmp) { throw "no writable shard log directory (tried: $($logCandidates -join ', '))" }
"shard logs -> $tmp"

$stamp = Get-Date -Format "HHmmss"

# --- 1) enumerate units PER BINARY (suite whole, or per test for big suites) --
# 2026-09-28 test-time split: units become (binary, unit) pairs so one run can
# cover several exes. Every test must live in exactly ONE exe: a suite name
# present in two exes would run twice (and double-count in the aggregate), so
# that is a hard error, not a warning.
$groups = @()
foreach ($b in $Binaries) {
    $list = & $b --gtest_list_tests "--gtest_filter=$Filter" 2>$null
    $suites = [ordered]@{}
    $current = $null
    foreach ($line in $list) {
        if ($line -match '^(\S+)\.\s*$') { $current = $Matches[1]; $suites[$current] = @(); continue }
        if ($current -and $line -match '^\s+(\S+)') { $suites[$current] += $Matches[1] }
    }
    $units = New-Object System.Collections.Generic.List[string]
    $serialUnits = New-Object System.Collections.Generic.List[string]
    # unit -> how many TESTS that unit intends to run (whole suite: its listed test count;
    # Suite.Test: 1). Read straight from --gtest_list_tests, so it is the same universe gtest
    # itself selects from, and it is what the executed count is verified against in step 4.
    $unitTests = @{}
    foreach ($suite in $suites.Keys) {
        $isSerial = $SerialSuites -and ($suite -match $SerialSuites)
        $tests = $suites[$suite]
        if ($isSerial) { $serialUnits.Add("$suite.*"); $unitTests["$suite.*"] = [int]$tests.Count; continue }
        if ($tests.Count -eq 0 -or $tests.Count -le $WholeSuiteThreshold) { $units.Add("$suite.*"); $unitTests["$suite.*"] = [int]$tests.Count }
        else { foreach ($t in $tests) { $units.Add("$suite.$t"); $unitTests["$suite.$t"] = 1 } }
    }
    $groups += [pscustomobject]@{ Bin = $b; Suites = $suites; Units = $units; SerialUnits = $serialUnits; UnitTests = $unitTests }
}
$seenSuites = @{}
foreach ($g in $groups) {
    foreach ($s in $g.Suites.Keys) {
        if ($seenSuites.ContainsKey($s)) { throw "suite '$s' is present in more than one test exe ('$($seenSuites[$s])' and '$($g.Bin)') - the split assigns every suite to exactly one exe" }
        $seenSuites[$s] = $g.Bin
    }
}
$totalShardable = ($groups | ForEach-Object { $_.Units.Count } | Measure-Object -Sum).Sum
$totalSerial = ($groups | ForEach-Object { $_.SerialUnits.Count } | Measure-Object -Sum).Sum
if (($totalShardable + $totalSerial) -eq 0) { throw "no tests matched filter '$Filter' in any of the $($Binaries.Count) test exe(s)" }

# --- 2) deal round-robin (across binaries; each bucket keeps (bin, unit) pairs)
$flat = @()
foreach ($g in $groups) { foreach ($u in $g.Units) { $flat += [pscustomobject]@{ Bin = $g.Bin; Unit = $u; Tests = [int]$g.UnitTests[$u] } } }
$laneCount = $Shards * ([Math]::Max(1, $BucketFactor))
$buckets = @()
for ($i = 0; $i -lt $laneCount; $i++) { $buckets += ,(New-Object System.Collections.Generic.List[object]) }
for ($i = 0; $i -lt $flat.Count; $i++) { $buckets[$i % $laneCount].Add($flat[$i]) }

$realPlugins = [bool]$env:HDAW_REAL_PLUGIN_TESTS
"hdaw sharded run: $($flat.Count) shardable units from $($seenSuites.Count) suites across $($Binaries.Count) exe(s) -> $laneCount buckets on $Shards lanes (+ $totalSerial serial suites) (filter '$Filter', real plugins: $realPlugins)"

# --- 3) run ------------------------------------------------------------------
# ONE gtest process accepts exactly ONE --gtest_filter: gtest OVERWRITES the flag for
# every repeat, so the previous per-bucket chunking (split at 6000 chars, one
# --gtest_filter arg per chunk) silently ran only the LAST chunk - a false green. Measured
# 2026-09-25 with the old code: the `-Shards 2` full suite printed a 1575-char
# `Note: Google Test filter = ...` and `Running 130 tests from 18 test suites` per shard
# (vs 287 suites / 2008 tests in the whole listing, i.e. ~72% of the suite never ran)
# while the runner still reported `TOTAL: 550 passed, 0 unique failed` and exited 0 in
# 347 s. A bucket is now turned into a PLAN - a list of argument arrays, one element per
# child process - by the first route that applies:
#   a. SINGLE  - every unit joined with ':' into ONE --gtest_filter arg, used whenever that
#                arg stays within $MaxFilterArgLength (default 24000 chars).
#   b. FLAGFILE - ONE --gtest_flagfile=<file> arg (gtest reads one flag per line from it, so
#                the same joined filter still arrives as a single filter). Preferred for a
#                bucket that is too long for (a), but only when the build supports the flag:
#                probed once, lazily, below. gtest 1.14 does (gtest.cc `LoadFlagsFromFile`).
#   c. SEQUENTIAL - no flagfile support: the bucket runs as several SEQUENTIAL invocations,
#                each its own single --gtest_filter under the cap; the per-invocation logs
#                are concatenated into the one shard log afterwards, so the shard still
#                aggregates as one unit. Never concurrent with itself (invocations run in
#                waves: every shard's 1st, then every shard's 2nd, ...).
# -DisableFlagfile forces route (c) so the fallback can be measured.
$script:flagfileSupported = $null
function Test-GtestFlagfileSupport([object]$suites, [string]$binary, [string]$dir, [string]$stamp) {
    if ($null -ne $script:flagfileSupported) { return $script:flagfileSupported }
    $script:flagfileSupported = $false
    try {
        $probeSuite = @($suites.Keys)[0]
        $probeFile = Join-Path $dir "hdaw_flagfile_probe_$stamp.txt"
        # LF only: gtest splits the file on '\n' and parses each line verbatim, so a CRLF
        # file would leave a trailing CR inside the filter value and match nothing.
        [System.IO.File]::WriteAllText($probeFile, "--gtest_filter=$probeSuite.*`n")
        $probeOut = @(& $binary "--gtest_flagfile=$probeFile" --gtest_list_tests 2>$null |
                      Where-Object { $_ -match '^\S+\.$' })
        $script:flagfileSupported = (($probeOut.Count -eq 1) -and ($probeOut[0] -eq "$probeSuite."))
    } catch {
        $script:flagfileSupported = $false
        Write-Host "gtest --gtest_flagfile probe failed ($($_.Exception.Message)) - assuming unsupported"
        return $script:flagfileSupported
    }
    Write-Host "gtest --gtest_flagfile support: $($script:flagfileSupported) (probe: '$probeSuite.*' listed $(@($probeOut).Count) suite(s))"
    return $script:flagfileSupported
}

function Get-BucketPlan([object]$bucket, [string]$tag, [object]$suites, [string]$binary, [string]$dir, [string]$stamp, [int]$maxLen) {
    $overhead = "--gtest_filter=".Length
    $joined = @($bucket) -join ':'
    $plan = New-Object System.Collections.Generic.List[object]
    $route = 'single'
    if (($joined.Length + $overhead) -le $maxLen) {
        $plan.Add(@("--gtest_filter=$joined"))
        return [pscustomobject]@{ Route = $route; Args = $plan }
    }
    if ((-not $DisableFlagfile) -and (Test-GtestFlagfileSupport $suites $binary $dir $stamp)) {
        $flagFile = Join-Path $dir "hdaw_shard_${stamp}_$tag.gtest_flags"
        [System.IO.File]::WriteAllText($flagFile, "--gtest_filter=$joined`n")
        # quoted: $dir can contain spaces (the repo path does), and Start-Process joins
        # -ArgumentList with spaces without adding quotes itself.
        $plan.Add(@("--gtest_flagfile=`"$flagFile`""))
        return [pscustomobject]@{ Route = 'flagfile'; Args = $plan }
    }
    $route = 'sequential'
    $chunk = ""; $args_ = New-Object System.Collections.Generic.List[string]
    foreach ($p in @($bucket)) {
        if ($chunk -and (($chunk.Length + $p.Length + 1 + $overhead) -gt $maxLen)) {
            $args_.Add("--gtest_filter=$chunk"); $plan.Add($args_.ToArray())
            $args_ = New-Object System.Collections.Generic.List[string]; $chunk = $p
        } else {
            $chunk = if ($chunk) { $chunk + ":" + $p } else { $p }
        }
    }
    if ($chunk) { $args_.Add("--gtest_filter=$chunk"); $plan.Add($args_.ToArray()) }
    return [pscustomobject]@{ Route = $route; Args = $plan }
}

# Get-InvocationPlans turns one bucket into per-binary invocations (see its
# comment); Invoke-BucketPool runs bucket metas through a greedy work pool -
# up to $lanes buckets in flight, no wave barriers - and stitches each bucket's
# per-invocation part logs together when the bucket completes.
# Turn one bucket (a list of {Bin, Unit, Tests}) into per-binary invocations:
# units are grouped by exe preserving deal order, each group goes through
# Get-BucketPlan (single filter / flagfile / sequential), and the results are
# chained into ONE Plan of {Bin, Args} invocation entries. A bucket spanning
# several exes therefore runs one gtest process per exe, sequentially.
function Get-InvocationPlans([object]$bucket, [string]$tag, [object]$groups, [string]$dir, [string]$stamp, [int]$maxLen) {
    $byBin = [ordered]@{}
    foreach ($u in $bucket) {
        if (-not $byBin.Contains($u.Bin)) { $byBin[$u.Bin] = (New-Object System.Collections.Generic.List[object]) }
        $byBin[$u.Bin].Add($u)
    }
    $invocations = New-Object System.Collections.Generic.List[object]
    $routes = @(); $intended = 0
    foreach ($bin in $byBin.Keys) {
        $units = $byBin[$bin]
        $unitStrings = @($units | ForEach-Object { $_.Unit })
        foreach ($t in $units) { $intended += $t.Tests }
        $suitesForBin = @($groups | Where-Object { $_.Bin -eq $bin })[0].Suites
        $bp = Get-BucketPlan $unitStrings $tag $suitesForBin $bin $dir $stamp $maxLen
        $routes += $bp.Route
        foreach ($a in $bp.Args) { $invocations.Add([pscustomobject]@{ Bin = $bin; Args = $a }) }
    }
    return [pscustomobject]@{ Invocations = $invocations; Route = (($routes | Sort-Object -Unique) -join '+'); Intended = $intended }
}

function Invoke-BucketPool([object]$metas, [int]$lanes) {
    $pending = New-Object System.Collections.Generic.Queue[object]
    foreach ($m in $metas) { $m | Add-Member -NotePropertyName Next -NotePropertyValue 0 -Force; $pending.Enqueue($m) }
    $active = @()
    while ($pending.Count -gt 0 -or $active.Count -gt 0) {
        while ($pending.Count -gt 0 -and $active.Count -lt $lanes) {
            $m = $pending.Dequeue()
            $inv = $m.Plan[$m.Next]
            # Defensive (2026-09-29): a full 12-minute run once died here on a null
            # Bin deep into the run. Skip-and-log with a full Plan dump instead of
            # dying: the affected bucket comes back INCOMPLETE via the banner
            # shortfall check, the run survives, and the dump names the entry.
            if (-not $inv -or [string]::IsNullOrWhiteSpace($inv.Bin)) {
                $dump = ($m.Plan | ForEach-Object { "[" + $_.Bin + "] " + ($_.Args -join ' ') }) -join "`n"
                [System.IO.File]::AppendAllText($m.Log + ".poolerror", "POOL ERROR: bucket $($m.Index) invocation $($m.Next): null/empty Bin. Plan dump:`n" + $dump + "`n")
                $script:poolErrorCount = $script:poolErrorCount + 1
                "POOL ERROR: bucket $($m.Index) invocation $($m.Next) has a null/empty Bin - bucket abandoned, remaining tests NOT run (see $($m.Log).poolerror)"
                continue
            }
            $part = if ($m.Plan.Count -eq 1) { $m.Log } else { "$($m.Log).part$($m.Next)" }
            $m.Parts.Add($part)
            # Each Plan entry names its own binary: a bucket may hold units from
            # several exes, one invocation per exe, never concurrent with itself.
            # -ArgumentList is bound through a temp variable: PS 5.1 refuses the indexed
            # member access (Object[] -> String) when written inline here.
            $invocationArgs = [string[]]$inv.Args
            $m.Proc = Start-Process -FilePath $inv.Bin -ArgumentList $invocationArgs `
                          -RedirectStandardOutput $part -RedirectStandardError "$part.err" `
                          -PassThru -NoNewWindow
            $active += $m
        }
        if ($active.Count -gt 0) { Start-Sleep -Milliseconds 400 }
        $finished = @($active | Where-Object { $_.Proc.HasExited })
        foreach ($d in $finished) {
            # The meta LEAVES the lane on every exit: re-enqueued buckets must not
            # stay in $active, or the next poll double-counts them (Next jumps,
            # invocations skip/duplicate, Plan[i] goes null - measured 2026-09-29
            # as 'invocation 4 has a null/empty Bin' + a double-stitch crash).
            $active = @($active | Where-Object { $_ -ne $d })
            $d.Next = $d.Next + 1
            if ($d.Next -lt $d.Plan.Count) {
                $pending.Enqueue($d)
            } else {
                if ($d.Plan.Count -gt 1) {
                    [System.IO.File]::WriteAllText($d.Log, "")
                    foreach ($part in $d.Parts) {
                        [System.IO.File]::AppendAllText($d.Log, [System.IO.File]::ReadAllText($part))
                        Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
                    }
                }
            }
        }
    }
}

$allMetas = @()
$shardMetas = @()
for ($s = 0; $s -lt $laneCount; $s++) {
    if ($buckets[$s].Count -eq 0) { continue }
    $bucketPlan = Get-InvocationPlans $buckets[$s] "$s" $groups $tmp $stamp $MaxFilterArgLength
    $meta = [pscustomobject]@{
        Index = $s; Log = Join-Path $tmp "hdaw_shard_${stamp}_$s.log"; Count = $buckets[$s].Count
        Intended = $bucketPlan.Intended; Plan = $bucketPlan.Invocations; Route = $bucketPlan.Route
        Parts = (New-Object System.Collections.Generic.List[string]); Proc = $null
    }
    $shardMetas += $meta; $allMetas += $meta
}

$poolErrorCount = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
Invoke-BucketPool $shardMetas $Shards

# --- 3b) the contention-sensitive suites, serially, one exe at a time ---------
# Device/plugin suites run in ONE extra bucket per exe AFTER the shards, exactly
# as before - each exe's serial bucket is a single process, and the exes run
# back-to-back (never concurrently: these open the audio device / spawn children).
foreach ($g in $groups) {
    if ($g.SerialUnits.Count -eq 0) { continue }
    $tag = [System.IO.Path]::GetFileNameWithoutExtension($g.Bin) -replace '^hdaw_tests_', ''
    $serialIntended = 0; foreach ($u in $g.SerialUnits) { $serialIntended += [int]$g.UnitTests[$u] }
    $serialPlan = Get-BucketPlan @($g.SerialUnits) "serial_$tag" $g.Suites $g.Bin $tmp $stamp $MaxFilterArgLength
    $serialInv = New-Object System.Collections.Generic.List[object]
    foreach ($a in $serialPlan.Args) { $serialInv.Add([pscustomobject]@{ Bin = $g.Bin; Args = $a }) }
    $serialMeta = [pscustomobject]@{
        Index = "serial_$tag"; Log = Join-Path $tmp "hdaw_shard_${stamp}_serial_$tag.log"; Count = $g.SerialUnits.Count
        Intended = $serialIntended; Plan = $serialInv; Route = $serialPlan.Route
        Parts = (New-Object System.Collections.Generic.List[string]); Proc = $null
    }
    Invoke-BucketPool @($serialMeta) 1
    $allMetas += $serialMeta
}
$sw.Stop()

# --- 4) aggregate ------------------------------------------------------------
# TOTAL is the number of UNIQUE failing test names. gtest prints each failure twice
# (timed "[  FAILED  ] Name (12 ms)" plus a bare "[  FAILED  ] Name" in the summary
# list), and the "[  FAILED  ] N test(s), listed below:" counter is not a test name;
# only the timed form identifies a test exactly once.
#
# INTENDED-vs-EXECUTED (2026-09-25): the runner knows each bucket's unit list, so it knows
# exactly how many tests the bucket intended to run ($unitTests, derived from
# --gtest_list_tests). Invariant enforced per shard, stated exactly:
#   * one gtest completion banner (`[==========] N tests from M test suites ran.`) per
#     launched invocation, and
#   * SUM(N over those banners) == the bucket's intended test count, and
#   * when gtest's startup `[==========] Running N tests from M test suites.` line is
#     present once per invocation, SUM(N) there must equal the intended count too - that is
#     the filter-selection check (it catches "the filter matched fewer tests than the units
#     name", which the ran. total alone could hide if a bucket died later).
# NOTE: SKIPPED tests count as run in both gtest totals, so they are included in the
# executed count; a shard is accepted as complete when its ran. total EQUALS the intended
# count (not "at least"), and any shortfall is an INCOMPLETE shard and a non-zero exit.
$gtestRan     = '^\[==========\] (\d+) tests? from (\d+) test suites? ran\.'
$gtestRunning = '^\[==========\] Running (\d+) tests? from (\d+) test suites?\.'
$passed = 0; $failedLines = 0; $failedTests = @(); $incompleteShards = @()
$intendedTotal = 0; $executedTotal = 0
foreach ($sh in $allMetas) {
    $ok  = (Select-String -Path $sh.Log -Pattern '^\[       OK \]'     -ErrorAction SilentlyContinue | Measure-Object).Count
    $bad = (Select-String -Path $sh.Log -Pattern '^\[  FAILED  \]'     -ErrorAction SilentlyContinue | Measure-Object).Count
    $passed += $ok; $failedLines += $bad
    foreach ($m in Select-String -Path $sh.Log -Pattern '^\[  FAILED  \] (\S+) \(' -ErrorAction SilentlyContinue) {
        $failedTests += $m.Matches[0].Groups[1].Value
    }
    $summaries = @(Select-String -Path $sh.Log -Pattern $gtestRan     -ErrorAction SilentlyContinue)
    $runnings  = @(Select-String -Path $sh.Log -Pattern $gtestRunning -ErrorAction SilentlyContinue)
    $ran = 0;      foreach ($m in $summaries) { $ran      += [int]$m.Matches[0].Groups[1].Value }
    $selected = 0; foreach ($m in $runnings)  { $selected += [int]$m.Matches[0].Groups[1].Value }
    $intendedTotal += [int]$sh.Intended; $executedTotal += $ran

    $problem = ""
    if ($summaries.Count -lt $sh.Plan.Count) {
        $last = Select-String -Path $sh.Log -Pattern '^\[ RUN      \] (\S+)' -ErrorAction SilentlyContinue | Select-Object -Last 1
        $lastName = if ($last) { $last.Matches[0].Groups[1].Value } else { '<no [ RUN ] line>' }
        $problem = "no gtest completion summary for $($sh.Plan.Count - $summaries.Count) of $($sh.Plan.Count) invocation(s) (ran $ran of $($sh.Intended) intended tests), last test reached '$lastName'"
    } elseif ($ran -ne [int]$sh.Intended) {
        $problem = "ran $ran of $($sh.Intended) intended tests"
    } elseif ($runnings.Count -eq $sh.Plan.Count -and $selected -ne [int]$sh.Intended) {
        $problem = "the filter selected $selected of $($sh.Intended) intended tests"
    }
    $mark = ""
    if ($problem) {
        $incompleteShards += [pscustomobject]@{ Index = $sh.Index; Ran = $ran; Intended = [int]$sh.Intended; Reason = $problem }
        $mark = " [INCOMPLETE: $problem]"
    }
    if (-not $Quiet) {
        "  shard $($sh.Index): $ok passed, $bad failed ($($sh.Count) units, $($sh.Intended) intended tests, $ran ran, route $($sh.Route), $($sh.Plan.Count) invocation(s)) -> $($sh.Log)$mark"
    }
}
$uniqueFailed = @($failedTests | Sort-Object -Unique)
$failed = $uniqueFailed.Count
"elapsed: {0:n1} min" -f ($sw.Elapsed.TotalMinutes)
"TOTAL: $passed passed, $failed unique failed (raw FAILED lines: $failedLines); coverage: executed $executedTotal of $intendedTotal intended tests"
if ($uniqueFailed.Count -gt 0) {
    "FAILED TESTS (re-run solo before blaming the code):"
    $uniqueFailed | ForEach-Object { "  $_" }
}
if ($incompleteShards.Count -gt 0) {
    "INCOMPLETE SHARDS ($($incompleteShards.Count)): the shard did not execute every test its bucket intended - its log has no/too few gtest completion summaries or the counts disagree, so its missing tests are NOT counted as passed. This is a RUNNER failure, not a test failure:"
    foreach ($ic in $incompleteShards) { "  INCOMPLETE SHARDS: shard $($ic.Index) (ran $($ic.Ran) of $($ic.Intended) intended tests): $($ic.Reason)" }
}
if ($poolErrorCount -gt 0) {
    "POOL ERRORS: $poolErrorCount invocation(s) skipped (null/empty Bin) - see *.poolerror files; affected buckets are INCOMPLETE above"
}
exit ([int](($failed -gt 0) -or ($incompleteShards.Count -gt 0) -or ($poolErrorCount -gt 0)))
