# One-shot build of CoD3.exe with the controller fix.
#
# Double-click BUILD-CONTROLLER-FIX.bat beside this file (or run this in
# PowerShell). It checks the tools, fetches XenonRecomp, recompiles the game
# and every level from your own installed copy, builds CoD3.exe, and puts it
# in your CoD3-PC-Port-0.4.0 folder with the old one kept as CoD3.old.exe.
#
# The first run takes a long time (the recompiled code is huge): expect
# anything from 30 minutes to a couple of hours depending on the CPU.

param(
    # Only rebuild CoD3.exe from what is already recompiled (after a change to
    # the host code, not to the game or the tools): minutes, not a quarter hour.
    [switch]$Quick
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Port = Join-Path (Split-Path -Parent (Split-Path -Parent $Root)) 'CoD3-PC-Port-0.4.0'
$Game = Join-Path $Port 'game'

function Step([string]$text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Fail([string]$text) { Write-Host "`nSTOPPED: $text" -ForegroundColor Red; exit 1 }

# --- 1. What has to be installed -------------------------------------------
Step 'Checking the tools'
$missing = @()
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += 'Git            -> winget install Git.Git' }
if (-not (Get-Command python -ErrorAction SilentlyContinue)) { $missing += 'Python 3       -> winget install Python.Python.3.12' }
if (-not (Test-Path 'C:\Program Files\LLVM\bin\clang-cl.exe')) { $missing += 'LLVM / Clang   -> winget install LLVM.LLVM' }
$vsWhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$haveVs = $false
if (Test-Path $vsWhere) {
    $vs = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) { $haveVs = $true }
}
if (-not $haveVs) { $missing += 'VS Build Tools -> winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"' }
if ($missing.Count -gt 0) {
    Write-Host 'Install these first (paste each line after the arrow into PowerShell), then run this again:' -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
    Fail 'tools missing'
}
Write-Host '    all there' -ForegroundColor Green

if (-not (Test-Path (Join-Path $Game 'default.xex'))) { Fail "the installed game was not found at $Game" }

# Without FFmpeg's headers the build still succeeds, but the decoder for the
# game's XMA sound is left out and every level is silent. Refuse instead.
if (-not (Test-Path (Join-Path $Root 'thirdparty\ffmpeg\include\libavcodec\avcodec.h'))) {
    Fail 'thirdparty\ffmpeg\include is missing: without FFmpeg 8.0 headers the in-game sound would be silent'
}

# The version: one higher than the last build's (VERSION.txt in the game
# folder), written where the code reads it; kept in VERSION.txt once built.
$VersionFile = Join-Path $Port 'VERSION.txt'
$Current = if (Test-Path $VersionFile) { (Get-Content $VersionFile -Raw).Trim() } else { '0.5.0' }
$Parts = $Current.Split('.')
$Parts[$Parts.Count - 1] = [string]([int]$Parts[$Parts.Count - 1] + 1)
$Version = $Parts -join '.'
Set-Content -Path (Join-Path $Root 'CoD3Host\version_number.h') -Value "#define COD3_VERSION `"$Version`"" -Encoding ASCII
Write-Host "    building version $Version (the last was $Current)" -ForegroundColor Green

if ($Quick) {
    Step 'Rebuilding CoD3.exe only'
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    $env:PATH = "C:\Program Files\LLVM\bin;$env:PATH"
    $Build = Join-Path $Root 'build'
    # The multiplayer program too, once BUILD-MULTIPLAYER has recompiled its code.
    $Targets = if (Test-Path (Join-Path $Root 'CoD3RecompLib\ppc_mp\ppc_func_mapping.cpp')) { 'CoD3 CoD3MP' } else { 'CoD3' }
    $ErrorActionPreference = 'Continue'
    cmd /c "call `"$vcvars`" >nul 2>&1 && cmake -S `"$Root`" -B `"$Build`" -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev && cmake --build `"$Build`" --target $Targets"
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { Fail 'the quick rebuild failed' }
}
else {

# --- 2. XenonRecomp (the zip download leaves this folder empty) -------------
$Xenon = Join-Path $Root 'tools\XenonRecomp'
if (-not (Test-Path (Join-Path $Xenon 'CMakeLists.txt'))) {
    Step 'Fetching XenonRecomp'
    if (Test-Path $Xenon) { Remove-Item $Xenon -Recurse -Force }
    git clone https://github.com/hedge-dev/XenonRecomp.git $Xenon
    if ($LASTEXITCODE -ne 0) { Fail 'git clone of XenonRecomp failed' }
    git -C $Xenon checkout ddd128bcca99fe8bfbb99bea583c972351fa6ace
    if ($LASTEXITCODE -ne 0) { Fail 'could not check out the pinned XenonRecomp commit' }
    git -C $Xenon submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { Fail 'XenonRecomp submodules failed' }
    git -C $Xenon apply (Join-Path $Root 'tools\patches\XenonRecomp.patch')
    if ($LASTEXITCODE -ne 0) { Fail 'the project patch did not apply to XenonRecomp' }
}

# --- 3. The game's own executable, from your install ------------------------
$Private = Join-Path $Root 'CoD3RecompLib\private'
New-Item -ItemType Directory -Force $Private | Out-Null
Copy-Item (Join-Path $Game 'default.xex') (Join-Path $Private 'default.xex') -Force

# --- 4. Tools and the game's code ------------------------------------------
# A configure that failed half way leaves its compiler choice cached; start
# that folder over if the recompiler was never built.
$ToolBuild = Join-Path $Root 'tools\build'
if ((Test-Path $ToolBuild) -and -not (Test-Path (Join-Path $ToolBuild 'XenonRecomp\XenonRecomp.exe'))) {
    Remove-Item $ToolBuild -Recurse -Force
}
Step 'Building the recompiler and recompiling the game'
& (Join-Path $Root 'scripts\recompile.ps1') -BuildTools

$ScanExe = Join-Path $Root 'tools\build-scan\CoD3Scan.exe'
if (-not (Test-Path $ScanExe)) {
    Step 'Building CoD3Scan'
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    $env:PATH = "C:\Program Files\LLVM\bin;$env:PATH"
    # CMake prints deprecation warnings on stderr; the exit code is what counts.
    $ErrorActionPreference = 'Continue'
    cmd /c "call `"$vcvars`" >nul 2>&1 && cmake -S `"$Root\tools\CoD3Scan`" -B `"$Root\tools\build-scan`" -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev && cmake --build `"$Root\tools\build-scan`""
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { Fail 'CoD3Scan did not build' }
}

Step 'Recompiling every level'
& (Join-Path $Root 'scripts\recompile_level.ps1') -All -GameRoot $Game

# --- 5. CoD3.exe ------------------------------------------------------------
Step 'Building CoD3.exe'
& (Join-Path $Root 'scripts\recompile.ps1') -BuildHost
}

$Exe = Join-Path $Root 'build\CoD3Host\CoD3.exe'
if (-not (Test-Path $Exe)) { Fail "the build finished but $Exe is not there" }

Step 'Putting it in your game folder'
$Target = Join-Path $Port 'CoD3.exe'
if ((Test-Path $Target) -and -not (Test-Path (Join-Path $Port 'CoD3.old.exe'))) {
    Copy-Item $Target (Join-Path $Port 'CoD3.old.exe')
}
Copy-Item $Exe $Target -Force
$Pdb = Join-Path $Root 'build\CoD3Host\CoD3.pdb'
if (Test-Path $Pdb) { Copy-Item $Pdb (Join-Path $Port 'CoD3.pdb') -Force }
$MpExe = Join-Path $Root 'build\CoD3Host\CoD3MP.exe'
if (Test-Path $MpExe) {
    Copy-Item $MpExe (Join-Path $Port 'CoD3MP.exe') -Force
    $MpPdb = Join-Path $Root 'build\CoD3Host\CoD3MP.pdb'
    if (Test-Path $MpPdb) { Copy-Item $MpPdb (Join-Path $Port 'CoD3MP.pdb') -Force }
}

Set-Content -Path $VersionFile -Value $Version -Encoding ASCII
Write-Host "`nVersion $Version is in your game folder." -ForegroundColor Green
Write-Host "`nDone. Start $Target, press F11, Controls tab: it says whether your controller is seen." -ForegroundColor Green
