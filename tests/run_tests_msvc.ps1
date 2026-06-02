<#
.SYNOPSIS
    Build and run the headless Terrax unit tests with MSVC (Windows).

.DESCRIPTION
    The Makefile's `make test` target is the canonical headless test runner,
    but it needs g++/make (Linux). This script is the Windows/MSVC equivalent:
    it imports the Visual Studio build environment, compiles the same source
    set (engine files under test + the GL stub + the test cases) with cl.exe,
    and runs the resulting binary. It keeps the source list in sync with the
    Makefile's TEST_SRCS.

    glm headers are located automatically (vcpkg manifest dir, then a classic
    vcpkg install). Override with -GlmInclude if they live elsewhere.

.EXAMPLE
    pwsh tests/run_tests_msvc.ps1
#>
param(
    [string]$GlmInclude = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

# --- Locate Visual Studio and import the x64 dev environment ---------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio with the 'Desktop development with C++' workload."
}
$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsPath) { throw "No Visual Studio install with the MSVC C++ toolset was found." }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

cmd /c "`"$vcvars`" >NUL 2>&1 && set" | ForEach-Object {
    if ($_ -match '^(.*?)=(.*)$') {
        try { Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction Stop } catch {}
    }
}

# --- Find glm headers ------------------------------------------------------
if (-not $GlmInclude) {
    $candidates = @( Join-Path $root 'vcpkg_installed\x64-windows\include' )
    foreach ($vr in @($env:VCPKG_ROOT, $env:VCPKG_INSTALLATION_ROOT)) {
        if ($vr) { $candidates += (Join-Path $vr 'installed\x64-windows\include') }
    }
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c 'glm\glm.hpp')) { $GlmInclude = $c; break }
    }
}
if (-not $GlmInclude -or -not (Test-Path (Join-Path $GlmInclude 'glm\glm.hpp'))) {
    throw "glm headers not found. Restore vcpkg (build Terrax.sln once), run 'vcpkg install glm:x64-windows', or pass -GlmInclude <dir>."
}
Write-Host "Using glm from: $GlmInclude"

# --- Source set (keep in sync with Makefile TEST_SRCS) ---------------------
$engine = @(
    'src\voxel_model.cpp','src\voxel_rig.cpp','src\voxel_house.cpp',
    'src\building.cpp','src\building_house.cpp',
    'src\building_special.cpp','src\world.cpp','src\world_gen.cpp','src\town.cpp',
    'src\town_stamp.cpp','src\town_roads.cpp','src\town_layout.cpp',
    'src\town_buildings.cpp','src\town_terrain.cpp',
    'src\vegetation.cpp','src\atlas.cpp','src\camera.cpp','src\physics.cpp'
)
$cases = @(
    'tests\test_main.cpp','tests\test_voxel_model.cpp','tests\test_noise.cpp',
    'tests\test_camera.cpp','tests\test_world.cpp','tests\test_physics.cpp',
    'tests\test_building.cpp','tests\test_atlas.cpp'
)
$srcs = $engine + @('tests\gl_stub.cpp') + $cases

$outDir = Join-Path $root 'build\tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'terrax_tests.exe'

Push-Location $root
try {
    Write-Host "Compiling $($srcs.Count) translation units with MSVC..."
    & cl /nologo /std:c++17 /EHsc /W3 /DTERRAX_TESTING /DNOMINMAX /D_USE_MATH_DEFINES `
        /I include /I src /I tests /I "$GlmInclude" `
        $srcs "/Fo:$outDir\\" "/Fe:$exe"
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed (exit $LASTEXITCODE)." }

    Write-Host "`nRunning $exe`n"
    & $exe
    $code = $LASTEXITCODE
    Write-Host "`n=== terrax_tests exit code: $code ==="
    exit $code
}
finally { Pop-Location }
