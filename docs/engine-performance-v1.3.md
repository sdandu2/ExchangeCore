# ExchangeCore v1.3 — Engineering Performance Workspace

The dashboard now includes an **Engine Performance** view at `http://127.0.0.1:5173/#performance`. It is separate from the Kraken live/replay view. It works **without** a running Python WebSocket server because it reads an **archived, static benchmark snapshot**.

## Source of displayed results

Six original user-supplied CSV files from the **v0.7** release are archived in `benchmarks/snapshots/v0.7/` (three workloads × throughput/latency). The data was measured with GCC 16.2 Release, seed 31, 1,000,000 operations, five throughput rounds on a Windows machine with an Intel Core Ultra 9 275HX CPU. The interactive dashboard reads `dashboard/src/data/benchmarks.json`, generated from the CSV files using only Python's standard library:

```powershell
py scripts/export_benchmark_snapshot.py
```

Please **do not edit the generated JSON by hand**. The script fails on missing, incomplete, or inconsistent series. If rerunning benchmarks, archive results in a new versioned snapshot directory and update the exporter and annotations. Do not silently replace the v0.7 measurements with new results while keeping the old label.

## Interpretation

- **Throughput** is the median of five independently timed replays. The interactive bars show no reservation, peak-live reservation (when distinct), and full-operation reservation. Configurations are separate measurements, not simultaneous parts of a single run.
- **Latency** comes from a **separate instrumented replay** (one million orders divided into ten chronological deciles). The plotted points are **p99.9 within each 100,000-operation decile**, and **not** overall p99.9 values.
- **Worst Observed Operation** is the maximum over the ten deciles in the instrumented replay. The dashboard intentionally does not report it as typical latency.
- **Index Rehashes** are diagnostic hash-table bucket-count changes observed in the replay.
- **Checksums** demonstrate agreement for the tested replay workloads, but do not replace formal engine correctness tests.
- These are **in-process C++ engine** measurements; they are not live Kraken exchange latency, network latency, or actual execution speed on an exchange.

## Usability improvements

- UI navigation and important headings now consistently use Title Case.
- Compact screens offer both Market Overview and Engine Performance navigation.
- Market socket connectivity and validated book freshness are no longer conflated in the health banner.
- The mid-price timeline exposes native hover tooltips with recorded receive time and midpoint.
- The older order-book value label remains explicitly **VALUE (USD)**.

## Final-release check

Run the C++, Python, and TypeScript tests from the repository root:

```powershell
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure
py -m unittest discover -s market_data/tests -v
cd dashboard
npm install
npm run build
npm run test:unit
npm run dev
```

In your browser, open `http://127.0.0.1:5173/#performance`, switch across all three workloads, and select each reservation strategy. The performance page must work even with the market-data server turned off. Check that the Market Overview still reconnects to the local stream when you switch back.
