"""Generate a clearly SYNTHETIC fixture to test offline replay."""
import json
from pathlib import Path
from decimal import Decimal as D
from .book import crc32_book
from .recording import envelope


def main():
    bids, asks = {D('50000.0'): D('0.12500000')}, {D('50000.1'): D('0.22000000')}
    msg = {'channel': 'book', 'type': 'snapshot', 'data': [{
        'symbol': 'BTC/USD', 'bids': [{'price': '50000.0', 'qty': '0.12500000'}],
        'asks': [{'price': '50000.1', 'qty': '0.22000000'}],
        'checksum': crc32_book(bids, asks)}]}
    path = Path('market_data/records/synthetic-fixture.jsonl')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(envelope(json.dumps(msg), 1), encoding='utf-8')
    print('SYNTHETIC fixture:', path)
    print('Replay: py -m market_data.replay', path, '--symbols BTC/USD')


if __name__ == '__main__':
    main()
