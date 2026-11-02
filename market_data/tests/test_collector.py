"""Optional local WebSocket end-to-end test; never connects to Kraken."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from decimal import Decimal as D
from market_data.book import crc32_book
from market_data.recording import replay

try:
    from websockets.asyncio.server import serve
    from market_data.collector import collect
    HAVE_WS = True
except ImportError:
    HAVE_WS = False


@unittest.skipUnless(HAVE_WS, 'websockets dependency not installed')
class CollectorTests(unittest.IsolatedAsyncioTestCase):
    async def test_local_websocket_capture_and_replay(self):
        async def server_handler(ws):
            a = json.loads(await ws.recv())
            b = json.loads(await ws.recv())
            self.assertEqual(a['params']['channel'], 'book')
            self.assertEqual(b['params']['channel'], 'trade')
            snapshot = {'channel': 'book', 'type': 'snapshot', 'data': [{
                'symbol': 'BTC/USD',
                'bids': [{'price': '25000.0', 'qty': '0.10000000'}],
                'asks': [{'price': '25000.1', 'qty': '0.20000000'}],
                'checksum': crc32_book({D('25000.0'): D('0.10000000')},
                                       {D('25000.1'): D('0.20000000')})}]}
            trade = {'channel': 'trade', 'type': 'update', 'data': [
                {'symbol': 'BTC/USD', 'price': 25000, 'qty': 1}]}
            await ws.send(json.dumps(snapshot))
            await ws.send(json.dumps(trade))
            await ws.wait_closed()

        with TemporaryDirectory() as temp:
            file = Path(temp)/'test.jsonl'
            async with serve(server_handler, '127.0.0.1', 0) as server:
                port = server.sockets[0].getsockname()[1]
                result = await collect(['BTC/USD'], 10, 5, file, max_events=2,
                                       print_every=0, endpoint=f'ws://127.0.0.1:{port}')
            self.assertEqual(result['book_messages'], 1)
            self.assertEqual(result['trade_messages'], 1)
            self.assertEqual(result['trades'], 1)
            replayed = replay(file, ['BTC/USD'])
            self.assertEqual(replayed['book_messages'], 1)
            self.assertEqual(replayed['trade_messages'], 1)
            self.assertEqual(replayed['sessions'], 1)
