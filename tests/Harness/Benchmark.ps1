function Get-BenchmarkMedian {
    param([double[]] $Values)
    $sorted = @($Values | Sort-Object)
    if (-not $sorted.Count) { throw 'A median requires samples.' }
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return $sorted[$middle] }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2
}

function Compare-BenchmarkPairs {
    param(
        [double[]] $Baseline, [double[]] $Candidate,
        [double] $ThresholdPercent = 3, [int] $Seed = 1729,
        [int] $Resamples = 10000
    )
    if ($Baseline.Count -ne $Candidate.Count -or $Baseline.Count -lt 6) {
        throw 'At least six complete baseline/candidate pairs are required.'
    }
    $ratios = for ($i = 0; $i -lt $Baseline.Count; ++$i) {
        if (-not [double]::IsFinite($Baseline[$i]) -or -not [double]::IsFinite($Candidate[$i]) -or
            $Baseline[$i] -le 0 -or $Candidate[$i] -le 0) { throw 'Timings must be finite and positive.' }
        [Math]::Log($Candidate[$i] / $Baseline[$i])
    }
    # Resample whole pairs. Positive percentages mean the candidate takes less time.
    $random = [Random]::new($Seed)
    $estimates = [double[]]::new($Resamples)
    for ($sample = 0; $sample -lt $Resamples; ++$sample) {
        $sum = 0.0
        for ($i = 0; $i -lt $ratios.Count; ++$i) { $sum += $ratios[$random.Next($ratios.Count)] }
        $estimates[$sample] = 100 * (1 - [Math]::Exp($sum / $ratios.Count))
    }
    [Array]::Sort($estimates)
    $low = $estimates[[int][Math]::Floor(0.025 * ($Resamples - 1))]
    $high = $estimates[[int][Math]::Ceiling(0.975 * ($Resamples - 1))]
    $classification = if ($low -gt $ThresholdPercent) { 'faster' }
        elseif ($high -lt -$ThresholdPercent) { 'slower' }
        elseif ($low -ge -$ThresholdPercent -and $high -le $ThresholdPercent) { 'equivalent' }
        else { 'inconclusive' }
    $baselineMedian = Get-BenchmarkMedian $Baseline
    $candidateMedian = Get-BenchmarkMedian $Candidate
    [ordered]@{
        Pairs = $Baseline.Count
        BaselineMedianSeconds = $baselineMedian; CandidateMedianSeconds = $candidateMedian
        BaselineMadSeconds = Get-BenchmarkMedian @($Baseline | ForEach-Object { [Math]::Abs($_ - $baselineMedian) })
        CandidateMadSeconds = Get-BenchmarkMedian @($Candidate | ForEach-Object { [Math]::Abs($_ - $candidateMedian) })
        ReductionPercent = 100 * (1 - [Math]::Exp(($ratios | Measure-Object -Average).Average))
        Lower95Percent = $low; Upper95Percent = $high
        ThresholdPercent = $ThresholdPercent; Classification = $classification
    }
}

function Get-BenchmarkSignature {
    param([object[]] $Rows)
    # Ignore enumeration order, but retain every exported field, duplicate and path.
    $canonical = @($Rows | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 8 } | Sort-Object)
    $bytes = [Text.Encoding]::UTF8.GetBytes(($canonical -join "`n"))
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
}
