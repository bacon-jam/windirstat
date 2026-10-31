<#
.SYNOPSIS
    Check and benchmark the production NTFS scanner without administrator access.
.DESCRIPTION
    Builds the scanner against a generated MFT fixture. Only volume discovery is
    substituted; the production parser uses real overlapped, unbuffered file reads.
    Checks names, parents, hardlinks, extension records, sizes, timestamps, read
    errors, pause/resume, cancellation, and fallback for HDD/unknown devices.

    Timings cover LoadRoot (read, parse, merge), excluding fixture generation and
    verification. These are repeated fixture reads, not cold-drive or full GUI scan
    measurements. Compare matching record counts, fragment counts and thread settings.
    The default fixture is 256 MiB; 1048576 records use 1 GiB of temporary disk space.
    Successful runs remove their generated fixture. Failed runs retain it for diagnosis.

    -ScannerDirectory can select a saved original FinderNtfs.cpp for a baseline.
    Use a different -OutputFolder for each version to keep their binaries and logs.
.EXAMPLE
    .\tests\Test-Scanner.ps1 -PlatformToolset v143 -RecordCount 1048576 -Repetitions 3
#>
param(
    [ValidateRange(1024, 4194304)]
    [ValidateScript({ $_ % 1024 -eq 0 })]
    [int] $RecordCount = 262144,
    [ValidateRange(1, 20)]
    [int] $Repetitions = 1,
    [string] $PlatformToolset,
    [string] $ScannerDirectory,
    [string] $OutputFolder
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$builder = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
if ($builder) {
    $builderPath = $builder.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    $builderPath = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
}
if (-not $OutputFolder) { $OutputFolder = Join-Path $repoRoot 'intermediate\scanner-tests' }
$OutputFolder = [IO.Path]::GetFullPath($OutputFolder)
[IO.Directory]::CreateDirectory($OutputFolder) | Out-Null
$parameters = @('/p:Configuration=Release', '/p:Platform=x64', "/p:OutDir=$OutputFolder\", '/v:minimal', '/nologo')
if ($PlatformToolset) { $parameters += "/p:PlatformToolset=$PlatformToolset" }
if ($ScannerDirectory) { $parameters += '/p:ScannerDirectory=' + [IO.Path]::GetFullPath($ScannerDirectory) }
& $builderPath (Join-Path $PSScriptRoot 'ScannerTests.vcxproj') @parameters
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$fixture = Join-Path $OutputFolder ('fixture-' + [guid]::NewGuid().ToString('N') + '.bin')
& (Join-Path $OutputFolder 'ScannerTests.exe') $fixture $RecordCount $Repetitions |
    Tee-Object -FilePath (Join-Path $OutputFolder 'results.txt')
exit $LASTEXITCODE
