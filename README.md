# ExchangeCore v0.7 — Performance diagnostics

ExchangeCore is a **single-threaded, in-process C++20 limit order book and matching engine** using price-time priority. It supports limit orders, partial fills, multi-level matching, cancellations and reproducible test workloads.

## What's new in v0.7

- `OrderBook::stats()`: constant-time read-only snapshot of active orders, price levels, ID-hash buckets and load factor.
- Diagnostic latency replay classifies cancellations that remove a price level vs. those that do not, and identifies observable hash-table bucket-count changes.
- Interleaved throughput experiment compares 0, **observed peak-live order count**, and full-operation-count reservation hints using exactly the same event stream.
- Output includes per-round throughput, chronological decile latency distributions, and replay consistency checks.
- Two added unit tests plus a profile smoke test. Existing `--reserve-ids`, `--csv`, `--latency-csv`, legacy preflight and v0.5 comparison scripts remain supported.

**No performance improvement is claimed in v0.7.** This release adds observation and experiment tools to choose future optimizations. For methodology and limitations see [docs/profiling-v0.7.md](docs/profiling-v0.7.md).

## Windows / VS Code / PowerShell

Install the contents of the ZIP into `C:\Users\sammy\Projects\ExchangeCore` after saving your local work in Git. Prefer a dedicated `feature/v0.7-profiling` branch. Then:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S . -B build-gcc-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure
.\scripts\profile_v07.ps1 -BuildDir build-gcc-release -Orders 200000 -Seed 31 -Rounds 5
```

If that succeeds, run again with `-Orders 1000000`. Each run writes six output CSVs under `benchmarks/results/` (ignored by Git). Share the six v0.7 files to plan the next targeted improvement.

Earlier technical decisions: [docs/design.md](docs/design.md), [docs/benchmarking.md](docs/benchmarking.md), [docs/optimization-v0.5.md](docs/optimization-v0.5.md), and [docs/latency-v0.6.md](docs/latency-v0.6.md).

## Important benchmark disclaimer

Throughput measures in-process `submit`/`cancel` operations, excluding event generation and reservation setup. Diagnostic latency timing is an independent, instrumented replay. Read-only snapshots may affect cache behavior. The project is not a production exchange or a low-latency networking stack.
