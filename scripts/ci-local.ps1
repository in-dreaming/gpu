# Mirror .github/workflows/ci.yml for native Windows runs.
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
    $BuildDir = Join-Path $Root "build/ci-windows-$Config"
}

Write-Host "=== CI local (Windows): $Config ==="
Write-Host "Build dir: $BuildDir"

function Import-MsvcDevEnvironment {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere not found; install Visual Studio Build Tools with C++ workload."
    }
    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "Visual Studio C++ toolchain not found."
    }
    $vcvars = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found: $vcvars"
    }
    $envDump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $envDump) {
        if ($line -match '^(?<key>[^=]+)=(?<value>.*)$') {
            Set-Item -Path "Env:$($Matches.key)" -Value $Matches.value
        }
    }
}

function Assert-LastExitCode([string]$Step) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE"
    }
}

Import-MsvcDevEnvironment

if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}

cmake -S $Root -B $BuildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    -DGPU_BUILD_TESTS=ON `
    -DGPU_BUILD_EXAMPLES=OFF `
    -DGPU_INSTALL=OFF `
    -DSLANG_RHI_ENABLE_WGPU=OFF `
    -DSLANG_RHI_ENABLE_CUDA=OFF `
    -DSLANG_RHI_ENABLE_D3D11=OFF `
    -DSLANG_RHI_ENABLE_OPTIX=OFF
Assert-LastExitCode 'cmake configure'

cmake --build $BuildDir --parallel
Assert-LastExitCode 'cmake build'

ctest --test-dir $BuildDir --output-on-failure --timeout 300 --parallel 2
Assert-LastExitCode 'ctest'

Write-Host "=== CI local (Windows): $Config PASSED ==="
