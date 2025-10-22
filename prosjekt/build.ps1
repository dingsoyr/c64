param(
  [string]$Cl65  = "..\cc65\bin\cl65.exe",
  [string]$Vice  = "..\vice\bin\x64sc.exe",
  [string]$C1541 = "..\vice\bin\c1541.exe",
  [string]$DiskName = "vreid",
  [string]$DiskId   = "vr"
)

$ErrorActionPreference = "Stop"

$Cl65  = (Resolve-Path $Cl65).Path
$Vice  = (Resolve-Path $Vice).Path
$C1541 = (Resolve-Path $C1541).Path

$srcMain = "src\main.c"
$srcAudio = "src\sid_audio.c"
$srcGfx = "src\gfx_helpers.c"
$outDir  = "build"
$outPrg  = Join-Path $outDir "main.prg"
$d64     = Join-Path $outDir "vreid.d64"
$koaHost = "assets\vreid_koala.koa"
$koaOnD  = "vreid.koa"
$prgOnD  = "main"

if (!(Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# Bygg PRG
Write-Host "Using cl65: $Cl65"
& $Cl65 -t c64 -Oirs -o $outPrg $srcMain $srcAudio $srcGfx

# Lag nytt d64-image (rett syntaks!)
if (Test-Path $d64) { Remove-Item $d64 -Force }
Write-Host "Creating D64: $d64"
& $C1541 -format "$DiskName,$DiskId" d64 $d64

# Legg inn KOA + PRG på disken
if (!(Test-Path $koaHost)) { throw "Fann ikkje '$koaHost' i prosjektmappa." }

Write-Host "Adding KOA to disk: $koaHost -> $koaOnD"
& $C1541 $d64 -write $koaHost $koaOnD

Write-Host "Adding PRG to disk: $outPrg -> $prgOnD"
& $C1541 $d64 -write $outPrg $prgOnD

# Start VICE, mount disken på 8 og autostart MAIN frå disken
Write-Host "Launching VICE with disk and autostart '$prgOnD'"
& $Vice -8 $d64 -autostart $outPrg
