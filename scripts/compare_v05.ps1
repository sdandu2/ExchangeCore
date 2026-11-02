param(
    [ValidateRange(1, 2000000)][int]$Orders = 200000,
    [ValidateRange(1, 10)][int]$Repeats = 5,
    [UInt64]$Seed = 31,
    [string]$OutputCsv = "benchmarks/results/v0.5-comparison.csv"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$mingwBin = "C:\msys64\ucrt64\bin"
$gxx = Join-Path $mingwBin "g++.exe"
$make = Join-Path $mingwBin "mingw32-make.exe"
$gxxCmake = $gxx.Replace("\", "/")
$makeCmake = $make.Replace("\", "/")
if (-not (Test-Path -LiteralPath $gxx) -or -not (Test-Path -LiteralPath $make)) {
    throw "MSYS2 UCRT64 toolchain not found in $mingwBin. See README.md for the manual build commands."
}

$env:Path = "$mingwBin;$env:Path"
$legacyBuild = Join-Path $root "build-v05-legacy"
$optimizedBuild = Join-Path $root "build-v05-optimized"

function Invoke-Checked {
    param([string]$Label, [string]$Executable, [string[]]$Arguments)
    Write-Host "`n[$Label]"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label failed (exit code $LASTEXITCODE)" }
}

Push-Location $root
try {
    foreach ($setup in @(
        @{Name="legacy"; Dir=$legacyBuild; Flag="ON"},
        @{Name="optimized"; Dir=$optimizedBuild; Flag="OFF"}
    )) {
        Invoke-Checked -Label "Configure $($setup.Name)" -Executable "cmake" -Arguments @(
            "-S", $root, "-B", $setup.Dir, "-G", "MinGW Makefiles",
            "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=$gxxCmake",
            "-DCMAKE_MAKE_PROGRAM=$makeCmake",
            "-DEXCHANGECORE_LEGACY_PREFLIGHT=$($setup.Flag)")
        Invoke-Checked -Label "Build $($setup.Name)" -Executable "cmake" -Arguments @("--build", $setup.Dir)
        Invoke-Checked -Label "Test $($setup.Name)" -Executable "ctest" -Arguments @("--test-dir", $setup.Dir, "--output-on-failure")
    }

    $csvPath = if ([System.IO.Path]::IsPathRooted($OutputCsv)) {
        $OutputCsv
    } else { Join-Path $root $OutputCsv }
    # A fresh file with a v0.5-specific schema; never overwrite older v0.4 results.
    if (Test-Path -LiteralPath $csvPath) { Remove-Item -LiteralPath $csvPath -Force }
    $cases = @(
        @{Label="v0.4-style preflight"; Build=$legacyBuild; Reserve=0},
        @{Label="v0.5 selective preflight"; Build=$optimizedBuild; Reserve=0},
        @{Label="v0.5 selective + reserved index"; Build=$optimizedBuild; Reserve=$Orders}
    )
    foreach ($workload in @("passive", "crossing", "mixed")) {
        foreach ($case in $cases) {
            Write-Host "`n=== $workload | $($case.Label) ==="
            $binary = Join-Path $case.Build "exchangecore_bench.exe"
            Invoke-Checked -Label "Benchmark $workload $($case.Label)" -Executable $binary -Arguments @(
                "--orders", "$Orders", "--seed", "$Seed", "--repeats", "$Repeats",
                "--workload", "$workload", "--reserve-ids", "$($case.Reserve)",
                "--csv", $csvPath)
        }
    }

    $rows = @(Import-Csv -LiteralPath $csvPath)
    Write-Host "`nComparison summary (throughput in millions of operations/sec):"
    $rows | Select-Object workload, preflight_mode, reserve_ids,
        @{Name="Mops/s";Expression={[math]::Round([double]$_.median_ops_per_sec / 1e6, 3)}},
        p99_ns, p999_ns, checksum | Format-Table -AutoSize
    # Same workload/seed/order count must produce identical checksums and counts.
    foreach ($workload in @("passive", "crossing", "mixed")) {
        $group = @($rows | Where-Object { $_.workload -eq $workload })
        if (@($group | Select-Object -ExpandProperty checksum -Unique).Count -ne 1) {
            throw "Replay checksum mismatch in workload: $workload"
        }
    }
    Write-Host "`nReplay checksums match across all tested configurations."
    Write-Host "Results saved to $csvPath"
} finally {
    Pop-Location
}
