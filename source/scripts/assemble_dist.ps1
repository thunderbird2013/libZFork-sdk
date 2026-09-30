#!/usr/bin/env pwsh
# Assembles a multi-platform dist tree:
#   dist/
#     include/, bindings/          (shared)
#     win-x64/{Release,Debug}/
#     linux-x64/{Release,Debug}/
#     linux-arm64/{Release,Debug}/
#
# Usage:
#   .\scripts\assemble_dist.ps1              # pack what exists
#   .\scripts\assemble_dist.ps1 -BuildMissing # also build missing Linux Debug via WSL
param(
    [switch]$BuildMissing,
    [switch]$CleanLibDirs
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not (Test-Path (Join-Path $Root "CMakeLists.txt"))) {
    throw "CMakeLists.txt nicht gefunden (Root=$Root). Script aus dem Repo bzw. dist\source ausfuehren."
}
$Dist = Join-Path $Root "dist"

function Ensure-Dir([string]$p) {
    $null = New-Item -ItemType Directory -Force -Path $p
}

function Copy-One([string]$src, [string]$destDir) {
    if (-not (Test-Path $src)) { return $false }
    Ensure-Dir $destDir
    Copy-Item -Path $src -Destination $destDir -Force
    return $true
}

function Copy-Glob([string]$dir, [string]$pattern, [string]$destDir) {
    if (-not (Test-Path $dir)) { return 0 }
    Ensure-Dir $destDir
    $n = 0
    Get-ChildItem -Path $dir -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName -Destination $destDir -Force
        $n++
    }
    return $n
}

Write-Host "=== assemble_dist → $Dist ===" -ForegroundColor Cyan

# Shared headers / bindings
Ensure-Dir "$Dist\include\libzfork"
Copy-Item "$Root\include\libzfork\*.h" "$Dist\include\libzfork\" -Force
if (Test-Path "$Root\bindings") {
    Ensure-Dir "$Dist\bindings"
    Copy-Item "$Root\bindings\*" "$Dist\bindings\" -Recurse -Force
    Write-Host "  [x] bindings\" -ForegroundColor Green
}

# Lazarus-Package (Komponenten + Wrapper, ohne Demos/Build-Artefakte)
$lazSrc = Join-Path $Root "Lazarus"
$lazDest = Join-Path $Dist "lazarus"
if (Test-Path $lazSrc) {
    if (Test-Path $lazDest) { Remove-Item $lazDest -Recurse -Force }
    Ensure-Dir $lazDest
    $lazExts = @("*.pas", "*.lpk", "*.lrs", "*.png")
    $lazCount = 0
    foreach ($pat in $lazExts) {
        Get-ChildItem -Path $lazSrc -File -Filter $pat -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item $_.FullName -Destination $lazDest -Force
            $lazCount++
        }
    }
    # Keine README/ROADMAP im Dist-Package
    $lazNote = @"
# Lazarus Package (libZFork)

Copyright (C) 2026 Matthias Stoltze
https://solid-software.net

Install: Package -> Open -> lazlibzfork.lpk -> Compile -> Use -> Install

DLL: copy ../win-x64/Release/libZFork.dll (or Debug) next to your EXE.
"@
    Set-Content -Path (Join-Path $lazDest "INSTALL.txt") -Value $lazNote -Encoding UTF8
    Write-Host "  [x] lazarus\  ($lazCount Dateien + INSTALL.txt)" -ForegroundColor Green
} else {
    Write-Host "  [ ] lazarus\  (Lazarus/ fehlt)" -ForegroundColor Yellow
}

# Optional README for DLL usage
if (Test-Path "$Root\installer\DLL_README.txt") {
    Copy-Item "$Root\installer\DLL_README.txt" "$Dist\DLL_README.txt" -Force
}

# Lizenzen: extra Ordner zum Weitergeben mit App/DLL (ohne _unused/)
$licSrc = Join-Path $Root "license"
$licDest = Join-Path $Dist "license"
if (Test-Path $licSrc) {
    if (Test-Path $licDest) { Remove-Item $licDest -Recurse -Force }
    Ensure-Dir $licDest
    $licN = 0
    Get-ChildItem $licSrc -File | ForEach-Object {
        Copy-Item $_.FullName $licDest -Force
        $licN++
    }
    Write-Host "  [x] license\  ($licN Dateien, Weitergabe)" -ForegroundColor Green
} else {
    Write-Host "  [ ] license\  (fehlt)" -ForegroundColor Yellow
}

# Corresponding Source (GPLv2) neben den Binaries
$srcDest = Join-Path $Dist "source"
if (Test-Path $srcDest) { Remove-Item $srcDest -Recurse -Force }
Ensure-Dir $srcDest
foreach ($f in @("CMakeLists.txt", "vcpkg.json", "build.ps1", "packagefiles.xml", "VERSION")) {
    $from = Join-Path $Root $f
    if (Test-Path $from) { Copy-Item $from $srcDest -Force }
}
foreach ($dir in @("include", "src", "cmake", "triplets", "overlays", "examples", "tests", "scripts")) {
    $from = Join-Path $Root $dir
    if (Test-Path $from) {
        Copy-Item $from (Join-Path $srcDest $dir) -Recurse -Force
    }
}
@"
libZFork — Corresponding Source (GPLv2)
=======================================

Copyright (C) 2026 Matthias Stoltze — https://solid-software.net

Complete corresponding source for the binaries in this dist/ SDK tree.

Use prebuilt binaries (no rebuild needed):
  ../win-x64/Release/libZFork.dll   (Windows)
  ../linux-x64/Release/libZFork.so* (Linux)
  Headers: ../include/libzfork/

Rebuild from this folder (needs vcpkg + VS Build Tools / WSL):
  Windows:  .\build.ps1 -Config Release -Link Both -NoExamples -NoCopyDist -VcpkgRoot <pfad>
  Linux:    bash scripts/wsl_build_variant.sh x64 Release

Scripts resolve paths relative to this source tree (no hardcoded drive letters).
Licenses: ../license/
Version:  see VERSION in this folder.
"@ | Set-Content (Join-Path $srcDest "BUILD.txt") -Encoding UTF8
$srcN = (Get-ChildItem $srcDest -Recurse -File -EA SilentlyContinue).Count
Write-Host "  [x] source\  ($srcN Dateien, Corresponding Source)" -ForegroundColor Green

# Hilfe: nur die CHM (kein HTML/CSS/Quellen-Baum)
$chmSrc = Join-Path $Root "docs\help\chm\libZFork.chm"
$helpDest = Join-Path $Dist "help"
$helpChm = Join-Path $helpDest "libZFork.chm"
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "SilentlyContinue"
try {
    if (Test-Path $helpDest) { Remove-Item $helpDest -Recurse -Force }
    if (Test-Path $chmSrc) {
        Ensure-Dir $helpDest
        try { Copy-Item $chmSrc $helpChm -Force } catch { }
        if (Test-Path $helpChm) {
            Write-Host "  [x] help\libZFork.chm" -ForegroundColor Green
        } else {
            Write-Host "  [!] help\libZFork.chm nicht kopierbar (gesperrt?)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  [ ] help\libZFork.chm  (fehlend)" -ForegroundColor Yellow
    }
} finally {
    $ErrorActionPreference = $prevEap
}

$platforms = @(
    @{
        Name = "win-x64"
        Release = @{
            Sources = @(
                @{ Path = "$Root\build\x64-Release-dll-static\Release"; Patterns = @("libZFork.dll","libZFork.lib") }
                @{ Path = "$Root\build\x64-Release-lib-static\Release"; Patterns = @("libZFork_static.lib") }
            )
        }
        Debug = @{
            Sources = @(
                @{ Path = "$Root\build\x64-Debug-dll-static\Debug"; Patterns = @("libZForkd.dll","libZForkd.lib") }
                @{ Path = "$Root\build\x64-Debug-lib-static\Debug"; Patterns = @("libZFork_staticd.lib") }
            )
        }
    }
    @{
        Name = "linux-x64"
        Release = @{
            Sources = @(
                @{ Path = "$Root\build\wsl-linux-release"; Patterns = @("libZFork.so*","libZFork_static.a") }
                @{ Path = "$Root\build\wsl-linux"; Patterns = @("libZFork.so*","libZFork_static.a") }
            )
        }
        Debug = @{
            Sources = @(
                @{ Path = "$Root\build\wsl-linux-debug"; Patterns = @("libZForkd.so*","libZFork_staticd.a") }
            )
        }
    }
    @{
        Name = "linux-arm64"
        Release = @{
            Sources = @(
                @{ Path = "$Root\build\wsl-linux-arm64-release"; Patterns = @("libZFork.so*","libZFork_static.a") }
                @{ Path = "$Root\build\wsl-linux-arm64"; Patterns = @("libZFork.so*","libZFork_static.a") }
            )
        }
        Debug = @{
            Sources = @(
                @{ Path = "$Root\build\wsl-linux-arm64-debug"; Patterns = @("libZForkd.so*","libZFork_staticd.a") }
            )
        }
    }
)

function Pack-Config([string]$platName, [string]$cfgName, $cfg) {
    $dest = Join-Path $Dist "$platName\$cfgName"
    if ($CleanLibDirs -and (Test-Path $dest)) {
        Remove-Item "$dest\*" -Recurse -Force -ErrorAction SilentlyContinue
    }
    Ensure-Dir $dest
    $copied = 0
    # Windows: DLL/SO und Static liegen in getrennten Build-Ordnern → alle Sources mergen.
    # Linux: nur den ersten vorhandenen Tree nehmen (sonst überschreibt z.B. altes
    # build\wsl-linux die frischen build\wsl-linux-release Outputs).
    $stopAfterFirstHit = $platName -like 'linux-*'
    foreach ($src in $cfg.Sources) {
        if (-not (Test-Path $src.Path)) { continue }
        $before = $copied
        foreach ($pat in $src.Patterns) {
            $copied += Copy-Glob $src.Path $pat $dest
        }
        if ($stopAfterFirstHit -and ($copied -gt $before)) { break }
    }
    $files = @(Get-ChildItem $dest -File -ErrorAction SilentlyContinue)
    if ($files.Count -eq 0) {
        Write-Host "  [ ] $platName/$cfgName  (fehlend)" -ForegroundColor Yellow
        return $false
    }
    $kb = [math]::Round(($files | Measure-Object Length -Sum).Sum / 1KB)
    Write-Host ("  [x] {0}/{1,-7} {2} Dateien, {3} KB" -f $platName, $cfgName, $files.Count, $kb) -ForegroundColor Green
    return $true
}

$missing = @()
foreach ($p in $platforms) {
    Write-Host "`n$($p.Name)" -ForegroundColor Cyan
    if (-not (Pack-Config $p.Name "Release" $p.Release)) { $missing += "$($p.Name)/Release" }
    if (-not (Pack-Config $p.Name "Debug" $p.Debug)) { $missing += "$($p.Name)/Debug" }
}

# Remove old flat windows binaries from dist root (now under win-x64/)
foreach ($f in @("libZFork.dll","libZFork.lib","libZFork_static.lib","libZForkd.dll","libZForkd.lib","libZFork_staticd.lib")) {
    $old = Join-Path $Dist $f
    if (Test-Path $old) {
        Remove-Item $old -Force
        Write-Host "  removed root/$f (jetzt unter win-x64/)" -ForegroundColor DarkGray
    }
}

# Entferne altes Flat-Layout (libZFork.* direkt unter linux-*/ statt Release|Debug)
foreach ($plat in @("linux-x64","linux-arm64","win-x64")) {
    $p = Join-Path $Dist $plat
    if (-not (Test-Path $p)) { continue }
    Get-ChildItem $p -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'libZFork*' } |
        ForEach-Object {
            Remove-Item $_.FullName -Force
            Write-Host "  removed $plat/$($_.Name) (jetzt unter Release|Debug)" -ForegroundColor DarkGray
        }
}

# Bank-Editor: nur Runtime-Binaries (kein Source / kein .lpi)
$beDest = Join-Path $Dist "bank_editor"
$beCandidates = @(
    (Join-Path $Root "Lazarus\bank_editor\release\bank_editor.exe"),
    (Join-Path $Root "Lazarus\bank_editor\bank_editor.exe")
)
$beExe = $beCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($beExe) {
    if (Test-Path $beDest) { Remove-Item $beDest -Recurse -Force }
    Ensure-Dir $beDest
    Copy-Item $beExe (Join-Path $beDest "bank_editor.exe") -Force

    $dllFresh = Join-Path $Dist "win-x64\Release\libZFork.dll"
    $dllBeside = Join-Path (Split-Path $beExe) "libZFork.dll"
    $dllBuild  = Join-Path $Root "build\x64-Release-dll-static\Release\libZFork.dll"
    $dllSrc = @($dllFresh, $dllBeside, $dllBuild) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($dllSrc) { Copy-Item $dllSrc (Join-Path $beDest "libZFork.dll") -Force }

    $icoBeside = Join-Path (Split-Path $beExe) "bank_editor.ico"
    if (-not (Test-Path $icoBeside)) {
        $icoBeside = Join-Path $Root "Lazarus\bank_editor\bank_editor.ico"
    }
    if (Test-Path $icoBeside) { Copy-Item $icoBeside (Join-Path $beDest "bank_editor.ico") -Force }

    # Icons sind in der EXE eingebettet — kein icons\-Ordner noetig
    $beFiles = @(Get-ChildItem $beDest -Recurse -File).Count
    Write-Host "  [x] bank_editor\  ($beFiles Binaries)" -ForegroundColor Green
} else {
    Write-Host "  [ ] bank_editor\  (bank_editor.exe fehlt)" -ForegroundColor Yellow
}

# (kein dist\README.md — SDK_README / DLL_README reichen)

Write-Host ""
if ($missing.Count -gt 0) {
    Write-Host "Fehlend: $($missing -join ', ')" -ForegroundColor Yellow
    if ($BuildMissing) {
        Write-Host "Baue fehlende Linux-Varianten via WSL…" -ForegroundColor Cyan
        foreach ($m in $missing) {
            if ($m -notmatch '^linux-(x64|arm64)/(Release|Debug)$') { continue }
            $arch = $Matches[1]
            $cfg  = $Matches[2]
            Write-Host "  → $arch $cfg" -ForegroundColor Cyan
            $sh = Join-Path $Root "scripts\wsl_build_variant.sh"
            # WSL sieht Windows-Pfade unter /mnt/<drive>/...
            $shWsl = (wsl wslpath -a $sh).Trim()
            wsl -- bash $shWsl $arch $cfg
            if ($LASTEXITCODE -ne 0) { throw "WSL build failed: $arch $cfg" }
        }
        Write-Host "Erneutes Packen…" -ForegroundColor Cyan
        & $PSCommandPath
        return
    }
    Write-Host "Tipp: .\scripts\assemble_dist.ps1 -BuildMissing" -ForegroundColor DarkGray
} else {
    Write-Host "Alle Plattformen Release+Debug vorhanden." -ForegroundColor Green
}

Write-Host "`nDist-Baum:" -ForegroundColor Cyan
Get-ChildItem $Dist -Directory | ForEach-Object {
    $name = $_.Name
    if ($name -in @('include','bindings','examples','help','lazarus','bank_editor','license','source')) {
        $n = (Get-ChildItem $_.FullName -File -Recurse -ErrorAction SilentlyContinue).Count
        Write-Host "  $name\  ($n Dateien)" -ForegroundColor DarkGray
    } else {
        foreach ($c in @('Release','Debug')) {
            $d = Join-Path $_.FullName $c
            if (Test-Path $d) {
                $n = (Get-ChildItem $d -File -ErrorAction SilentlyContinue).Count
                Write-Host "  $name\$c\  ($n Dateien)" -ForegroundColor White
            }
        }
    }
}
