param(
    [ValidateSet('x64','x86')][string[]]$Architecture = @('x64','x86'),
    [ValidateSet('Release','Analyze','Asan')][string]$Mode = 'Release',
    [switch]$Package,
    [switch]$SkipVisualStudio
)
$ErrorActionPreference = 'Stop'
$sdkRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Install Visual Studio 2026 C++ desktop tools and Windows SDK' }
$vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvarsall.bat'
$ninja = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$msbuild = Join-Path $vsRoot 'MSBuild/Current/Bin/MSBuild.exe'
if ($Package -and $Mode -ne 'Release') { throw 'Only Release builds can be packaged' }
Push-Location $sdkRoot
try {
    foreach ($arch in $Architecture) {
        $buildDir = "out/build/$arch-$($Mode.ToLowerInvariant())"
        $switches = '-DCCHP_ANALYZE=OFF -DCCHP_ASAN=OFF'
        if ($Mode -eq 'Analyze') { $switches = '-DCCHP_ANALYZE=ON -DCCHP_ASAN=OFF' }
        if ($Mode -eq 'Asan') { $switches = '-DCCHP_ANALYZE=OFF -DCCHP_ASAN=ON' }
        # All interpolated options are validated above or local VS/workspace paths.
        $command = 'chcp 65001 >nul && "{0}" {1} >nul && cmake -S . -B "{2}" -G Ninja -DCMAKE_MAKE_PROGRAM="{3}" -DCMAKE_BUILD_TYPE=Release {4} && cmake --build "{2}" --parallel && ctest --test-dir "{2}" --output-on-failure' -f $vcvars, $arch, $buildDir, $ninja, $switches
        & $env:ComSpec /d /c $command
        if ($LASTEXITCODE) { throw "$arch $Mode build or tests failed" }
        if ($Mode -eq 'Release') {
            $checkCommand = 'chcp 65001 >nul && "{0}" {1} >nul && powershell -NoProfile -ExecutionPolicy RemoteSigned -File scripts/check-exports.ps1 -Dll "{2}/CCHPNetwork.dll" -Architecture {1}' -f $vcvars, $arch, $buildDir
            & $env:ComSpec /d /c $checkCommand
            if ($LASTEXITCODE) { throw 'Export/dependency validation failed' }
            if (-not $SkipVisualStudio) {
                & $msbuild cchp-api-dll.slnx /m /p:Configuration=Release "/p:Platform=$arch" /v:minimal /nologo
                if ($LASTEXITCODE) { throw 'VS2026 solution build failed' }
            }
            if ($Package) {
                $destination = "dist/$arch"
                New-Item -ItemType Directory -Force -Path $destination | Out-Null
                foreach ($legacy in @('cchp.dll','cchp.lib','cchp.def')) {
                    $legacyPath = Join-Path $destination $legacy
                    if (Test-Path -LiteralPath $legacyPath) { Remove-Item -LiteralPath $legacyPath -Force }
                }
                foreach ($file in @('CCHPNetwork.dll','CCHPNetwork.lib','CCHPNetwork.def','cchp_status.exe')) {
                    Copy-Item -LiteralPath "$buildDir/$file" -Destination $destination -Force
                }
                Copy-Item -LiteralPath include/cchp.h,README.md -Destination $destination -Force
                Copy-Item -LiteralPath examples/status.c,examples/e-language-x86.txt -Destination $destination -Force
                Copy-Item -LiteralPath LICENSE.txt,third_party/nlohmann/LICENSE.MIT -Destination $destination -Force
                New-Item -ItemType Directory -Force -Path "$destination/docs" | Out-Null
                Copy-Item -LiteralPath docs/smoke-matrix.md -Destination "$destination/docs" -Force
                $packageRoot = (Resolve-Path -LiteralPath $destination).Path
                $checksums = Get-ChildItem -LiteralPath $destination -File -Recurse | Where-Object Name -ne SHA256SUMS.txt |
                    Sort-Object FullName | ForEach-Object {
                        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' +
                        $_.FullName.Substring($packageRoot.Length + 1).Replace('\','/')
                    }
                $checksums | Set-Content -LiteralPath "$destination/SHA256SUMS.txt" -Encoding ascii
                Compress-Archive -Path "$destination/*" -DestinationPath "dist/CCHPNetwork-$arch.zip" -Force
            }
        }
    }
} finally { Pop-Location }
