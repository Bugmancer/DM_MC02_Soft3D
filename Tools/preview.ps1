param(
    [string]$Compiler = 'gcc',
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Docs/preview.png')
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) { throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$workDirectory = Join-Path ([IO.Path]::GetTempPath()) ('soft3d-preview-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workDirectory
$executable = Join-Path $workDirectory 'render_preview.exe'
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath)

Add-Type -AssemblyName System.Drawing

function Read-PreviewBitmap([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        # This P6 header is emitted by the native preview tool with fixed dimensions.
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
                    if ($blue -lt 0) { throw "Truncated PPM image: $Path" }
                    $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb($red, $green, $blue))
                }
            }
            if ($stream.ReadByte() -ne -1) { throw "Unexpected trailing PPM data: $Path" }
            return $bitmap
        }
        catch {
            $bitmap.Dispose()
            throw
        }
    }
    finally { $stream.Dispose() }
}

Push-Location $projectRoot
try {
    & $compilerCommand.Source -std=c99 -Wall -Wextra -Werror -Wconversion -pedantic -O2 `
        -I Renderer -I App Renderer/soft3d.c Renderer/soft3d_models.c App/soft3d_scene.c App/soft3d_ui.c `
        Tests/render_preview.c -lm -o $executable
    if ($LASTEXITCODE -ne 0) { throw 'Preview compilation failed.' }

    $sheet = New-Object Drawing.Bitmap 1744, 1568
    $graphics = [Drawing.Graphics]::FromImage($sheet)
    $titleFont = New-Object Drawing.Font 'Segoe UI', 18, ([Drawing.FontStyle]::Bold)
    $captionFont = New-Object Drawing.Font 'Segoe UI', 11
    $white = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(236, 241, 242))
    $gray = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(164, 174, 177))
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(16, 16, 16))
        $graphics.DrawString('DM-MC02 Soft3D | Host Rendered Preview', $titleFont, $white, 16, 8)
        $graphics.DrawString('280 x 240 RGB565 - shared renderer and UI code - hardware performance is not measured here', $captionFont, $gray, 18, 42)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        $modelIndex = 0
        $allHashes = @{}
        foreach ($model in @('cube', 'torus', 'orbit')) {
            for ($mode = 0; $mode -lt 3; $mode++) {
                $poses = @()
                for ($pose = 0; $pose -lt 2; $pose++) {
                    $path = Join-Path $workDirectory ($model + '-' + $mode + '-' + $pose + '.ppm')
                    $result = & $executable $path $model $mode $pose
                    if ($LASTEXITCODE -ne 0) { throw 'Native preview generation failed.' }
                    Write-Host $result
                    if ($result -notmatch 'non_background=(\d+) hash=([0-9a-f]+)') {
                        throw 'Preview did not return pixel verification data.'
                    }
                    if ([int]$Matches[1] -lt 100) { throw "Blank or underfilled scene: $path" }
                    $hash = $Matches[2]
                    if ($allHashes.ContainsKey($hash)) { throw "Duplicate geometry image: $path" }
                    $allHashes[$hash] = $true
                    $poses += $hash
                    if ($pose -eq 0) {
                        $bitmap = Read-PreviewBitmap $path
                        try {
                            $rectangle = New-Object Drawing.Rectangle (16 + $mode * 576), (80 + $modelIndex * 496), 560, 480
                            $graphics.DrawImage($bitmap, $rectangle)
                        }
                        finally { $bitmap.Dispose() }
                    }
                }
                if ($poses[0] -eq $poses[1]) { throw "Rotation did not change the $model image." }
            }
            $modelIndex++
        }
        $sheet.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $gray.Dispose()
        $white.Dispose()
        $captionFont.Dispose()
        $titleFont.Dispose()
        $graphics.Dispose()
        $sheet.Dispose()
    }
}
finally { Pop-Location }
Write-Host ('Preview saved: {0}' -f $OutputPath)
Write-Host ('Verified 18 distinct nonblank geometry frames. Native artifacts: {0}' -f $workDirectory)
