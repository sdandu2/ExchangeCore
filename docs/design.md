# ExchangeCore v0.3 — Matching engine design

## Scope

Single-threaded, in-memory limit-order matching, one logical order book; no persistence or real-money functionality. Marketable limit orders execute immediately with **price-time priority**. Only any unfilled remainder rests.

## Data structures

- `std::map` of bids sorted highest-price-first; asks sorted lowest-price-first
- Each price level holds a `std::list<Order>` in arrival order; `front()` is first to execute
- `unordered_map<OrderId, Location>` tracks *active resting* orders for efficient cancellation
- `PriceLevel.total_quantity` caches the remaining total at that price
- An execution is reported as `{maker_id, taker_id, price_ticks, quantity}`

## Matching protocol

1. Validate order fields, side, and active ID uniqueness.
2. Preflight the portion that would rest and reject an aggregate-level quantity overflow *before any trade*. This uses a read-only scan of crossing price levels.
3. If marketable, repeatedly match the most competitive price level. At each level, fill the oldest live maker first and execute at the maker's price.
4. Reduce maker and level quantities by fill size; remove completed maker orders and empty price levels.
5. If any incoming quantity remains after all eligible prices, append it at the end of its price level's FIFO list.
6. Return fill records, total executed, and total resting quantities.

## Invariants

1. Active order IDs are unique; fully filled or canceled IDs may be reused (until a global order ID policy is added).
2. Every live order has positive remaining quantity and valid price.
3. No crossed resting market remains after an accepted submission.
4. Maker price determines execution price; existing resting orders have priority at their price.
5. At each level, cached total equals the sum of remaining maker quantities.
6. Incoming quantity equals executed quantity plus resting quantity.
7. Rejected invalid submissions execute no trades and do not mutate the book.
8. Empty price levels are removed.

## Complexity

Let `P` = number of levels and `F` = number of maker orders filled.

- Validate/check active ID: expected O(1) hash lookup
- Preflight: O(P) worst case for crossing levels
- Match: O(F + P log P) worst case including traversing/removing crossed levels and ID erasures
- Cancel: expected O(1) ID lookup + O(log P) map lookup/removal
- Best bid/ask: O(1)

The data structures prioritize clarity/correctness over low latency. Later versions will measure these overheads before altering them.

## Known limitations

- Single-threaded and non-durable; not a production exchange.
- No per-instrument tick size, multi-symbol support, market orders, trading sessions, or exchange risk checks.
- Does not provide strong exception safety if allocation fails mid-match; a future hardened transaction layer would address this.
- Every submitted order gets fill vectors allocated as needed. Not tuned for low-latency workloads.
- No throughput or latency numbers claimed for v0.3.

## Testing

The test binary verifies the original passive order book behavior where still relevant, plus exact-price, FIFO, partial, sweep, cancellation, limit, overflow, duplicate, and quantity-conservation cases. CTest registers the test binary as one executable, and the binary prints the count of internal cases.

## v0.4 — Performance harness

The core order-book API and matching semantics are unchanged. `exchangecore_bench` is a separate executable, linked against the same `exchangecore_core` library as the demo and tests. It generates input outside measured regions, validates event outcomes through deterministic replay, and reports both bulk throughput and individually sampled latency. See [benchmarking.md](benchmarking.md) for measurement methodology, workload mix, and limitations.
