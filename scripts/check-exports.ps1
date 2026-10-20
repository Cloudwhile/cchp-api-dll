param(
    [Parameter(Mandatory=$true)][string]$Dll,
    [string]$Header,
    [string]$Definition,
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [string]$Dumpbin = 'dumpbin'
)
$ErrorActionPreference = 'Stop'
if (-not $Header) { $Header = Join-Path $PSScriptRoot '../include/cchp.h' }
if (-not $Definition) { $Definition = Join-Path $PSScriptRoot '../cchp-api-dll/cchp.def' }
$headerText = Get-Content -LiteralPath $Header -Raw
$declared = @([regex]::Matches($headerText, 'CCHP_API\s+[^;\r\n]+?\s+CCHP_CALL\s+(cchp_\w+)\s*\(') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object)
$defined = @(Get-Content -LiteralPath $Definition | Where-Object { $_ -match '^\s+(cchp_\w+)\s*$' } |
    ForEach-Object { $_.Trim() } | Sort-Object)
if ($declared.Count -eq 0 -or (Compare-Object $declared $defined)) { throw 'Header and .def exports differ' }
$exports = & $Dumpbin /nologo /exports $Dll
if ($LASTEXITCODE) { throw 'dumpbin /exports failed' }
$actual = @($exports | Where-Object { $_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+\S+' } |
    ForEach-Object { ($_.Trim() -split '\s+')[3] } | Sort-Object)
if (Compare-Object $declared $actual) { throw 'PE exports must exactly match undecorated C header symbols' }
$headers = & $Dumpbin /nologo /headers $Dll
if ($LASTEXITCODE) { throw 'dumpbin /headers failed' }
$machine = if ($Architecture -eq 'x64') { '8664' } else { '14C' }
if (-not ($headers -match "\b$machine machine")) { throw "Incorrect PE architecture: $Architecture" }
$dependencies = & $Dumpbin /nologo /dependents $Dll
if ($LASTEXITCODE) { throw 'dumpbin /dependents failed' }
$allowed = @('KERNEL32.dll','WINHTTP.dll','RPCRT4.dll','ADVAPI32.dll','USER32.dll','CRYPT32.dll','WS2_32.dll','ntdll.dll')
$dllNames = @($dependencies | Where-Object { $_ -match '^\s+[\w.-]+\.dll\s*$' } | ForEach-Object { $_.Trim() })
if ($dllNames.Count -eq 0) { throw 'No PE dependencies found' }
foreach ($dependency in $dllNames) {
    if ($dependency -notin $allowed -and $dependency -notmatch '^api-ms-win-core-') {
        throw "Unexpected runtime dependency: $dependency"
    }
}
Write-Output "$Architecture : $($actual.Count) undecorated exports; dependencies: $($dllNames -join ', ')"
