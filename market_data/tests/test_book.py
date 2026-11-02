"""Run with python -m unittest discover -s market_data/tests -v."""
from decimal import Decimal as D
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from market_data.book import L2Book, FeedError, crc32_book
from market_data.recording import parse, envelope, replay, Session


class MarketDataTests(unittest.TestCase):
    def setUp(self):
        self.bids = {D('99.0'): D('1.23450000')}
        self.asks = {D('101.0'): D('0.15000000')}

    def snapshot(self):
        return {'channel': 'book', 'type': 'snapshot', 'data': [{
            'symbol': 'BTC/USD', 'bids': [{'price': '99.0', 'qty': '1.23450000'}],
            'asks': [{'price': '101.0', 'qty': '0.15000000'}],
            'checksum': crc32_book(self.bids, self.asks)}]}

    def test_official_kraken_checksum_example(self):
        # Official WebSocket v2 CRC32 guide, published example checksum: 3310070434.
        bids = [('45283.5','0.10000000'),('45283.4','1.54582015'),
                ('45282.1','0.10000000'),('45281.0','0.10000000'),
                ('45280.3','1.54592586'),('45279.0','0.07990000'),
                ('45277.6','0.03310103'),('45277.5','0.30000000'),
                ('45277.3','1.54602737'),('45276.6','0.15445238')]
        asks = [('45285.2','0.00100000'),('45286.4','1.54571953'),
                ('45286.6','1.54571109'),('45289.6','1.54560911'),
                ('45290.2','0.15890660'),('45291.8','1.54553491'),
                ('45294.7','0.04454749'),('45296.1','0.35380000'),
                ('45297.5','0.09945542'),('45299.5','0.18772827')]
        self.assertEqual(crc32_book({D(p):D(q) for p,q in bids},
                                    {D(p):D(q) for p,q in asks}),3310070434)

    def test_snapshot_and_change(self):
        book = L2Book('BTC/USD')
        self.assertTrue(book.apply(self.snapshot()))
        changed = {D('100.0'): D('2.50000000')}
        update = {'channel':'book','type':'update','data':[{
            'symbol':'BTC/USD', 'bids':[{'price':'99.0','qty':'0'},
                                       {'price':'100.0','qty':'2.50000000'}],
            'asks':[], 'checksum':crc32_book(changed, self.asks)}]}
        self.assertTrue(book.apply(update))
        self.assertEqual(book.bids, changed)

    def test_missing_snapshot_rejected(self):
        book = L2Book('BTC/USD')
        event = self.snapshot()
        event['type'] = 'update'
        with self.assertRaises(FeedError):
            book.apply(event)

    def test_checksum_mismatch_rolls_back(self):
        book = L2Book('BTC/USD')
        book.apply(self.snapshot())
        copy = book.bids.copy()
        event = {'channel':'book','type':'update','data':[{
            'symbol':'BTC/USD','bids':[{'price':'100.0','qty':'1.0'}],
            'asks':[], 'checksum':1}]}
        with self.assertRaises(FeedError):
            book.apply(event)
        self.assertEqual(book.bids, copy)

    def test_truncate_depth(self):
        bids = {D(str(i)):D('0.02') for i in range(80,101)}
        asks = {D(str(i)):D('0.03') for i in range(101,123)}
        b10 = {p:q for p,q in bids.items() if p >= 91}
        a10 = {p:q for p,q in asks.items() if p <= 110}
        msg = {'channel':'book', 'type':'snapshot','data':[{'symbol':'BTC/USD',
            'bids':[{'price':str(p),'qty':'0.02'} for p in bids],
            'asks':[{'price':str(p),'qty':'0.03'} for p in asks],
            'checksum':crc32_book(b10,a10)}]}
        book = L2Book('BTC/USD')
        book.apply(msg)
        self.assertEqual(len(book.bids),10)
        self.assertEqual(len(book.asks),10)

    def test_replay_decimal_precision(self):
        raw = json.dumps(self.snapshot()).replace('"1.23450000"','1.23450000')
        book = L2Book('BTC/USD')
        book.apply(parse(raw))
        with TemporaryDirectory() as tmp:
            path = Path(tmp)/'data.jsonl'
            path.write_text(envelope(raw,1), encoding='utf-8')
            stats = replay(path, ['BTC/USD'])
            self.assertEqual(stats['book_messages'],1)
            self.assertEqual(stats['sessions'],1)

    def test_trade_is_not_an_order(self):
        state = Session(['BTC/USD'],10)
        self.assertEqual(state.accept({'channel':'trade','type':'update',
                        'data':[{'symbol':'BTC/USD','price':100,'qty':1}]}),'trade')
        self.assertFalse(state.books['BTC/USD'].ready)
        self.assertEqual(state.trades,1)

    def test_reconnect_requires_snapshot(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp)/'bad.jsonl'
            snap = self.snapshot()
            snap['type'] = 'update'
            path.write_text(envelope(json.dumps(self.snapshot()),1) +
                            envelope(json.dumps(snap),2),encoding='utf-8')
            with self.assertRaises(FeedError):
                replay(path,['BTC/USD'])


if __name__ == '__main__':
    unittest.main()
