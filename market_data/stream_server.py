"""Run a loopback WebSocket service backed by live or replayed validated Kraken data."""
import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
from .book import FeedError
from .collector import collect, ENDPOINT, SubscriptionRejected
from .recording import Session, parse
from .stream import MarketHub
from .playback import ReplayPlayer, load_recording

HOST = '127.0.0.1'  # Never publicly bind unlicensed exchange data / unauthenticated API.


async def stream_replay(path, symbols, depth, hub, speed=1):
    if speed <= 0:
        raise ValueError('Replay speed must be positive')
    session = None
    last_id = 0
    last_time = None
    records = 0
    with Path(path).open(encoding='utf-8') as source:
        for line_number, line in enumerate(source, 1):
            try:
                record = json.loads(line)
                if record['source'] != 'kraken-spot-ws-v2':
                    raise FeedError('Unrecognized data source')
                sid = record['session']
                if isinstance(sid, bool) or not isinstance(sid, int) or sid < 1 or sid < last_id:
                    raise FeedError('Invalid recording session')
                received = record['received_at']
                stamp = datetime.fromisoformat(received.replace('Z', '+00:00'))
                if stamp.tzinfo is None:
                    raise FeedError('Received timestamp must include UTC offset')
                if session is None or sid != last_id:
                    session = Session(symbols, depth)
                    hub.change_phase('replaying', sid)
                    last_time = None  # reconnects may contain gaps of unknown length
                if last_time is not None:
                    gap = (stamp - last_time).total_seconds()
                    if gap < 0:
                        raise FeedError('Recording timestamps out of order')
                    if gap:
                        await asyncio.sleep(gap / speed)
                last_id = sid
                last_time = stamp
                message = parse(record['raw'])
                kind = session.accept(message)  # reject before publishing
                if kind is None:
                    raise FeedError('Unexpected recording control event')
                hub.publish_verified(kind, message, session, sid, received)
                records += 1
            except (KeyError, TypeError, ValueError) as error:
                raise FeedError(f'Invalid recording on line {line_number}: {error}') from error
    if not records:
        raise FeedError('Recording has no events')
    hub.change_phase('finished', last_id)
    return records


async def run(args):
    try:
        from websockets.asyncio.server import serve
    except ImportError as error:
        raise RuntimeError('Install dependencies: py -m pip install -r market_data/requirements.txt') from error

    mode = 'replay' if args.replay else 'live'
    hub = MarketHub(args.symbols, args.depth, mode, args.stale_seconds,
                    queue_limit=args.queue_limit, max_clients=args.max_clients)
    async with serve(hub.handle, HOST, args.port, max_size=4096, max_queue=4,
                     origins=[None, 'http://127.0.0.1:5173', 'http://localhost:5173']) as server:
        actual_port = server.sockets[0].getsockname()[1]
        print(f'LOCAL READ-ONLY STREAM: ws://{HOST}:{actual_port}', flush=True)
        print(f'Mode: {mode} | Symbols: {", ".join(args.symbols)} | Depth: {args.depth}', flush=True)
        heartbeat = asyncio.create_task(hub.heartbeat())
        try:
            if args.replay:
                records = load_recording(args.replay, args.symbols, args.depth)
                player = ReplayPlayer(records, hub, args.symbols, args.depth, args.speed)
                print(f'Interactive replay ready: {len(records)} validated events; playback controls available locally', flush=True)
                await player.run()  # Remains available for pause, seek, replay after completion.
            else:
                output = args.output or Path('market_data/records')/(
                    'kraken-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '.jsonl')
                print('Local recording:', output, flush=True)
                count = await collect(args.symbols, args.depth, args.duration, output,
                                      print_every=0, endpoint=args.endpoint,
                                      on_status=hub.change_phase,
                                      on_event=hub.publish_verified)
                print('Capture completed:', count, flush=True)
                hub.change_phase('finished', hub.session)
                if not count['book_messages']:
                    hub.change_phase('error', hub.session, 'No validated book messages received')
                    return 2
            if args.linger > 0:
                print(f'Stream remains available for {args.linger:g}s (last book snapshot).', flush=True)
                await asyncio.sleep(args.linger)
            return 0
        except (FeedError, SubscriptionRejected, OSError, ValueError) as error:
            hub.change_phase('error', hub.session, type(error).__name__ + ': ' + str(error)[:160])
            print('Stream error:', error, file=sys.stderr, flush=True)
            if args.linger > 0:
                await asyncio.sleep(args.linger)
            return 1
        finally:
            heartbeat.cancel()
            await asyncio.gather(heartbeat, return_exceptions=True)


def main():
    parser = argparse.ArgumentParser(description='Read-only local Kraken L2 WebSocket stream; no trading')
    parser.add_argument('--replay', type=Path, help='Saved validated JSONL session instead of live feed')
    parser.add_argument('--symbols', nargs='+', default=['BTC/USD', 'ETH/USD'])
    parser.add_argument('--depth', type=int, choices=[10,25,100,500,1000], default=10)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--duration', type=float, default=300, help='Live collection seconds')
    parser.add_argument('--speed', type=float, default=1, choices=ReplayPlayer.SPEEDS, help='Replay speed multiplier')
    parser.add_argument('--linger', type=float, default=15, help='Keep socket open after done')
    parser.add_argument('--stale-seconds', type=float, default=15)
    parser.add_argument('--queue-limit', type=int, default=256)
    parser.add_argument('--max-clients', type=int, default=8)
    parser.add_argument('--output', type=Path, help='Path for live JSONL recording')
    parser.add_argument('--endpoint', default=ENDPOINT, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or args.duration <= 0 or args.speed <= 0 or args.linger < 0:
        parser.error('Invalid port, duration, speed or linger')
    if len(args.symbols) != len(set(args.symbols)) or not args.symbols:
        parser.error('Provide distinct symbols')
    try:
        return asyncio.run(run(args))
    except (RuntimeError, KeyboardInterrupt) as error:
        print('Stream stopped:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
