"""Minimal local subscription client, so no dashboard is required for v1.0."""
import argparse
import asyncio
import json


async def watch(uri, max_messages, book_every):
    from websockets.asyncio.client import connect
    count = 0
    async with connect(uri, open_timeout=5, max_size=2_000_000) as socket:
        async for raw in socket:
            message = json.loads(raw)
            kind = message.get('type')
            if kind == 'book':
                count += 1
                if count == 1 or count % book_every == 0:
                    print(f"{message['symbol']}: {message['best_bid']} / {message['best_ask']}"
                          f" spread={message['spread']} | session={message['session']}", flush=True)
            elif kind == 'trades':
                count += 1
                print(f"Trade events: {len(message['trades'])} | session={message['session']}", flush=True)
            elif kind == 'status':
                print(f"Health: {message['phase']} ready={message['ready_symbols']}"
                      f" healthy={message['healthy']} stale={message['stale']}", flush=True)
            elif kind == 'reset':
                print(f"Session reset: {message['session']} ({message['reason']})", flush=True)
            else:
                print(f"Connected: {kind}", flush=True)
            if max_messages and count >= max_messages:
                return


def main():
    p = argparse.ArgumentParser(description='Inspect the local ExchangeCore read-only feed')
    p.add_argument('--uri', default='ws://127.0.0.1:8765')
    p.add_argument('--max-messages', type=int, default=0)
    p.add_argument('--book-every', type=int, default=25)
    args = p.parse_args()
    if args.book_every < 1 or args.max_messages < 0:
        p.error('Invalid message counts')
    try:
        asyncio.run(watch(args.uri, args.max_messages, args.book_every))
    except (OSError, KeyboardInterrupt) as error:
        print('Watch ended:', error)


if __name__ == '__main__':
    main()
