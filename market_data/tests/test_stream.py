"""Offline tests for bounded, local-only, checksum-validated feed streaming."""
import asyncio
from datetime import datetime, timezone
from decimal import Decimal as D
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from market_data.book import FeedError, crc32_book
from market_data.recording import Session, envelope
from market_data.stream import MarketHub, SCHEMA
from market_data.stream_server import stream_replay

try:
    from websockets.asyncio.client import connect
    from websockets.asyncio.server import serve
    HAVE_WEBSOCKETS = True
except ImportError:
    HAVE_WEBSOCKETS = False


def snapshot(symbol='BTC/USD', bid='25000.0', ask='25000.1'):
    bids = {D(bid): D('0.10000000')}
    asks = {D(ask): D('0.20000000')}
    return {'channel': 'book', 'type': 'snapshot', 'data': [{
        'symbol': symbol, 'bids': [{'price': bid, 'qty': '0.10000000'}],
        'asks': [{'price': ask, 'qty': '0.20000000'}],
        'checksum': crc32_book(bids, asks)}]}


class StreamCoreTests(unittest.TestCase):
    def test_verified_snapshot_and_decimal_preservation(self):
        state = Session(['BTC/USD'], 10)
        hub = MarketHub(['BTC/USD'], 10, 'live')
        hub.change_phase('connected', 1)
        msg = snapshot()
        kind = state.accept(msg)
        hub.publish_verified(kind, msg, state, 1, '2026-11-02T13:25:02+00:00')
        book = hub.books['BTC/USD']
        self.assertEqual(book['best_bid'], '25000.0')
        self.assertEqual(book['spread'], '0.1')
        self.assertTrue(book['verified'])
        self.assertTrue(hub.status()['healthy'])
        self.assertEqual(book['schema'], SCHEMA)

    def test_reconnect_invalidates_old_quotes(self):
        hub = MarketHub(['BTC/USD'], 10, 'live')
        state = Session(['BTC/USD'], 10)
        hub.change_phase('connected', 1)
        msg = snapshot()
        hub.publish_verified(state.accept(msg), msg, state, 1, 'time')
        hub.change_phase('reconnecting', 1)
        self.assertFalse(hub.books)
        self.assertFalse(hub.status()['healthy'])
        hub.change_phase('connected', 2)
        with self.assertRaises(ValueError):
            hub.publish_verified('BTC/USD', msg, state, 1, 'time')
        self.assertFalse(hub.books)

    def test_each_symbol_has_independent_stale_indicator(self):
        hub = MarketHub(['BTC/USD', 'ETH/USD'], 10, 'live', stale_seconds=0.1)
        state = Session(['BTC/USD', 'ETH/USD'], 10)
        hub.change_phase('connected', 1)
        for sym in ['BTC/USD', 'ETH/USD']:
            msg = snapshot(symbol=sym)
            hub.publish_verified(state.accept(msg), msg, state, 1, 'now')
        hub.last_book_at['ETH/USD'] -= 1
        self.assertFalse(hub.status()['healthy'])
        self.assertEqual(hub.status()['stale_symbols'], ['ETH/USD'])

    def test_invalid_crc_not_published(self):
        hub = MarketHub(['BTC/USD'], 10, 'live')
        state = Session(['BTC/USD'], 10)
        hub.change_phase('connected', 1)
        msg = snapshot()
        msg['data'][0]['checksum'] += 1
        with self.assertRaises(FeedError):
            state.accept(msg)
        self.assertEqual(hub.book_messages, 0)

    def test_health_stale(self):
        hub = MarketHub(['BTC/USD'], 10, 'live', stale_seconds=0.1)
        hub.change_phase('connected', 1)
        state = Session(['BTC/USD'], 10)
        msg = snapshot()
        hub.publish_verified(state.accept(msg), msg, state, 1, 'now')
        hub.last_book_at['BTC/USD'] -= 1
        self.assertTrue(hub.status()['stale'])
        self.assertFalse(hub.status()['healthy'])


@unittest.skipUnless(HAVE_WEBSOCKETS, 'websockets dependency unavailable')
class StreamSocketTests(unittest.IsolatedAsyncioTestCase):
    async def test_new_subscriber_gets_verified_full_book(self):
        hub = MarketHub(['BTC/USD'], 10, 'replay')
        hub.change_phase('replaying', 1)
        state = Session(['BTC/USD'], 10)
        msg = snapshot()
        hub.publish_verified(state.accept(msg), msg, state, 1, '2026-11-02T13:25:02+00:00')
        async with serve(hub.handle, '127.0.0.1', 0) as server:
            port = server.sockets[0].getsockname()[1]
            async with connect(f'ws://127.0.0.1:{port}') as client:
                hello = json.loads(await asyncio.wait_for(client.recv(), 2))
                status = json.loads(await asyncio.wait_for(client.recv(), 2))
                book = json.loads(await asyncio.wait_for(client.recv(), 2))
                self.assertEqual(hello['type'], 'hello')
                self.assertTrue(hello['read_only'])
                self.assertEqual(status['type'], 'status')
                self.assertTrue(status['healthy'])
                self.assertEqual(book['best_ask'], '25000.1')
                self.assertEqual(book['mode'], 'replay')
                await client.send('submit order')
                await asyncio.wait_for(client.wait_closed(), 2)
                self.assertEqual(client.close_code, 1008)

    async def test_saved_events_replay_to_socket(self):
        hub = MarketHub(['BTC/USD'], 10, 'replay')
        with TemporaryDirectory() as temp:
            path = Path(temp) / 'capture.jsonl'
            msg = snapshot()
            trade = {'channel': 'trade', 'type': 'update', 'data': [
                {'symbol': 'BTC/USD', 'price': 25000.05, 'qty': 0.01}]}
            path.write_text(envelope(json.dumps(msg), 1, '2026-11-02T13:25:02+00:00') +
                            envelope(json.dumps(trade), 1, '2026-11-02T13:25:02.001+00:00'),
                            encoding='utf-8')
            async with serve(hub.handle, '127.0.0.1', 0) as server:
                port = server.sockets[0].getsockname()[1]
                async with connect(f'ws://127.0.0.1:{port}') as client:
                    self.assertEqual(json.loads(await client.recv())['type'], 'hello')
                    count = await stream_replay(path, ['BTC/USD'], 10, hub, speed=100)
                    self.assertEqual(count, 2)
                    messages = []
                    for _ in range(5):
                        messages.append(json.loads(await asyncio.wait_for(client.recv(), 2)))
                    self.assertIn('book', [m['type'] for m in messages])
                    self.assertIn('trades', [m['type'] for m in messages])
                    self.assertEqual(hub.status()['book_messages'], 1)
                    self.assertEqual(hub.status()['trades'], 1)
                    self.assertEqual(hub.status()['phase'], 'finished')

    async def test_corrupt_replay_stops_and_never_publishes(self):
        hub = MarketHub(['BTC/USD'], 10, 'replay')
        with TemporaryDirectory() as temp:
            file = Path(temp) / 'bad.jsonl'
            msg = snapshot()
            msg['data'][0]['checksum'] = 1
            file.write_text(envelope(json.dumps(msg), 1), encoding='utf-8')
            with self.assertRaises(FeedError):
                await stream_replay(file, ['BTC/USD'], 10, hub, speed=100)
            self.assertEqual(hub.book_messages, 0)


if __name__ == '__main__':
    unittest.main()
