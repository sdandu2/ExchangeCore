# v0.4 benchmark methodology

## What is being measured?

This executable measures the **single-threaded in-process** `OrderBook::submit` and `OrderBook::cancel` APIs, including order matching and returned trade vector construction. It does **not** include networking, serialization, persistence, risk checks, clock synchronization, or market-data dissemination.

Two distinct passes replay the same *pre-generated* vector of events into fresh books:

1. **Bulk throughput:** time one contiguous loop that invokes matching/cancellation and accumulates outcome metrics. Divide total operation count by elapsed wall-clock seconds. Repeat the entire replay `--repeats` times (default 3); report median rate and median duration. Timed loop includes minimal statistics/checksum overhead, not event generation.
2. **Latency distribution:** separately time every book operation with two `steady_clock` readings, then update checksum/counters *outside* the measured interval. Sort measured nanoseconds and report nearest-rank p50/p95/p99/p99.9 and arithmetic mean. Timer overhead and interrupt scheduling affect short-duration samples, so do **not** equate nanosecond values with hardware-cycle-exact execution latency.

The replay runs verify matching/cancellation outcomes via a checksum and summary counters. An inconsistency is an error, not a benchmark result.

**Operation** = one order submission or cancellation attempt. **Trade** = one maker/taker execution record; one operation can produce several trades. Throughput is *operations/sec*, not trades/sec or orders/sec if cancellations are present.

## Workloads

Deterministic SplitMix64 PRNG, default seed 31, generates these synthetic scenarios:

- **Passive:** limit prices intentionally separated so incoming buy and sell orders do not cross; no cancels. Book size generally grows with operation count.
- **Crossing:** selects a price crossing the current best opposite level when possible, otherwise places passive orders; no cancels. Measures matching pressure.
- **Mixed:** approximately 15% cancels when live orders exist, 45% aggressive submissions, 40% passive submissions. Cancel IDs refer to live resting orders. The exact realized mix depends on available liquidity.

Event generation happens *before* timing and uses a reference order book to maintain correct live-order state. This is synthetic traffic and isn't intended to statistically reproduce a real exchange's order flow. Changing the generator changes what is measured; compare matching versions with the same workload and seed.

## Conduct a defensible comparison

1. Use **Release** builds for performance comparisons, not Debug; keep the same compiler and compiler flags when comparing algorithm changes.
2. Run one benchmark process at a time. Prefer AC power and consistent power/thermal settings. Stop unnecessary background tasks.
3. Run on the **same machine**, with the **same input parameters** (`--orders`, `--seed`, `--workload`, `--repeats`).
4. Record the Git commit SHA, CPU model, RAM, operating system, toolchain version, generator, and build flags alongside the CSV.
5. Repeat the complete benchmark command on multiple occasions; report variability and the median rather than cherry-picking the fastest run.
6. Treat short benchmark passes cautiously. Use a larger `--orders` value when trial durations are very brief, while watching memory usage.
7. Study each workload separately. Do not claim a single throughput number represents every possible market condition.

Helpful Windows PowerShell metadata commands:

```powershell
git rev-parse HEAD
Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors
(Get-CimInstance Win32_OperatingSystem).Caption
cmake --version
g++ --version
```

Results are appended to `benchmarks/results/*.csv`, which Git ignores by default to avoid accidentally publishing unreviewed experiments. Commit **curated, reproducible** figures later with full hardware/setup metadata.

## Next iterations

- Measure instrumentation overhead (empty calibration) and impact of CPU scheduling / core affinity.
- Add warmup trials and more rigorous multi-run statistical summaries.
- Benchmark allocation counts, memory use, and how cost varies with price-level depth.
- Add realistic dataset replay, a reference model for property-based correctness tests, and performance regression checks with configurable, non-flaky thresholds.
