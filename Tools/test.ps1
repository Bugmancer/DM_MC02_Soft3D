param(
    [string]$Compiler = 'gcc',
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('soft3d-tests-' + [Guid]::NewGuid().ToString('N')))
)

$ErrorActionPreference = 'Stop'
$compilerCommand = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCommand) {
    throw 'GCC was not found. Pass -Compiler with the path to gcc.exe.'
}
$compilerPath = $compilerCommand.Source
$projectRoot = Split-Path -Parent $PSScriptRoot
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

$cases = @(
    @{
        Name = 'renderer'
        Sources = @('Renderer/soft3d.c', 'Renderer/soft3d_models.c', 'Tests/test_renderer.c')
        Includes = @('Renderer')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @('-lm')
    },
    @{
        Name = 'scene'
        Sources = @('Renderer/soft3d.c', 'Renderer/soft3d_models.c', 'App/soft3d_scene.c', 'Tests/test_scene.c')
        Includes = @('Renderer', 'App')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @('-lm')
    },
    @{
        Name = 'damage'
        Sources = @('Renderer/soft3d.c', 'Renderer/soft3d_models.c', 'App/soft3d_scene.c', 'App/soft3d_ui.c', 'App/soft3d_damage.c', 'Tests/test_damage.c')
        Includes = @('Renderer', 'App')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @('-lm')
    },
    @{
        Name = 'lcd'
        Sources = @('Board/soft3d_lcd.c', 'Tests/lcd/test_soft3d_lcd.c')
        Includes = @('Tests/lcd/stubs', 'Board')
        Flags = @('-Wconversion', '-pedantic')
        Libraries = @()
    },
    @{
        Name = 'imu'
        Sources = @('Board/soft3d_imu.c', 'Tests/imu/test_soft3d_imu.c')
        Includes = @('Tests/imu/stubs', 'Board')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @()
    },
    @{
        Name = 'motion'
        Sources = @('Motion/soft3d_motion.c', 'ThirdParty/Fusion/FusionAhrs.c', 'ThirdParty/Fusion/FusionBias.c', 'Tests/test_motion.c')
        Includes = @('Motion', 'ThirdParty/Fusion')
        Flags = @('-pedantic', '-O2', '-DFUSION_USE_NORMAL_SQRT')
        Libraries = @('-lm')
    },
    @{
        Name = 'maze'
        Sources = @('Game/soft3d_maze.c', 'Tests/test_maze.c')
        Includes = @('Game')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @('-lm')
    },
    @{
        Name = 'maze_scene'
        Sources = @('Game/soft3d_maze.c', 'Game/soft3d_maze_scene.c', 'Renderer/soft3d.c', 'App/soft3d_ui.c', 'Tests/test_maze_scene.c')
        Includes = @('Game', 'Renderer', 'App')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @('-lm')
    },
    @{
        Name = 'profile'
        Sources = @('App/soft3d_profile.c', 'Tests/test_profile.c')
        Includes = @('App')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @()
    },
    @{
        Name = 'input_ui'
        Sources = @('App/soft3d_input.c', 'App/soft3d_ui.c', 'Tests/test_input_ui.c')
        Includes = @('App')
        Flags = @('-Wconversion', '-pedantic')
        Libraries = @()
    },
    @{
        Name = 'boot'
        Sources = @('App/soft3d_boot.c', 'App/soft3d_ui.c', 'Tests/test_boot.c')
        Includes = @('App', 'Board')
        Flags = @('-Wconversion', '-pedantic', '-O2')
        Libraries = @()
    },
    @{
        Name = 'app_polling'
        Sources = @('Tests/app/test_soft3d_app.c', 'Renderer/soft3d.c', 'Renderer/soft3d_models.c', 'Game/soft3d_maze.c', 'Game/soft3d_maze_scene.c', 'App/soft3d_scene.c', 'App/soft3d_profile.c', 'App/soft3d_damage.c', 'App/soft3d_ui.c', 'App/soft3d_input.c')
        Includes = @('Tests/app/stubs', 'App', 'Board', 'Motion', 'Renderer', 'Game', 'ThirdParty/Fusion')
        Flags = @('-pedantic', '-O2', '-DFUSION_USE_NORMAL_SQRT', '-DSOFT3D_DIAGNOSTIC_POLLING=1')
        Libraries = @('-lm')
    },
    @{
        Name = 'app_dma'
        Sources = @('Tests/app/test_soft3d_app.c', 'Renderer/soft3d.c', 'Renderer/soft3d_models.c', 'Game/soft3d_maze.c', 'Game/soft3d_maze_scene.c', 'App/soft3d_scene.c', 'App/soft3d_profile.c', 'App/soft3d_damage.c', 'App/soft3d_ui.c', 'App/soft3d_input.c')
        Includes = @('Tests/app/stubs', 'App', 'Board', 'Motion', 'Renderer', 'Game', 'ThirdParty/Fusion')
        Flags = @('-pedantic', '-O2', '-DFUSION_USE_NORMAL_SQRT', '-DSOFT3D_DIAGNOSTIC_POLLING=0')
        Libraries = @('-lm')
    },
    @{
        Name = 'usb'
        Sources = @('USB_DEVICE/App/usbd_cdc_if.c', 'Tests/usb/test_usbd_cdc_if.c')
        Includes = @('Tests/usb/stubs', 'USB_DEVICE/App')
        Flags = @('-pedantic')
        Libraries = @()
    }
)

Push-Location $projectRoot
try {
    foreach ($case in $cases) {
        $executable = Join-Path $OutputDirectory ($case.Name + '.exe')
        $arguments = @('-std=c99', '-Wall', '-Wextra', '-Werror') + $case.Flags
        foreach ($include in $case.Includes) {
            $arguments += @('-I', $include)
        }
        if ($case.Name -eq 'renderer') {
            $referenceObject = Join-Path $OutputDirectory 'soft3d_linear_reference.o'
            $referenceArguments = $arguments + @(
                '-Dsoft3d_init=reference_soft3d_init',
                '-Dsoft3d_begin_frame=reference_soft3d_begin_frame',
                '-Dsoft3d_submit=reference_soft3d_submit',
                '-Dsoft3d_submit_quaternion=reference_soft3d_submit_quaternion',
                '-Dsoft3d_render_band=reference_soft3d_render_band',
                '-c', 'Tests/reference/soft3d_linear_reference.c', '-o', $referenceObject
            )
            Write-Host 'Building renderer reference'
            & $compilerPath @referenceArguments
            if ($LASTEXITCODE -ne 0) { throw 'Renderer reference compilation failed.' }
            $arguments += @($referenceObject)
        }
        $arguments += $case.Sources + @('-o', $executable) + $case.Libraries
        Write-Host ('Building {0} tests' -f $case.Name)
        & $compilerPath @arguments
        if ($LASTEXITCODE -ne 0) { throw ('{0} test compilation failed.' -f $case.Name) }
        & $executable
        if ($LASTEXITCODE -ne 0) { throw ('{0} tests failed.' -f $case.Name) }
    }
}
finally {
    Pop-Location
}
Write-Host ('All test suites passed. Executables: {0}' -f $OutputDirectory)
