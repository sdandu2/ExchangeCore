param(
    [string]$BuildDir = "build-gcc-release",
    [ValidateRange(1, 2000000)][int]$Orders = 100000,
    [UInt64]$Seed = 31,
    [ValidateRange(1, 10)][int]$Repeats = 3,
    [string]$Csv = "benchmarks/results/v0.4.csv"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$binary = Join-Path (Join-Path $root $BuildDir) "exchangecore_bench.exe"
if (-not (Test-Path -LiteralPath $binary)) {
    throw "Benchmark executable not found: $binary. Build the Release target first."
}
Push-Location $root
try {
    foreach ($workload in @("passive", "crossing", "mixed")) {
        Write-Host "`n=== $workload benchmark ==="
        & $binary --orders $Orders --seed $Seed --workload $workload --repeats $Repeats --csv $Csv
        if ($LASTEXITCODE -ne 0) { throw "Benchmark failed for workload: $workload" }
    }
} finally {
    Pop-Location
}
