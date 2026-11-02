"""Bounded, local-only read-only streaming of checksum-validated L2 book state."""
import asyncio
from datetime import datetime, timezone
from decimal import Decimal
import json
import time
from websockets.exceptions import ConnectionClosed

SCHEMA = 'exchangecore.market.v1'
SOURCE = 'kraken-spot-ws-v2'


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def book_message(book, session, received_at, mode):
    """Decimal values are strings: clients must not confuse floats with exact prices."""
    bids = [{'price': str(price), 'qty': str(qty)}
            for price, qty in sorted(book.bids.items(), reverse=True)]
    asks = [{'price': str(price), 'qty': str(qty)}
            for price, qty in sorted(book.asks.items())]
    best_bid = bids[0]['price'] if bids else None
    best_ask = asks[0]['price'] if asks else None
    spread = str(Decimal(best_ask) - Decimal(best_bid)) if best_bid and best_ask else None
    return {'schema': SCHEMA, 'type': 'book', 'mode': mode, 'source': SOURCE,
            'session': session, 'symbol': book.symbol,
            'received_at': received_at, 'depth': book.depth, 'updates': book.updates,
            'best_bid': best_bid, 'best_ask': best_ask, 'spread': spread,
            'bids': bids, 'asks': asks, 'verified': True}


class MarketHub:
    """Broadcast validated data to loopback clients; disconnect slow consumers.

    Never drops a market event silently: a queue overflow closes the affected client.
    Each new client receives the current status and an independent full L2 snapshot.
    The hub is strictly one event-loop / one producer, with no thread synchronization.
    """

    def __init__(self, symbols, depth, mode, stale_seconds=15, queue_limit=256, max_clients=8):
        if mode not in ('live', 'replay') or not symbols or stale_seconds <= 0:
            raise ValueError('Invalid stream configuration')
        if queue_limit < 1 or max_clients < 1:
            raise ValueError('Invalid stream limits')
        self.symbols = list(symbols)
        self.depth = depth
        self.mode = mode
        self.stale_seconds = stale_seconds
        self.queue_limit = queue_limit
        self.max_clients = max_clients
        self.phase = 'starting'
        self.session = 0
        self.reason = None
        self.books = {}
        self.last_book_at = {}  # each symbol can go stale independently
        self.book_messages = 0
        self.trade_messages = 0
        self.trades = 0
        self.slow_clients = 0
        self.clients = {}  # websocket -> bounded queue of serialized JSON
        self.replay_controller = None

    def status(self):
        ages = {symbol: round((time.monotonic() - at) * 1000, 1)
                for symbol, at in self.last_book_at.items()}
        ready = set(self.books) == set(self.symbols)
        running = self.phase in ('connected', 'replaying', 'paused')
        stale_symbols = sorted(symbol for symbol, ms in ages.items()
                               if ms > self.stale_seconds * 1000) if running and self.phase != 'paused' else []
        stale = bool(stale_symbols)
        return {'schema': SCHEMA, 'type': 'status', 'mode': self.mode, 'source': SOURCE,
                'phase': self.phase, 'session': self.session, 'at': utc_now(),
                'ready_symbols': sorted(self.books), 'expected_symbols': self.symbols,
                'healthy': running and ready and not stale,
                'stale': stale, 'stale_symbols': stale_symbols,
                'book_age_ms': ages,
                'book_messages': self.book_messages, 'trade_messages': self.trade_messages,
                'trades': self.trades, 'clients': len(self.clients),
                'slow_clients_disconnected': self.slow_clients, 'reason': self.reason}

    def _broadcast(self, payload):
        encoded = json.dumps(payload, separators=(',', ':'), ensure_ascii=False)
        for ws, queue in list(self.clients.items()):
            try:
                queue.put_nowait(encoded)
            except asyncio.QueueFull:
                self.slow_clients += 1
                # A slow client must re-subscribe to get an honest fresh snapshot.
                self.clients.pop(ws, None)
                asyncio.create_task(ws.close(code=1013, reason='Stream backlog; reconnect'))

    def change_phase(self, phase, session, reason=None):
        if phase not in ('starting', 'connecting', 'connected', 'reconnecting',
                         'replaying', 'paused', 'finished', 'error'):
            raise ValueError('Unknown stream phase')
        if session != self.session or phase in ('connecting', 'reconnecting'):
            self.books.clear()  # do not retain stale bids/asks after a lost connection
            self.last_book_at = {}  # each symbol can go stale independently
            self.session = session
            self._broadcast({'schema': SCHEMA, 'type': 'reset', 'mode': self.mode,
                             'source': SOURCE, 'session': session,
                             'at': utc_now(), 'reason': phase})
        self.phase = phase
        self.reason = reason
        self._broadcast(self.status())

    def publish_verified(self, kind, message, state, session, received_at):
        """Only call after Session.accept completes (including book CRC check)."""
        if session != self.session:
            raise ValueError('Publishing from an outdated session')
        if kind == 'trade':
            self.trade_messages += 1
            self.trades += len(message['data'])
            items = []
            for trade in message['data']:
                items.append({key: str(value) if isinstance(value, Decimal) else value
                              for key, value in trade.items()})
            self._broadcast({'schema': SCHEMA, 'type': 'trades', 'mode': self.mode,
                             'source': SOURCE, 'session': session,
                             'received_at': received_at, 'trades': items})
        elif kind in state.books:
            self.book_messages += 1
            self.last_book_at[kind] = time.monotonic()
            payload = book_message(state.books[kind], session, received_at, self.mode)
            self.books[kind] = payload
            self._broadcast(payload)
        else:
            raise ValueError('Invalid validated market-data kind')

    async def _sender(self, ws, queue):
        while True:
            await ws.send(await queue.get())

    async def handle(self, ws):
        if len(self.clients) >= self.max_clients:
            await ws.close(code=1013, reason='Too many local subscribers')
            return
        queue = asyncio.Queue(maxsize=self.queue_limit)
        self.clients[ws] = queue
        queue.put_nowait(json.dumps({'schema': SCHEMA, 'type': 'hello',
                                     'read_only': True, 'source': SOURCE, 'mode': self.mode}))
        if not queue.full():
            queue.put_nowait(json.dumps(self.status()))
        if self.replay_controller is not None and not queue.full():
            queue.put_nowait(json.dumps(self.replay_controller.info()))
        # On a connection, the latest validated full depth replaces missed deltas.
        for book in self.books.values():
            if queue.full():
                await ws.close(code=1013, reason='Subscription backlog; reconnect')
                self.clients.pop(ws, None)
                return
            queue.put_nowait(json.dumps(book))
        sender = asyncio.create_task(self._sender(ws, queue))
        try:
            async for raw in ws:
                if self.mode != 'replay' or self.replay_controller is None:
                    await ws.close(code=1008, reason='Read-only feed; client messages disallowed')
                    break
                try:
                    if not isinstance(raw, str) or len(raw) > 256:
                        raise ValueError('Invalid control message')
                    await self.replay_controller.command(json.loads(raw))
                except (ValueError, TypeError, json.JSONDecodeError):
                    await ws.close(code=1008, reason='Invalid replay control')
                    break
        except ConnectionClosed:
            # Browser refresh/closed tab often skips the close handshake.
            pass
        finally:
            self.clients.pop(ws, None)
            sender.cancel()
            await asyncio.gather(sender, return_exceptions=True)

    async def heartbeat(self, seconds=2):
        while True:
            await asyncio.sleep(seconds)
            self._broadcast(self.status())
