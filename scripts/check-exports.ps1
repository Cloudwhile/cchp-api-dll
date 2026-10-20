param(
    [Parameter(Mandatory=$true)][string]$Dll,
    [string]$Header,
    [string]$Definition,
    [string]$ImportLibrary,
    [string]$Example,
    [ValidateSet('x64','x86')][string]$Architecture = 'x64',
    [string]$Dumpbin = 'dumpbin'
)
$ErrorActionPreference = 'Stop'
if (-not $Header) { $Header = Join-Path $PSScriptRoot '../include/cchp.h' }
if (-not $Definition) { $Definition = Join-Path $PSScriptRoot '../cchp-api-dll/CCHPNetwork.def' }
$headerText = Get-Content -LiteralPath $Header -Raw
$dllName = [IO.Path]::GetFileName($Dll)
$definitionText = Get-Content -LiteralPath $Definition -Raw
if ($definitionText -notmatch '(?m)^LIBRARY\s+(\S+)\s*$' -or $Matches[1] -ine $dllName) {
    throw 'Definition LIBRARY name must match the DLL filename'
}
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
if ($ImportLibrary) {
    $libraryHeaders = & $Dumpbin /nologo /headers $ImportLibrary
    if ($LASTEXITCODE) { throw 'Import library inspection failed' }
    $importedDlls = @($libraryHeaders | Where-Object { $_ -match '^\s*DLL name\s*:\s*(\S+)\s*$' } |
        ForEach-Object { $_.Trim() -replace '^DLL name\s*:\s*', '' } | Sort-Object -Unique)
    if ($importedDlls.Count -ne 1 -or $importedDlls[0] -ine $dllName) {
        throw 'Import library must reference the packaged DLL name'
    }
}
if ($Example) {
    $exampleDependencies = & $Dumpbin /nologo /dependents $Example
    if ($LASTEXITCODE) { throw 'Example dependency inspection failed' }
    $sdkDependencies = @($exampleDependencies | Where-Object { $_ -match '^\s+CCHPNetwork[\w.]*\.dll\s*$' } |
        ForEach-Object { $_.Trim() })
    if ($sdkDependencies.Count -ne 1 -or $sdkDependencies[0] -ine $dllName) {
        throw 'Example must reference the packaged DLL name'
    }
    $process = New-Object System.Diagnostics.Process
    try {
        $process.StartInfo.FileName = (Resolve-Path -LiteralPath $Example).Path
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardError = $true
        $process.Start() | Out-Null
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            throw 'Example usage check timed out'
        }
        $exampleOutput = $process.StandardError.ReadToEnd()
        if ($process.ExitCode -ne 2 -or $exampleOutput -notmatch 'Usage: cchp_status') {
            throw 'Example failed to load the DLL and print usage'
        }
    } finally { $process.Dispose() }
}
Write-Output "$Architecture : $($actual.Count) undecorated exports; dependencies: $($dllNames -join ', ')"
