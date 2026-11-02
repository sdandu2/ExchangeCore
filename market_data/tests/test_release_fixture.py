"""Ensure fresh Git clones include a checksum-valid, clearly synthetic demo fixture."""
from pathlib import Path
import unittest
from market_data.playback import load_recording


class ReleaseFixtureTests(unittest.TestCase):
    def test_bundled_synthetic_fixture(self):
        fixture = Path(__file__).resolve().parents[1] / 'fixtures' / 'synthetic-fixture.jsonl'
        self.assertTrue(fixture.is_file(), 'Required offline replay fixture is missing')
        records = load_recording(fixture, ['BTC/USD'], 10)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].kind, 'BTC/USD')
        self.assertEqual(records[0].message['type'], 'snapshot')


if __name__ == '__main__':
    unittest.main()
