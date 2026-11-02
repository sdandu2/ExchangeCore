# v0.5 optimization experiment

## Hypotheses from v0.4

1. **Overflow preflight overhead.** The original `submit()` walks crossing opposite-side price levels before every order submission, even if the incoming quantity cannot overflow any existing same-side price level. This can do unnecessary work for ordinary orders.
2. **Active-ID map rehashing.** The order-ID lookup `std::unordered_map` grows on demand. Rare hash-table rehashes are a plausible contributor to passive-order tail latency. This is a hypothesis, *not* a proven explanation for the Windows p99.9 spikes.

## Changes

### Selective overflow preflight

The fast path first checks whether a same-side level exists at the incoming order's exact price, and whether adding the *entire* order quantity to that level could overflow `uint64_t`.

- No level, or enough headroom? Matching is safe without extra scans because the actual resting remainder can never exceed the original incoming quantity.
- Overflow may be possible? Use the original `predicted_resting_quantity()` scan, and reject **before matching** if the aggregate would overflow.

This preserves the existing rejection semantics, including atomic rejection of overflow requests. The compile flag `EXCHANGECORE_LEGACY_PREFLIGHT=ON` selects the original algorithm for A/B testing without changing tests or benchmark input generation.

### Optional order-ID reservation

The new `reserve_order_ids(size_t)` method calls `locations_.reserve(...)`. It is not a limit on orders, and does not reserve `std::list<Order>` nodes or prevent future allocations. Whether the memory tradeoff is worthwhile depends on workload shape. Benchmarks can measure `--reserve-ids 0` against a chosen positive hint while holding the engine/inputs constant.

## Running the experiments (PowerShell)

```powershell
.\scripts\compare_v05.ps1 -Orders 200000 -Repeats 5 -Seed 31
```

The script creates the following two distinct Release build directories:

- `build-v05-legacy` (`EXCHANGECORE_LEGACY_PREFLIGHT=ON`)
- `build-v05-optimized` (`EXCHANGECORE_LEGACY_PREFLIGHT=OFF`)

Then tests both and compares three configurations for each `passive`, `crossing`, and `mixed` workload. Default report: `benchmarks/results/v0.5-comparison.csv`.

## Interpretation checklist

- [ ] Confirm all CTest suites pass in both builds.
- [ ] Verify identical checksums and order/trade counts for equal workloads.
- [ ] Compare legacy preflight vs optimized with **reserve=0** to isolate the code-path change.
- [ ] Compare optimized reserve=0 vs optimized reserve=N to isolate capacity reservation.
- [ ] Run larger 1M-operation tests and repeat on multiple sessions.
- [ ] Save CPU, RAM, compiler, compiler flags, Git SHA, and Windows build number.
- [ ] Note any regressions and avoid advertising an improvement not reproducible on target hardware.

## Known limits

- The app and benchmark remain single-threaded and in-process.
- Clock-based per-operation samples include timer cost and interruptions; p99.9 from a single latency replay should be interpreted cautiously.
- Reservation cost and extra RAM are excluded from the timed operation window.
- OS resource monitoring is not implemented; use an external profiler (e.g. Visual Studio CPU Usage or Windows Performance Recorder) before attributing latency spikes to specific allocation sites.
