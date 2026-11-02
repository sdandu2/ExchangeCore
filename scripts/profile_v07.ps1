param(
    [string]$BuildDir = "build-gcc-release",
    [int]$Orders = 1000000,
    [int]$Seed = 31,
    [int]$Rounds = 5
)
$ErrorActionPreference = "Stop"
$exe = Join-Path $BuildDir "exchangecore_bench.exe"
if (-not (Test-Path $exe)) { throw "Benchmark executable missing: $exe. Build Release first." }
$outDir = "benchmarks/results"
New-Item -ItemType Directory -Force $outDir | Out-Null
foreach ($workload in @("passive", "crossing", "mixed")) {
    Write-Host "`n=== v0.7 interleaved: $workload ==="
    $raw = Join-Path $outDir "v0.7-$workload-throughput.csv"
    $diag = Join-Path $outDir "v0.7-$workload-latency.csv"
    if (Test-Path $diag) { Remove-Item $diag }
    & $exe --orders $Orders --seed $Seed --workload $workload `
        --profile-grid auto --profile-rounds $Rounds `
        --profile-csv $raw --profile-latency-csv $diag
    if ($LASTEXITCODE -ne 0) { throw "v0.7 experiment failed: $workload" }
}
Write-Host "`nDone. Upload the six v0.7-*.csv files in benchmarks/results/."
