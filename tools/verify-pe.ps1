# Prints the PE machine type and import table of a binary, and optionally
# verifies that every non-system imported DLL exists in a given directory.
#
# Usage:
#   powershell -File tools\verify-pe.ps1 -Path file.exe [-ExpectMachine ARM64|x64]
#       [-RequireDir stage_dir]
#
# Exit code: 0 = OK, 1 = verification failed

param(
    [Parameter(Mandatory = $true)][string]$Path,
    [ValidateSet('ARM64', 'x64', 'x86')][string]$ExpectMachine,
    [string]$RequireDir
)

$ErrorActionPreference = 'Stop'

$SystemDlls = @(
    'kernel32.dll', 'kernelbase.dll', 'ntdll.dll', 'msvcrt.dll', 'ws2_32.dll',
    'user32.dll', 'advapi32.dll', 'shell32.dll', 'shlwapi.dll', 'ole32.dll',
    'oleaut32.dll', 'rpcrt4.dll', 'bcrypt.dll', 'crypt32.dll', 'sechost.dll',
    'combase.dll', 'version.dll', 'iphlpapi.dll', 'psapi.dll', 'dbghelp.dll',
    'gdi32.dll', 'setupapi.dll', 'winhttp.dll', 'wininet.dll', 'secur32.dll',
    'powrprof.dll', 'cfgmgr32.dll', 'ncrypt.dll', 'cryptbase.dll', 'imm32.dll',
    'dnsapi.dll', 'wldap32.dll', 'winmm.dll', 'mswsock.dll', 'opengl32.dll'
)

function Read-PE([string]$file) {
    $b = [System.IO.File]::ReadAllBytes($file)
    if ($b.Length -lt 0x40 -or [BitConverter]::ToUInt16($b, 0) -ne 0x5A4D) {
        throw "${file}: not a PE file"
    }
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    if ([BitConverter]::ToUInt32($b, $pe) -ne 0x00004550) { throw "${file}: bad PE signature" }

    $machine = [BitConverter]::ToUInt16($b, $pe + 4)
    $numSections = [BitConverter]::ToUInt16($b, $pe + 6)
    $sizeOpt = [BitConverter]::ToUInt16($b, $pe + 20)
    $magic = [BitConverter]::ToUInt16($b, $pe + 24)
    $is64 = ($magic -eq 0x20b)

    $optStart = $pe + 24
    $ddStart = if ($is64) { $optStart + 112 } else { $optStart + 96 }
    $importRVA = [BitConverter]::ToUInt32($b, $ddStart + 8)

    $secStart = $pe + 24 + $sizeOpt
    $sections = @()
    for ($i = 0; $i -lt $numSections; $i++) {
        $o = $secStart + $i * 40
        $sections += [pscustomobject]@{
            Name    = [System.Text.Encoding]::ASCII.GetString($b, $o, 8).Trim([char]0)
            VA      = [BitConverter]::ToUInt32($b, $o + 12)
            VSize   = [BitConverter]::ToUInt32($b, $o + 8)
            RawPtr  = [BitConverter]::ToUInt32($b, $o + 20)
            RawSize = [BitConverter]::ToUInt32($b, $o + 16)
        }
    }

    function Convert-Rva([uint32]$rva) {
        foreach ($s in $sections) {
            $span = [Math]::Max($s.VSize, $s.RawSize)
            if ($rva -ge $s.VA -and $rva -lt ($s.VA + $span)) {
                return [int]($s.RawPtr + ($rva - $s.VA))
            }
        }
        return -1
    }

    $imports = @()
    if ($importRVA -ne 0) {
        $off = Convert-Rva $importRVA
        if ($off -ge 0) {
            while ($true) {
                $nameRva = [BitConverter]::ToUInt32($b, $off + 12)
                if ($nameRva -eq 0) { break }
                $nOff = Convert-Rva $nameRva
                if ($nOff -lt 0) { break }
                $sb = New-Object System.Text.StringBuilder
                while ($b[$nOff] -ne 0) { [void]$sb.Append([char]$b[$nOff]); $nOff++ }
                $imports += $sb.ToString().ToLower()
                $off += 20
            }
        }
    }

    $machineName = switch ($machine) {
        0x014C { 'x86' }
        0x8664 { 'x64' }
        0xAA64 { 'ARM64' }
        default { "0x{0:X4}" -f $machine }
    }

    [pscustomobject]@{
        Machine = $machineName
        Imports = $imports
    }
}

$pe = Read-PE $Path
$failures = @()

Write-Host ("{0}: machine={1}" -f $Path, $pe.Machine)

if ($ExpectMachine -and $pe.Machine -ne $ExpectMachine) {
    $failures += "expected machine $ExpectMachine but got $($pe.Machine)"
}

if ($RequireDir) {
    foreach ($dll in ($pe.Imports | Sort-Object -Unique)) {
        if ($dll -like 'api-ms-win-*' -or $dll -like 'ext-ms-win-*') { continue }
        if ($SystemDlls -contains $dll) { continue }
        if (-not (Test-Path (Join-Path $RequireDir $dll))) {
            $failures += "imported DLL missing from stage: $dll"
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host "FAIL: $_" }
    exit 1
}
Write-Host ("  imports: " + (($pe.Imports | Sort-Object -Unique) -join ', '))
exit 0
