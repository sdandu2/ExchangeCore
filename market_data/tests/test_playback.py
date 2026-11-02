"""Playback controls must be deterministic and may never execute trading instructions."""
import asyncio
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from market_data.book import FeedError
from market_data.playback import ReplayPlayer, load_recording
from market_data.recording import envelope
from market_data.stream import MarketHub
from market_data.tests.test_stream import snapshot

try:
    from websockets.asyncio.client import connect
    from websockets.asyncio.server import serve
    HAS_SOCKET = True
except ImportError:
    HAS_SOCKET = False


class ReplayTests(unittest.IsolatedAsyncioTestCase):
    def make_recording(self, temp):
        path = Path(temp) / 'recording.jsonl'
        stamp = datetime(2026, 11, 2, 13, 25, tzinfo=timezone.utc)
        events = [snapshot(),
                  {'channel': 'trade', 'type': 'update', 'data': [
                      {'symbol': 'BTC/USD', 'price': 25000.05, 'qty': 0.01, 'side': 'buy'}]}]
        path.write_text(''.join(envelope(json.dumps(event), 1, (stamp + timedelta(seconds=i)).isoformat()) for i, event in enumerate(events)), encoding='utf-8')
        return path

    async def test_pause_seek_resume_speed(self):
        with TemporaryDirectory() as tmp:
            records = load_recording(self.make_recording(tmp), ['BTC/USD'], 10)
            hub = MarketHub(['BTC/USD'], 10, 'replay')
            replay = ReplayPlayer(records, hub, ['BTC/USD'], 10)
            await replay.command({'type': 'replay_control', 'action': 'pause'})
            self.assertTrue(replay.paused)
            await replay.command({'type': 'replay_control', 'action': 'seek', 'progress': 1})
            self.assertEqual(replay.cursor, 2)
            self.assertEqual(hub.books['BTC/USD']['best_bid'], '25000.0')
            self.assertEqual(hub.trades, 1)
            self.assertTrue(hub.status()['healthy'])
            await replay.command({'type': 'replay_control', 'action': 'speed', 'speed': 4})
            self.assertEqual(replay.speed, 4.0)
            await replay.command({'type': 'replay_control', 'action': 'seek', 'progress': 0})
            self.assertFalse(hub.books)
            self.assertEqual(replay.cursor, 0)
            await replay.command({'type': 'replay_control', 'action': 'resume'})
            self.assertFalse(replay.paused)

    async def test_invalid_controls_and_bad_checksum(self):
        with TemporaryDirectory() as tmp:
            path = self.make_recording(tmp)
            records = load_recording(path, ['BTC/USD'], 10)
            hub = MarketHub(['BTC/USD'], 10, 'replay')
            replay = ReplayPlayer(records, hub, ['BTC/USD'], 10)
            for bad in [
                {'type':'order', 'side':'buy'},
                {'type':'replay_control', 'action':'speed', 'speed':0},
                {'type':'replay_control', 'action':'seek', 'progress':1.1},
            ]:
                with self.assertRaises(ValueError):
                    await replay.command(bad)
            broken = snapshot()
            broken['data'][0]['checksum'] += 12
            path.write_text(envelope(json.dumps(broken), 1), encoding='utf-8')
            with self.assertRaises(FeedError):
                load_recording(path, ['BTC/USD'], 10)

    @unittest.skipUnless(HAS_SOCKET, 'Install websockets')
    async def test_client_can_pause_replay_but_cannot_trade(self):
        with TemporaryDirectory() as tmp:
            records = load_recording(self.make_recording(tmp), ['BTC/USD'], 10)
            hub = MarketHub(['BTC/USD'], 10, 'replay')
            controller = ReplayPlayer(records, hub, ['BTC/USD'], 10)
            async with serve(hub.handle, '127.0.0.1', 0) as server:
                port = server.sockets[0].getsockname()[1]
                async with connect(f'ws://127.0.0.1:{port}') as client:
                    hello = json.loads(await client.recv())
                    self.assertTrue(hello['read_only'])
                    self.assertEqual(json.loads(await client.recv())['type'], 'status')
                    self.assertEqual(json.loads(await client.recv())['type'], 'replay_state')
                    await client.send(json.dumps({'type':'replay_control', 'action':'pause'}))
                    response = json.loads(await asyncio.wait_for(client.recv(), 2))
                    self.assertEqual(response['type'], 'replay_state')
                    self.assertTrue(response['paused'])
                    await client.send(json.dumps({'type':'order', 'side':'buy'}))
                    await asyncio.wait_for(client.wait_closed(), 2)
                    self.assertEqual(client.close_code, 1008)


if __name__ == '__main__':
    unittest.main()
