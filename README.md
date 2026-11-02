## v0.6 — Tail latency diagnostics

Run `./scripts/diagnose_v06.ps1` in PowerShell after building Release. The optional `--latency-csv` flag creates decile-level latency diagnostics under `benchmarks/results/`. This is a **measurement improvement**, not a validated performance speedup. See `docs/latency-v0.6.md`.

# ExchangeCore v0.6 — controlled performance optimization

A C++20 single-threaded, in-process price-time priority limit order book and matching engine.

**New in v0.5:**
- Selective overflow preflight avoids scanning opposite-side levels unless overflow could occur.
- `OrderBook::reserve_order_ids(expected_live_orders)` optionally pre-sizes the active-order hash index. This is an optimization hint, **not** a limit on order capacity.
- `exchangecore_bench --reserve-ids N` measures the effect of index capacity reservations.
- `EXCHANGECORE_LEGACY_PREFLIGHT=ON` preserves the original v0.4 overflow-validation algorithm so it can be benchmarked with otherwise identical v0.5 code.
- A paired Windows PowerShell runner performs the same deterministic workloads and checks the replay checksums between variants.
- Three new correctness tests (34 internal cases total), plus three benchmark smoke tests.

## Run on Windows (MSYS2 UCRT64, PowerShell)

From the `ExchangeCore` project root:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S . -B build-v05-optimized -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe" -DEXCHANGECORE_LEGACY_PREFLIGHT=OFF
cmake --build build-v05-optimized
ctest --test-dir build-v05-optimized --output-on-failure
.\build-v05-optimized\exchangecore.exe
```

### Compare baseline vs improvements

```powershell
.\scripts\compare_v05.ps1 -Orders 200000 -Seed 31 -Repeats 5
```

The script builds the legacy-preflight and optimized-preflight versions and benchmarks three configurations against the same generated workload:

1. Original v0.4-style overflow preflight, no reserved index
2. v0.5 selective preflight, no reserved index
3. v0.5 selective preflight, index reserve equal to operation count

All combinations must produce the same checksum for each workload. Results are written to `benchmarks/results/v0.5-comparison.csv` (ignored by Git). Run `-Orders 1000000` later for your reference-scale experiment if RAM allows. Reserving `Orders` entries is an intentionally generous hint and can increase memory consumption; it is not necessarily optimal, especially for crossing workloads with small resting books.

If scripts are blocked by PowerShell execution policy, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\compare_v05.ps1 -Orders 200000 -Repeats 5` **only if you trust this project's script**; alternatively use the manual commands in `docs/optimization-v0.5.md`.

For full interface, command-line switches, definitions, benchmarking limits and methodology, see `docs/benchmarking.md`, `docs/optimization-v0.5.md`, and `docs/design.md`.

## What the benchmark measures

The benchmark times **single-threaded, in-process** order submission and cancellation. It excludes networking, persistence, serialization, and input generation. It uses a separately timed latency replay, which includes clock overhead and scheduling effects. A single experiment is not evidence of production-ready or deterministic microsecond latency.

For each change, run both builds under identical parameters, check correctness, record CPU/compiler/compiler flags, and compare *within the same machine*. Do not compare absolute throughput from different computers as if all conditions were controlled.
