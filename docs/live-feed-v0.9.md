# v0.9 — Live Kraken spot market-data collector

**Two independent systems**: ExchangeCore's C++ price-time priority matching engine
executes **simulated** orders; the new Python `market_data` package receives **real**
Kraken spot market data. Kraken's `book` channel is **L2 aggregated prices and sizes**, not
individual order messages, and cannot reproduce Kraken's true FIFO queue. No component
sends orders to Kraken or requires an API key.

## Public market-data protocol

- Endpoint: `wss://ws.kraken.com/v2` (TLS).
- Subscribe `book`, initial snapshot requested, depth 10 by default; optionally
  subscribe `trade`, with no historical snapshot.
- Parse fractional JSON values as `Decimal`. Maintain a separate L2 state per symbol.
- Apply all book updates in message order; quantity 0 removes price level; truncate
  to the requested depth; compare CRC32 over top 10 asks low-to-high plus bids high-to-low.
- If a message is malformed or checksum mismatches, **abandon that connection's state**
  and reconnect to request fresh snapshots. After a disconnect there is no catch-up
  guarantee; recording a fresh session makes gaps visible to offline replay.
- Archive only validated book/trade messages with the exact raw JSON text, UTC receive time,
  and session ID. Live stream errors and acknowledgments are not archived.
- Replay consumes the archive with the same validation logic, offline. It will reject
  updates that lack their session's snapshot.

## Windows + VS Code PowerShell quickstart

```powershell
py -m pip install -r market_data/requirements.txt
py -m unittest discover -s market_data/tests -v
py -m market_data.collector --symbols BTC/USD ETH/USD --depth 10 --duration 60
```

Look for a printed file in `market_data/records/` and replay the **actual** path:

```powershell
py -m market_data.replay 'market_data/records/kraken-YYYYMMDDTHHMMSSZ.jsonl'
```

The sample generator uses synthetic data and requires no network:

```powershell
py -m market_data.sample
py -m market_data.replay market_data/records/synthetic-fixture.jsonl --symbols BTC/USD
```

Other collector flags: `--no-trades`, `--print-every 25`, `--max-events 500`,
`--output market_data/records/my-capture.jsonl`. The recorder refuses to overwrite
an existing file. Recording is local-only and excluded from Git.

## Engineering caveats

- Measurements are local receive times, **not exchange execution latency**. Neither
  Kraken L2 market depth nor trade events are customer orders in our C++ matcher.
- CRC32 covers the top ten levels only; it cannot prove the completeness of every trade
  or detect events missed during a broken connection beyond a mismatching book checksum.
- Network availability, firewall restrictions, exchange outages, and data usage rights
  may prevent data collection or public display. A local test of the adapter is not
  evidence that a public live feed has successfully connected.
- Recording currently writes JSONL synchronously; it is deliberately not benchmarked
  together with the low-latency C++ matching engine.
- **Before publishing a public live-data dashboard**, verify Kraken's redistribution
  policy and obtain permission where required. Kraken's API overview indicates certain
  non-personal commercial market-data uses require advance consent:
  `marketdata@kraken.com`. Do not commit raw captures.

## Official docs

- https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book
- https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade
- https://docs.kraken.com/exchange/guides/websockets/book-checksum-v2
- https://docs.kraken.com/api/docs/guides/global-intro/
