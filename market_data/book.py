"""Kraken spot WebSocket v2 L2 book; decimal-safe and CRC32 validated."""
from dataclasses import dataclass, field
from decimal import Decimal
import zlib


class FeedError(ValueError):
    """Feed is invalid or requires a fresh WebSocket snapshot."""


def dec(value):
    return Decimal(str(value))


def crc32_book(bids, asks):
    def part(levels, reverse=False):
        out = []
        for price, qty in sorted(levels.items(), reverse=reverse)[:10]:
            out.extend((format(price, 'f').replace('.', '').lstrip('0'),
                        format(qty, 'f').replace('.', '').lstrip('0')))
        return ''.join(out)
    raw = part(asks) + part(bids, True)
    return zlib.crc32(raw.encode('ascii')) & 0xffffffff


def change_levels(levels, incoming):
    if not isinstance(incoming, list):
        raise FeedError('Book levels must be arrays')
    for item in incoming:
        try:
            price, qty = dec(item['price']), dec(item['qty'])
        except (KeyError, TypeError, ValueError) as e:
            raise FeedError(f'Invalid level: {item}') from e
        if not price.is_finite() or not qty.is_finite() or price <= 0 or qty < 0:
            raise FeedError('Invalid price or quantity')
        if qty == 0:
            levels.pop(price, None)
        else:
            levels[price] = qty


@dataclass
class L2Book:
    symbol: str
    depth: int = 10
    bids: dict = field(default_factory=dict)
    asks: dict = field(default_factory=dict)
    ready: bool = False
    updates: int = 0

    def __post_init__(self):
        if self.depth not in (10, 25, 100, 500, 1000):
            raise ValueError('Unsupported Kraken book depth')

    def apply(self, message):
        if message.get('channel') != 'book':
            return False
        kind = message.get('type')
        data = message.get('data')
        if kind not in ('snapshot', 'update') or not isinstance(data, list) or len(data) != 1:
            raise FeedError('Invalid book event structure')
        event = data[0]
        if event.get('symbol') != self.symbol:
            raise FeedError('Unexpected book symbol')
        if kind == 'update' and not self.ready:
            raise FeedError('Missing snapshot; reconnect required')
        bids = {} if kind == 'snapshot' else self.bids.copy()
        asks = {} if kind == 'snapshot' else self.asks.copy()
        change_levels(bids, event.get('bids', []))
        change_levels(asks, event.get('asks', []))
        bids = {p: bids[p] for p in sorted(bids, reverse=True)[:self.depth]}
        asks = {p: asks[p] for p in sorted(asks)[:self.depth]}
        try:
            expected = event['checksum']
            if isinstance(expected, bool) or not isinstance(expected, int):
                raise FeedError('Invalid book checksum type')
        except KeyError as e:
            raise FeedError('Missing CRC32 checksum') from e
        actual = crc32_book(bids, asks)
        if expected != actual:
            raise FeedError(f'{self.symbol}: checksum mismatch: expected {expected}, computed {actual}')
        self.bids, self.asks = bids, asks
        self.ready = True
        self.updates = 0 if kind == 'snapshot' else self.updates + 1
        return True

    def quote(self):
        if not self.ready or not self.bids or not self.asks:
            return f'{self.symbol}: awaiting complete bid/ask book'
        bid, ask = max(self.bids), min(self.asks)
        return (f'{self.symbol}: bid={bid} ({self.bids[bid]}) '
                f'ask={ask} ({self.asks[ask]}) spread={ask-bid} updates={self.updates}')
