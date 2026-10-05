param(
    [string]$Compiler = 'gcc',
    [ValidateRange(128, 100000)]
    [int]$Frames = 1024,
    [ValidateRange(1, 20)]
    [int]$Pairs = 5,
    [string]$BaselineExecutable,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) { throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.' }
if ($BaselineExecutable) {
    if (-not (Test-Path -LiteralPath $BaselineExecutable -PathType Leaf)) {
        throw 'The baseline executable does not exist.'
    }
    $BaselineExecutable = (Resolve-Path -LiteralPath $BaselineExecutable).Path
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path ([IO.Path]::GetTempPath()) ('soft3d-benchmark-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workDirectory
$executable = Join-Path $workDirectory 'benchmark_renderer.exe'
if (-not $OutputPath) { $OutputPath = Join-Path $workDirectory 'benchmark.json' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)

function Invoke-RendererBenchmark([string]$Path, [string]$Label) {
    Write-Host ('Running {0} host benchmark' -f $Label)
    $lines = & $Path $Frames
    if ($LASTEXITCODE -ne 0) { throw ('{0} benchmark failed.' -f $Label) }
    $rows = @()
    foreach ($line in $lines) {
        Write-Host $line
        if ($line -match '^(cube|torus) (texture|lit|wire) raster_us=([0-9.]+) frame_us=([0-9.]+)$') {
            $rows += [pscustomobject]@{
                Model = $Matches[1]
                Mode = $Matches[2]
                RasterMicroseconds = [double]::Parse($Matches[3], [Globalization.CultureInfo]::InvariantCulture)
                FrameMicroseconds = [double]::Parse($Matches[4], [Globalization.CultureInfo]::InvariantCulture)
            }
        }
    }
    if ($rows.Count -ne 6 -or @($rows | Group-Object Model, Mode).Count -ne 6) {
        throw ('{0} benchmark did not return all six unique results.' -f $Label)
    }
    foreach ($row in $rows) {
        if ($row.RasterMicroseconds -le 0 -or $row.FrameMicroseconds -le 0) {
            throw ('{0} benchmark returned invalid timing.' -f $Label)
        }
    }
    return $rows
}

function Get-Median([double[]]$Values) {
    if ($Values.Count -eq 0) { throw 'Cannot aggregate an empty benchmark.' }
    $sorted = @($Values | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 0) { return ($sorted[$middle - 1] + $sorted[$middle]) / 2 }
    return $sorted[$middle]
}

Push-Location $projectRoot
try {
    & $compilerCommand.Source -std=c99 -Wall -Wextra -Werror -Wconversion -pedantic -O2 `
        -I Renderer Renderer/soft3d.c Renderer/soft3d_models.c Tests/benchmark_renderer.c `
        -lm -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark compilation failed.' }
    $compilerVersion = (& $compilerCommand.Source --version | Select-Object -First 1)
    $pairedRuns = @()
    for ($pair = 0; $pair -lt $Pairs; $pair++) {
        $before = @()
        # Alternate order so a consistently warmer second run does not favor one build.
        if (($pair % 2) -eq 0) {
            if ($BaselineExecutable) { $before = @(Invoke-RendererBenchmark $BaselineExecutable ('baseline pair ' + ($pair + 1))) }
            $after = @(Invoke-RendererBenchmark $executable ('current pair ' + ($pair + 1)))
        }
        else {
            $after = @(Invoke-RendererBenchmark $executable ('current pair ' + ($pair + 1)))
            if ($BaselineExecutable) { $before = @(Invoke-RendererBenchmark $BaselineExecutable ('baseline pair ' + ($pair + 1))) }
        }
        $pairedRuns += [pscustomobject]@{
            Pair = $pair + 1
            Order = $(if (-not $BaselineExecutable) { 'current' } elseif (($pair % 2) -eq 0) { 'baseline,current' } else { 'current,baseline' })
            Baseline = $before
            Current = $after
        }
    }
    $baseline = @()
    $current = @()
    $comparison = @()
    foreach ($model in @('cube', 'torus')) {
        foreach ($mode in @('texture', 'lit', 'wire')) {
            $newRows = @($pairedRuns | ForEach-Object { $_.Current } | Where-Object { $_.Model -eq $model -and $_.Mode -eq $mode })
            $current += [pscustomobject]@{
                Model = $model
                Mode = $mode
                RasterMicroseconds = Get-Median @($newRows.RasterMicroseconds)
                FrameMicroseconds = Get-Median @($newRows.FrameMicroseconds)
            }
            if ($BaselineExecutable) {
                $oldRows = @($pairedRuns | ForEach-Object { $_.Baseline } | Where-Object { $_.Model -eq $model -and $_.Mode -eq $mode })
                $baseline += [pscustomobject]@{
                    Model = $model
                    Mode = $mode
                    RasterMicroseconds = Get-Median @($oldRows.RasterMicroseconds)
                    FrameMicroseconds = Get-Median @($oldRows.FrameMicroseconds)
                }
                $rasterRatios = @()
                $frameRatios = @()
                for ($i = 0; $i -lt $Pairs; $i++) {
                    $rasterRatios += $oldRows[$i].RasterMicroseconds / $newRows[$i].RasterMicroseconds
                    $frameRatios += $oldRows[$i].FrameMicroseconds / $newRows[$i].FrameMicroseconds
                }
                $comparison += [pscustomobject]@{
                    Model = $model
                    Mode = $mode
                    RasterSpeedup = [math]::Round((Get-Median $rasterRatios), 3)
                    FrameSpeedup = [math]::Round((Get-Median $frameRatios), 3)
                }
            }
        }
    }
    $sourceHashes = [ordered]@{}
    foreach ($source in @('Renderer/soft3d.c', 'Renderer/soft3d_models.c', 'Renderer/soft3d.h', 'Tests/benchmark_renderer.c')) {
        $sourceHashes[$source] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    }
    $report = [ordered]@{
        TimestampUtc = [DateTime]::UtcNow.ToString('o')
        Scope = 'Native CPU renderer only; excludes STM32, SPI DMA, LCD transfer and application tasks.'
        Compiler = $compilerVersion
        CompilerFlags = '-std=c99 -O2'
        Processor = $env:PROCESSOR_IDENTIFIER
        Width = 280
        Height = 240
        BandRows = 16
        FramesPerRun = $Frames
        RunsPerPair = 3
        Pairs = $Pairs
        Statistic = 'Median of three native runs per result, then median across pairs; speedups are medians of paired ratios.'
        CurrentSourceHashes = $sourceHashes
        CurrentExecutableSha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
        BaselineExecutableSha256 = $(if ($BaselineExecutable) { (Get-FileHash -LiteralPath $BaselineExecutable -Algorithm SHA256).Hash } else { $null })
        Current = $current
        Baseline = $baseline
        Comparison = $comparison
        RawPairedRuns = $pairedRuns
    }
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    if ($comparison.Count -gt 0) { $comparison | Format-Table -AutoSize | Out-Host }
}
finally { Pop-Location }
Write-Host ('Host benchmark report: {0}' -f $OutputPath)
Write-Host ('Current benchmark executable: {0}' -f $executable)
