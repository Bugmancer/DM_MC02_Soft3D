param(
    [string]$Compiler = 'gcc',
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Docs/status-preview.png')
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) { throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path ([IO.Path]::GetTempPath()) ('soft3d-status-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workDirectory
$executable = Join-Path $workDirectory 'render_status_preview.exe'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)
Add-Type -AssemblyName System.Drawing

function Read-StatusBitmap([string]$Path) {
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
        -I App App/soft3d_ui.c Tests/render_status_preview.c -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Status preview compilation failed.' }
    $sheet = New-Object Drawing.Bitmap 1744, 576
    $graphics = [Drawing.Graphics]::FromImage($sheet)
    $font = New-Object Drawing.Font 'Segoe UI', 15, ([Drawing.FontStyle]::Bold)
    $brush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(236, 241, 242))
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32, 32, 32))
        $graphics.DrawString('SOFT3D STATUS SCREEN | Host preview of firmware UI', $font, $brush, 16, 14)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        for ($state = 0; $state -lt 3; $state++) {
            $path = Join-Path $workDirectory ('status-' + $state + '.ppm')
            & $executable $path $state
            if ($LASTEXITCODE -ne 0) { throw 'Status preview rendering failed.' }
            $bitmap = Read-StatusBitmap $path
            try {
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
Write-Host ('Status preview saved: {0}' -f $OutputPath)
