# ExchangeCore

A C++20 portfolio project building a single-threaded limit order book and price-time priority matching engine. The project is developed incrementally with tests before performance optimization.

## Status: v0.3 — Limit order matching

- Integer price ticks and integer quantities (no floating-point order prices)
- Bids sorted high-to-low, asks sorted low-to-high; FIFO within each price level
- Marketable incoming limit orders execute against resting liquidity at **maker price**
- Partial fills, multi-level sweeps, unfilled residuals resting at their limit price
- Trade reports with maker ID, taker ID, executed price, and quantity
- Order cancellation; nonzero order ID and positive price/quantity checks
- Duplicate active IDs and per-level quantity overflow rejected before matching
- Automated scenario tests using CTest; builds with MSVC or modern MinGW/GCC

**Not yet implemented:** stop/market orders, multi-symbol routing, persistent global order-ID uniqueness, network protocol, durable event log, concurrency, benchmarks, or web dashboard. Do not claim low-latency performance without an explicitly measured benchmark.

## Requirements

- CMake 3.20+
- A C++20 compiler (MSVC from Visual Studio or current MSYS2 UCRT64 GCC)

## Build / test in VS Code PowerShell

### Existing MSVC + NMake build (`build/`)

Use your working PowerShell developer environment:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
.\build\exchangecore.exe
```

### MSYS2 UCRT64 GCC (`build-gcc/`)

If the compiler isn't already on PATH, add it temporarily:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

If `build-gcc` already has a successful CMake configuration:

```powershell
cmake --build build-gcc
ctest --test-dir build-gcc --output-on-failure
.\build-gcc\exchangecore.exe
```

If you need to configure GCC from scratch instead:

```powershell
cmake -S . -B build-gcc -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER="C:/msys64/ucrt64/bin/g++.exe" -DCMAKE_MAKE_PROGRAM="C:/msys64/ucrt64/bin/mingw32-make.exe"
cmake --build build-gcc
```

Keep separate build directories for the different compiler/toolchain combinations.

### Expected demo output

```text
ExchangeCore v0.3 - Price-Time Priority Matching
Trade: maker=1 taker=3 qty=50 price=10100 ticks
Trade: maker=2 taker=3 qty=20 price=10200 ticks
Executed quantity: 70
Remaining resting orders: 1
Best ask: 10200 ticks, 10 units
```

## API (v0.3)

```cpp
OrderBook book;
Submission r = book.submit({1, Side::Sell, 100, 10});
Submission s = book.submit({2, Side::Buy, 101, 4});
// s.status == SubmitStatus::Accepted
// s.executed_quantity == 4
// s.resting_quantity == 0
// s.trades[0] == {maker_id=1, taker_id=2, price_ticks=100, quantity=4}
```

`submit` replaces the old passive-only `add` API. A marketable order **executes** rather than returning `WouldCross`.

## Project layout

- `include/exchangecore/` — order, trade, and order book public types
- `src/` — C++ matching engine implementation and CLI demo
- `tests/` — automated correctness scenarios (no additional testing dependencies)
- `docs/` — design notes and invariants
- `benchmarks/` — performance benchmarks planned for v0.4

## Next steps

- v0.4: randomized/model-based tests, sanitizer runs, benchmark harness with reproducible workload generation
- v0.5+: event log/replay, API server, dashboard, performance profiling

See [docs/design.md](docs/design.md).
