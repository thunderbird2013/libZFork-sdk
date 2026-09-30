#!/usr/bin/env pwsh
# ── libZFork Build Script ──────────────────────────────────────────────────────
#
# Beispiele:
#   .\build.ps1                                    → Release, DLL, Examples, dist\
#   .\build.ps1 -Config Debug                      → Debug-Build
#   .\build.ps1 -Link Static                       → nur static .lib (kein DLL-Build)
#   .\build.ps1 -Link Both                         → DLL + static .lib
#   .\build.ps1 -NoExamples                        → ohne Beispiel-Quellcode in dist\
#   .\build.ps1 -SyncDistOnly                      → nur dist\ aus build\ kopieren
#   .\build.ps1 -NoCopyDist                        → bauen ohne dist-Kopie
#   .\build.ps1 -Setup                             → Both/Both bauen + Inno-Setup
#   .\build.ps1 -SetupOnly                         → Setup aus vorhandenen Builds
#
# Setup (Inno Setup):
#   Enthält Header, Bindings, DLL+Static (Release+Debug), Example-Quellcode — kein libZFork-Source.
#   Ausgabe: installer\output\libZFork-<version>-<arch>-sdk.exe
# Parameter:
#   -Config  Release | Debug | Both     (default: Release)
#   -Link    Dll    | Static | Both      (default: Dll)
#            Dll    = self-contained libZFork.dll (x64-windows-static-md)
#            Static = nur libZFork_static.lib (x64-windows-static)
#            Both   = beide Varianten
#   -NoExamples                        Examples nicht bauen; kein Quellcode in dist\
#   -Tests                             Catch2-Tests mitbauen
#   -Apps                              zfork_console mitbauen
#   -CopyDist / -NoCopyDist            nach Build nach dist\ kopieren (default: an)
#   -SyncDistOnly                      nur dist\ aktualisieren, nicht neu bauen
#   -Setup                             SDK-Installer mit Inno Setup erzeugen
#   -SetupOnly                         nur Staging + Installer (ohne Build)
#   -InnoSetupPath <pfad>              Pfad zu ISCC.exe (optional)
#   -Arch x64|x86   -Ninja   -VcpkgRoot <pfad>
#
# Legacy (weiterhin unterstützt):
#   -Mode Dynamic | StaticDeps | All   überschreibt -Link wenn gesetzt
param(
    [ValidateSet("Release","Debug","Both")]
    [string]$Config = "Release",

    [ValidateSet("Dll","Static","Both")]
    [string]$Link = "Dll",

    [switch]$NoExamples,
    [switch]$Tests,
    [switch]$Apps,
    [switch]$NoCopyDist,
    [switch]$SyncDistOnly,
    [switch]$Setup,
    [switch]$SetupOnly,
    [string]$InnoSetupPath = "",
    [string]$LogoPath = "",

    [string]$Arch = "x64",
    # Default: $env:VCPKG_ROOT, sonst E:\vcpkg (lokale Maschine), sonst Fehler beim Build
    [string]$VcpkgRoot = "",
    [switch]$Ninja,

    # Legacy
    [ValidateSet("Dynamic","StaticDeps","All")]
    [string]$Mode
)

$ErrorActionPreference = "Stop"

if (-not $VcpkgRoot) {
    if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
        $VcpkgRoot = $env:VCPKG_ROOT
    } elseif (Test-Path "E:\vcpkg") {
        $VcpkgRoot = "E:\vcpkg"
    } elseif (Test-Path (Join-Path $env:USERPROFILE "vcpkg")) {
        $VcpkgRoot = Join-Path $env:USERPROFILE "vcpkg"
    } else {
        throw "vcpkg nicht gefunden. -VcpkgRoot <pfad> setzen oder VCPKG_ROOT Environment Variable."
    }
}

# Legacy -Mode → neues -Link Mapping
$UseDynamicTriplet = $false
$BuildAllModes     = $false
if ($Mode) {
    switch ($Mode) {
        "Dynamic"    { $Link = "Dll"; $UseDynamicTriplet = $true }
        "StaticDeps" { $Link = "Both" }
        "All"        { $BuildAllModes = $true }
    }
}

$BuildExamples = if ($NoExamples) { "OFF" } else { "ON" }
$BuildTests    = if ($Tests)      { "ON" } else { "OFF" }
$BuildApps     = if ($Apps)       { "ON" } else { "OFF" }
$CopyDist      = -not $NoCopyDist
$RunSetup      = $Setup -or $SetupOnly

if ($Setup -and -not $SetupOnly) {
    $Config      = "Both"
    $Link        = "Both"
    $NoExamples  = $false
    $CopyDist    = $true
}

# ── Locate VS BuildTools ───────────────────────────────────────────────────────
$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) { throw "vswhere.exe not found - install VS BuildTools 2022" }
$VsRoot = & $VsWhere -products * -version "[17,18)" -latest -property installationPath
if (-not $VsRoot) { throw "VS 2022 / BuildTools 2022 not found" }
$VcVarsAll = "$VsRoot\VC\Auxiliary\Build\vcvarsall.bat"
$VcArch    = if ($Arch -eq "x64") { "x64" } else { "x86" }
$Toolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"

Write-Host "=== libZFork Build ===" -ForegroundColor Cyan
Write-Host "VS / BuildTools : $VsRoot"
Write-Host "Config / Arch   : $Config / $Arch"
Write-Host "Link            : $Link$(if ($UseDynamicTriplet) { ' (dynamic triplet)' })"
Write-Host "Examples/Tests  : $BuildExamples / $BuildTests"
Write-Host "Apps / CopyDist : $BuildApps / $CopyDist"
if ($RunSetup) { Write-Host "Setup (Inno)    : $(if ($SetupOnly) { 'only' } else { 'build+pack' })" }
Write-Host "vcpkg root      : $VcpkgRoot"
if ($Mode) { Write-Host "Mode (legacy)   : $Mode" -ForegroundColor DarkGray }
Write-Host ""

# ────────────────────────────────────────────────────────────────────────────
# Helper: configure + build one variant
# ────────────────────────────────────────────────────────────────────────────
function Build-Variant {
    param(
        [string]$Triplet,
        [string]$BuildDir,
        [string]$DllShared,
        [string]$DllStatic,
        [string]$BuildConfig = $Config
    )

    Write-Host ""
    Write-Host "── Variant: $Triplet ──────────────────────────────" -ForegroundColor Cyan
    Write-Host "   BuildDir : $BuildDir"
    Write-Host "   DLL      : $DllShared   Static: $DllStatic"
    Write-Host ""

    # ── vcpkg install ────────────────────────────────────────────────────────
    Write-Host "[1] Installing vcpkg packages ($Triplet)..." -ForegroundColor Yellow
    & "$VcpkgRoot\vcpkg.exe" install --triplet $Triplet
    if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed for $Triplet" }

    # ── CMake configure ──────────────────────────────────────────────────────
    Write-Host "[2] CMake configure..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

    $commonArgs = @(
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DVCPKG_TARGET_TRIPLET=$Triplet",
        "-DCMAKE_BUILD_TYPE=$BuildConfig",
        "-DLIBZFORK_BUILD_SHARED=$DllShared",
        "-DLIBZFORK_BUILD_STATIC=$DllStatic",
        "-DLIBZFORK_BUILD_EXAMPLES=$BuildExamples",
        "-DLIBZFORK_BUILD_TESTS=$BuildTests",
        "-DLIBZFORK_BUILD_APPS=$BuildApps"
    )

    # cmake schreibt Warnungen auf stderr; PowerShell 5.1 würde das mit
    # $ErrorActionPreference=Stop als Fehler werten → try/catch + LASTEXITCODE
    if ($Ninja) {
        $argsStr = ("-B $BuildDir -G Ninja " + ($commonArgs -join " "))
        cmd /c "`"$VcVarsAll`" $VcArch && cmake $argsStr"
    } else {
        $allArgs = @("-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", $Arch) + $commonArgs
        try { & cmake @allArgs } catch {}
    }
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($Triplet)" }

    # ── CMake build ──────────────────────────────────────────────────────────
    Write-Host "[3] Building $BuildConfig..." -ForegroundColor Yellow
    if ($Ninja) {
        cmd /c "`"$VcVarsAll`" $VcArch && cmake --build $BuildDir --parallel"
    } else {
        try { cmake --build $BuildDir --config $BuildConfig --parallel } catch {}
    }
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($Triplet)" }

    # ── Summary ──────────────────────────────────────────────────────────────
    $outDir = if ($Ninja) { $BuildDir } else { "$BuildDir\$BuildConfig" }
    $exDir  = if ($Ninja) { $BuildDir } else { "$BuildDir\examples\$BuildConfig" }

    Write-Host ""
    Write-Host "Output → $outDir" -ForegroundColor Green
    # DLL-Name hat in Debug-Builds den Postfix "d" (CMakeLists DEBUG_POSTFIX "d")
    foreach ($f in @("libZFork.dll","libZForkd.dll",
                     "libZFork.lib","libZForkd.lib",
                     "libZFork_static.lib","libZFork_staticd.lib")) {
        $p = "$outDir\$f"
        if (Test-Path $p) {
            $kb = [math]::Round((Get-Item $p).Length/1KB,0)
            Write-Host ("  {0,-32} {1,7} KB" -f $f, $kb) -ForegroundColor Green
        }
    }
    # EXE-Übersicht: foo.exe (DLL) + foo_static.exe — kein _dll-Suffix mehr
    foreach ($exe in (Get-ChildItem -Path $exDir -Filter "*.exe" -ErrorAction SilentlyContinue |
                      Sort-Object Name)) {
        $kb = [math]::Round($exe.Length/1KB, 0)
        Write-Host ("  {0,-38} {1,6} KB" -f $exe.Name, $kb) -ForegroundColor Green
    }
}

# ────────────────────────────────────────────────────────────────────────────
# Helper: Sprach-Bindings (C#, Pascal, PureBasic) kopieren
# ────────────────────────────────────────────────────────────────────────────
function Copy-BindingsTo {
    param([string]$DestRoot)

    $src = Join-Path $PSScriptRoot "bindings"
    if (-not (Test-Path $src)) {
        Write-Host "  Warnung  bindings\ nicht gefunden" -ForegroundColor DarkYellow
        return 0
    }

    $dest = Join-Path $DestRoot "bindings"
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Copy-Item -Path $src -Destination $dest -Recurse -Force

    $files = Get-ChildItem -Path $dest -Recurse -File
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($dest.Length + 1)
        Write-Host "  Kopiert  bindings\$rel" -ForegroundColor Green
    }
    return $files.Count
}

function Copy-LicensesTo {
    param([string]$DestRoot)

    # Weitergabe-Ordner: GPLv2 + Drittanbieter-Volltexte (ohne _unused/)
    $src = Join-Path $PSScriptRoot "license"
    if (-not (Test-Path $src)) {
        Write-Host "  Warnung  license\ nicht gefunden" -ForegroundColor DarkYellow
        return 0
    }

    $dest = Join-Path $DestRoot "license"
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    $null = New-Item -ItemType Directory -Force -Path $dest | Out-Null

    $n = 0
    Get-ChildItem $src -File | ForEach-Object {
        Copy-Item $_.FullName $dest -Force
        $n++
    }
    Write-Host "  Kopiert  license\  ($n Dateien)" -ForegroundColor Green
    return $n
}

function Copy-HelpDocsTo {
    param([string]$DestRoot)

    $src = Join-Path $PSScriptRoot "docs\help"
    if (-not (Test-Path $src)) {
        Write-Host "  Warnung  docs\help\ nicht gefunden" -ForegroundColor DarkYellow
        return 0
    }

    # CHM (C++ DLL-Hilfe) erzeugen, falls Generator vorhanden
    $chmScript = Join-Path $PSScriptRoot "build-chm.ps1"
    $chmFile   = Join-Path $src "chm\libZFork.chm"
    if ((Test-Path $chmScript) -and -not (Test-Path $chmFile)) {
        Write-Host "  Erzeuge libZFork.chm ..." -ForegroundColor DarkGray
        & $chmScript
    }

    if (-not (Test-Path $chmFile)) {
        Write-Host "  Warnung  libZFork.chm nicht gefunden" -ForegroundColor DarkYellow
        return 0
    }

    # Dist/Setup: nur die fertige CHM, kein HTML/CSS/Quellen-Baum
    $dest = Join-Path $DestRoot "help"
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item -Path $chmFile -Destination (Join-Path $dest "libZFork.chm") -Force

    Write-Host "  Kopiert  help\libZFork.chm" -ForegroundColor Green
    return 1
}

# ────────────────────────────────────────────────────────────────────────────
# Helper: kopiert Bibliotheken, Header und Example-Quellcode in dist\
# ────────────────────────────────────────────────────────────────────────────
function Publish-Dist {
    param(
        [string]$LibDir,      # Ordner mit .dll / .lib Dateien
        [string]$ExDir,       # nicht leer = Beispiel-Quellcode nach dist\examples kopieren
        [string]$SubDir = "", # Unterordner in dist\ (z.B. "Release" oder "Debug")
        [switch]$Clean        # dist\SubDir\ vorher leeren (nur beim ersten Aufruf)
    )

    $distRoot = Join-Path $PSScriptRoot "dist"
    $dist     = if ($SubDir) { Join-Path $distRoot $SubDir } else { $distRoot }

    if ($Clean -and (Test-Path $dist)) {
        if ($SubDir) {
            Write-Host "  Leere dist\$SubDir\ ..." -ForegroundColor DarkGray
            Remove-Item "$dist\*" -Recurse -Force -ErrorAction SilentlyContinue
        } else {
            # Nie den ganzen dist\-Root leeren (sonst weg: win-x64, linux-*, source, …).
            Write-Host "  Entferne Root-Libs in dist\ ..." -ForegroundColor DarkGray
            foreach ($f in @(
                "libZFork.dll","libZFork.lib","libZFork_static.lib",
                "libZForkd.dll","libZForkd.lib","libZFork_staticd.lib"
            )) {
                $p = Join-Path $dist $f
                if (Test-Path $p) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
            }
        }
    }

    $null = New-Item -ItemType Directory -Force -Path "$dist\include\libzfork"
    $null = New-Item -ItemType Directory -Force -Path "$dist\examples"

    # ── Bibliotheken ──────────────────────────────────────────────────────────
    # DLL-Name hat in Debug-Builds den Postfix "d" (CMakeLists DEBUG_POSTFIX "d").
    # Wir suchen beide Varianten; wer zuerst gefunden wird, gewinnt.
    $dllFound = $null
    foreach ($candidate in @("libZFork.dll", "libZForkd.dll")) {
        $src = Join-Path $LibDir $candidate
        if (Test-Path $src) { $dllFound = $src; break }
    }
    if ($dllFound) {
        $fname = [IO.Path]::GetFileName($dllFound)
        Copy-Item -Path $dllFound -Destination $dist -Force
        $kb = [math]::Round((Get-Item $dllFound).Length / 1KB, 0)
        Write-Host ("  Kopiert  {0,-32} {1,6} KB" -f $fname, $kb) -ForegroundColor Green
    }

    # Import-Lib + Static-Lib
    foreach ($candidate in @("libZFork.lib","libZForkd.lib",
                              "libZFork_static.lib","libZFork_staticd.lib")) {
        $src = Join-Path $LibDir $candidate
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination $dist -Force
            $kb = [math]::Round((Get-Item $src).Length / 1KB, 0)
            Write-Host ("  Kopiert  {0,-32} {1,6} KB" -f $candidate, $kb) -ForegroundColor Green
        }
    }

    # Öffentliche Header (libzfork.h, libzfork2.h, …)
    $hdrDir = Join-Path $PSScriptRoot "include\libzfork"
    if (Test-Path $hdrDir) {
        foreach ($hdr in (Get-ChildItem -Path $hdrDir -Filter "*.h")) {
            Copy-Item -Path $hdr.FullName -Destination "$dist\include\libzfork\" -Force
            Write-Host "  Kopiert  include\libzfork\$($hdr.Name)" -ForegroundColor Green
        }
    }

    # Shared Meta immer in dist\-Root (nicht in win-x64\Release\ o.ae.)
    Copy-BindingsTo -DestRoot $distRoot | Out-Null
    Copy-HelpDocsTo -DestRoot $distRoot | Out-Null
    Copy-LicensesTo -DestRoot $distRoot | Out-Null

    $dllReadme = Join-Path $PSScriptRoot "installer\DLL_README.txt"
    if (Test-Path $dllReadme) {
        Copy-Item -Path $dllReadme -Destination $distRoot -Force
    }

    # Beispiel-Quellcode (.cpp, CMakeLists, samples)
    if ($ExDir) {
        $exInfo = Copy-ExampleSourcesTo -DestRoot $dist
        foreach ($cpp in (Get-ChildItem -Path (Join-Path $dist "examples") -Filter "*.cpp" | Sort-Object Name)) {
            $kb = [math]::Round($cpp.Length / 1KB, 0)
            Write-Host ("  Kopiert  examples\{0,-30} {1,6} KB" -f $cpp.Name, $kb) -ForegroundColor Green
        }
        $assetCount = $exInfo.FileCount - $exInfo.CppCount
        if ($assetCount -gt 0) {
            Write-Host ("  Kopiert  examples\samples\ + Assets     {0,6} Dateien" -f $assetCount) -ForegroundColor Green
        }
    }

    Write-Host ""
    $label = if ($SubDir) { "dist\$SubDir\" } else { "dist\" }
    Write-Host "  $label → $dist" -ForegroundColor Cyan
}

function Get-OutputPaths {
    param(
        [string]$BuildDir,
        [string]$Cfg
    )
    $outDir = if ($Ninja) { $BuildDir } else { "$BuildDir\$Cfg" }
    $exDir  = if ($NoExamples) { "" }
             elseif ($Ninja)    { $BuildDir }
             else               { "$BuildDir\examples\$Cfg" }
    return @{ Out = $outDir; Ex = $exDir }
}

function Get-BuildPlans {
    param([string]$Cfg)

    $tripletDyn = if ($Arch -eq "x64") { "x64-windows"           } else { "x86-windows"           }
    $tripletDll = if ($Arch -eq "x64") { "x64-windows-static-md" } else { "x86-windows-static-md" }
    $tripletLib = if ($Arch -eq "x64") { "x64-windows-static"    } else { "x86-windows-static"    }

    $plans = @()
    if ($UseDynamicTriplet) {
        $plans += @{
            Triplet   = $tripletDyn
            BuildDir  = "build\$Arch-$Cfg-dynamic"
            DllShared = "ON"
            DllStatic = "ON"
        }
        return $plans
    }

    switch ($Link) {
        "Dll" {
            $plans += @{
                Triplet   = $tripletDll
                BuildDir  = "build\$Arch-$Cfg-dll-static"
                DllShared = "ON"
                DllStatic = "ON"
            }
        }
        "Static" {
            $plans += @{
                Triplet   = $tripletLib
                BuildDir  = "build\$Arch-$Cfg-lib-static"
                DllShared = "OFF"
                DllStatic = "ON"
            }
        }
        "Both" {
            $plans += @{
                Triplet   = $tripletDll
                BuildDir  = "build\$Arch-$Cfg-dll-static"
                DllShared = "ON"
                DllStatic = "ON"
            }
            $plans += @{
                Triplet   = $tripletLib
                BuildDir  = "build\$Arch-$Cfg-lib-static"
                DllShared = "OFF"
                DllStatic = "ON"
            }
        }
    }
    return $plans
}

function Publish-ConfigToDist {
    param(
        [string]$Cfg,
        [switch]$Clean
    )

    if (-not $CopyDist) { return }

    $sub   = if ($Config -eq "Both") { $Cfg } else { "" }
    $plans = Get-BuildPlans -Cfg $Cfg
    $first = $Clean

    foreach ($plan in $plans) {
        $paths = Get-OutputPaths -BuildDir $plan.BuildDir -Cfg $Cfg
        $exDir = if ($NoExamples) { "" } else { $paths.Ex }

        Write-Host ""
        Write-Host "── dist\$sub ($($plan.BuildDir)) ──────────────" -ForegroundColor Cyan
        if ($first) {
            Publish-Dist -LibDir $paths.Out -ExDir $exDir -SubDir $sub -Clean
            $first = $false
        } else {
            Publish-Dist -LibDir $paths.Out -ExDir $exDir -SubDir $sub
        }
    }
}

function Get-ProjectVersion {
    $verFile = Join-Path $PSScriptRoot "VERSION"
    if (Test-Path $verFile) {
        $v = (Get-Content -Path $verFile -Raw -ErrorAction SilentlyContinue).Trim()
        if ($v -match '^\d+\.\d+\.\d+$') { return $v }
    }
    $cmakeFile = Join-Path $PSScriptRoot "CMakeLists.txt"
    $content   = Get-Content -Path $cmakeFile -Raw -ErrorAction SilentlyContinue
    if ($content -match 'VERSION\s+(\d+\.\d+\.\d+)') {
        return $Matches[1]
    }
    return "1.0.0"
}

function Write-Utf8NoBom([string]$Path, [string]$Content) {
    $enc = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $Content, $enc)
}

function Sync-VersionArtifacts {
    # Propagate VERSION → header, RC include, vcpkg.json, Lazarus, installer default.
    $ver = Get-ProjectVersion
    if ($ver -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
        throw "Ungueltige VERSION: '$ver' (erwartet MAJOR.MINOR.PATCH)"
    }
    $maj = $Matches[1]; $min = $Matches[2]; $pat = $Matches[3]
    Write-Host "Version sync → $ver" -ForegroundColor Cyan

    $hdr = @"
/* Auto-generated from VERSION — do not edit by hand.
 * Source of truth: <repo>/VERSION
 * Regenerated by CMake and build.ps1 Sync-VersionArtifacts.
 */
#pragma once

#define LIBZFORK_VERSION_MAJOR $maj
#define LIBZFORK_VERSION_MINOR $min
#define LIBZFORK_VERSION_PATCH $pat
#define LIBZFORK_VERSION_STRING "$ver"
"@
    $hdrPath = Join-Path $PSScriptRoot "include\libzfork\libzfork_version.h"
    Write-Utf8NoBom $hdrPath $hdr

    $rcH = @"
/* Auto-generated from VERSION — included by src/libzfork.rc */
#ifndef LIBZFORK_FILEVERSION
#  define LIBZFORK_FILEVERSION $maj,$min,$pat,0
#endif
#ifndef LIBZFORK_FILEVERSION_STR
#  define LIBZFORK_FILEVERSION_STR "$ver\0"
#endif
"@
    Write-Utf8NoBom (Join-Path $PSScriptRoot "src\libzfork_version.rc.h") $rcH

    $vcpkg = Join-Path $PSScriptRoot "vcpkg.json"
    if (Test-Path $vcpkg) {
        $json = Get-Content $vcpkg -Raw
        $json2 = [regex]::Replace($json, '("version"\s*:\s*")[^"]+(")', "`${1}$ver`${2}")
        if ($json2 -ne $json) {
            Write-Utf8NoBom $vcpkg $json2
            Write-Host "  vcpkg.json → $ver" -ForegroundColor Green
        }
    }

    $lic = Join-Path $PSScriptRoot "Lazarus\ZForkLicenses.pas"
    if (Test-Path $lic) {
        $txt = Get-Content $lic -Raw
        if ($txt -match "ZFORK_PRODUCT_VERSION") {
            $txt2 = [regex]::Replace($txt, "ZFORK_PRODUCT_VERSION\s*=\s*'[^']*'", "ZFORK_PRODUCT_VERSION = '$ver'")
        } else {
            $txt2 = [regex]::Replace($txt,
                "(ZFORK_PRODUCT_NAME\s*=\s*'[^']*';)",
                "`$1`r`n  ZFORK_PRODUCT_VERSION  = '$ver';")
        }
        if ($txt2 -ne $txt) {
            Write-Utf8NoBom $lic $txt2
            Write-Host "  ZForkLicenses.pas → $ver" -ForegroundColor Green
        }
    }

    $lpk = Join-Path $PSScriptRoot "Lazarus\lazlibzfork.lpk"
    if (Test-Path $lpk) {
        $lpkTxt = Get-Content $lpk -Raw
        $lpkNew = [regex]::Replace($lpkTxt,
            '<Version\s+Major="\d+"(?:\s+Minor="\d+")?(?:\s+Release="\d+")?(?:\s+Build="\d+")?\s*/>',
            "<Version Major=`"$maj`" Minor=`"$min`" Release=`"$pat`" Build=`"0`"/>")
        if ($lpkNew -ne $lpkTxt) {
            Write-Utf8NoBom $lpk $lpkNew
            Write-Host "  lazlibzfork.lpk → $maj.$min.$pat" -ForegroundColor Green
        }
    }

    $iss = Join-Path $PSScriptRoot "installer\libZFork.iss"
    if (Test-Path $iss) {
        $issTxt = Get-Content $iss -Raw
        $issNew = [regex]::Replace($issTxt,
            '(#define MyAppVersion ")[^"]+(")',
            "`${1}$ver`${2}")
        if ($issNew -ne $issTxt) {
            Write-Utf8NoBom $iss $issNew
            Write-Host "  libZFork.iss default → $ver" -ForegroundColor Green
        }
    }

    return $ver
}

function Find-InnoSetupCompiler {
    if ($InnoSetupPath -and (Test-Path $InnoSetupPath)) { return $InnoSetupPath }

    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    throw "Inno Setup Compiler (ISCC.exe) nicht gefunden. Installiere Inno Setup 6 oder -InnoSetupPath angeben."
}

function Copy-SetupFiles {
    param(
        [string[]]$Patterns,
        [string]$SourceDir,
        [string]$DestDir
    )
    if (-not (Test-Path $SourceDir)) { return @() }
    $null = New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
    $copied = @()
    foreach ($pat in $Patterns) {
        foreach ($f in (Get-ChildItem -Path $SourceDir -Filter $pat -ErrorAction SilentlyContinue)) {
            Copy-Item -Path $f.FullName -Destination $DestDir -Force
            $copied += $f.Name
        }
    }
    return $copied
}

function Copy-ExampleSourcesTo {
    param([string]$DestRoot)

    $exSrc  = Join-Path $PSScriptRoot "examples"
    $exDest = Join-Path $DestRoot "examples"
    if (-not (Test-Path $exSrc)) { throw "Examples nicht gefunden: $exSrc" }

    $null = New-Item -ItemType Directory -Force -Path $exDest | Out-Null

    foreach ($cpp in (Get-ChildItem -Path $exSrc -Filter "*.cpp")) {
        Copy-Item -Path $cpp.FullName -Destination $exDest -Force
    }

    $cmakeLists = Join-Path $exSrc "CMakeLists.txt"
    if (Test-Path $cmakeLists) {
        Copy-Item -Path $cmakeLists -Destination $exDest -Force
    }

    $samplesDir = Join-Path $exSrc "samples"
    if (Test-Path $samplesDir) {
        Copy-Item -Path $samplesDir -Destination $exDest -Recurse -Force
    }

    $demoBank = Join-Path $exSrc "demo.bank"
    if (Test-Path $demoBank) {
        Copy-Item -Path $demoBank -Destination $exDest -Force
    }

    return @{
        CppCount  = (Get-ChildItem $exDest -Filter "*.cpp").Count
        FileCount = (Get-ChildItem $exDest -Recurse -File).Count
    }
}

function Copy-LibrarySourceTo {
    param([string]$DestRoot)

    # Corresponding source for GPLv2/LGPL binary distribution (with the DLL).
    $dest = Join-Path $DestRoot "source"
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    $null = New-Item -ItemType Directory -Force -Path $dest | Out-Null

    $copyTree = {
        param([string]$Rel)
        $from = Join-Path $PSScriptRoot $Rel
        $to   = Join-Path $dest $Rel
        if (-not (Test-Path $from)) { return 0 }
        $parent = Split-Path $to -Parent
        if ($parent -and -not (Test-Path $parent)) {
            $null = New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        Copy-Item -Path $from -Destination $to -Recurse -Force
        return (Get-ChildItem $to -Recurse -File -EA SilentlyContinue).Count
    }

    $n = 0
    foreach ($f in @("CMakeLists.txt", "vcpkg.json", "build.ps1", "packagefiles.xml", "VERSION")) {
        $from = Join-Path $PSScriptRoot $f
        if (Test-Path $from) {
            Copy-Item $from $dest -Force
            $n++
        }
    }
    foreach ($dir in @("include", "src", "cmake", "triplets", "overlays", "examples", "tests", "scripts")) {
        $n += & $copyTree $dir
    }

    @"
libZFork — Corresponding Source (GPLv2)
=======================================

Copyright (C) 2026 Matthias Stoltze — https://solid-software.net

This folder is the complete corresponding source for the binaries shipped
in this SDK (libZFork.dll / .so / static libs), including embedded codec
integration code under src/.

Build (Windows, overview):
  .\build.ps1 -Config Release -Link Both -NoExamples -NoCopyDist

Build (Linux via WSL):
  bash scripts/wsl_build_variant.sh x64 Release
  bash scripts/wsl_build_variant.sh arm64 Release

License texts: see ../license/ (GPL.TXT, LGPL.TXT, …).
"@ | Set-Content (Join-Path $dest "BUILD.txt") -Encoding UTF8
    $n++

    Write-Host "  Kopiert  source\  ($n Dateien/Eintraege, Corresponding Source)" -ForegroundColor Green
    return (Get-ChildItem $dest -Recurse -File -EA SilentlyContinue).Count
}

function Stage-SetupDist {
    $setupRoot = Join-Path $PSScriptRoot "dist-setup"
    $distRoot  = Join-Path $PSScriptRoot "dist"
    $archLabel = "win-x64"   # Ordnerlayout wie dist\

    if (Test-Path $setupRoot) {
        Remove-Item $setupRoot -Recurse -Force
    }
    $null = New-Item -ItemType Directory -Force -Path $setupRoot | Out-Null

    # --- Header ---
    $hdrDest = Join-Path $setupRoot "include\libzfork"
    $null = New-Item -ItemType Directory -Force -Path $hdrDest | Out-Null
    $hdrSrc = Join-Path $PSScriptRoot "include\libzfork"
    if (-not (Test-Path $hdrSrc)) { throw "Header nicht gefunden: $hdrSrc" }
    Copy-Item -Path "$hdrSrc\*.h" -Destination $hdrDest -Force

    # --- Bindings / Help / Lizenzen (Weitergabe-Ordner) ---
    $bindCount = Copy-BindingsTo -DestRoot $setupRoot
    $helpCount = Copy-HelpDocsTo -DestRoot $setupRoot
    $licCount  = Copy-LicensesTo -DestRoot $setupRoot
    $licDest   = Join-Path $setupRoot "license"

    # --- Docs + License agreement ---
    Copy-Item -Path (Join-Path $PSScriptRoot "installer\SDK_README.txt") `
              -Destination (Join-Path $setupRoot "SDK_README.txt") -Force
    Copy-Item -Path (Join-Path $PSScriptRoot "installer\DLL_README.txt") `
              -Destination (Join-Path $setupRoot "DLL_README.txt") -Force
    Copy-Item -Path (Join-Path $PSScriptRoot "installer\LICENSE.txt") `
              -Destination (Join-Path $setupRoot "LICENSE.txt") -Force

    # --- Windows libs (Build bevorzugt, sonst dist\) ---
    $libSpec = @{
        Release = @{
            DllPatterns    = @("libZFork.dll", "libZFork.lib")
            StaticPatterns = @("libZFork_static.lib")
            Missing        = @("libZFork.dll", "libZFork.lib", "libZFork_static.lib")
            DistSub        = "win-x64\Release"
            BuildDll       = "build\$Arch-Release-dll-static\Release"
            BuildLib       = "build\$Arch-Release-lib-static\Release"
        }
        Debug = @{
            DllPatterns    = @("libZForkd.dll", "libZForkd.lib")
            StaticPatterns = @("libZFork_staticd.lib")
            Missing        = @("libZForkd.dll", "libZForkd.lib", "libZFork_staticd.lib")
            DistSub        = "win-x64\Debug"
            BuildDll       = "build\$Arch-Debug-dll-static\Debug"
            BuildLib       = "build\$Arch-Debug-lib-static\Debug"
        }
    }

    $errors = @()

    foreach ($cfg in @("Release", "Debug")) {
        $spec = $libSpec[$cfg]
        $libDest = Join-Path $setupRoot $spec.DistSub
        $null = New-Item -ItemType Directory -Force -Path $libDest | Out-Null

        $dllOut = Join-Path $PSScriptRoot $spec.BuildDll
        $libOut = Join-Path $PSScriptRoot $spec.BuildLib
        $distOut = Join-Path $distRoot $spec.DistSub

        if (Test-Path $dllOut) {
            Copy-SetupFiles -Patterns $spec.DllPatterns -SourceDir $dllOut -DestDir $libDest | Out-Null
        }
        if (Test-Path $libOut) {
            Copy-SetupFiles -Patterns $spec.StaticPatterns -SourceDir $libOut -DestDir $libDest | Out-Null
        }
        # Fallback: bereits gepacktes dist\
        if (Test-Path $distOut) {
            foreach ($pat in ($spec.DllPatterns + $spec.StaticPatterns)) {
                Get-ChildItem $distOut -Filter $pat -ErrorAction SilentlyContinue | ForEach-Object {
                    if (-not (Test-Path (Join-Path $libDest $_.Name))) {
                        Copy-Item $_.FullName $libDest -Force
                    }
                }
            }
        }

        foreach ($req in $spec.Missing) {
            if (-not (Test-Path (Join-Path $libDest $req))) {
                $errors += "${cfg}: $req fehlt (erwartet in $libDest)"
            }
        }
    }

    # --- Linux aus dist\ (optional) ---
    foreach ($plat in @("linux-x64", "linux-arm64")) {
        $src = Join-Path $distRoot $plat
        if (Test-Path $src) {
            $dst = Join-Path $setupRoot $plat
            Copy-Item $src $dst -Recurse -Force
        }
    }

    # --- Lazarus Package (Source, ohne Demos) ---
    $lazDest = Join-Path $setupRoot "lazarus"
    $lazFromDist = Join-Path $distRoot "lazarus"
    $lazFromSrc  = Join-Path $PSScriptRoot "Lazarus"
    if (Test-Path $lazFromDist) {
        Copy-Item $lazFromDist $lazDest -Recurse -Force
    } elseif (Test-Path $lazFromSrc) {
        $null = New-Item -ItemType Directory -Force -Path $lazDest | Out-Null
        foreach ($pat in @("*.pas", "*.lpk", "*.lrs", "*.png")) {
            Get-ChildItem $lazFromSrc -File -Filter $pat -ErrorAction SilentlyContinue |
                ForEach-Object { Copy-Item $_.FullName $lazDest -Force }
        }
        @"
# Lazarus Package (libZFork)
Copyright (C) 2026 Matthias Stoltze
https://solid-software.net
Package -> Open -> lazlibzfork.lpk -> Compile -> Use -> Install
DLL: ../win-x64/Release/libZFork.dll neben die EXE.
"@ | Set-Content (Join-Path $lazDest "INSTALL.txt") -Encoding UTF8
    }

    # --- Bank Editor Binaries ---
    $beDest = Join-Path $setupRoot "bank_editor"
    $beFromDist = Join-Path $distRoot "bank_editor"
    $beExe = Join-Path $PSScriptRoot "Lazarus\bank_editor\release\bank_editor.exe"
    if (Test-Path $beFromDist) {
        Copy-Item $beFromDist $beDest -Recurse -Force
    } elseif (Test-Path $beExe) {
        $null = New-Item -ItemType Directory -Force -Path $beDest | Out-Null
        Copy-Item $beExe (Join-Path $beDest "bank_editor.exe") -Force
        $dll = Join-Path $setupRoot "win-x64\Release\libZFork.dll"
        if (Test-Path $dll) { Copy-Item $dll (Join-Path $beDest "libZFork.dll") -Force }
        $ico = Join-Path $PSScriptRoot "Lazarus\bank_editor\release\bank_editor.ico"
        if (Test-Path $ico) { Copy-Item $ico $beDest -Force }
    }

    $exInfo = Copy-ExampleSourcesTo -DestRoot $setupRoot
    $srcCount = Copy-LibrarySourceTo -DestRoot $setupRoot

    if ($errors.Count -gt 0) {
        Write-Host ""
        Write-Host "Setup-Staging fehlgeschlagen:" -ForegroundColor Red
        foreach ($e in $errors) { Write-Host "  - $e" -ForegroundColor Red }
        Write-Host ""
        Write-Host "Tipp: .\build.ps1 -Setup  (baut Release+Debug, DLL+Static, Examples)" -ForegroundColor Yellow
        Write-Host "  oder zuerst .\scripts\assemble_dist.ps1 und dann .\build.ps1 -SetupOnly" -ForegroundColor Yellow
        throw "Unvollstaendige Build-Artefakte fuer SDK-Installer."
    }

    Write-Host ""
    Write-Host "Setup-Staging → $setupRoot" -ForegroundColor Green
    Write-Host "  include\libzfork\  ($(( Get-ChildItem $hdrDest -Filter '*.h').Count) Header)" -ForegroundColor Green
    Write-Host "  bindings\         ($bindCount Dateien)" -ForegroundColor Green
    Write-Host "  help\             libZFork.chm" -ForegroundColor Green
    Write-Host "  license\          $licCount Dateien (Weitergabe)" -ForegroundColor Green
    foreach ($cfg in @("Release", "Debug")) {
        $libDest = Join-Path $setupRoot ("win-x64\" + $cfg)
        if (Test-Path $libDest) {
            $libKb = [math]::Round(((Get-ChildItem $libDest -File | Measure-Object Length -Sum).Sum / 1KB), 0)
            Write-Host ("  win-x64\{0,-7} {1,6} KB" -f $cfg, $libKb) -ForegroundColor Green
        }
    }
    foreach ($extra in @("linux-x64","linux-arm64","lazarus","bank_editor")) {
        $p = Join-Path $setupRoot $extra
        if (Test-Path $p) {
            $n = (Get-ChildItem $p -Recurse -File -EA SilentlyContinue).Count
            Write-Host "  $extra\          ($n Dateien)" -ForegroundColor Green
        }
    }
    Write-Host ("  examples\          {0,3} .cpp, {1} Dateien gesamt" -f $exInfo.CppCount, $exInfo.FileCount) -ForegroundColor Green
    Write-Host ("  source\            {0} Dateien (Corresponding Source)" -f $srcCount) -ForegroundColor Green

    return $setupRoot
}

function Prepare-InnoWizardAssets {
    param(
        [string]$LogoPath,
        [string]$AssetsDir
    )

    if (-not (Test-Path $LogoPath)) {
        throw "Logo nicht gefunden: $LogoPath"
    }

    Add-Type -AssemblyName System.Drawing
    $null = New-Item -ItemType Directory -Force -Path $AssetsDir | Out-Null
    Copy-Item -Path $LogoPath -Destination (Join-Path $AssetsDir "logo-wordmark-light.png") -Force

    $logo = [System.Drawing.Image]::FromFile($LogoPath)
    try {
        function New-DarkCanvas {
            param([int]$W, [int]$H)
            $bmp = [System.Drawing.Bitmap]::new($W, $H)
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $g.Clear([System.Drawing.Color]::FromArgb(255, 20, 20, 30))
            return @{ Bmp = $bmp; G = $g }
        }

        function Draw-CenteredLogo {
            param($G, $Img, [int]$CanvasW, [int]$CanvasH, [int]$Pad, [int]$OffsetY = 0)
            $maxW = $CanvasW - 2 * $Pad
            $maxH = $CanvasH - 2 * $Pad
            $scale = [Math]::Min($maxW / $Img.Width, $maxH / $Img.Height)
            $w = [int][Math]::Round($Img.Width * $scale)
            $h = [int][Math]::Round($Img.Height * $scale)
            $x = [int](($CanvasW - $w) / 2)
            $y = [int](($CanvasH - $h) / 2) + $OffsetY
            $G.DrawImage($Img, $x, $y, $w, $h)
        }

        # Oben rechts auf allen Wizard-Seiten (modern: 128x128)
        $small = New-DarkCanvas 128 128
        Draw-CenteredLogo -G $small.G -Img $logo -CanvasW 128 -CanvasH 128 -Pad 10
        $small.G.Dispose()
        $smallPath = Join-Path $AssetsDir "wizard-small.png"
        $small.Bmp.Save($smallPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $small.Bmp.Dispose()

        # Links auf Willkommen/Fertig-Seite (164x314)
        $side = New-DarkCanvas 164 314
        $sideRect = [System.Drawing.Rectangle]::new(0, 0, 164, 314)
        $sideBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
            $sideRect,
            [System.Drawing.Color]::FromArgb(255, 16, 16, 24),
            [System.Drawing.Color]::FromArgb(255, 36, 36, 52),
            90)
        $side.G.FillRectangle($sideBrush, 0, 0, 164, 314)
        $sideBrush.Dispose()
        Draw-CenteredLogo -G $side.G -Img $logo -CanvasW 164 -CanvasH 314 -Pad 14 -OffsetY -40

        $font = [System.Drawing.Font]::new("Segoe UI", 9.0, [System.Drawing.FontStyle]::Regular)
        $textBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 210, 210, 220))
        $format = [System.Drawing.StringFormat]::new()
        $format.Alignment = [System.Drawing.StringAlignment]::Center
        $side.G.DrawString("libZFork SDK", $font, $textBrush, 82.0, 210.0, $format)
        $side.G.DrawString("Matthias Stoltze", $font, $textBrush, 82.0, 228.0, $format)
        $side.G.DrawString("solid-software.net", $font, $textBrush, 82.0, 246.0, $format)
        $font.Dispose()
        $textBrush.Dispose()
        $format.Dispose()
        $side.G.Dispose()
        $sidePath = Join-Path $AssetsDir "wizard-side.png"
        $side.Bmp.Save($sidePath, [System.Drawing.Imaging.ImageFormat]::Png)
        $side.Bmp.Dispose()
    }
    finally {
        $logo.Dispose()
    }

    Write-Host "  Wizard-Logo   → $AssetsDir" -ForegroundColor Green
}

function Build-InnoSetup {
    param([string]$StageDir)

    $iscc    = Find-InnoSetupCompiler
    $version = Get-ProjectVersion
    $issFile = Join-Path $PSScriptRoot "installer\libZFork.iss"

    if (-not (Test-Path $issFile)) { throw "Inno-Skript nicht gefunden: $issFile" }

    $outDir = Join-Path $PSScriptRoot "installer\output"
    $assets = Join-Path $PSScriptRoot "installer\assets"
    $null = New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    Prepare-InnoWizardAssets -LogoPath $LogoPath -AssetsDir $assets

    Write-Host ""
    Write-Host "── Inno Setup ─────────────────────────────────" -ForegroundColor Cyan
    Write-Host "  ISCC    : $iscc"
    Write-Host "  Version : $version"
    Write-Host "  Logo    : $LogoPath"
    Write-Host "  Stage   : $StageDir"
    Write-Host ""

    $stageAbs = (Resolve-Path $StageDir).Path
    & $iscc "/DStageDir=$stageAbs" "/DMyAppVersion=$version" "/DMyArch=$Arch" $issFile
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup Kompilierung fehlgeschlagen." }

    $setupExe = Join-Path $outDir "libZFork-$version-$Arch-sdk.exe"
    if (Test-Path $setupExe) {
        $mb = [math]::Round((Get-Item $setupExe).Length / 1MB, 2)
        Write-Host ""
        Write-Host "  Installer → $setupExe  ($mb MB)" -ForegroundColor Green
        return $setupExe
    }

    $found = Get-ChildItem -Path $outDir -Filter "libZFork-*-sdk.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($found) {
        Write-Host "  Installer → $($found.FullName)" -ForegroundColor Green
        return $found.FullName
    }
    throw "Installer-EXE nicht gefunden in $outDir"
}

# ────────────────────────────────────────────────────────────────────────────
# Helper: dist\ aus vorhandenen Build-Ordnern synchronisieren (ohne Build)
# ────────────────────────────────────────────────────────────────────────────
function Sync-DistFromExistingBuilds {
    $configs = if ($Config -eq "Both") { @("Release","Debug") } else { @($Config) }

    foreach ($cfg in $configs) {
        $plans = Get-BuildPlans -Cfg $cfg
        if ($plans.Count -eq 0) { continue }

        $sub   = if ($Config -eq "Both") { $cfg } else { "" }
        $first = $true

        foreach ($plan in $plans) {
            $paths = Get-OutputPaths -BuildDir $plan.BuildDir -Cfg $cfg
            if (-not (Test-Path $paths.Out)) {
                Write-Host "SyncDistOnly: Ausgabe nicht gefunden: $($paths.Out)" -ForegroundColor DarkYellow
                continue
            }

            $exDir = if ($NoExamples) { "" } else { $paths.Ex }
            Write-Host ""
            Write-Host "── dist\$sub sync-only ($cfg / $($plan.BuildDir)) ──" -ForegroundColor Cyan
            if ($first) {
                Publish-Dist -LibDir $paths.Out -ExDir $exDir -SubDir $sub -Clean
                $first = $false
            } else {
                Publish-Dist -LibDir $paths.Out -ExDir $exDir -SubDir $sub
            }
        }
    }

    # win-x64 / linux-* / source / bank_editor / lazarus immer mitziehen
    Invoke-AssembleDist
}

function Invoke-AssembleDist {
    $script = Join-Path $PSScriptRoot "scripts\assemble_dist.ps1"
    if (-not (Test-Path $script)) {
        Write-Host "  Warnung  scripts\assemble_dist.ps1 fehlt — source/win-x64/linux nicht synchronisiert" -ForegroundColor DarkYellow
        # Fallback: wenigstens Corresponding Source aktualisieren
        Copy-LibrarySourceTo -DestRoot (Join-Path $PSScriptRoot "dist") | Out-Null
        return
    }
    Write-Host ""
    Write-Host "── assemble_dist (win/linux/source/bank_editor/…) ──" -ForegroundColor Cyan
    & $script
    if ($LASTEXITCODE -ne 0) { throw "assemble_dist.ps1 fehlgeschlagen" }
}

# ────────────────────────────────────────────────────────────────────────────
# Dispatch
# ────────────────────────────────────────────────────────────────────────────

# Propagate VERSION → headers / vcpkg / Lazarus / installer before any build.
$null = Sync-VersionArtifacts
Write-Host ""

if ($SetupOnly -and -not $SyncDistOnly) {
    $stage = Stage-SetupDist
    Build-InnoSetup -StageDir $stage | Out-Null
} elseif ($SyncDistOnly) {
    # Flat Release+Debug + win-x64/linux/source brauchen beide Configs.
    if ($Config -ne "Both") {
        Write-Host "SyncDistOnly: Config=$Config → Both (vollstaendiges dist\)" -ForegroundColor DarkYellow
        $Config = "Both"
    }
    Sync-DistFromExistingBuilds
    if ($RunSetup) {
        $stage = Stage-SetupDist
        Build-InnoSetup -StageDir $stage | Out-Null
    }
} elseif ($BuildAllModes) {
    Write-Host "Building ALL variants (Dynamic + StaticDeps)..." -ForegroundColor Magenta
    $triplet    = if ($Arch -eq "x64") { "x64-windows"           } else { "x86-windows"           }
    $tripletDll = if ($Arch -eq "x64") { "x64-windows-static-md" } else { "x86-windows-static-md" }
    $tripletLib = if ($Arch -eq "x64") { "x64-windows-static"    } else { "x86-windows-static"    }
    $cfgs       = if ($Config -eq "Both") { @("Release","Debug") } else { @($Config) }

    foreach ($cfg in $cfgs) {
        Build-Variant -Triplet $triplet    -BuildDir "build\$Arch-$cfg-dynamic"    -DllShared ON  -DllStatic ON  -BuildConfig $cfg
        Build-Variant -Triplet $tripletDll -BuildDir "build\$Arch-$cfg-dll-static" -DllShared ON  -DllStatic ON  -BuildConfig $cfg
        Build-Variant -Triplet $tripletLib -BuildDir "build\$Arch-$cfg-lib-static" -DllShared OFF -DllStatic ON  -BuildConfig $cfg

        if ($CopyDist) {
            $sub = if ($Config -eq "Both") { $cfg } else { "" }
            $dllOut = if ($Ninja) { "build\$Arch-$cfg-dll-static" } else { "build\$Arch-$cfg-dll-static\$cfg" }
            $exDir  = if ($NoExamples) { "" }
                     elseif ($Ninja)    { "build\$Arch-$cfg-dll-static" }
                     else               { "build\$Arch-$cfg-dll-static\examples\$cfg" }
            $libOut = if ($Ninja) { "build\$Arch-$cfg-lib-static" } else { "build\$Arch-$cfg-lib-static\$cfg" }

            Write-Host "" ; Write-Host "── dist\$sub (Mode All) ─────────────────────" -ForegroundColor Cyan
            Publish-Dist -LibDir $dllOut -ExDir $exDir -SubDir $sub -Clean
            Publish-Dist -LibDir $libOut -ExDir "" -SubDir $sub
        }
    }

    if ($CopyDist) {
        Invoke-AssembleDist
    }

    if ($RunSetup) {
        $stage = Stage-SetupDist
        Build-InnoSetup -StageDir $stage | Out-Null
    }
} else {
    $cfgs = if ($Config -eq "Both") { @("Release","Debug") } else { @($Config) }

    foreach ($cfg in $cfgs) {
        $plans = Get-BuildPlans -Cfg $cfg
        foreach ($plan in $plans) {
            Build-Variant -Triplet $plan.Triplet `
                          -BuildDir $plan.BuildDir `
                          -DllShared $plan.DllShared `
                          -DllStatic $plan.DllStatic `
                          -BuildConfig $cfg
        }
        Publish-ConfigToDist -Cfg $cfg -Clean:($cfg -eq $cfgs[0])
    }

    if ($CopyDist) {
        Invoke-AssembleDist
    }

    if ($RunSetup) {
        $stage = Stage-SetupDist
        Build-InnoSetup -StageDir $stage | Out-Null
    }
}

Write-Host ""
Write-Host "=== Alle Builds abgeschlossen ===" -ForegroundColor Cyan
Write-Host "    Verteilung: $(Join-Path $PSScriptRoot 'dist')" -ForegroundColor Cyan
if ($RunSetup) {
    Write-Host "    Installer:  $(Join-Path $PSScriptRoot 'installer\output')" -ForegroundColor Cyan
}

