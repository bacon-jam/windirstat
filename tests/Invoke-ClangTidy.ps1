#requires -Version 7.0
<#
.SYNOPSIS
    Run clang-tidy with the evaluated WinDirStat MSBuild configuration.
.DESCRIPTION
    Requires MSBuild 17.8 or later, the Windows SDK, MSVC/ATL headers and clang-tidy.
    Generates resources and compile_commands.json without building the application.
    MSVC PCH binaries cannot be read by Clang; headers are parsed normally instead.
    All project C++ files are checked by default, including LegacyOsCompat.cpp.
    Each file has a log and YAML diagnostics; summary.json records coverage and exit codes.
    Parse errors always fail. Use -WarningsAsErrors to also fail on enabled tidy checks.
.EXAMPLE
    pwsh -File tests/Invoke-ClangTidy.ps1 -PlatformToolset v143
.EXAMPLE
    pwsh -File tests/Invoke-ClangTidy.ps1 -PlatformToolset v143 -Source '*Finder*.cpp','Filtering.cpp'
#>
param(
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    [string] $PlatformToolset,
    [string] $WindowsTargetPlatformVersion,
    [string] $ClangTidy,
    [string[]] $Source = @('*.cpp'),
    [ValidateRange(1, 32)] [int] $Jobs = 4,
    [string] $OutputFolder,
    [switch] $GenerateOnly,
    [switch] $WarningsAsErrors
)

$ErrorActionPreference = 'Stop'
$Source = @($Source | ForEach-Object { $_ -split ',' })
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectDirectory = Join-Path $repoRoot 'windirstat'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $installation) { throw 'Visual Studio with the C++ tools is required.' }
$builder = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
if (-not $ClangTidy) { $ClangTidy = Join-Path $installation 'VC\Tools\Llvm\x64\bin\clang-tidy.exe' }
$ClangTidy = (Get-Item -LiteralPath $ClangTidy).FullName
$compiler = Join-Path ([IO.Path]::GetDirectoryName($ClangTidy)) 'clang-cl.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'clang-cl.exe must accompany clang-tidy.exe.' }
if (-not $OutputFolder) { $OutputFolder = Join-Path $repoRoot 'intermediate\clang-tidy' }
$OutputFolder = [IO.Path]::GetFullPath($OutputFolder)
[IO.Directory]::CreateDirectory($OutputFolder) | Out-Null

$parameters = @("/p:Configuration=$Configuration", '/p:Platform=x64', "/p:SolutionDir=$repoRoot\", '/nologo', '/v:quiet')
if ($PlatformToolset) { $parameters += "/p:PlatformToolset=$PlatformToolset" }
if ($WindowsTargetPlatformVersion) { $parameters += "/p:WindowsTargetPlatformVersion=$WindowsTargetPlatformVersion" }
# Initial targets add the Git definitions. PrepareForBuild resolves SDK paths and xxHash.
$metadata = & $builder (Join-Path $projectDirectory 'windirstat.vcxproj') @parameters `
    /t:PrepareForBuild /getItem:ClCompile /getProperty:IncludePath,PlatformToolset,VCToolsVersion,WindowsTargetPlatformVersion
if ($LASTEXITCODE -ne 0) { throw 'MSBuild project evaluation failed.' }
$metadata | Set-Content -LiteralPath (Join-Path $OutputFolder 'project.json') -Encoding utf8
$project = ($metadata -join "`n") | ConvertFrom-Json
& (Join-Path $projectDirectory 'Build\Compress Strings.ps1') -Path (Join-Path $projectDirectory 'res')

$database = foreach ($item in $project.Items.ClCompile) {
    if ($item.ExcludedFromBuild -eq 'true' -or [IO.Path]::GetExtension($item.FullPath) -ne '.cpp') { continue }
    $arguments = [Collections.Generic.List[string]]::new()
    $arguments.AddRange([string[]]@($compiler, '--target=x86_64-pc-windows-msvc', '/c', $item.FullPath))
    $standard = switch ($item.LanguageStandard) {
        stdcpp14 { '/std:c++14' }; stdcpp17 { '/std:c++17' }; stdcpp20 { '/std:c++20' }
        stdcpp23 { '/clang:-std=c++23' }; stdcpplatest { '/std:c++latest' }
        default { throw "Unsupported C++ standard: $($item.LanguageStandard)" }
    }
    $arguments.Add($standard)
    $runtime = switch ($item.RuntimeLibrary) {
        MultiThreaded { '/MT' }; MultiThreadedDebug { '/MTd' }
        MultiThreadedDLL { '/MD' }; MultiThreadedDebugDLL { '/MDd' }
        default { throw "Unsupported runtime: $($item.RuntimeLibrary)" }
    }
    $arguments.Add($runtime)
    if ($item.ExceptionHandling -eq 'Sync') { $arguments.Add('/EHsc') }
    elseif ($item.ExceptionHandling -eq 'Async') { $arguments.Add('/EHa') }
    elseif ($item.ExceptionHandling -eq 'SyncCThrow') { $arguments.Add('/EHs') }
    if ($item.RuntimeTypeInfo -eq 'false') { $arguments.Add('/GR-') }
    if ($item.ConformanceMode -eq 'true') { $arguments.Add('/permissive-') }
    if ($item.TreatWChar_tAsBuiltInType -eq 'false') { $arguments.Add('/Zc:wchar_t-') }
    if ($item.FloatingPointModel -eq 'Fast') { $arguments.Add('/fp:fast') }
    foreach ($define in $item.PreprocessorDefinitions.Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
        $arguments.Add('/D' + $define)
    }
    foreach ($include in $item.AdditionalIncludeDirectories.Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
        $arguments.Add('/I' + $include)
    }
    foreach ($include in $project.Properties.IncludePath.Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
        $arguments.Add('/imsvc' + $include)
    }
    foreach ($include in ([string]$item.ForcedIncludeFiles).Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
        $arguments.Add('/FI' + $include)
    }
    # Current project options have no quoted values. Fail rather than silently split a new one incorrectly.
    if ($item.AdditionalOptions -match '["'']') { throw 'Quoted AdditionalOptions need explicit argument parsing.' }
    foreach ($option in ($item.AdditionalOptions.Trim() -split '\s+')) {
        if ($option) { $arguments.Add($option) }
    }
    [ordered]@{ directory = $projectDirectory; file = $item.FullPath; arguments = $arguments.ToArray() }
}
ConvertTo-Json -InputObject @($database) -Depth 5 |
    Set-Content -LiteralPath (Join-Path $OutputFolder 'compile_commands.json') -Encoding utf8
if ($GenerateOnly) { Write-Host "Compilation database: $OutputFolder"; exit 0 }

$selected = @($database | Where-Object {
    $relative = [IO.Path]::GetRelativePath($projectDirectory, $_.file)
    @($Source | Where-Object { $relative -like $_ }).Count -gt 0
})
if (-not $selected.Count) { throw 'No project C++ files match -Source.' }
$version = & $ClangTidy --version
if ($LASTEXITCODE -ne 0) { throw 'Cannot run clang-tidy.' }
$config = Join-Path $OutputFolder 'clang-tidy.yml'
Copy-Item -LiteralPath (Join-Path $repoRoot '.clang-tidy') -Destination $config
& $ClangTidy "--config-file=$config" --verify-config > (Join-Path $OutputFolder 'config-check.log') 2>&1
if ($LASTEXITCODE -ne 0) { throw 'clang-tidy configuration is invalid; see config-check.log.' }
$watch = [Diagnostics.Stopwatch]::StartNew()
$results = @($selected | ForEach-Object -Parallel {
    $entry = $_
    $relative = [IO.Path]::GetRelativePath($using:projectDirectory, $entry.file)
    $name = $relative -replace '[/\\]', '_'
    $log = Join-Path $using:OutputFolder "$name.log"
    $fixes = Join-Path $using:OutputFolder "$name.yaml"
    if (Test-Path -LiteralPath $fixes) { Remove-Item -LiteralPath $fixes }
    $parameters = @($entry.file, '-p', $using:OutputFolder, "--export-fixes=$fixes", "--config-file=$using:config")
    if ($using:WarningsAsErrors) { $parameters += '--warnings-as-errors=*' }
    & $using:ClangTidy @parameters > $log 2>&1
    $code = $LASTEXITCODE
    Write-Host "$relative : exit $code"
    [pscustomobject]@{ File = $relative; ExitCode = $code; Log = $log; Diagnostics = $fixes }
} -ThrottleLimit $Jobs)
$failed = $selected.Count - @($results | Where-Object ExitCode -eq 0).Count
[ordered]@{
    Tool = $ClangTidy; Version = ($version -join "`n"); Configuration = $Configuration
    Project = $project.Properties; Selected = $selected.Count; Total = @($database).Count
    ConfigSha256 = (Get-FileHash -LiteralPath $config).Hash
    Completed = $results.Count; Failed = $failed; Seconds = $watch.Elapsed.TotalSeconds
    WarningsAsErrors = $WarningsAsErrors.IsPresent; Files = @($results | Sort-Object File)
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputFolder 'summary.json') -Encoding utf8
Write-Host "Checked $($results.Count)/$(@($database).Count) C++ files; $failed failed or incomplete. Reports: $OutputFolder"
if ($failed) { exit 1 }
