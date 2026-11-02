"""Validated, bounded local-only historical playback with pause, seek and speed.

Only book and trade events are replayable; this module never submits orders.
"""
import asyncio
from dataclasses import dataclass
from datetime import datetime
import json
import math
from pathlib import Path

from .book import FeedError
from .recording import Session, parse


@dataclass(frozen=True)
class Record:
    source_session: int
    timestamp: datetime
    received_at: str
    message: dict
    kind: str


def load_recording(path, symbols, depth, max_bytes=128 * 1024 * 1024):
    path = Path(path)
    if path.stat().st_size > max_bytes:
        raise FeedError('Recording exceeds local replay memory limit (128 MiB)')
    records = []
    last_session = 0
    last_time = None
    state = None
    with path.open(encoding='utf-8') as source:
        for line_number, line in enumerate(source, 1):
            try:
                item = json.loads(line)
                if item['source'] != 'kraken-spot-ws-v2':
                    raise FeedError('Unexpected data source')
                sid = item['session']
                if type(sid) is not int or sid < 1 or sid < last_session:
                    raise FeedError('Invalid or reversed source session')
                received_at = item['received_at']
                stamp = datetime.fromisoformat(received_at.replace('Z', '+00:00'))
                if stamp.tzinfo is None:
                    raise FeedError('Timestamps must be timezone aware')
                if sid != last_session:
                    state = Session(symbols, depth)
                    last_time = None
                if last_time and stamp < last_time:
                    raise FeedError('Recording time moved backward within a session')
                message = parse(item['raw'])
                kind = state.accept(message)
                if kind is None:
                    raise FeedError('Unknown market event')
                records.append(Record(sid, stamp, received_at, message, kind))
                last_session, last_time = sid, stamp
            except (KeyError, TypeError, ValueError, OverflowError) as error:
                raise FeedError(f'Recording line {line_number}: {error}') from error
    if not records:
        raise FeedError('Recording is empty')
    return records


class ReplayPlayer:
    """A single controlled playback session, shared by local WebSocket clients."""
    SPEEDS = (0.25, 0.5, 1.0, 2.0, 4.0, 8.0)

    def __init__(self, records, hub, symbols, depth, speed=1.0):
        if not records or speed not in self.SPEEDS:
            raise ValueError('Invalid playback configuration')
        self.records = records
        self.hub = hub
        self.symbols, self.depth = symbols, depth
        self.speed = float(speed)
        self.paused = False
        self.cursor = 0
        self.changed = asyncio.Event()
        self._state = None
        self._source_session = None
        self._last_stamp = None
        self.elapsed = 0.0
        self.duration = sum(max(0, (records[i].timestamp - records[i-1].timestamp).total_seconds())
                            for i in range(1, len(records)) if records[i].source_session == records[i-1].source_session)
        self.hub.replay_controller = self

    def info(self):
        return {'schema': 'exchangecore.market.v1', 'type': 'replay_state', 'mode': 'replay',
                'cursor': self.cursor, 'total': len(self.records),
                'progress': self.cursor / len(self.records),
                'paused': self.paused, 'finished': self.cursor >= len(self.records),
                'speed': self.speed, 'elapsed_seconds': round(self.elapsed, 3),
                'duration_seconds': round(self.duration, 3),
                'source_time': self.records[max(0, self.cursor - 1)].received_at if self.cursor else None}

    def announce(self):
        self.hub._broadcast(self.info())

    def _new_epoch(self, phase='replaying'):
        sid = self.hub.session + 1
        self.hub.change_phase('connecting', sid)
        self.hub.change_phase(phase, sid)
        self._source_session = None
        self._state = None
        self._last_stamp = None

    def _accept(self, rec, publish=True):
        if rec.source_session != self._source_session:
            self._source_session = rec.source_session
            self._state = Session(self.symbols, self.depth)
            if publish:
                self._new_epoch('paused' if self.paused else 'replaying')
                self._source_session = rec.source_session
                self._state = Session(self.symbols, self.depth)
            self._last_stamp = None
        kind = self._state.accept(rec.message)
        if publish:
            self.hub.publish_verified(kind, rec.message, self._state, self.hub.session, rec.received_at)
        self._last_stamp = rec.timestamp
        return kind

    async def seek(self, progress):
        # Re-apply validated records; this replay never reuses stale state from a later cursor.
        target = min(len(self.records), int(progress * len(self.records)))
        self._new_epoch('paused' if self.paused else 'replaying')
        latest_books = {}
        recent_trades = []
        elapsed = 0.0
        prior = None
        for rec in self.records[:target]:
            if prior is not None and rec.source_session == prior.source_session:
                elapsed += max(0, (rec.timestamp - prior.timestamp).total_seconds())
            kind = self._accept(rec, publish=False)
            if kind == 'trade':
                recent_trades.append(rec)
            else:
                latest_books[kind] = rec
            if prior is not None and rec.source_session != prior.source_session:
                latest_books, recent_trades = {kind: rec} if kind != 'trade' else {}, [rec] if kind == 'trade' else []
            prior = rec
        for symbol, rec in latest_books.items():
            if self._state.books[symbol].ready:
                self.hub.publish_verified(symbol, rec.message, self._state, self.hub.session, rec.received_at)
        for rec in recent_trades[-20:]:
            self.hub.publish_verified('trade', rec.message, self._state, self.hub.session, rec.received_at)
        self.cursor = target
        self.elapsed = elapsed
        self._last_stamp = self.records[target - 1].timestamp if target else None
        self.hub.change_phase('paused' if self.paused else 'replaying', self.hub.session)
        self.announce()
        self.changed.set()

    async def command(self, raw):
        if not isinstance(raw, dict) or raw.get('type') != 'replay_control':
            raise ValueError('Only replay controls are permitted')
        action = raw.get('action')
        if action == 'pause':
            self.paused = True
            if self.hub.phase == 'replaying':
                self.hub.change_phase('paused', self.hub.session)
        elif action == 'resume':
            self.paused = False
            if self.hub.phase in ('paused', 'finished'):
                self.hub.change_phase('replaying', self.hub.session)
        elif action == 'speed':
            value = raw.get('speed')
            if type(value) not in (int, float) or float(value) not in self.SPEEDS:
                raise ValueError('Unsupported replay speed')
            self.speed = float(value)
        elif action == 'seek':
            value = raw.get('progress')
            if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1:
                raise ValueError('Seek progress must be between 0 and 1')
            await self.seek(float(value))
            return
        else:
            raise ValueError('Unknown replay action')
        self.announce()
        self.changed.set()

    async def run(self):
        self._new_epoch()
        self.announce()
        while True:
            if self.changed.is_set():
                self.changed.clear()
            if self.cursor >= len(self.records):
                self.hub.change_phase('finished', self.hub.session)
                self.announce()
                self.changed.clear()
                await self.changed.wait()
                if self.cursor >= len(self.records) and not self.paused:
                    await self.seek(0)
                continue
            if self.paused:
                self.changed.clear()
                await self.changed.wait()
                continue
            rec = self.records[self.cursor]
            if self._last_stamp is not None and rec.source_session == self._source_session:
                delay = max(0, (rec.timestamp - self._last_stamp).total_seconds()) / self.speed
                if delay > 0:
                    self.changed.clear()
                    try:
                        await asyncio.wait_for(self.changed.wait(), delay)
                        continue
                    except asyncio.TimeoutError:
                        pass
            if self.paused or self.changed.is_set():
                continue
            if self._last_stamp and rec.source_session == self._source_session:
                self.elapsed += max(0, (rec.timestamp - self._last_stamp).total_seconds())
            self._accept(rec)
            self.cursor += 1
            if self.cursor % 25 == 0:
                await asyncio.sleep(0)  # Allow control commands during dense same-time bursts.
            if self.cursor % 30 == 0 or self.cursor == len(self.records):
                self.announce()
