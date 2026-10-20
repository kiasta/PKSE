<#
    Build and run a host harness against real saves, on Windows with MSVC.

    Companion to build.sh (which needs a native g++). devkitPro's MSys2 ships only the aarch64
    cross-compiler, whose output can't run on the PC -- so on a normal PKSE dev machine MSVC is the
    host compiler that's actually present.

        pwsh tools\harness\build.ps1 [-Harness roundtrip|legality] [-Saves <dir>] [-HarnessArgs ...]

    roundtrip (default) -- read -> parse -> serialize back -> compare. Exits non-zero if any game
                           fails to reproduce its file exactly, so it can gate a release.
    legality            -- run the legality checker over every Pokemon in each save and histogram
                           what it flags. Reports only; judging the output is the point.
                           Pass -HarnessArgs --verbose to list every flagged Pokemon.
    editpersist         -- edit a met level and an item count, serialize, re-parse, and assert the
                           new values survived. The complement to roundtrip: a writer that drops
                           every edit passes roundtrip perfectly, because nothing was written.
#>
param(
    [ValidateSet('roundtrip', 'legality', 'editpersist', 'blockprobe')]
    [string]$Harness = 'roundtrip',
    [string]$Saves = (Join-Path $env:USERPROFILE 'pkse-test-saves'),
    [string[]]$HarnessArgs = @()
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$out  = Join-Path $repo 'build-harness'

# Locate MSVC rather than assuming a version or path.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Write-Error 'vswhere.exe not found - is Visual Studio installed?' }
$vsdir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsdir) { Write-Error 'Visual Studio found, but without the C++ toolset (VC.Tools.x86.x64).' }

# Pull the MSVC environment into this session.
#
# Two traps here, both hit while writing this:
#  - MSys2 puts its own `cmd` on PATH, which PowerShell picks up and chokes on. Use the real one by
#    full path.
#  - vcvars64 emits a harmless "vswhere.exe is not recognized" line on stderr while still succeeding.
#    PowerShell turns any native stderr into a NativeCommandError, which $ErrorActionPreference=Stop
#    then treats as fatal. Silencing the batch INSIDE cmd keeps that noise out of PowerShell entirely
#    -- redirecting on the PowerShell side would not help, since the error is raised either way.
$vcvars = Join-Path $vsdir 'VC\Auxiliary\Build\vcvars64.bat'
$realCmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
& $realCmd /c "call `"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}
if (-not $env:VCToolsInstallDir) { Write-Error 'vcvars64 did not initialise the MSVC environment.' }

New-Item -ItemType Directory -Force -Path $out | Out-Null

# Every source the save layer needs, and nothing that touches the Switch SDK.
# src\Legality joins the list because the legality harness needs it; roundtrip does not call it,
# and the linker drops what nothing references, so one source list serves both.
$src = @()
foreach ($d in 'Trainer', 'Pokemon', 'Encryption', 'Names', 'Enums', 'Legality') {
    $src += (Get-ChildItem (Join-Path $repo "src\$d") -Filter *.cpp).FullName
}
# Bank.cpp is out of scope here: it's SD-card storage, not a save write path, and it uses POSIX
# mkdir. The bank has its own in-app round-trip verification.
$src = $src | Where-Object { $_ -notmatch 'Bank\.cpp$' }
$src += (Join-Path $repo 'src\Save\Block.cpp')          # SCBlock parse/serialize
$src += (Join-Path $repo 'src\Utils\StringHelpers.cpp')
$src += (Join-Path $repo 'src\Utils\HelperUtilities.cpp')
$src += (Join-Path $repo 'src\Utils\SHA256.cpp')        # SCBlock save hashing
$src += (Join-Path $repo 'src\Utils\MD5.cpp')           # BDSP whole-file hash
# Logger.cpp is deliberately absent: it writes a rotating log via POSIX dirent, which MSVC has no
# header for. Each harness .cpp defines the log entry points as no-ops instead.

Write-Host "Compiling $($src.Count) sources for the host ($Harness)..."
# /wd4267 /wd4244: size_t and integer narrowing, pervasive in byte-shuffling code and not what this
# harness is looking for.
# /utf-8: the sources ARE UTF-8, and MSVC otherwise reads them in the system codepage and rejects
# every multi-byte char16_t literal in Gen3Text.h ("too many characters in constant"). GCC, which
# the real build uses, assumes UTF-8 already.
$flags = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/O1', '/W0', '/wd4267', '/wd4244',
           "/I$repo\include")
$log = Join-Path $out "build-$Harness.log"
$exe = Join-Path $out "$Harness.exe"
# Per-harness object dir: the harnesses link different main()s over the same object set, so
# sharing one directory would make whichever built last win.
$objDir = Join-Path $out "obj-$Harness"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

# The harness itself is compiled separately, to a fixed object name. cl derives an object name
# from the source STEM, and Windows filenames are case-insensitive, so legality.cpp and
# src\Legality\Legality.cpp both want Legality.obj -- the second silently overwrites the first
# and the link fails with "entry point must be defined", nowhere near the actual cause.
$harnessObj = Join-Path $objDir 'harness_main.obj'
& cl @flags /c /Fo:"$harnessObj" (Join-Path $repo "tools\harness\$Harness.cpp") *> $log
if ($LASTEXITCODE -eq 0) {
    & cl @flags /Fe:"$exe" /Fo:"$objDir\\" $src $harnessObj *>> $log
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED - see $log"
    Get-Content $log -Tail 25
    exit 1
}

Write-Host ''
& $exe $Saves @HarnessArgs
exit $LASTEXITCODE
