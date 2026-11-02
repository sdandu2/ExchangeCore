"""Read-only public Kraken v2 WebSocket collector. Never submits actual orders."""
import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import time
from .book import FeedError
from .recording import Session, envelope, parse

ENDPOINT = 'wss://ws.kraken.com/v2'


class SubscriptionRejected(FeedError):
    pass


async def collect(symbols, depth, duration, output, max_events=0, print_every=25, trades=True,
                  endpoint=ENDPOINT, on_event=None, on_status=None):
    try:
        from websockets.asyncio.client import connect
        from websockets.exceptions import WebSocketException
    except ImportError as e:
        raise RuntimeError('Install websockets: py -m pip install -r market_data/requirements.txt') from e
    if duration <= 0 or max_events < 0 or print_every < 0 or not symbols:
        raise ValueError('Invalid collector arguments')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    until = time.monotonic() + duration
    result = {'sessions': 0, 'book_messages': 0, 'trade_messages': 0, 'trades': 0}
    backoff = 1
    # Exclusive creation avoids accidentally truncating old recordings.
    with output.open('x', encoding='utf-8', buffering=1) as writer:
        while time.monotonic() < until:
            result['sessions'] += 1
            sid = result['sessions']
            state = Session(symbols, depth)
            if on_status:
                on_status('connecting', sid, None)
            try:
                async with connect(endpoint, open_timeout=10, ping_interval=20,
                                   ping_timeout=20, max_size=2_000_000) as sock:
                    backoff = 1
                    if on_status:
                        on_status('connected', sid, None)
                    await sock.send(json.dumps({'method': 'subscribe', 'params': {
                        'channel': 'book', 'symbol': symbols, 'depth': depth, 'snapshot': True}}))
                    if trades:
                        await sock.send(json.dumps({'method': 'subscribe', 'params': {
                            'channel': 'trade', 'symbol': symbols, 'snapshot': False}}))
                    print(f'Connected session {sid}: {", ".join(symbols)}', flush=True)
                    while time.monotonic() < until:
                        if max_events and result['book_messages'] + result['trade_messages'] >= max_events:
                            return result
                        try:
                            raw = await asyncio.wait_for(sock.recv(),
                                timeout=min(2., max(.001, until-time.monotonic())))
                        except asyncio.TimeoutError:
                            continue
                        if not isinstance(raw, str):
                            raise FeedError('Expected JSON text message')
                        message = parse(raw)
                        if message.get('method') == 'subscribe':
                            if message.get('success') is False:
                                raise SubscriptionRejected(f'Subscription refused: {message.get("error")}')
                            continue
                        kind = state.accept(message)
                        if kind is None:
                            continue  # heartbeat, status, subscription ACK
                        received_at = datetime.now(timezone.utc).isoformat()
                        writer.write(envelope(raw, sid, received_at))
                        if on_event:
                            on_event(kind, message, state, sid, received_at)
                        if kind == 'trade':
                            result['trade_messages'] += 1
                            result['trades'] += len(message['data'])
                        else:
                            result['book_messages'] += 1
                            if message['type'] == 'snapshot' or (print_every and result['book_messages'] % print_every == 0):
                                print(state.books[kind].quote(), flush=True)
                    return result
            except SubscriptionRejected:
                if on_status:
                    on_status('error', sid, 'Subscription rejected')
                raise  # Invalid symbol/permission cannot be fixed by reconnecting.
            except (OSError, TimeoutError, FeedError, WebSocketException, ValueError) as err:
                # Includes the WebSocket close exception. Never reuse an old L2 book.
                if time.monotonic() >= until:
                    break
                if on_status:
                    on_status('reconnecting', sid, type(err).__name__)
                print(f'{type(err).__name__}: {err}; reconnecting with fresh snapshot', file=sys.stderr)
                await asyncio.sleep(min(backoff, max(0, until-time.monotonic())))
                backoff = min(backoff * 2, 30)
    if on_status:
        on_status('finished', result['sessions'], None)
    return result


def main():
    parser = argparse.ArgumentParser(description='Record and validate Kraken public L2 data')
    parser.add_argument('--symbols', nargs='+', default=['BTC/USD', 'ETH/USD'])
    parser.add_argument('--depth', type=int, choices=[10,25,100,500,1000], default=10)
    parser.add_argument('--duration', type=float, default=60.)
    parser.add_argument('--max-events', type=int, default=0)
    parser.add_argument('--print-every', type=int, default=25)
    parser.add_argument('--no-trades', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    filename = args.output or Path('market_data/records')/f'kraken-{stamp}.jsonl'
    print('Recording Kraken public data to local file:', filename)
    try:
        result = asyncio.run(collect(args.symbols, args.depth, args.duration, filename,
                                     args.max_events, args.print_every, not args.no_trades))
    except (RuntimeError, ValueError, OSError, KeyboardInterrupt) as e:
        print('Collector failed:', str(e), file=sys.stderr)
        return 1
    print('Capture complete:', result)
    print('Verify: py -m market_data.replay', filename)
    return 0 if result['book_messages'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
