param(
    [string]$BuildDir = "build-gcc-release",
    [int]$Orders = 1000000,
    [int]$Seed = 31,
    [int]$Repeats = 5
)
$ErrorActionPreference = "Stop"
$exe = Join-Path $BuildDir "exchangecore_bench.exe"
if (-not (Test-Path $exe)) { throw "Benchmark not found: $exe. Build the Release target first." }
foreach ($workload in @("passive", "crossing", "mixed")) {
    foreach ($reserve in @(0, $Orders)) {
        Write-Host "`n=== $workload reserve=$reserve ==="
        & $exe --orders $Orders --seed $Seed --repeats $Repeats --workload $workload --reserve-ids $reserve `
            --csv "benchmarks/results/v0.6-throughput.csv" `
            --latency-csv "benchmarks/results/v0.6-latency-deciles.csv"
        if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $workload reserve=$reserve" }
    }
}
Write-Host "`nReview benchmarks/results/v0.6-latency-deciles.csv for chronological tail-latency changes."
