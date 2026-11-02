# ExchangeCore v1.2 — C++ Matching Engine + Live Market Terminal

ExchangeCore combines a **C++20 simulated limit-order matching engine** with
**read-only public Kraken spot market data**. The engine simulates price-time
priority; it **does not** place real trades or recreate Kraken's actual order queue.

Our goal is a credible, testable, real-world engineering portfolio rather than a
trading profit claim. The core has correctness tests and reproducible benchmarks;
the Python adapter independently validates, records and streams market data.

## Quick start: Windows VS Code PowerShell

### 1) Compile and test the C++ engine

```powershell
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
cmake -S . -B build-gcc-release -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER='C:/msys64/ucrt64/bin/g++.exe' -DCMAKE_MAKE_PROGRAM='C:/msys64/ucrt64/bin/mingw32-make.exe'
cmake --build build-gcc-release
ctest --test-dir build-gcc-release --output-on-failure
```

### 2) Install and verify the data adapter

```powershell
py -m pip install -r market_data/requirements.txt
py -m unittest discover -s market_data/tests -v
```

### 3) Start a real-time stream (Terminal 1)

```powershell
py -m market_data.stream_server --symbols BTC/USD ETH/USD --duration 300
```

**Public market data, no login or API key.** Your console will show:

`LOCAL READ-ONLY STREAM: ws://127.0.0.1:8765`

A timestamped JSONL recording is saved locally to `market_data/records/`.
If the exchange is unreachable, use offline replay instead (see below).

### 4) Connect a local viewer (Terminal 2)

```powershell
py -m market_data.watch --max-messages 30 --book-every 10
```

The viewer reports connection health and validated bid/ask levels from the
same live feed. It cannot place real orders.

### 5) Replay your own captured market recording

```powershell
py -m market_data.stream_server --replay "market_data/records/YOUR-EXACT-FILENAME.jsonl" --speed 1
```

Connect the watcher from a separate terminal as above. Recorded captures are
ignored by Git and **not included in this ZIP**. For a built-in offline fixture,
run:

```powershell
py -m market_data.stream_server --replay market_data/records/synthetic-fixture.jsonl --symbols BTC/USD --linger 30
```

The fixture is synthetic and is clearly labeled; your own `kraken-*.jsonl`
recordings are real-feed captures from your machine.

## v1.2 historical replay and UX fixes

The market-data server now supports replay-only pause/resume, speed, and seek-to-event-position controls. Recorded books are reconstructed from checksum-validated events and the frontend displays source timestamps on a time-proportional chart. The live mode remains read-only. Order-book VALUE (USD), locally paused trade tape, and graceful disconnect handling are included.

See [historical replay guide](docs/replay-v1.2.md).

## Real-time dashboard (v1.1)

The `dashboard/` folder contains a read-only React + TypeScript interface for
verified Kraken Level 2 data, mid-price observations, depth, trade tape and feed
health. Start `market_data.stream_server` first (as above), then in a **second**
PowerShell terminal:

```powershell
cd dashboard
node --version
npm install
npm run dev
```

Open **http://127.0.0.1:5173**. Node.js 22 LTS is recommended; Vite requires
Node 20.19+ or 22.12+. See [dashboard instructions](dashboard/README.md).

The viewer is strictly localhost-only. It does not send orders and must not be
exposed publicly without appropriate provider permissions and security controls.

## Development milestones

- **v0.1–0.3:** deterministic C++ matching and cancellation.
- **v0.4–0.7:** throughput, tail-latency and memory profiling.
- **v0.8:** configurable cancellation implementation and regression tests.
- **v0.9:** public Kraken WebSocket collector, decimal-safe book, CRC32, JSONL replay.
- **v1.0:** **local streaming API**, health/staleness, session resets, bounded fanout,
  read-only clients, validated live feed, and timed replay.
- **v1.2:** interactive replay and timestamp-aware chart; **v1.1:** React + TypeScript local live-market terminal with verified L2 book,
  trade tape, observed mid-price and feed health.

## Architecture and trust boundaries

```text
Kraken public WebSocket (spot L2 + trades)     Saved Kraken JSONL capture
              |                                        |
         CRC32 validation                 Decimal-safe CRC32 replay
              |                                        |
           recording                              validation
              |_________________   ___________________|
                                | |
                       local WebSocket hub
                     ws://127.0.0.1:8765
                       (read-only, local)
                                |
                    CLI watcher / Local React dashboard

C++20 Matching Engine (separate: simulated orders and benchmark workloads)
```

The hub and recorder run outside the matching-engine benchmark path. **Matching
latency benchmarks are not Kraken feed latency or end-to-end network latency.**

Full details: [v1.0 streaming API](docs/live-stream-v1.0.md) ·
[v0.9 market-data validation](docs/live-feed-v0.9.md) ·
[matching design](docs/design.md) ·
[benchmarking](docs/benchmarking.md).

**Privacy and market-data licensing:** do not commit your captured JSONL files.
The stream is intentionally bound only to localhost. Verify Kraken's API terms
and obtain necessary permission before publicly redistributing real exchange data.
