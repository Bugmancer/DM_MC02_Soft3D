param(
    [string]$Compiler = 'gcc',
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Docs/engine-lab-preview.png')
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) { throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path ([IO.Path]::GetTempPath()) ('soft3d-lab-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workDirectory
$executable = Join-Path $workDirectory 'render_lab_preview.exe'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$torusPath = Join-Path (Split-Path -Parent $OutputPath) 'engine-lab-torus.png'
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)
Add-Type -AssemblyName System.Drawing

function Read-LabBitmap([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $expected = [Text.Encoding]::ASCII.GetBytes("P6`n280 240`n255`n")
        foreach ($value in $expected) {
            if ($stream.ReadByte() -ne $value) { throw "Unexpected PPM header: $Path" }
        }
        $bitmap = New-Object Drawing.Bitmap 280, 240
        try {
            for ($y = 0; $y -lt 240; $y++) {
                for ($x = 0; $x -lt 280; $x++) {
                    $red = $stream.ReadByte()
                    $green = $stream.ReadByte()
                    $blue = $stream.ReadByte()
                    if ($blue -lt 0) { throw "Truncated image: $Path" }
                    $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb($red, $green, $blue))
                }
            }
            if ($stream.ReadByte() -ne -1) { throw "Unexpected trailing data: $Path" }
            return $bitmap
        }
        catch { $bitmap.Dispose(); throw }
    }
    finally { $stream.Dispose() }
}

Push-Location $projectRoot
try {
    & $compilerCommand.Source -std=c99 -Wall -Wextra -Werror -Wconversion -pedantic -O2 `
        -I App -I Game -I Renderer App/soft3d_ui.c App/soft3d_scene.c Renderer/soft3d.c `
        Renderer/soft3d_models.c Game/soft3d_maze.c Game/soft3d_maze_scene.c `
        Tests/render_lab_preview.c -lm -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'LAB preview compilation failed.' }
    $sheet = New-Object Drawing.Bitmap 1168, 1072
    $graphics = [Drawing.Graphics]::FromImage($sheet)
    $titleFont = New-Object Drawing.Font 'Segoe UI', 18, ([Drawing.FontStyle]::Bold)
    $captionFont = New-Object Drawing.Font 'Segoe UI', 13
    $brush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(236, 241, 242))
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32, 32, 32))
        $graphics.DrawString('DM-MC02 ENGINE LAB', $titleFont, $brush, 16, 10)
        $graphics.DrawString('Host rendering, initial metrics; no board timing', $captionFont, $brush, 16, 44)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        for ($workload = 0; $workload -lt 4; $workload++) {
            $path = Join-Path $workDirectory ('lab-' + $workload + '.ppm')
            & $executable $path $workload
            if ($LASTEXITCODE -ne 0) { throw 'LAB preview rendering or framing check failed.' }
            $bitmap = Read-LabBitmap $path
            try {
                if ($workload -eq 2) { $bitmap.Save($torusPath, [Drawing.Imaging.ImageFormat]::Png) }
                $column = $workload % 2
                $row = [Math]::Floor($workload / 2)
                $rectangle = New-Object Drawing.Rectangle (16 + $column * 576), (80 + $row * 496), 560, 480
                $graphics.DrawImage($bitmap, $rectangle)
            }
            finally { $bitmap.Dispose() }
        }
        $sheet.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $brush.Dispose()
        $captionFont.Dispose()
        $titleFont.Dispose()
        $graphics.Dispose()
        $sheet.Dispose()
    }
}
finally { Pop-Location }
Write-Host ('LAB preview saved: {0}' -f $OutputPath)
Write-Host ('Native 280x240 torus frame saved: {0}' -f $torusPath)
