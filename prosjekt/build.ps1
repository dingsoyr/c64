param(
  [string]$Cl65 = "..\cc65\bin\cl65.exe",
  [string]$Vice = "..\vice\bin\x64sc.exe",
  [string]$C1541 = "..\vice\bin\c1541.exe",
  [ValidateSet("Release","Debug")] [string]$Config = "Release",
  [ValidateSet("Pal","Ntsc")] [string]$ViceModel = "Pal",
  [switch]$Headless,
  [switch]$NoRun,
  [string]$DiskName = "vreid",
  [string]$DiskId = "vr",
  [string]$Tag = ""   # t.d. "2025-10-22"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# --- Paths (robuste) ---
$root   = $PSScriptRoot
$srcDir = Join-Path $root "src"
$outDir = Join-Path $root "build"
$assets = Join-Path $root "assets"

$srcMain  = Join-Path $srcDir "main.c"
$srcAudio = Join-Path $srcDir "sid_audio.c"
$srcGfx   = Join-Path $srcDir "gfx_helpers.c"

$Cl65  = (Resolve-Path -LiteralPath (Join-Path $root $Cl65)).Path
$Vice  = (Resolve-Path -LiteralPath (Join-Path $root $Vice)).Path
$C1541 = (Resolve-Path -LiteralPath (Join-Path $root $C1541)).Path

if (!(Test-Path $Cl65))  { throw "Could not find cl65 on '$Cl65'." }
if (!(Test-Path $C1541)) { throw "Could not find c1541 on '$C1541'." }
if (!(Test-Path $Vice) -and -not $NoRun) { throw "Could not find VICE on '$Vice'." }

# --- Sørg for build-mappe og tøm innhald ---
if (!(Test-Path $outDir)) {
  New-Item -ItemType Directory -Path $outDir | Out-Null
} else {
  # Slett ALT innhald i build/, men la selve mappa stå
  Get-ChildItem -Path $outDir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}

# --- Output-namn ---
$tagSuffix = if ($Tag) { "_$Tag" } else { "" }
$outPrg = Join-Path $outDir "main$tagSuffix.prg"
$d64    = Join-Path $outDir "vreid$tagSuffix.d64"

$koaHost = Join-Path $assets "vreid_koala.koa"
$koaOnD  = "vreid.koa"
$prgOnD  = "main"  # filnamn på disken (utan .prg)
$sampleHost = Join-Path $assets "vreid_sample.bin"
$sampleOnD  = "vreid.bin"

# --- cc65-flagg ---
$ccFlags = @("-t","c64")
switch ($Config) {
  "Release" { $ccFlags += @("-Oirs","--static-locals") }
  "Debug"   { $ccFlags += @("-g") }
}

# Nyttig feilsøking
$mapFile = [IO.Path]::ChangeExtension($outPrg,".map")
$lblFile = [IO.Path]::ChangeExtension($outPrg,".lbl")
$lstFile = [IO.Path]::ChangeExtension($outPrg,".lst")
$ccFlags += @("-m", $mapFile, "-Ln", $lblFile, "-l", $lstFile)
# Valfritt for meir detaljert map:
# $ccFlags += @("-vm")

Write-Host "=== BUILD ==================================================="
Write-Host "Using cl65: $Cl65"
& $Cl65 @ccFlags -o $outPrg $srcMain $srcAudio $srcGfx

if (!(Test-Path $outPrg)) { throw "Kompilation error: '$outPrg' does not exist." }

Write-Host "=== DISK IMAGE ============================================="
if (Test-Path $d64) { Remove-Item $d64 -Force }
Write-Host "Creating D64: $d64"
& $C1541 -format "$DiskName,$DiskId" d64 $d64

if (!(Test-Path $koaHost)) { throw "Could not find asset: '$koaHost'." }
Write-Host "Adding KOA: $koaHost -> $koaOnD"
& $C1541 $d64 -write $koaHost $koaOnD

if (!(Test-Path $sampleHost)) { throw "Could not find sample: '$sampleHost'." }
Write-Host "Adding sample: $sampleHost -> $sampleOnD"
& $C1541 $d64 -write $sampleHost $sampleOnD

Write-Host "Adding PRG: $outPrg -> $prgOnD"
& $C1541 $d64 -write $outPrg $prgOnD

if ($NoRun) {
  Write-Host "Build done. Skipping VICE."
  exit 0
}

Write-Host "=== RUN (VICE) ============================================="
$viceArgs = @("-8",$d64,"-autostart",$outPrg)
if ($ViceModel -eq "Pal")  { $viceArgs += @("-pal") } else { $viceArgs += @("-ntsc") }
if ($Headless) { $viceArgs += @("-console","-sounddev","dummy") }

Write-Host "Launching VICE: $($viceArgs -join ' ')"
& $Vice @viceArgs
