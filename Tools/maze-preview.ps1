param(
    [string]$Compiler = 'gcc',
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Docs/maze-preview.png')
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) { throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path ([IO.Path]::GetTempPath()) ('soft3d-maze-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workDirectory
$executable = Join-Path $workDirectory 'render_maze_preview.exe'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$startPath = Join-Path (Split-Path -Parent $OutputPath) 'maze-v3-start.png'
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)
Add-Type -AssemblyName System.Drawing

function Read-MazeBitmap([string]$Path) {
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
        -I App -I Game -I Renderer App/soft3d_ui.c Renderer/soft3d.c Game/soft3d_maze.c `
        Game/soft3d_maze_scene.c Tests/render_maze_preview.c -lm -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Maze preview compilation failed.' }
    $sheet = New-Object Drawing.Bitmap 1744, 576
    $graphics = [Drawing.Graphics]::FromImage($sheet)
    $font = New-Object Drawing.Font 'Segoe UI', 15, ([Drawing.FontStyle]::Bold)
    $brush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(236, 241, 242))
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32, 32, 32))
        $graphics.DrawString('DM-MC02 GRAVITY MAZE V3 | Host-rendered level 01', $font, $brush, 16, 14)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        for ($state = 0; $state -lt 3; $state++) {
            $path = Join-Path $workDirectory ('maze-' + $state + '.ppm')
            & $executable $path $state
            if ($LASTEXITCODE -ne 0) { throw 'Maze preview rendering failed.' }
            $bitmap = Read-MazeBitmap $path
            try {
                if ($state -eq 0) { $bitmap.Save($startPath, [Drawing.Imaging.ImageFormat]::Png) }
                $rectangle = New-Object Drawing.Rectangle (16 + $state * 576), 64, 560, 480
                $graphics.DrawImage($bitmap, $rectangle)
            }
            finally { $bitmap.Dispose() }
        }
        $sheet.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $brush.Dispose()
        $font.Dispose()
        $graphics.Dispose()
        $sheet.Dispose()
    }
}
finally { Pop-Location }
Write-Host ('Maze preview saved: {0}' -f $OutputPath)
Write-Host ('Native 280x240 start frame saved: {0}' -f $startPath)
