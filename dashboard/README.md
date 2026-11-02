# ExchangeCore v1.3 — Read-Only Market And Engineering Terminal

The React + TypeScript dashboard subscribes to **the existing local Python WebSocket
stream**, not directly to Kraken. It displays only checksum-validated full L2 books
and reported market trades, and it cannot send orders.

## Prerequisites

- Node.js 22 LTS recommended (or Node 20.19+), plus npm.
- Python market-data dependencies installed from `market_data/requirements.txt`.

## Run live (Windows / VS Code)

At the repository root, use two PowerShell terminals:

**Terminal 1**

```powershell
py -m market_data.stream_server --symbols BTC/USD ETH/USD --duration 3600 --linger 60
```

**Terminal 2**

```powershell
cd dashboard
npm install
npm run dev
```

Open **http://127.0.0.1:5173** in your browser.
The dashboard connects automatically to `ws://127.0.0.1:8765`.
No API keys are required. Start the Python server first; the dashboard also retries
when the server is unavailable.

## Offline replay

Start the Python backend with a recording from `market_data/records/`:

```powershell
py -m market_data.stream_server --replay "market_data/records/kraken-YOUR-RECORDING.jsonl" --symbols BTC/USD ETH/USD --speed 1 --linger 60
```

For the bundled **synthetic** fixture:

```powershell
py -m market_data.stream_server --replay market_data/records/synthetic-fixture.jsonl --symbols BTC/USD --linger 60
```

The UI labels recorded vs. live feed from the server's `mode` field. It never
invents prices, trades, or volumes if the feed is disconnected.

## Build

```powershell
cd dashboard
npm install
npm run build
```

`dist/` is ignored. The frontend is a read-only local visualization and is not
configured for public hosting of Kraken data. Review provider permissions and
build an approved data-delivery architecture before a public demo.

## Data integrity and limitations

- Market-book data is accepted only with `schema=exchangecore.market.v1` and
  `verified=true`, after verification in the Python adapter.
- `reset` clears cached prices, trades and chart history across sessions.
- Price history begins when **this browser tab** receives new books; it is not
  an exchange historical candle chart.
- Depth is aggregated **L2**, not individual orders or exchange order priority.
- Local time and exchange timestamps can differ; the UI does not claim precise
  network latency or synchronization.
- React renders at most ~7 FPS while the message collector accepts all events.
  A slow consumer is disconnected by the Python server to avoid silent loss.
- Market trades come from the feed, while the C++ matching engine is independent
  and used for deterministic simulations and performance benchmarks.
- This is read-only market visualization. No paper-order entry is enabled yet.

## Recorded-data controls (v1.2)
Start the Python backend in replay mode with a saved JSONL capture as shown in [`docs/replay-v1.2.md`](../docs/replay-v1.2.md). The toolbar appears only for recorded sessions. Live streams remain read-only and never accept commands.

## Engineering Performance (v1.3)

Open `http://127.0.0.1:5173/#performance` or select **Engine Performance** in the left navigation. The interactive comparison is based on archived user-run C++ measurements and is available even without a Python stream server. See [methodology and CSV provenance](../docs/engine-performance-v1.3.md). This is not a real-time performance monitor or a Kraken latency test.
