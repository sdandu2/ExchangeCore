"""Validate a previously captured market-data JSONL file without networking."""
import argparse
from .book import FeedError
from .recording import replay


def main():
    parser = argparse.ArgumentParser(description='Offline Kraken feed replay and CRC validation')
    parser.add_argument('path')
    parser.add_argument('--symbols', nargs='+', default=['BTC/USD', 'ETH/USD'])
    parser.add_argument('--depth', choices=[10,25,100,500,1000], type=int, default=10)
    args = parser.parse_args()
    try:
        result = replay(args.path, args.symbols, args.depth)
    except (FeedError, OSError) as e:
        print('Replay failed:', e)
        return 1
    print('Archive validated:', args.path)
    for key, value in result.items():
        print(key, ':', value)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
