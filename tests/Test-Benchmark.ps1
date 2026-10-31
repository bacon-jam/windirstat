#requires -Version 7.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Harness\Benchmark.ps1')

function Require {
    param([bool] $Condition, [string] $Message)
    if (-not $Condition) { throw $Message }
}

$baseline = @(1.0, 2.0, 4.0, 3.0, 7.0, 5.0, 2.0, 4.0)
$fast = Compare-BenchmarkPairs $baseline @($baseline | ForEach-Object { $_ * 0.8 })
Require ($fast.Classification -eq 'faster' -and [Math]::Abs($fast.ReductionPercent - 20) -lt 0.000001) 'Known 20% reduction'
$slow = Compare-BenchmarkPairs $baseline @($baseline | ForEach-Object { $_ * 1.2 })
Require ($slow.Classification -eq 'slower' -and $slow.Upper95Percent -lt -19.999) 'Known regression'
$same = Compare-BenchmarkPairs $baseline $baseline
Require ($same.Classification -eq 'equivalent' -and $same.ReductionPercent -eq 0) 'A/A comparison'
$noise = Compare-BenchmarkPairs @(1, 1, 1, 1, 1, 1, 1, 1) @(0.5, 2, 0.5, 2, 0.5, 2, 0.5, 2)
Require ($noise.Classification -eq 'inconclusive') 'Noisy results must not claim an improvement'
Require ((Get-BenchmarkMedian @(4, 1, 2, 3)) -eq 2.5) 'Even median'
Require ((Get-BenchmarkMedian @(7, 1, 3)) -eq 3) 'Odd median'
foreach ($bad in @(@(1, 2), @(1, 1, 1, 1, 1, 1, 1, 0), @(1, 1, 1, 1, 1, 1, 1, [double]::NaN))) {
    $rejected = $false
    try { Compare-BenchmarkPairs $baseline $bad | Out-Null } catch { $rejected = $true }
    Require $rejected 'Reject incomplete or invalid measurements'
}
$a = [pscustomobject]@{ Name = 'a'; Size = 3 }
$b = [pscustomobject]@{ Name = 'b'; Size = 5 }
Require ((Get-BenchmarkSignature @($a, $b)) -eq (Get-BenchmarkSignature @($b, $a))) 'Order independent reports'
Require ((Get-BenchmarkSignature @($a, $b)) -ne (Get-BenchmarkSignature @($a, $a, $b))) 'Duplicate detection'
Require ((Get-BenchmarkSignature @($a, $b)) -ne (Get-BenchmarkSignature @($a))) 'Missing record detection'
Write-Host 'PASS benchmark statistics and report comparison'
