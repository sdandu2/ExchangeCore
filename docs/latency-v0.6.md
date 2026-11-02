# ExchangeCore v0.6 — Tail-latency diagnostics

This release **does not claim a tail-latency improvement**. It adds diagnostic reporting to the existing, separately instrumented latency replay.

## Key observations motivating this work
- Passive p99.9 stayed around 23–24 microseconds in v0.5 even with `reserve_order_ids`.
- The 1M mixed workload showed increased p99/p99.9 with a reserved hash index.
- Checksums matched across the legacy and selective paths on the tested workloads, but this does not prove all correctness properties.

## New capability
`--latency-csv PATH` appends chronologically ordered deciles of the replay with separate breakdowns for submission and cancellation operations. Each row includes count, mean, p50, p99, p99.9, maximum latency, and the count above 10 microseconds. The old global summary and CSV are unchanged.

## Run
```powershell
cmake -S . -B build-gcc-release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure
.\scripts\diagnose_v06.ps1 -BuildDir build-gcc-release -Orders 1000000 -Seed 31 -Repeats 5
```

## Interpretation precautions
- This measures in-process calls on a synthetic, deterministically generated workload, **not** network or exchange end-to-end latency.
- `steady_clock` timer quantization, OS preemption, cache effects and CPU frequency changes may affect nanosecond samples.
- Separate instrumented latency replay means these percentiles do not correspond to the throughput repetitions.
- The 10 chronological bins reveal changes over replay progression, but do not isolate causes. Check whether outliers cluster late in the passive run or coincide with cancellations.
- Reserving one million slots is an optimistic capacity oracle: compare practical reserve sizes before productizing it.
- Run control and treatment in alternating order across several sessions; avoid drawing conclusions from one run.

## Next experiment
If passive spikes rise with live-order count, add counters for hash index rehashes and allocator activity, then profile using a sampling profiler. If they remain isolated across deciles, prioritize timer/OS scheduling characterization.
