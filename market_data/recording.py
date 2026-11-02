"""Store validated, raw Kraken JSON frames in JSONL; replay them offline."""
from datetime import datetime, timezone
from decimal import Decimal
import json
from pathlib import Path
from .book import L2Book, FeedError


def parse(raw):
    message = json.loads(raw, parse_float=Decimal)
    if not isinstance(message, dict):
        raise FeedError('Market event must be a JSON object')
    return message


def envelope(raw, session, received_at=None):
    return json.dumps({'source': 'kraken-spot-ws-v2', 'session': session,
                       'received_at': received_at or datetime.now(timezone.utc).isoformat(),
                       'raw': raw}, separators=(',', ':')) + '\n'


class Session:
    def __init__(self, symbols, depth):
        self.books = {symbol: L2Book(symbol, depth) for symbol in symbols}
        self.book_count = 0
        self.trade_messages = 0
        self.trades = 0

    def accept(self, message):
        if message.get('channel') == 'book':
            data = message.get('data')
            if not isinstance(data, list) or len(data) != 1 or not isinstance(data[0], dict):
                raise FeedError('Malformed book event')
            symbol = data[0].get('symbol')
            if symbol not in self.books:
                raise FeedError(f'Unexpected symbol {symbol}')
            self.books[symbol].apply(message)
            self.book_count += 1
            return symbol
        if message.get('channel') == 'trade':
            if message.get('type') not in ('snapshot', 'update'):
                raise FeedError('Invalid trade event type')
            trades = message.get('data')
            if not isinstance(trades, list) or any(not isinstance(t, dict) or t.get('symbol') not in self.books for t in trades):
                raise FeedError('Malformed trade event')
            self.trade_messages += 1
            self.trades += len(trades)
            return 'trade'
        return None


def replay(path, symbols, depth=10):
    current_id = None
    session = None
    count = {'sessions': 0, 'book_messages': 0, 'trade_messages': 0, 'trades': 0}
    with Path(path).open(encoding='utf-8') as stream:
        for number, line in enumerate(stream, 1):
            try:
                rec = json.loads(line)
                if rec['source'] != 'kraken-spot-ws-v2':
                    raise FeedError('Unknown source')
                sid = rec['session']
                if not isinstance(sid, int) or isinstance(sid, bool) or sid <= 0:
                    raise FeedError('Invalid session number')
                if current_id != sid:
                    if current_id is not None and sid < current_id:
                        raise FeedError('Sessions out of order')
                    current_id = sid
                    session = Session(symbols, depth)  # reconnect requires new snapshot
                    count['sessions'] += 1
                kind = session.accept(parse(rec['raw']))
                if kind == 'trade':
                    count['trade_messages'] += 1
                    count['trades'] += len(parse(rec['raw'])['data'])
                elif kind is not None:
                    count['book_messages'] += 1
                else:
                    raise FeedError('Archive contains non-data frame')
            except (KeyError, TypeError, ValueError) as e:
                raise FeedError(f'Replay failed on line {number}: {e}') from e
    if session is None:
        raise FeedError('Archive is empty')
    count['last_books'] = {symbol: book.quote() for symbol, book in session.books.items()}
    return count
