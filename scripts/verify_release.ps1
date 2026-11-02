param(
    [switch]$SkipCpp,
    [switch]$SkipFrontend
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $project
try {
    function Check-Command {
        param([string]$Name)
        if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
            throw "Required executable not found: $Name"
        }
    }
    function Invoke-Step {
        param([string]$Name, [scriptblock]$Action)
        Write-Host "`n== $Name ==" -ForegroundColor Cyan
        & $Action
        if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
    }

    Check-Command 'py'
    Invoke-Step 'Python offline market data and replay tests' {
        py -m unittest discover -s market_data/tests -v
    }
    Invoke-Step 'Validate the committed synthetic replay fixture' {
        py -m market_data.replay market_data/fixtures/synthetic-fixture.jsonl --symbols BTC/USD
    }

    if (-not $SkipCpp) {
        $toolchain = 'C:\msys64\ucrt64\bin'
        if (-not (Test-Path (Join-Path $toolchain 'g++.exe'))) {
            throw "GCC not found at $toolchain. Install MSYS2 UCRT64 or run with -SkipCpp."
        }
        $env:Path = "$toolchain;$env:Path"
        Check-Command 'cmake'
        Invoke-Step 'Configure C++ Release build' {
            cmake -S . -B build-release-check -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER='C:/msys64/ucrt64/bin/g++.exe' -DCMAKE_MAKE_PROGRAM='C:/msys64/ucrt64/bin/mingw32-make.exe'
        }
        Invoke-Step 'Build C++ engine and benchmarks' { cmake --build build-release-check }
        Invoke-Step 'Run CTest C++ and market data suites' { ctest --test-dir build-release-check --output-on-failure }
    }

    if (-not $SkipFrontend) {
        Check-Command 'npm.cmd'
        Push-Location dashboard
        try {
            Invoke-Step 'Build TypeScript and Vite dashboard' { npm.cmd run build }
            Invoke-Step 'Run frontend unit tests' { npm.cmd run test:unit }
        }
        finally { Pop-Location }
    }
    Write-Host "`nLocal automated release checks PASSED. Browser and GitHub CI verification remain manual." -ForegroundColor Green
}
finally { Pop-Location }
