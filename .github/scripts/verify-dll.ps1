<#
.SYNOPSIS
  Checks that a built DLL is 32-bit x86 and exports exactly the names in its .def file.

.DESCRIPTION
  MicroMill is a 32-bit process and binds these entry points with GetProcAddress
  by undecorated name. A 64-bit build, a leaked __stdcall decoration (_Name@8),
  or a missing export all link cleanly and only fail once loaded inside MicroMill.

.EXAMPLE
  .github/scripts/verify-dll.ps1 -Dll src/build/PXC200_Support.dll -Def src/pxcsupport.def
#>
param(
    [Parameter(Mandatory)] [string] $Dll,
    [Parameter(Mandatory)] [string] $Def
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-DefExports([string] $Path) {
    $inExports = $false
    foreach ($line in Get-Content $Path) {
        $line = ($line -replace ';.*$', '').Trim()
        if (-not $line) { continue }
        if ($line -match '^EXPORTS\b') { $inExports = $true; continue }
        if ($line -match '^(LIBRARY|NAME|SECTIONS|STACKSIZE|HEAPSIZE|VERSION)\b') { $inExports = $false; continue }
        if ($inExports) { ($line -split '[\s=@]')[0] }
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$dumpbin = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe not found; is the MSVC toolchain installed?' }

$headers = & $dumpbin /nologo /headers $Dll
if ($LASTEXITCODE) { throw "dumpbin /headers failed with exit code $LASTEXITCODE" }
$machine = $headers | Select-String -Pattern '^\s*[0-9A-F]+ machine \((.+)\)' | Select-Object -First 1
if (-not $machine -or $machine.Matches[0].Groups[1].Value -ne 'x86') {
    throw "$Dll is not an x86 image: $($machine.Line.Trim())"
}

$dumped = & $dumpbin /nologo /exports $Dll
if ($LASTEXITCODE) { throw "dumpbin /exports failed with exit code $LASTEXITCODE" }
# Export rows look like: "  1    0 00001000 PXC_DoGrab" (ordinal, hint, RVA, name).
$actual = @($dumped | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') { $Matches[1] }
}) | Sort-Object
$expected = @(Get-DefExports $Def) | Sort-Object

if (-not $expected) { throw "No exports found in $Def" }
$diff = Compare-Object -ReferenceObject $expected -DifferenceObject $actual
if ($diff) {
    $missing = @($diff | Where-Object SideIndicator -EQ '<=' | ForEach-Object InputObject)
    $extra = @($diff | Where-Object SideIndicator -EQ '=>' | ForEach-Object InputObject)
    if ($missing) { Write-Host "::error file=$Def::Missing exports: $($missing -join ', ')" }
    if ($extra) { Write-Host "::error file=$Def::Unexpected exports: $($extra -join ', ')" }
    exit 1
}

Write-Host "$Dll OK: x86, $($actual.Count) exports match $Def"
