# v1.0 — Local read-only streaming API

## What changed

ExchangeCore now offers a **loopback-only WebSocket** at `ws://127.0.0.1:8765`.
It can stream **Kraken public L2 snapshots / updates** from the current live collector,
or play back an already captured JSONL file at adjustable speed. A minimal console
client proves the integration before any dashboard is created.

**Nothing here sends orders to Kraken.** The C++ engine continues to simulate trades
and is not connected to the data stream. The local feed reports aggregated L2
market depth; it does **not** expose per-order queues or exchange matching.

## First terminal: start server

```powershell
# A saved capture is easiest for an offline acceptance test:
py -m market_data.stream_server --replay "market_data/records/your-capture.jsonl" --speed 1

# Or capture and stream real Kraken updates for five minutes:
py -m market_data.stream_server --duration 300 --symbols BTC/USD ETH/USD --depth 10
```

The host is fixed to `127.0.0.1`. This unauthenticated local API is **not safe for
public deployment** and must not be placed behind a reverse proxy without explicit
authentication and licensed exchange-data access.

## Second terminal: observe the feed

```powershell
py -m market_data.watch --max-messages 30 --book-every 10
```

The watcher receives `hello`, `status`, `reset`, `book`, and `trades` messages. It
prints occasional book updates to avoid overwhelming the console.

## JSON messages

All outgoing messages contain `schema: exchangecore.market.v1`. Values like
`best_bid`, `best_ask`, `spread`, and `book.bids[].price`/`qty` are **decimal
strings**, never imprecise floating-point values. Book messages include both
sides of the validated L2 book (a **complete depth-N snapshot** on every update)
so a connecting/reconnecting client never needs to reconstruct lost deltas.

Types:

- `hello`: stream identity and `read_only: true`.
- `status`: connection phase, symbols with verified snapshots, per-symbol book age,
  stale symbols, message counts, client counts, and `healthy` flag.
- `reset`: previous book state is invalid; replace all previous snapshots.
- `book`: full validated L2 state for one symbol, verified by Kraken top-10 CRC32.
- `trades`: reports received from Kraken `trade` channel; *not executions by ExchangeCore*.

A `status.healthy` value of `true` requires an active live/replaying connection,
a validated snapshot for each requested symbol, and sufficiently fresh book data.
This indicates **local feed health**, not that the market is open, licensed, or
that a trading strategy is profitable. Kraken timestamps may differ from the
local clock; feed `received_at` is the local UTC receive timestamp, **not**
network latency.

The hub publishes only after the existing `Session.accept` has parsed and
validated the event; checksum mismatches force a new connection and snapshot.
It also retains validated latest books for clients that connect mid-session.
Queues are bounded. A client falling behind is explicitly disconnected with
WebSocket close code 1013 instead of receiving silently dropped market events.
Sending any client request closes the connection with WebSocket code 1008.

## Live recording and replay

The live streaming command writes the same JSONL format as v0.9 to
`market_data/records/kraken-*.jsonl` (ignored by Git).

Offline replay checks each message with the same decimal-safe book validation,
retains recorded order and session boundaries, uses `received_at` gaps divided
by `--speed`, and rejects bad snapshots or out-of-order timestamps.
`--speed 10` is approximately ten times faster playback; rapid playback can
outpace slow subscribers, which will then be disconnected.

Example with a different local port:

```powershell
py -m market_data.stream_server --replay "market_data/records/my-capture.jsonl" --speed 2 --port 8766
py -m market_data.watch --uri ws://127.0.0.1:8766 --max-messages 30
```

**Known constraints:** no persistent external streaming server, no HTTP service,
no UI, no historical reconciliation of events missed during a Kraken disconnect,
no trade execution against Kraken's actual queue. Offline replay replays *market
messages*, not exchange orders. Public redistribution needs provider permission.

## Validation

```powershell
py -m pip install -r market_data/requirements.txt
py -m unittest discover -s market_data/tests -v
ctest --test-dir build-gcc-release --output-on-failure
```

The tests include mock local WebSocket sessions; no Kraken connection is required.
