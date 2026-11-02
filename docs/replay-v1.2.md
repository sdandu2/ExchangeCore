# v1.2 — Controlled, verified historical replay

Replay uses the exact JSONL capture written by the v0.9/v1.0 collector and **never** submits real or simulated orders.

## Start

From the ExchangeCore project root:

```powershell
py -m pip install -r market_data/requirements.txt
py -m market_data.stream_server --replay 'market_data/records/kraken-REPLACE.jsonl' --symbols BTC/USD ETH/USD --depth 10 --speed 1
```

In a second terminal:

```powershell
cd dashboard
npm install
npm run build
npm run dev
```

Open http://127.0.0.1:5173 and use the green **Historical Replay** toolbar.

In live mode use `py -m market_data.stream_server --symbols BTC/USD ETH/USD --duration 1800` instead.

## Controls

- **Pause / Resume:** stop or resume advancing the recording; read-only book can still be inspected.
- **Speed:** 0.25×, 0.5×, 1×, 2×, 4×, or 8× historical speed.
- **Seek:** select a percentage of the recorded **event count**, then press **Jump**. The server validates and reconstructs all preceding book events and sends a fresh full order book, resetting frontend history/tape for the new playback position. Event percentage is **not** a normalized elapsed-time percentage.
- **Recorded source timestamp:** shown in the instrument header and playback toolbar; this is not the computer's current time.

The replay server stays on localhost after it reaches the end, enabling users to seek or restart without restarting Python. Stop with Ctrl+C.

The replay is fully validated before playback: each incoming snapshot and incremental update must pass the Kraken L2 checksum. Recordings over 128 MiB are rejected to cap in-memory buffering. For larger recordings, future iterations should implement disk-backed checkpoints.

Only a narrow `replay_control` message (`pause`, `resume`, `seek`, `speed`) is accepted **in replay mode**. The live feed rejects client messages with WebSocket 1008. Browsers from other origins are denied by the local server; the Python watcher without an Origin header remains supported.

## Fixes and limitations

- Mid-price chart x positions are based on recorded receive timestamps. Coalesced frames are sampled, not a complete OHLC series.
- Order-book third column is renamed **VALUE (USD)** (price × quantity, not cumulative depth).
- Unexpected browser tab closes/disconnects no longer emit noisy Python tracebacks.
- Trade tape can be paused locally for inspection. This does not pause or buffer the exchange and is not an archival trade viewer.
- The schematic market-depth chart is **not price-scaled**. Future work should provide a price axis, spread-normalized zoom, and interactive crosshair.
- Entire project is a read-only local research/demo tool; no claim of real trade execution.
- Browser production build needs Node 20.19+ or 22.12+ and `npm install` access. Verify on your own machine.
