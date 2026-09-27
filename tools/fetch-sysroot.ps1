# Downloads the MSYS2 clangarm64 (aarch64-w64-mingw32) sysroot packages needed to
# cross-compile Windows ARM64 binaries from an x86_64 Windows host.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\fetch-sysroot.ps1 [-Force]
#
# Output: tools\sysroot-aarch64\clangarm64\{include,lib,aarch64-w64-mingw32,...}
# A marker file (tools\sysroot-aarch64\.ready) records what was fetched.

param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$IndexUrl  = 'https://repo.msys2.org/mingw/clangarm64/'
$RepoRoot  = Split-Path -Parent $PSScriptRoot
$SysRoot   = Join-Path $RepoRoot 'tools\sysroot-aarch64'
$Marker    = Join-Path $SysRoot '.ready'
$TmpDir    = Join-Path $SysRoot '.dl'

$Packages = @(
    'mingw-w64-clang-aarch64-headers',
    'mingw-w64-clang-aarch64-crt',
    'mingw-w64-clang-aarch64-winpthreads',
    'mingw-w64-clang-aarch64-libwinpthread',
    'mingw-w64-clang-aarch64-libmangle',
    'mingw-w64-clang-aarch64-compiler-rt',
    'mingw-w64-clang-aarch64-libc++',
    'mingw-w64-clang-aarch64-yaml-cpp'
)

function Get-VersionTokens([string]$v) {
    return @([regex]::Matches($v, '\d+|\D+') | ForEach-Object { $_.Value })
}

function Compare-Version([string]$a, [string]$b) {
    $ta = Get-VersionTokens $a
    $tb = Get-VersionTokens $b
    $n = [Math]::Min($ta.Count, $tb.Count)
    for ($i = 0; $i -lt $n; $i++) {
        $x = $ta[$i]; $y = $tb[$i]
        $xn = 0; $yn = 0
        $xi = [int]::TryParse($x, [ref]$xn)
        $yi = [int]::TryParse($y, [ref]$yn)
        if ($xi -and $yi) {
            if ($xn -ne $yn) { return $xn - $yn }
        } elseif ($xi -and -not $yi) { return 1 }
        elseif (-not $xi -and $yi) { return -1 }
        else {
            $c = [string]::Compare($x, $y, [StringComparison]::Ordinal)
            if ($c -ne 0) { return $c }
        }
    }
    return ($ta.Count - $tb.Count)
}

function Ensure-ResourceDir([string]$prefix) {
    $res = Join-Path $prefix 'lib\clang\22'
    $hostRes = $null
    $clang = Get-Command 'clang++.exe' -ErrorAction SilentlyContinue
    if ($clang) { $hostRes = (& $clang.Source -print-resource-dir 2>$null | Select-Object -First 1) }
    if (-not $hostRes) { $hostRes = 'C:\msys64\ucrt64\lib\clang\22' }

    $resInclude = Join-Path $res 'include'
    if (-not (Test-Path $resInclude) -or @(Get-ChildItem $resInclude -File -ErrorAction SilentlyContinue).Count -eq 0) {
        if (-not (Test-Path (Join-Path $hostRes 'include'))) {
            throw "[fetch-sysroot] host clang builtin headers not found at $hostRes\include (install mingw-w64-ucrt-x86_64-clang)"
        }
        New-Item -ItemType Directory -Force -Path $resInclude | Out-Null
        Copy-Item (Join-Path $hostRes 'include\*') $resInclude -Recurse -Force
        Write-Host "[fetch-sysroot] copied host builtin headers into resource dir"
    }

    $rtDir = Join-Path $res 'lib\aarch64-w64-windows-gnu'
    $winDir = Join-Path $res 'lib\windows'
    New-Item -ItemType Directory -Force -Path $rtDir | Out-Null
    foreach ($pair in @(@('libclang_rt.builtins-aarch64.a', 'libclang_rt.builtins.a'),
                        @('libclang_rt.profile-aarch64.a',  'libclang_rt.profile.a'))) {
        $src = Join-Path $winDir $pair[0]
        $dst = Join-Path $rtDir $pair[1]
        if ((Test-Path $src) -and -not (Test-Path $dst)) {
            Copy-Item $src $dst -Force
        }
        if (-not (Test-Path $dst)) {
            throw "[fetch-sysroot] missing compiler-rt archive: $dst"
        }
    }
}

if ((Test-Path $Marker) -and -not $Force) {
    $fetched = @(Get-Content $Marker -ErrorAction SilentlyContinue)
    $missing = @($Packages | Where-Object {
        $pkg = $_
        -not ($fetched | Where-Object { $_ -like "$pkg-*" })
    })
    if ($missing.Count -eq 0) {
        $prefixChk = Join-Path $SysRoot 'clangarm64'
        Ensure-ResourceDir $prefixChk
        Write-Host "[fetch-sysroot] sysroot already present at $SysRoot (use -Force to refetch)"
        exit 0
    }
    Write-Host "[fetch-sysroot] sysroot incomplete, missing: $($missing -join ', ')"
}

Write-Host "[fetch-sysroot] fetching package index from $IndexUrl ..."
$index = & curl.exe -sS --max-time 60 $IndexUrl
if ($LASTEXITCODE -ne 0 -or -not $index) {
    throw "[fetch-sysroot] failed to download package index"
}
$indexText = if ($index -is [array]) { $index -join "`n" } else { [string]$index }

New-Item -ItemType Directory -Force -Path $SysRoot | Out-Null
if (Test-Path $TmpDir) { Remove-Item -Recurse -Force $TmpDir }
New-Item -ItemType Directory -Force -Path $TmpDir | Out-Null

$manifest = @()

foreach ($pkg in $Packages) {
    $pkgEnc = [System.Uri]::EscapeDataString($pkg)
    $rx = [regex]"href=""(?<file>$([regex]::Escape($pkgEnc))-(?<rest>[^""/]+)\.pkg\.tar\.zst)"""
    $candidates = @()
    foreach ($m in $rx.Matches($indexText)) {
        $file = [System.Uri]::UnescapeDataString($m.Groups['file'].Value)
        $rest = [System.Uri]::UnescapeDataString($m.Groups['rest'].Value)
        $dashIdx = $rest.LastIndexOf('-')
        if ($dashIdx -lt 1) { continue }
        $version = $rest.Substring(0, $dashIdx)
        $arch    = $rest.Substring($dashIdx + 1)
        $candidates += [pscustomobject]@{
            File    = $file
            Href    = $m.Groups['file'].Value
            Version = $version
            Arch    = $arch
        }
    }
    if ($candidates.Count -eq 0) {
        throw "[fetch-sysroot] no package found in index for: $pkg"
    }
    $best = $candidates[0]
    foreach ($c in $candidates) {
        if ((Compare-Version $c.Version $best.Version) -gt 0) { $best = $c }
    }

    $url = $IndexUrl + $best.Href
    $out = Join-Path $TmpDir $best.File
    Write-Host "[fetch-sysroot] $($best.File)"
    & curl.exe -sS -L --max-time 300 -o $out $url
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) {
        throw "[fetch-sysroot] download failed: $url"
    }
    $manifest += "$($best.File)  [$($best.Arch)]"
}

Write-Host "[fetch-sysroot] extracting packages..."
foreach ($f in Get-ChildItem $TmpDir -Filter '*.pkg.tar.zst') {
    & tar.exe -xf $f.FullName -C $SysRoot
    if ($LASTEXITCODE -ne 0) { throw "[fetch-sysroot] extract failed: $($f.Name)" }
}

$prefix = Join-Path $SysRoot 'clangarm64'
if (-not (Test-Path (Join-Path $prefix 'include'))) {
    throw "[fetch-sysroot] unexpected layout: $prefix\include not found after extraction"
}

Ensure-ResourceDir $prefix

$manifest | Set-Content -Path $Marker -Encoding ascii
Remove-Item -Recurse -Force $TmpDir

Write-Host "[fetch-sysroot] done. sysroot prefix: $prefix"
$manifest | ForEach-Object { Write-Host "  $_" }
