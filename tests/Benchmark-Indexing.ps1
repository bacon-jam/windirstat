#requires -Version 7.0
<#
.SYNOPSIS
    Build and compare a Git baseline with the current working tree using paired trials.
.DESCRIPTION
    Uses the same compiler, scanner harness, settings and input files for both builds.
    Builds source snapshots without changing the checkout. All timings are repeated,
    warm runs; no cache eviction, reboot, elevation or raw volume access is performed.
    Scanner timings cover LoadRoot only. Application timings cover process launch,
    folder scanning, tree construction, report export and exit (not GUI rendering).

    Every measured scan is verified. Samples, source manifests, hashes, settings,
    commands, logs and a statistical summary are retained in a new output directory.
    Default: 12 pairs, 2 warmup pairs, 1 GiB MFT, 12000-file tree and duplicate corpora.
    Requires MSBuild 17.8+, MSVC with ATL, Windows SDK and PowerShell 7.
    Run on an idle machine, outside a synchronized folder for steadier results.

    Exit 1: build, correctness or execution failure. Exit 2: requested performance
    gate failed. A completed experiment without a gate may report regressions.
.EXAMPLE
    pwsh -File tests/Benchmark-Indexing.ps1 -BaselineRef 324dc5b -PlatformToolset v143
.EXAMPLE
    pwsh -File tests/Benchmark-Indexing.ps1 -BaselineRef 324dc5b -PlatformToolset v143 `
        -Lane Scanner -RequireImprovement scanner-contiguous -FailOnRegression
#>
param(
    [Parameter(Mandatory)] [string] $BaselineRef,
    [ValidateSet('All', 'Scanner', 'Application', 'Duplicates')] [string] $Lane = 'All',
    [string] $PlatformToolset,
    [string] $WindowsTargetPlatformVersion,
    [ValidateRange(1024, 4194304)] [ValidateScript({ $_ % 1024 -eq 0 })] [int] $RecordCount = 1048576,
    [ValidateRange(100, 100000)] [int] $FileCount = 12000,
    [ValidateRange(6, 100)] [int] $Pairs = 12,
    [ValidateRange(1, 10)] [int] $Warmups = 2,
    [ValidateRange(1, 32)] [int] $Threads = 4,
    [ValidateSet(2, 5)] [int] $HashAlgorithm = 5,
    [ValidateRange(0.1, 50)] [double] $ThresholdPercent = 3,
    [ValidateRange(10, 3600)] [int] $TimeoutSeconds = 180,
    [int] $Seed = 1729,
    [string] $OutputFolder,
    [string[]] $RequireImprovement = @(),
    [switch] $FailOnRegression
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'Harness\Benchmark.ps1')
. (Join-Path $PSScriptRoot 'Harness\Fixtures.ps1')

function Invoke-Git {
    param([string[]] $Arguments)
    $result = & git -C $repoRoot -c core.quotepath=false @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git failed: $($Arguments -join ' ')" }
    return $result
}

function Write-Json {
    param([string] $Path, [object] $Value)
    ConvertTo-Json -InputObject $Value -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-SourceManifest {
    param([string] $Root)
    return @(Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{ Path = [IO.Path]::GetRelativePath($Root, $_.FullName); Sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
}

function Invoke-Build {
    param([string] $Project, [string[]] $Properties, [string] $Log)
    & $builder $Project /m:4 /t:Rebuild /nologo /v:minimal @buildParameters @Properties > $Log 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Build failed; see $Log" }
}

function Invoke-MeasuredProcess {
    param([string] $Exe, [string[]] $Arguments, [string] $Log)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Exe
    $info.WorkingDirectory = [IO.Path]::GetDirectoryName($Exe)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = [Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $memory = $null
        while (-not $process.WaitForExit(20)) {
            $process.Refresh()
            $peak = $process.PeakWorkingSet64
            if ($null -ne $peak) { $memory = [Math]::Max([long]$memory, $peak) }
            if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $process.Kill($true)
                $process.WaitForExit()
                throw "Benchmark timed out: $Exe"
            }
        }
        $watch.Stop()
        $result = [ordered]@{
            Exe = $Exe; Arguments = $Arguments; ExitCode = $process.ExitCode
            Seconds = $watch.Elapsed.TotalSeconds; CpuSeconds = $process.TotalProcessorTime.TotalSeconds
            ObservedPeakWorkingSetBytes = $memory
            Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult()
        }
        Write-Json $Log $result
        if ($result.ExitCode -ne 0) { throw "Benchmark process failed; see $Log" }
        return $result
    } finally { $process.Dispose() }
}

function Assert-ApplicationReport {
    param([object[]] $Rows)
    if ($Rows.Count -ne $expected.Count) { throw 'Application report has missing or extra entries.' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Rows) {
        $path = $row.Name.TrimEnd('\')
        if (-not $expected.ContainsKey($path) -or -not $seen.Add($path)) { throw "Unexpected/duplicate path: $path" }
        $entry = $expected[$path]
        $type = [Convert]::ToUInt32($row.'WinDirStat Attributes', 16) -band 15
        if ($type -ne $entry.Type -or [long]$row.'Logical Size' -ne $entry.Bytes -or
            [long]$row.Files -ne $entry.Files -or [long]$row.Folders -ne $entry.Folders) {
            throw "Incorrect type, size or recursive counts: $path"
        }
    }
}

$baselineCommit = Invoke-Git @('rev-parse', '--verify', "$BaselineRef^{commit}")
$candidateCommit = Invoke-Git @('rev-parse', 'HEAD')
$workingFiles = @(Invoke-Git @('ls-files', '--cached', '--others', '--exclude-standard'))
if (-not $OutputFolder) {
    $OutputFolder = Join-Path $repoRoot ('intermediate\benchmark-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputFolder = [IO.Path]::GetFullPath($OutputFolder)
if (Test-Path -LiteralPath $OutputFolder) { throw 'Use a new output directory; previous evidence is never overwritten.' }
[IO.Directory]::CreateDirectory($OutputFolder) | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ tools are required.' }
$builder = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
$buildParameters = @('/p:Configuration=Release', '/p:Platform=x64')
if ($PlatformToolset) { $buildParameters += "/p:PlatformToolset=$PlatformToolset" }
if ($WindowsTargetPlatformVersion) { $buildParameters += "/p:WindowsTargetPlatformVersion=$WindowsTargetPlatformVersion" }

$cases = @()
if ($Lane -in 'All', 'Scanner') {
    $cases += @(
        @{ Id = 'scanner-contiguous'; Fragments = 1; Threads = $Threads; Device = 'ssd' }
        @{ Id = 'scanner-fragmented-2'; Fragments = 2; Threads = $Threads; Device = 'ssd' }
        @{ Id = 'scanner-fragmented-3'; Fragments = 3; Threads = $Threads; Device = 'ssd' }
        @{ Id = 'scanner-fragmented-37'; Fragments = 37; Threads = $Threads; Device = 'ssd' }
        @{ Id = 'scanner-skewed-2'; Fragments = 2; Threads = $Threads; Device = 'ssd'; Layout = 'skewed' }
        @{ Id = 'scanner-skewed-3'; Fragments = 3; Threads = $Threads; Device = 'ssd'; Layout = 'skewed' }
        @{ Id = 'scanner-small-fragmented'; Fragments = 37; Threads = $Threads; Device = 'ssd'; Records = 1024 }
        @{ Id = 'scanner-single-worker'; Fragments = 1; Threads = 1; Device = 'ssd' }
        @{ Id = 'scanner-seek-penalty'; Fragments = 3; Threads = $Threads; Device = 'hdd' }
        @{ Id = 'scanner-unknown-device'; Fragments = 3; Threads = $Threads; Device = 'unknown' }
    )
}
if ($Lane -in 'All', 'Application') { $cases += @{ Id = 'application-folder' } }
if ($Lane -in 'All', 'Duplicates') {
    $cases += @(
        @{ Id = 'duplicates-sizes' }
        @{ Id = 'duplicates-flat'; Bytes = 8193; Count = [Math]::Min($FileCount, 16384); Flat = $true }
        @{ Id = 'duplicates-nested'; Bytes = 8193; Count = [Math]::Min($FileCount, 16384) }
        @{ Id = 'duplicates-unique-sizes'; Bytes = 0; Count = [Math]::Min($FileCount, 16384); Flat = $true }
        @{ Id = 'duplicates-identical'; Bytes = 4096; Count = [Math]::Min($FileCount, 16384) }
        @{ Id = 'duplicates-distinct'; Bytes = 4096; Count = [Math]::Min($FileCount, 16384) }
        @{ Id = 'duplicates-prefixes'; Bytes = 2MB; Count = [Math]::Min(128, [Math]::Max(8, 2 * [int]($FileCount / 256))) }
    )
    foreach ($size in 16KB, 32KB, 64KB) {
        foreach ($kind in 'identical', 'distinct') {
            $cases += @{ Id = "duplicates-$kind-$($size / 1KB)k"; Bytes = $size; Count = [Math]::Min($FileCount, 8192) }
        }
    }
}
foreach ($case in $cases) {
    if (-not $case.ContainsKey('Fragments')) { continue }
    if (-not $case.ContainsKey('Layout')) { $case.Layout = 'balanced' }
    if (-not $case.ContainsKey('Records')) { $case.Records = $RecordCount }
}
foreach ($id in $RequireImprovement) {
    if ($id -notin $cases.Id) { throw "Unknown/unselected improvement gate: $id" }
}
$metadata = [ordered]@{
    StartedUtc = [DateTime]::UtcNow.ToString('o'); BaselineCommit = $baselineCommit; CandidateCommit = $candidateCommit
    CandidateStatus = @(Invoke-Git @('status', '--porcelain')); Parameters = $PSBoundParameters
    RecordCount = $RecordCount; FileCount = $FileCount; Pairs = $Pairs; Warmups = $Warmups; Threads = $Threads
    Seed = $Seed; HashAlgorithm = $HashAlgorithm; ThresholdPercent = $ThresholdPercent; Cases = $cases
    OS = [Environment]::OSVersion.VersionString; PowerShell = $PSVersionTable.PSVersion.ToString()
    Processor = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
    MemoryBytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
    Disks = @(Get-CimInstance Win32_DiskDrive | Select-Object Model, MediaType, InterfaceType, Size)
    PowerScheme = (& powercfg /getactivescheme) -join "`n"
    MSBuild = $builder; BuildParameters = $buildParameters; Scope = 'Warm repeated runs; no cache eviction'
}
Write-Json (Join-Path $OutputFolder 'metadata.json') $metadata
Invoke-Git @('diff', '--binary', 'HEAD') | Set-Content -LiteralPath (Join-Path $OutputFolder 'candidate.patch') -Encoding utf8
$MyInvocation.MyCommand.ScriptBlock.ToString() |
    Set-Content -LiteralPath (Join-Path $OutputFolder 'benchmark-script.ps1') -Encoding utf8

try {
    $harness = Join-Path $OutputFolder 'harness'
    [IO.Directory]::CreateDirectory((Join-Path $harness 'Fixtures')) | Out-Null
    foreach ($file in 'Fixtures\Scanner.cpp', 'Fixtures\ScannerPch.h', 'ScannerTests.vcxproj', 'Harness\Benchmark.ps1') {
        $destination = Join-Path $harness $file
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $destination
    }
    Write-Json (Join-Path $OutputFolder 'harness.json') (Get-SourceManifest $harness)
    $binaries = @{}
    foreach ($version in 'baseline', 'candidate') {
        Write-Host "Building $version with identical Release x64 settings..."
        $source = Join-Path $OutputFolder "sources\$version"
        $archive = Join-Path $OutputFolder "$version.zip"
        $commit = if ($version -eq 'baseline') { $baselineCommit } else { $candidateCommit }
        if ($version -eq 'baseline') {
            Invoke-Git @('archive', '--format=zip', "--output=$archive", $commit)
            Expand-Archive -LiteralPath $archive -DestinationPath $source
        } else {
            [IO.Directory]::CreateDirectory($source) | Out-Null
            foreach ($relative in $workingFiles) {
                $original = Join-Path $repoRoot $relative
                $destination = Join-Path $source $relative
                if (Test-Path -LiteralPath $original -PathType Leaf) {
                    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
                    Copy-Item -LiteralPath $original -Destination $destination
                }
            }
        }
        $output = Join-Path $OutputFolder "binaries\$version"
        [IO.Directory]::CreateDirectory($output) | Out-Null
        $binaries[$version] = @{ Directory = $output }
        if ($Lane -ne 'Scanner') {
            # Both builds use exactly the local dependency; never independently fetch "latest".
            $dependency = Join-Path $source 'contrib\xxhash'
            [IO.Directory]::CreateDirectory($dependency) | Out-Null
            foreach ($file in 'xxhash.c', 'xxhash.h') {
                $original = Join-Path $repoRoot "contrib\xxhash\$file"
                if (-not (Test-Path -LiteralPath $original)) { throw 'Build WinDirStat once to obtain xxHash before benchmarking.' }
                Copy-Item -LiteralPath $original -Destination $dependency
            }
            $date = Invoke-Git @('show', '-s', '--format=%cs', $commit)
            $count = Invoke-Git @('rev-list', '--count', $commit)
            Invoke-Build (Join-Path $source 'windirstat.sln') @(
                "/p:OutDir=$output\", "/p:IntDir=$output\app-obj\", "/p:GitRevision=$commit", "/p:GitDate=$date", "/p:GitCount=$count"
                '/p:GitExecutableFullPath='
            ) (Join-Path $output 'application-build.log')
            $exe = Join-Path $output 'WinDirStat-portable.exe'
            Copy-Item -LiteralPath (Join-Path $output 'WinDirStat_x64.exe') -Destination $exe
            $runner = [pscustomobject]@{ Exe = $exe; Directory = $output; Ini = [IO.Path]::ChangeExtension($exe, 'ini') }
            Set-TestSettings $runner @{ Options = @{
                ScanningThreads = $Threads; FileHashAlgorithm = $HashAlgorithm; SampleLargeFiles = 0
            } }
            Copy-Item -LiteralPath $runner.Ini -Destination (Join-Path $OutputFolder "$version-settings.ini")
            $binaries[$version].Application = $runner
        }
        if ($Lane -in 'All', 'Scanner') {
            Invoke-Build (Join-Path $harness 'ScannerTests.vcxproj') @(
                "/p:ScannerDirectory=$source\windirstat", "/p:OutDir=$output\scanner\"
            ) (Join-Path $output 'scanner-build.log')
            $binaries[$version].Scanner = Join-Path $output 'scanner\ScannerTests.exe'
        }
        Write-Json (Join-Path $OutputFolder "$version-sources.json") (Get-SourceManifest $source)
        Write-Json (Join-Path $OutputFolder "$version-binaries.json") @(
            Get-ChildItem -LiteralPath $output -Filter '*.exe' -Recurse | ForEach-Object {
                [ordered]@{ Path = $_.FullName; Sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
            }
        )
    }
    $toolchain = & $builder (Join-Path $harness 'ScannerTests.vcxproj') @buildParameters /nologo /v:quiet `
        /getProperty:PlatformToolset,VCToolsVersion,WindowsTargetPlatformVersion
    if ($LASTEXITCODE -ne 0) { throw 'Could not identify the evaluated compiler and SDK.' }
    $toolchain | Set-Content -LiteralPath (Join-Path $OutputFolder 'toolchain.json') -Encoding utf8

    $inputDirectory = Join-Path $OutputFolder 'inputs'
    $logDirectory = Join-Path $OutputFolder 'samples'
    [IO.Directory]::CreateDirectory($inputDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($logDirectory) | Out-Null
    if ($Lane -in 'All', 'Scanner') {
        $fixtures = @{}
        foreach ($case in $cases) {
            if (-not $case.ContainsKey('Fragments')) { continue }
            $key = "$($case.Records)-$($case.Fragments)-$($case.Layout)"
            if (-not $fixtures.ContainsKey($key)) {
                $fixture = Join-Path $inputDirectory "mft-$key.bin"
                Invoke-MeasuredProcess $binaries.candidate.Scanner @('--create', $fixture, $case.Records, $case.Fragments, $case.Layout) `
                    (Join-Path $logDirectory "create-$key.json") | Out-Null
                $fixtures[$key] = $fixture
            }
            $case.Fixture = $fixtures[$key]
        }
        Write-Json (Join-Path $OutputFolder 'inputs.json') (Get-SourceManifest $inputDirectory)
    }
    if ($Lane -ne 'Scanner') {
        $folder = Join-Path $inputDirectory 'tree'
        [IO.Directory]::CreateDirectory($folder) | Out-Null
        for ($i = 0; $i -lt $FileCount; ++$i) {
            $directory = Join-Path $folder ("branch-{0}\sub-{1}" -f [int][Math]::Floor($i / 128), ($i % 4))
            [IO.Directory]::CreateDirectory($directory) | Out-Null
            $file = Join-Path $directory "file-$i-é.dat"
            [IO.File]::WriteAllBytes($file, [byte[]]::new($i % 8193))
        }
        [IO.Directory]::CreateDirectory((Join-Path $folder 'empty')) | Out-Null
        $expected = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in @(Get-Item -LiteralPath $folder) + @(Get-ChildItem -LiteralPath $folder -Recurse)) {
            $expected[$entry.FullName] = [pscustomobject]@{
                Type = $(if ($entry.PSIsContainer) { 4 } else { 8 })
                Bytes = $(if ($entry.PSIsContainer) { 0L } else { $entry.Length }); Files = 0L; Folders = 0L
            }
        }
        foreach ($entry in @($expected.GetEnumerator())) {
            $parent = [IO.Path]::GetDirectoryName($entry.Key)
            while ($expected.ContainsKey($parent)) {
                if ($entry.Value.Type -eq 8) { $expected[$parent].Bytes += $entry.Value.Bytes; ++$expected[$parent].Files }
                else { ++$expected[$parent].Folders }
                $parent = [IO.Path]::GetDirectoryName($parent)
            }
        }
        Write-Json (Join-Path $OutputFolder 'tree.json') $expected
    }

    foreach ($case in $cases | Where-Object Id -Like 'duplicates-*') {
        $case.Folder = $folder
        if ($case.ContainsKey('Bytes')) {
            $case.Folder = Join-Path $inputDirectory $case.Id
            $payload = [byte[]]::new($case.Bytes)
            [Random]::new($Seed).NextBytes($payload)
            for ($i = 0; $i -lt $case.Count; ++$i) {
                if ($case.Id -eq 'duplicates-unique-sizes') { $payload = [byte[]]::new($i + 1) }
                if ($case.Id -like 'duplicates-distinct*') { [BitConverter]::GetBytes($i).CopyTo($payload, 0) }
                if ($case.Id -eq 'duplicates-prefixes') {
                    [BitConverter]::GetBytes([int][Math]::Floor($i / 2)).CopyTo($payload, (1MB + 211))
                }
                $directory = if ($case.ContainsKey('Flat')) { $case.Folder }
                    else { Join-Path $case.Folder ('branch-{0}' -f ($i % 64)) }
                [IO.Directory]::CreateDirectory($directory) | Out-Null
                [IO.File]::WriteAllBytes((Join-Path $directory "file-$i.bin"), $payload)
            }
        }
        # Independent whole-file SHA-256 oracle; input hashing is outside every timed process.
        $manifest = Get-SourceManifest $case.Folder
        Write-Json (Join-Path $OutputFolder "$($case.Id)-inputs.json") $manifest
        $case.Expected = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($group in $manifest | Group-Object { $_.Sha256 } | Where-Object Count -GT 1) {
            foreach ($entry in $group.Group) {
                $case.Expected[(Join-Path $case.Folder $entry.Path)] = $group.Name
            }
        }
    }

    $random = [Random]::new($Seed)
    $orders = @{}
    foreach ($case in $cases) { $orders[$case.Id] = $random.Next(2) }
    $samples = [Collections.Generic.List[object]]::new()
    $signatures = @{}
    for ($round = -$Warmups; $round -lt $Pairs; ++$round) {
        # Shuffle cases each round, and alternate AB/BA within each case to balance time drift.
        $order = @($cases)
        for ($i = $order.Count - 1; $i -gt 0; --$i) { $j = $random.Next($i + 1); $order[$i], $order[$j] = $order[$j], $order[$i] }
        foreach ($case in $order) {
            $versions = if (($round + $Warmups + $orders[$case.Id]) % 2) { @('candidate', 'baseline') } else { @('baseline', 'candidate') }
            foreach ($version in $versions) {
                $name = "$($case.Id)-$round-$version"
                $log = Join-Path $logDirectory "$name.json"
                if (-not $case.ContainsKey('Fragments')) {
                    $report = Join-Path $logDirectory "$name.csv"
                    Copy-Item -LiteralPath (Join-Path $OutputFolder "$version-settings.ini") -Destination $binaries[$version].Application.Ini
                    $arguments = if ($case.Id -eq 'application-folder') { @('/saveto', $report, $folder) }
                        else { @('/savedupesto', $report, $case.Folder) }
                    $result = Invoke-MeasuredProcess $binaries[$version].Application.Exe $arguments $log
                    $rows = @(Import-Csv -LiteralPath $report -Encoding utf8)
                    if ($case.Id -eq 'application-folder') { Assert-ApplicationReport $rows }
                    else {
                        if ($rows.Count -ne $case.Expected.Count) { throw "Incorrect duplicate count: $name" }
                        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
                        $groups = @{}; $hashes = @{}
                        foreach ($row in $rows) {
                            if (-not $case.Expected.ContainsKey($row.Name) -or -not $seen.Add($row.Name) -or
                                [string]::IsNullOrEmpty($row.'Hash Prefix') -or $row.'Sampled Hash' -ine 'false') {
                                throw "Incorrect duplicate membership or digest: $($row.Name)"
                            }
                            $hash = $case.Expected[$row.Name]
                            if (($HashAlgorithm -eq 2 -and $row.'Hash Prefix' -ine $hash.Substring(0, 32)) -or
                                ($groups.ContainsKey($hash) -and $groups[$hash] -ine $row.'Hash Prefix') -or
                                ($hashes.ContainsKey($row.'Hash Prefix') -and $hashes[$row.'Hash Prefix'] -ine $hash)) {
                                throw "Split, merged or incorrect duplicate group: $($row.Name)"
                            }
                            $groups[$hash] = $row.'Hash Prefix'; $hashes[$row.'Hash Prefix'] = $hash
                        }
                    }
                    $signature = Get-BenchmarkSignature $rows
                    $entries = $rows.Count
                    $seconds = $result.Seconds; $cpu = $result.CpuSeconds; $memory = $result.ObservedPeakWorkingSetBytes
                    $reads = $null; $workers = $null
                } else {
                    $result = Invoke-MeasuredProcess $binaries[$version].Scanner @(
                        '--measure', $case.Fixture, $case.Records, $case.Fragments, $case.Threads, $case.Device, $case.Layout
                    ) $log
                    $measurement = $result.Stdout | ConvertFrom-Json
                    if ($measurement.records -ne $case.Records -or $measurement.fragments -ne $case.Fragments -or
                        $measurement.threads -ne $case.Threads) { throw 'Scanner returned mismatched settings.' }
                    $entries = $measurement.entries; $signature = [string]$entries
                    $seconds = $measurement.seconds; $cpu = $measurement.cpu_seconds
                    $memory = $measurement.peak_working_set_bytes; $reads = $measurement.reads; $workers = $measurement.workers
                }
                if ($signatures.ContainsKey($case.Id) -and $signatures[$case.Id] -cne $signature) {
                    throw "Result mismatch in $name; timings cannot be compared."
                }
                $signatures[$case.Id] = $signature
                $sample = [pscustomobject]@{
                    Case = $case.Id; Round = $round; Warmup = $round -lt 0; Version = $version
                    Order = [Array]::IndexOf($versions, $version); Seconds = $seconds; CpuSeconds = $cpu
                    PeakWorkingSetBytes = $memory; Entries = $entries; Reads = $reads; Workers = $workers; Signature = $signature
                }
                $samples.Add($sample)
                $sample | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $OutputFolder 'samples.jsonl') -Encoding utf8
                Write-Host ('{0} {1}: {2:F6}s ({3} entries)' -f $name, $(if ($round -lt 0) { 'warmup' } else { 'sample' }), $seconds, $entries)
            }
        }
    }
    $samples | Export-Csv -LiteralPath (Join-Path $OutputFolder 'samples.csv') -NoTypeInformation
    $summary = foreach ($case in $cases) {
        $measured = @($samples | Where-Object { $_.Case -eq $case.Id -and -not $_.Warmup })
        $baseline = @($measured | Where-Object Version -eq baseline | Sort-Object Round)
        $candidate = @($measured | Where-Object Version -eq candidate | Sort-Object Round)
        $comparison = Compare-BenchmarkPairs $baseline.Seconds $candidate.Seconds -ThresholdPercent $ThresholdPercent -Seed $Seed
        $comparison.Case = $case.Id
        $comparison.BaselineMedianCpuSeconds = Get-BenchmarkMedian $baseline.CpuSeconds
        $comparison.CandidateMedianCpuSeconds = Get-BenchmarkMedian $candidate.CpuSeconds
        $comparison.BaselineMedianPeakBytes = if ($null -notin $baseline.PeakWorkingSetBytes) {
            Get-BenchmarkMedian $baseline.PeakWorkingSetBytes
        } else { $null }
        $comparison.CandidateMedianPeakBytes = if ($null -notin $candidate.PeakWorkingSetBytes) {
            Get-BenchmarkMedian $candidate.PeakWorkingSetBytes
        } else { $null }
        [pscustomobject]$comparison
    }
    Write-Json (Join-Path $OutputFolder 'summary.json') @($summary)
    $lines = @(
        'Warm paired indexing benchmark', ''
        "Baseline: $baselineCommit", "Candidate HEAD: $candidateCommit (working tree snapshot)"
        "$Pairs pairs; $Warmups warmup pairs excluded; seed $Seed; practical threshold $ThresholdPercent%."
        '95% paired bootstrap intervals for geometric mean time reduction; positive means faster.'
        'Intervals describe this run and host, not cold scans or other machines. No samples discarded.'
        'Scanner: LoadRoot only, synthetic MFT with unbuffered file reads; device policy is simulated.'
        'Application: launch through folder scan/export/exit; ordinary traversal, no raw MFT or GUI timing.'
        "Duplicates: launch through full-file duplicate detection/export/exit; hash algorithm $HashAlgorithm; independently checked groups."
        ''
    )
    foreach ($entry in $summary) {
        $lines += '{0}: {1}; {2:F6}s -> {3:F6}s; reduction {4:F1}% [95% {5:F1}, {6:F1}]' -f `
            $entry.Case, $entry.Classification, $entry.BaselineMedianSeconds, $entry.CandidateMedianSeconds, `
            $entry.ReductionPercent, $entry.Lower95Percent, $entry.Upper95Percent
    }
    $lines | Set-Content -LiteralPath (Join-Path $OutputFolder 'summary.txt') -Encoding utf8
    $lines | ForEach-Object { Write-Host $_ }
    Write-Host "Evidence: $OutputFolder"
    if (@($summary | Where-Object {
        ($FailOnRegression -and $_.Classification -eq 'slower') -or
        ($_.Case -in $RequireImprovement -and $_.Classification -ne 'faster')
    }).Count) { exit 2 }
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $OutputFolder 'failure.txt') -Encoding utf8
    Write-Error $_
    exit 1
}
