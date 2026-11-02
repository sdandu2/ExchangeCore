# ExchangeCore

**High-Performance C++ Matching Engine · Verified Market Data · Interactive Engineering Terminal**

ExchangeCore is a portfolio-grade software engineering project with two deliberately separate systems:

- **Simulated exchange core:** a C++20 limit-order book with price-time-priority matching, partial fills, cancellations, correctness tests, and reproducible latency/throughput benchmarks.
- **Real market-data infrastructure:** a read-only Python service that collects public Kraken spot L2 and trade messages, validates order-book CRC32 checksums, records JSONL captures, and serves a local React/TypeScript dashboard with historical replay.

**No real orders or funds are involved.** ExchangeCore does not connect its C++ engine to Kraken execution, reconstruct individual exchange queue positions, or claim to predict profitable trades.

![ExchangeCore Market Overview showing a locally observed Kraken session](docs/assets/market-overview.png)

*Market Overview — example screenshot from a local Kraken feed session on November 2, 2026. Prices are historical, not current quotes.*

## Engineering Results

The **Engine Performance** workspace visualizes measured, archived results from one Windows system (Intel Core Ultra 9 275HX, GCC 16.2.0 Release, seed 31). Each workload contained 1,000,000 in-process operations; throughput is the median of five measured runs.

| Deterministic workload | No reservation | Best measured reservation configuration | Relative gain |
|---|---:|---:|---:|
| Passive (resting orders) | 5.56M ops/s | 7.26M ops/s | +30.5% |
| Crossing (aggressive matching) | 7.06M ops/s | 7.92M ops/s | +12.1% |
| Mixed (submission and cancellation) | 5.72M ops/s | 7.06M ops/s | +23.5% |

**Benchmark caveats:** These are local in-process results, **not** network round-trip times, exchange execution latency, financial returns, or universal performance guarantees. The best reservation setting differs by workload. The UI shows latency from a *separate instrumented replay*, including chronological p99.9 diagnostics; latency and throughput were not collected in the same run. Replay checksums agree across the tested configurations. Read [the full methodology](docs/engine-performance-v1.3.md) and [archived source CSVs](benchmarks/snapshots/v0.7/).

![Engine Performance workspace showing measured C++ throughput](docs/assets/engine-performance.png)

*Engine Performance — frozen benchmark snapshot from the v0.7 experiment, not live Kraken latency.*

## What You Can Explore

| View | What it demonstrates |
|---|---|
| Market Overview | BTC/USD and ETH/USD live or recorded bid/ask levels, spread, midpoint history, recent exchange trades and feed health |
| Historical Replay | Pause, resume, change speed, and seek through a **locally saved, validated** Kraken recording |
| Engine Performance | Compare deterministic workloads, order-ID reservation strategies, throughput, replay tail latency, rehash counts and methodology |

The **Market Overview** needs the local Python WebSocket server. The **Engine Performance** workspace works offline from archived measurements and does not require Kraken connectivity.

## Quick Start (Windows / VS Code PowerShell)

Prerequisites: Python 3, Node.js 22 (or Vite-compatible Node version), npm, and an up-to-date C++20 compiler/CMake for the C++ engine. The Python and frontend views can be explored before building C++.

**1 — Install local dependencies and start the frontend (Terminal A):**

```powershell
cd "$HOME\Projects\ExchangeCore"
py -m pip install -r market_data/requirements.txt
cd dashboard
npm install
npm run dev
```

Open **http://127.0.0.1:5173** for the dashboard, or **http://127.0.0.1:5173/#performance** to view C++ results without a feed.

**2 — Start a Kraken live feed (Terminal B, from project root):**

```powershell
cd "$HOME\Projects\ExchangeCore"
py -m market_data.stream_server --symbols BTC/USD ETH/USD --duration 1800 --linger 60
```

The WebSocket service is bound to `ws://127.0.0.1:8765` on **localhost only**. Live capture produces an **ignored, local** recording under `market_data/records/`.

**Alternative — Offline replay without exchange access:** Stop any live server on port 8765, then start:

```powershell
cd "$HOME\Projects\ExchangeCore"
py -m market_data.stream_server --replay market_data/fixtures/synthetic-fixture.jsonl --symbols BTC/USD --depth 10 --speed 1
```

This tiny **synthetic** snapshot is committed for smoke-testing the replay pipeline; it is not a realistic historical session. For a full, interactive replay with trades and meaningful seeking, replace the fixture path with one of your own `market_data/records/kraken-*.jsonl` captures.

## C++ Build and Reproducible Verification

Example for **MSYS2 UCRT64 GCC** in Windows PowerShell, from the project root:

```powershell
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
cmake -S . -B build-release-check -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER='C:/msys64/ucrt64/bin/g++.exe' -DCMAKE_MAKE_PROGRAM='C:/msys64/ucrt64/bin/mingw32-make.exe'
cmake --build build-release-check
ctest --test-dir build-release-check --output-on-failure
py -m unittest discover -s market_data/tests -v
cd dashboard
npm run build
npm run test:unit
```

Or, with dependencies already installed, run the included comprehensive check from the project root:

```powershell
.\scripts\verify_release.ps1
```

See [Final Release QA](docs/release-qa.md) for manual disconnection, replay, UI and GitHub Actions checks.

## System Architecture

```text
Public Kraken WebSocket v2 (L2 + trade events)    Local JSONL recordings
                      |                                   |
         Python decimal-safe book + CRC32        Checked replay parser
                      |                                   |
                Session reset / health / verified updates
                                  |
                        Local WebSocket hub
                        ws://127.0.0.1:8765
                                  |
                         React / TypeScript UI
                          (live / replay views)

    Separately: C++20 matching engine -> deterministic benchmarks ->
       archived CSVs -> generated JSON -> Engine Performance view
```

The two pipelines are intentionally separated so live WebSocket/network activity cannot distort the in-process C++ benchmarks. Learn more in [Design](docs/design.md), [Market Feed](docs/live-feed-v0.9.md), [Live Stream](docs/live-stream-v1.0.md), and [Historical Replay](docs/replay-v1.2.md).

## Project Scope, Limitations and Attribution

- **Depth:** Public Kraken L2 is aggregated by price level. It does not reveal individual queue priority or all liquidity across venues.
- **Replay:** Replays use source and local receipt timestamps and reconstruct checksum-verified books; gaps in collection are not magically filled. Recorded files over the in-memory replay limit cannot be opened with the current player.
- **Benchmark:** 1M-operation deterministic workloads do not capture networking, risk limits, real capital, multi-venue routing or production SLAs.
- **Presentation:** Live exchange data may be subject to provider display/redistribution restrictions. The server is local-only and this repository does **not** publicly redistribute user-captured Kraken data.
- **Security:** The current local server is read-only. Do not expose port 8765 to the public internet.

More: [Demo Walkthrough](docs/demo-walkthrough.md) · [Release QA](docs/release-qa.md) · [Benchmarking](docs/benchmarking.md).

## Status

**Portfolio release candidate — v1.3.0.** Feature development is frozen for this initial portfolio milestone. The remaining publication gates are a local clean build, passing GitHub Actions, and merging the reviewed branch before tagging `v1.3.0`. See [Release QA](docs/release-qa.md).
