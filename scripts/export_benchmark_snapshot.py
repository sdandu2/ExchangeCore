"""Rebuild the dashboard's static benchmark snapshot from archived v0.7 CSV files."""
from __future__ import annotations
import csv
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'benchmarks' / 'snapshots' / 'v0.7'
DEST = ROOT / 'dashboard' / 'src' / 'data' / 'benchmarks.json'


def generate():
    workloads = []
    for name, label, description in [
        ('passive', 'Passive Orders', 'Resting limit orders without execution'),
        ('crossing', 'Crossing Orders', 'Aggressive order flow across price levels'),
        ('mixed', 'Mixed Activity', 'Submissions, matching, and cancellations'),
    ]:
        with (SRC / f'v0.7-{name}-throughput.csv').open(newline='', encoding='utf-8') as file:
            rows = list(csv.DictReader(file))
        with (SRC / f'v0.7-{name}-latency.csv').open(newline='', encoding='utf-8') as file:
            latencies = list(csv.DictReader(file))
        if len(rows) < 2:
            raise ValueError(f'Missing throughput measurements: {name}')
        peak = int(rows[0]['peak_live'])
        configs = []
        for reserve in sorted({int(row['reserve_ids']) for row in rows}):
            samples = [r for r in rows if int(r['reserve_ids']) == reserve]
            values = [float(r['ops_per_sec']) for r in samples]
            if len(values) != 5 or len({r['checksum'] for r in samples}) != 1:
                raise ValueError(f'Incomplete/inconsistent throughput series: {name}/{reserve}')
            deciles = sorted(
                (r for r in latencies if int(r['reserve_ids']) == reserve and r['kind'] == 'all'),
                key=lambda r: int(r['decile']),
            )
            if len(deciles) != 10 or [int(x['decile']) for x in deciles] != list(range(1, 11)):
                raise ValueError(f'Incomplete latency deciles: {name}/{reserve}')
            key, name_label = (
                ('none', 'No Reserve') if reserve == 0 else
                ('full', 'Peak-Live / Full') if reserve == peak == 1000000 else
                ('peak', 'Peak-Live Reserve') if reserve == peak else
                ('full', 'Full Reserve')
            )
            configs.append({
                'key': key, 'label': name_label, 'reserve': reserve,
                'median': round(statistics.median(values)), 'min': round(min(values)),
                'max': round(max(values)), 'rounds': [round(x) for x in values],
                'checksum': samples[0]['checksum'],
                'deciles': [
                    {'decile': int(x['decile']), 'meanNs': float(x['mean_ns']),
                     'p99Ns': int(x['p99_ns']), 'p999Ns': int(x['p999_ns']),
                     'maxNs': int(x['max_ns']), 'rehashes': int(x['observed_rehashes']),
                     'over10us': int(x['over10us']), 'startLive': int(x['start_live']),
                     'endLive': int(x['end_live'])}
                    for x in deciles
                ],
            })
        workloads.append({'key': name, 'label': label, 'desc': description,
                          'peakLive': peak, 'operations': 1000000, 'configs': configs})
    data = {
        'meta': {'compiler': 'GCC 16.2.0', 'build': 'Release', 'seed': 31,
                 'rounds': 5, 'source': 'User-supplied ExchangeCore v0.7 CSV benchmark snapshots',
                 'date': '2026-11-02', 'machine': 'Intel Core Ultra 9 275HX (Windows)',
                 'notes': 'In-process order-book operations; throughput and per-operation latency sampled in separate runs.'},
        'workloads': workloads,
    }
    DEST.parent.mkdir(parents=True, exist_ok=True)
    DEST.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    return data

if __name__ == '__main__':
    data = generate()
    print('Exported', len(data['workloads']), 'workloads to', DEST)
