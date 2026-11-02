# ExchangeCore v0.7: focused profiling and interleaved capacity experiments

## Research questions

1. Do large passive-order latency spikes coincide with hash-table bucket-count changes?
2. Do expensive cancellations correlate with removing the last order at a price level?
3. Does sizing the order-ID hash index for observed **peak simultaneously live orders** beat reserving for the full operation count?
4. How stable are these improvements under rotated, interleaved configuration ordering?

## Instrumentation

`OrderBook::stats()` returns a read-only **constant-time** snapshot of active order count, bid and ask price-level counts, hash-table bucket count, and load factor.
The profiling replay observes statistics immediately before and after each operation. Bucket-count changes detect **observable rehashes**, not all allocations; removing a price level is inferred from a decline in total level count during a cancellation. This does **not** measure the cost of each allocation or its precise cause. It does not provide a sampling profiler or CPU flamegraph.

A separate throughput phase **does not sample `stats()`** and excludes event generation and initial `reserve()` time; initial reservation can consume memory and startup time. The diagnostic latency phase measures `submit()` / `cancel()` but the surrounding snapshots can alter cache behavior. The timer includes function and clock overhead and cannot isolate CPU time from scheduling interruptions. Report throughput and latency separately.

The benchmark uses one generated event sequence (SplitMix64 seed 31 by default) and cycles through distinct reservation choices:

- `0` (let unordered_map grow)
- `peak_live` (the maximum simultaneous live-order count observed during event generation)
- `operations` (intentionally oversized upper-bound hint)

Duplicate choices are removed (for example, passive `peak_live == operations`). For throughput the configurations are interleaved in a rotating order and reversed on alternating rounds. Raw per-round measurements are saved so results can be analyzed for run-order drift. Every throughput and profiling replay must produce **identical aggregate metrics and checksums** across configurations or the executable fails. Checksums are supplementary; unit tests remain necessary.

## Windows instructions (PowerShell + GCC Release)

```powershell
cd "$HOME\Projects\ExchangeCore"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S . -B build-gcc-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure
.\scripts\profile_v07.ps1 -BuildDir build-gcc-release -Orders 200000 -Seed 31 -Rounds 5
```

Repeat with `-Orders 1000000` once the smoke experiment succeeds.

## Output schemas

`v0.7-<workload>-throughput.csv`: one **raw timed run per row** including round, execution order, reserve hint, elapsed seconds, operations/sec and replay checksum. Use this for distribution and run-order analysis. The terminal reports medians by capacity choice.

`v0.7-<workload>-latency.csv`: one row per replay decile, category and capacity choice, including count, mean, p50/p99/p99.9, max, operations slower than 10 µs, rehash count, cancellations that removed a level, and observed book state. Categories: `all`, `submit`, `cancel_kept_level`, `cancel_removed_level`, `index_rehash` (only when present). Small category counts make high percentile statistics noisy.

## Scope and interpretation

This is a **single-threaded, in-process** matching-engine benchmark, not network or production exchange latency. Benchmark timing can be influenced by Windows scheduling, clocks (100ns quantization is common), CPU frequency, background tasks, and memory management. The benchmark does not quantify cache misses or heap allocation activity directly. To prove an allocation cause, use ETW/Windows Performance Recorder, Visual Studio Profiler, Linux perf, or allocator-level instrumentation in a later version.

Suggested analysis: compare medians across rounds, look for drift by execution position, isolate expensive rehash samples, compare cancel-category distributions, and correlate increasing active-order counts with decile latency. Do not treat correlation as causality.
