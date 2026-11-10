param(
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\StellarBlade',
    [string]$OutputRoot = (Join-Path $env:LOCALAPPDATA 'StellarTool\CrashTraces'),
    [ValidateRange(1, 60)]
    [int]$LaunchWaitMinutes = 10,
    [ValidateRange(1, 60)]
    [int]$MaxRunMinutes = 20
)

$ErrorActionPreference = 'Stop'
$toolDir = $PSScriptRoot
$wprProfile = Join-Path $toolDir 'stellarblade_fileio.wprp'
$gameExe = Join-Path $GameRoot 'SB\Binaries\Win64\SB-Win64-Shipping.exe'
$gameProcessName = [IO.Path]::GetFileNameWithoutExtension($gameExe)
$wpr = Get-Command 'wpr.exe' -ErrorAction SilentlyContinue
$tracerpt = Get-Command 'tracerpt.exe' -ErrorAction SilentlyContinue
$python = Get-Command 'python.exe' -ErrorAction SilentlyContinue

if (-not $wpr) { throw 'Windows Performance Recorder (wpr.exe) is not available.' }
if (-not $tracerpt) { throw 'Windows Performance Recorder tools (tracerpt.exe) are not available.' }
if (-not $python) { throw 'python.exe is not available in PATH; install Python 3 or add it to PATH.' }
if (-not (Test-Path -LiteralPath $wprProfile -PathType Leaf)) { throw "WPR profile not found: $wprProfile" }
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Game executable not found: $gameExe" }

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run PowerShell as Administrator, then run this script again. WPR FileIO tracing requires elevation.'
}

$alreadyRunning = Get-Process -Name $gameProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if ($alreadyRunning) {
    throw 'Close Stellar Blade first. The script starts the trace before the next launch so it captures startup.'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outputDir = Join-Path $OutputRoot $stamp
$contextDir = Join-Path $outputDir 'context'
$etlPath = Join-Path $outputDir 'system_fileio.etl'
$xmlPath = Join-Path $outputDir 'system_fileio.xml'
New-Item -ItemType Directory -Path $contextDir -Force | Out-Null

$captureStart = Get-Date
$manifestPaths = [System.Collections.Generic.List[string]]::new()
$manifestDirs = @(
    $GameRoot,
    (Join-Path $GameRoot 'SB\Binaries\Win64'),
    (Join-Path $GameRoot 'SB\Binaries\Win64\ue4ss\Mods'),
    (Join-Path $GameRoot 'SB\Content\Paks\~mods')
)
$pakModRoot = Join-Path $GameRoot 'SB\Content\Paks\~mods'
if (Test-Path -LiteralPath $pakModRoot -PathType Container) {
    $manifestDirs += Get-ChildItem -LiteralPath $pakModRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne 'disabled' } |
        ForEach-Object { $_.FullName }
}
foreach ($manifestDir in $manifestDirs) {
    if (Test-Path -LiteralPath $manifestDir -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $manifestDir -Filter 'vortex.deployment*.json' -File -ErrorAction SilentlyContinue) {
            if (-not $manifestPaths.Contains($file.FullName)) { $manifestPaths.Add($file.FullName) }
        }
    }
}

$manifestSnapshotDir = Join-Path $contextDir 'vortex-manifests'
New-Item -ItemType Directory -Path $manifestSnapshotDir -Force | Out-Null
$manifestIndex = 0
foreach ($manifestPath in $manifestPaths) {
    $manifestIndex++
    $destination = Join-Path $manifestSnapshotDir ('{0:D2}-{1}' -f $manifestIndex, [IO.Path]::GetFileName($manifestPath))
    Copy-Item -LiteralPath $manifestPath -Destination $destination
}

$modsTxt = Join-Path $GameRoot 'SB\Binaries\Win64\ue4ss\Mods\mods.txt'
if (Test-Path -LiteralPath $modsTxt -PathType Leaf) {
    Copy-Item -LiteralPath $modsTxt -Destination (Join-Path $contextDir 'ue4ss-mods.txt')
}

$captureStarted = $false
$gameProcessId = $null
$gameExitObserved = $false
$wprDroppedEvents = 'not reported'
try {
    Write-Host "Report folder: $outputDir"
    Write-Host 'Starting Windows File I/O trace...'
    $profileSpec = "$wprProfile!StellarBladeFileIO.Light"
    $startOutput = & $wpr.Source -start $profileSpec -filemode 2>&1
    $startOutput | Set-Content -LiteralPath (Join-Path $outputDir 'wpr_start.txt') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw "WPR could not start (exit $LASTEXITCODE). See wpr_start.txt." }
    $captureStarted = $true

    Write-Host "Trace is active. Launch Stellar Blade through Vortex within $LaunchWaitMinutes minutes."
    $launchDeadline = (Get-Date).AddMinutes($LaunchWaitMinutes)
    $gameProcess = $null
    while ((Get-Date) -lt $launchDeadline) {
        $gameProcess = Get-Process -Name $gameProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($gameProcess) { break }
        Start-Sleep -Milliseconds 500
    }

    if (-not $gameProcess) {
        Write-Host 'The game process did not appear before the launch timeout.'
    }
    else {
        $gameProcessId = $gameProcess.Id
        Write-Host "Capturing game PID $gameProcessId. Reproduce the crash; the trace stops when the game exits."
        $runDeadline = (Get-Date).AddMinutes($MaxRunMinutes)
        while ((Get-Date) -lt $runDeadline) {
            $stillRunning = Get-Process -Id $gameProcessId -ErrorAction SilentlyContinue
            if (-not $stillRunning) {
                $gameExitObserved = $true
                Write-Host 'The game process exited; stopping the trace.'
                break
            }
            Start-Sleep -Seconds 1
        }
        if (-not $gameExitObserved) {
            Write-Host "The $MaxRunMinutes-minute capture limit was reached; the game was left running."
        }
    }
}
finally {
    if ($captureStarted) {
        Write-Host 'Stopping and saving the ETW trace...'
        $stopOutput = & $wpr.Source -stop $etlPath 'Stellar Blade file I/O crash trace' -skipPdbGen 2>&1
        $stopOutput | Set-Content -LiteralPath (Join-Path $outputDir 'wpr_stop.txt') -Encoding UTF8
        if ($LASTEXITCODE -ne 0) { throw "WPR could not save the ETL (exit $LASTEXITCODE). See wpr_stop.txt." }
        $stopText = [string]::Join("`n", @($stopOutput | ForEach-Object { [string]$_ }))
        $dropMatch = [regex]::Match($stopText, '(?i)dropped\s+([\d,]+)\s+events?')
        if ($dropMatch.Success) {
            $wprDroppedEvents = $dropMatch.Groups[1].Value
            Write-Warning "WPR reported $wprDroppedEvents dropped events. Treat the file trace as incomplete."
        }
        elseif ($stopText -match '(?i)successfully saved') {
            $wprDroppedEvents = '0 reported'
        }
    }
}

$captureInfoPath = Join-Path $outputDir 'capture_info.txt'
@(
    "Capture started: $($captureStart.ToString('o'))",
    "ETL saved: $(Test-Path -LiteralPath $etlPath -PathType Leaf)",
    "Game PID: $(if ($gameProcessId) { $gameProcessId } else { 'not observed' })",
    "Game exit observed: $gameExitObserved",
    "WPR dropped events: $wprDroppedEvents",
    "Deployment manifests snapshotted: $($manifestPaths.Count)",
    'Trace conversion: pending'
) | Set-Content -LiteralPath $captureInfoPath -Encoding UTF8

$ue4ssLog = Join-Path $GameRoot 'SB\Binaries\Win64\ue4ss\UE4SS.log'
if (Test-Path -LiteralPath $ue4ssLog -PathType Leaf) {
    Copy-Item -LiteralPath $ue4ssLog -Destination (Join-Path $contextDir 'UE4SS.log') -Force
}

$tracerptOutput = & $tracerpt.Source $etlPath -o $xmlPath -of XML -y -lr 2>&1
$tracerptOutput | Set-Content -LiteralPath (Join-Path $outputDir 'tracerpt.txt') -Encoding UTF8
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $xmlPath -PathType Leaf)) {
    throw "tracerpt could not convert the ETL (exit $LASTEXITCODE). See tracerpt.txt. The raw ETL was kept."
}

$modsRoot = Join-Path $env:APPDATA 'Vortex\stellarblade\mods'
$parserArgs = @(
    (Join-Path $toolDir 'ue4_fileio_trace_report.py'),
    '--xml', $xmlPath,
    '--pid', $(if ($gameProcessId) { [string]$gameProcessId } else { '0' }),
    '--game-root', $GameRoot,
    '--mods-root', $modsRoot,
    '--dropped-events', $wprDroppedEvents,
    '--output-dir', $outputDir
)
foreach ($manifestPath in $manifestPaths) {
    $parserArgs += @('--manifest', $manifestPath)
}
& $python.Source @parserArgs
if ($LASTEXITCODE -ne 0) { throw "File-I/O report generation failed (exit $LASTEXITCODE). The ETL and XML were kept." }

$crashRoot = Join-Path $env:LOCALAPPDATA 'SB\Saved\Crashes'
$crashContext = $null
if (Test-Path -LiteralPath $crashRoot -PathType Container) {
    $crashContext = Get-ChildItem -LiteralPath $crashRoot -Filter 'CrashContext.runtime-xml' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $captureStart } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}
if ($crashContext) {
    $crashDir = $crashContext.Directory.FullName
    $analyzerArgs = @(
        (Join-Path $toolDir 'ue4_minidump_report.py'),
        $crashDir,
        '--game-dir', $GameRoot,
        '--exe', $gameExe,
        '--output', (Join-Path $outputDir 'crash_analysis.md')
    )
    $analysisOutput = & $python.Source @analyzerArgs 2>&1
    $analysisOutput | Set-Content -LiteralPath (Join-Path $outputDir 'crash_analyzer.txt') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "The crash analyzer did not finish successfully (exit $LASTEXITCODE); see crash_analyzer.txt."
    }
}
else {
    Write-Host 'No new CrashContext.runtime-xml was found; the File-I/O trace is still available.'
}

Remove-Item -LiteralPath $xmlPath -Force
@(
    "Capture started: $($captureStart.ToString('o'))",
    "Capture ended: $((Get-Date).ToString('o'))",
    "Game PID: $(if ($gameProcessId) { $gameProcessId } else { 'not observed' })",
    "Game exit observed: $gameExitObserved",
    "WPR dropped events: $wprDroppedEvents",
    "Deployment manifests snapshotted: $($manifestPaths.Count)",
    "Crash context: $(if ($crashContext) { $crashContext.FullName } else { 'not found' })",
    'The ETL is filtered to Stellar Blade file events; keep it local unless you review it before sharing.'
) | Set-Content -LiteralPath $captureInfoPath -Encoding UTF8

Write-Host ''
Write-Host 'Capture finished.'
Write-Host "Primary report: $(Join-Path $outputDir 'fileio_report.md')"
if ($crashContext) { Write-Host "Crash report:   $(Join-Path $outputDir 'crash_analysis.md')" }
Write-Host "CSV details:    $(Join-Path $outputDir 'game_fileio.csv')"
Write-Host 'Share the two Markdown reports and capture_info.txt. Keep system_fileio.etl private unless reviewed.'
