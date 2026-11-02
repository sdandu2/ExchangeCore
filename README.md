# ExchangeCore

**v0.4 — a C++20 limit-order matching engine with reproducible performance benchmarks.**

Single-threaded, in-memory exchange simulator. Emphasizes correctness and measured engineering rather than unsupported "low-latency" claims. No brokerage connectivity and no real-money trading.

## What's implemented

- Price-time-priority FIFO limit-order matching using integer price ticks
- Partial fills, multi-level sweeps, maker-price executions, cancellations
- Quantity conservation, active ID validation, overflow checks
- 31 internal matching-engine correctness cases (one CTest executable)
- Three additional CTest benchmark smoke tests (`passive`, `crossing`, `mixed`)
- GitHub Actions CI building/testing on Windows and Linux
- Configurable benchmark: seed, operation count, workload, repeat count, CSV output
- Separate bulk-throughput and per-operation-latency passes; replay digest cross-check

## Project structure

```text
ExchangeCore/
├── include/exchangecore/         C++ public types and order-book API
├── src/                          Matching engine and console demo
├── tests/                        Correctness tests
├── benchmarks/matching_bench.cpp Deterministic workload / measurement CLI
├── benchmarks/results/           Local CSV measurements (ignored by Git)
├── scripts/run_benchmarks.ps1     Run all three workloads from VS Code
├── docs/design.md                Engine behavior and design limitations
├── docs/benchmarking.md          Benchmarking methodology and caveats
├── .github/workflows/ci.yml      CI for MSVC/Windows and GCC/Linux
├── CMakeLists.txt
├── .gitattributes
└── .gitignore
```

## Requirements

- CMake >= 3.20
- C++20 compiler: MSVC, modern GCC (MSYS2 UCRT64 on Windows), or Clang
- Visual Studio Code + PowerShell recommended for Windows

## Windows setup: build and test with MSYS2 GCC (Release)

Use VS Code **PowerShell** in your `ExchangeCore` directory. Use a **new Release build directory** to avoid timing Debug binaries:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S . -B build-gcc-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure
.\build-gcc-release\exchangecore.exe
```

To run **all three** benchmark workloads and save one CSV:

```powershell
.\scripts\run_benchmarks.ps1 -BuildDir build-gcc-release -Orders 100000 -Seed 31 -Repeats 3
```

If script execution is restricted, run the binary directly:

```powershell
.\build-gcc-release\exchangecore_bench.exe --orders 100000 --seed 31 --workload mixed --repeats 3 --csv benchmarks/results/v0.4.csv
```

Run the other workloads by substituting `passive` and `crossing` for `mixed`.

### MSVC build

Keep your working `build/` MSVC/NMake configuration for correctness testing. For meaningful MSVC performance measurements, configure a **separate Release build** in a terminal where `cl` and `nmake` are available:

```powershell
cmake -S . -B build-msvc-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc-release
ctest --test-dir build-msvc-release --output-on-failure
.\build-msvc-release\exchangecore_bench.exe --workload mixed --orders 100000 --repeats 3 --seed 31 --csv benchmarks/results/v0.4-msvc.csv
```

For a regular MSVC Debug build, run `cmake --build build` and `ctest --test-dir build --output-on-failure`.

## Benchmark interface

```text
exchangecore_bench [--orders N] [--seed N]
                   [--workload passive|crossing|mixed]
                   [--repeats N] [--csv PATH]
```

Defaults: **100,000 operations**, **seed 31**, **mixed** workload, **3 repeats**.

- `passive`: mostly non-crossing submissions; stresses accumulating resting orders
- `crossing`: deliberately marketable submissions when opposite liquidity exists
- `mixed`: passive and aggressive submissions plus valid cancellations (roughly 15% when orders exist)

The CLI reports **operations per second** (submissions plus cancellations), latency mean and nearest-rank p50/p95/p99/p99.9, trade count, executed quantity, remaining orders, and a replay checksum. The CSV stores one row per invocation; repeated commands append more rows.

See [docs/benchmarking.md](docs/benchmarking.md) for important interpretation notes. **Do not use the example numbers from someone else's hardware as your own.**

## Current limitations / next steps

- Single-threaded, single-book simulator, no network round trips, persistence, or production exchange fault-tolerance
- Latency samples include high-resolution timer overhead and are subject to OS scheduling noise
- Benchmark workload is synthetic, not a calibrated exchange feed
- No assertion that performance is "HFT-grade" or faster than commercial matching engines

Next: v0.5 event logging and replay, then production-style APIs, observability, and UI.
