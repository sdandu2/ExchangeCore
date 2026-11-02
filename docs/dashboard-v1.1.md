# v1.1 — Local React market terminal

## Architecture

The Vite/React/TypeScript frontend is located under `dashboard/`. It never
requests data directly from Kraken or the C++ matcher. It consumes the existing
`exchangecore.market.v1` local read-only WebSocket schema.

- **book**: a full checksum-verified L2 snapshot; decimal strings retained until
  formatted. Best bid and ask are from the server's verified market book.
- **trades**: exchange trades, not simulated orders. The tape filters by symbol.
- **status**: source mode (live/replay), session, phase, health, freshness,
  message counts. Only a healthy selected-symbol book is labelled live.
- **reset**: clear all book data, trade tape and in-browser price history.
- **hello**: confirms read-only socket identity.

The frontend batches UI updates every ~140 ms to avoid repainting at feed rates.
It still parses all socket messages; if the server's bounded fanout queue
fills, the server explicitly disconnects the client (code 1013), forcing a
reconnect and fresh subscription.

The book is **aggregated L2 liquidity**. It cannot reconstruct Kraken's
individual order FIFO priority. The chart is a short, in-browser sample of
observed bid/ask *midpoints*, not a complete historical price feed.

## Development and testing

```powershell
py -m market_data.stream_server --symbols BTC/USD ETH/USD --duration 3600
# New VS Code PowerShell:
cd dashboard
npm install
npm run dev
# http://127.0.0.1:5173
```

`npm run build` runs TypeScript checks and creates `dashboard/dist`.
`npm run test:unit` runs Node 22+ pure-function tests. Backend Python tests
and CMake/CTest are unchanged. GitHub Actions has a new frontend job.

**Restrictions:** local read-only development app; no order entry, no public
Kraken redistribution, no end-to-end network latency guarantee. Deployment
requires explicit security and data-rights review. No generated or synthetic
prices appear in the main dashboard when disconnected.
