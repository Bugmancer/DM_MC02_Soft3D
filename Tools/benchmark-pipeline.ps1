param(
    [string]$Compiler = 'gcc',
    [ValidateRange(1, 100000)]
    [int]$Frames = 128,
    [ValidateRange(1, 9)]
    [int]$Pairs = 3,
    [string]$OutputPath,
    [switch]$BuildOnly,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerCommand = Get-Command $Compiler -ErrorAction Stop
$compilerPath = $compilerCommand.Source
$workDirectory = Join-Path ([IO.Path]::GetTempPath()) ('soft3d-pipeline-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workDirectory
$referenceExecutable = Join-Path $workDirectory 'reference.exe'
$currentExecutable = Join-Path $workDirectory 'current.exe'
if (-not $OutputPath) { $OutputPath = Join-Path $projectRoot 'Docs/pipeline-benchmark.json' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$flags = @('-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-pedantic', '-IRenderer', '-IGame')
$sharedSources = @('Renderer/soft3d_models.c', 'Game/soft3d_maze.c', 'Game/soft3d_maze_scene.c', 'Tests/benchmark_pipeline.c')
$trackedFiles = @(
    'Tests/reference/soft3d_linear_reference.c', 'Renderer/soft3d.c', 'Renderer/soft3d.h',
    'Renderer/soft3d_models.c', 'Renderer/soft3d_models.h', 'Game/soft3d_maze.c', 'Game/soft3d_maze.h',
    'Game/soft3d_maze_scene.c', 'Game/soft3d_maze_scene.h', 'ThirdParty/cute_c2/cute_c2.h',
    'ThirdParty/cute_c2/cute_c2_port.h', 'Tests/benchmark_pipeline.c', 'Tools/benchmark-pipeline.ps1'
)

function Get-SourceHashes {
    $hashes = [ordered]@{}
    foreach ($path in $trackedFiles) { $hashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    return $hashes
}

function Invoke-Pipeline([string]$Path, [string]$Argument) {
    $json = (& $Path $Argument | Out-String)
    if ($LASTEXITCODE -ne 0) { throw "Pipeline executable failed: $Path" }
    $result = $json | ConvertFrom-Json
    if ($result.schema_version -ne 1 -or $result.width -ne 280 -or $result.height -ne 240 -or
        $result.band_rows -ne 16 -or @($result.workloads).Count -ne 20 -or
        @($result.workloads | Group-Object id).Count -ne 20) {
        throw 'Pipeline output did not contain the complete workload set.'
    }
    foreach ($row in $result.workloads) {
        if ($row.oracle_hash -notmatch '^[0-9a-f]{16}$') { throw "Invalid oracle hash for $($row.id)." }
        if (-not $result.oracle_only) {
            foreach ($field in @('geometry_us', 'raster_us', 'total_us')) {
                $value = [double]$row.$field
                if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -le 0) {
                    throw "Invalid $field for $($row.id)."
                }
            }
        }
    }
    return $result
}

function Assert-Oracle($Expected, $Actual) {
    foreach ($row in $Expected.workloads) {
        $other = @($Actual.workloads | Where-Object { $_.id -eq $row.id })
        if ($other.Count -ne 1 -or $other[0].oracle_hash -ne $row.oracle_hash -or
            $other[0].seed -ne $row.seed -or $other[0].walls -ne $row.walls) {
            throw "Reference/current oracle mismatch in $($row.id); timing comparison is blocked."
        }
    }
}

function Get-Median([double[]]$Values) {
    $sorted = @($Values | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 0) { return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0 }
    return $sorted[$middle]
}

Push-Location $projectRoot
try {
    $sourceHashes = Get-SourceHashes
    & $compilerPath @flags Tests/reference/soft3d_linear_reference.c @sharedSources -lm -o $referenceExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Reference pipeline compilation failed.' }
    & $compilerPath @flags Renderer/soft3d.c @sharedSources -lm -o $currentExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Current pipeline compilation failed.' }
    Write-Host "Reference executable: $referenceExecutable"
    Write-Host "Current executable: $currentExecutable"
    if ($BuildOnly) { return }
    $referenceOracle = Invoke-Pipeline $referenceExecutable '--oracle-only'
    $currentOracle = Invoke-Pipeline $currentExecutable '--oracle-only'
    Assert-Oracle $referenceOracle $currentOracle
    if ($VerifyOnly) {
        Write-Host 'All 20 reference/current workload pixel hashes match; no timings collected.'
        return
    }
    $runs = @()
    for ($pair = 0; $pair -lt $Pairs; $pair++) {
        Write-Host ('Native pipeline pair {0}/{1}' -f ($pair + 1), $Pairs)
        if (($pair % 2) -eq 0) {
            $reference = Invoke-Pipeline $referenceExecutable ([string]$Frames)
            $current = Invoke-Pipeline $currentExecutable ([string]$Frames)
            $order = @('reference', 'current')
        } else {
            $current = Invoke-Pipeline $currentExecutable ([string]$Frames)
            $reference = Invoke-Pipeline $referenceExecutable ([string]$Frames)
            $order = @('current', 'reference')
        }
        Assert-Oracle $referenceOracle $reference
        Assert-Oracle $referenceOracle $current
        $runs += [pscustomobject]@{ pair = $pair + 1; order = $order; reference = $reference; current = $current }
    }
    $comparison = @()
    foreach ($expected in $referenceOracle.workloads) {
        $row = [ordered]@{ id = $expected.id; level = $expected.level; seed = $expected.seed; walls = $expected.walls; oracle_hash = $expected.oracle_hash }
        foreach ($field in @('geometry_us', 'raster_us', 'total_us')) {
            $oldValues = @()
            $newValues = @()
            $ratios = @()
            foreach ($run in $runs) {
                $old = $run.reference.workloads | Where-Object { $_.id -eq $expected.id }
                $new = $run.current.workloads | Where-Object { $_.id -eq $expected.id }
                $oldValues += [double]$old.$field
                $newValues += [double]$new.$field
                $ratios += [double]$old.$field / [double]$new.$field
            }
            $row[$field] = [ordered]@{
                reference_median = Get-Median $oldValues
                current_median = Get-Median $newValues
                paired_speedup_median = Get-Median $ratios
            }
        }
        $comparison += [pscustomobject]$row
    }
    $finalHashes = Get-SourceHashes
    foreach ($path in $trackedFiles) {
        if ($sourceHashes[$path] -ne $finalHashes[$path]) { throw "Source changed during benchmark: $path" }
    }
    $report = [ordered]@{
        schema_version = 1
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        scope = 'Native host CPU only. Not STM32 or LCD/SPI timing. Geometry includes begin-frame and scene submission; raster includes all 16-row bands. Pose creation, oracle hashing, warmup and level generation are outside timed phases.'
        statistic = 'Per-run arithmetic mean over frames; median across alternating pairs. Speedup is median of paired reference/current ratios.'
        oracle = 'Exact FNV-1a-64 over every RGB565 pixel in little-byte-first order at eight deterministic poses per workload. A mismatch blocks timing comparison.'
        counter_note = 'Only common prepared-triangle statistics are recorded. Newly added core-specific counters are not compared against the legacy reference.'
        frames_per_run = $Frames
        pairs = $Pairs
        compiler = ((& $compilerPath --version | Select-Object -First 1) -join '')
        compiler_path = $compilerPath
        compiler_flags = @($flags + @('-lm'))
        host = [ordered]@{
            machine = [Environment]::MachineName
            os = [Environment]::OSVersion.ToString()
            processor = $env:PROCESSOR_IDENTIFIER
            logical_processors = [Environment]::ProcessorCount
            process_64_bit = [Environment]::Is64BitProcess
            powershell = $PSVersionTable.PSVersion.ToString()
        }
        source_sha256 = $sourceHashes
        executable_sha256 = [ordered]@{
            reference = (Get-FileHash -LiteralPath $referenceExecutable -Algorithm SHA256).Hash
            current = (Get-FileHash -LiteralPath $currentExecutable -Algorithm SHA256).Hash
        }
        reference_oracle = $referenceOracle
        current_oracle = $currentOracle
        comparison = $comparison
        raw_paired_runs = $runs
    }
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    Write-Host "Native pipeline benchmark report: $OutputPath"
}
finally { Pop-Location }
