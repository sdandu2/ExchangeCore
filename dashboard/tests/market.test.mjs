// Standalone pure-function tests can be run with Node's built-in TS stripping in Node 22+.
import { strict as assert } from 'node:assert'
import { test } from 'node:test'
import { appendHistory, isBook, mergeTrades, midpoint, priceText, replayTime, utcText, totalDepth } from '../src/lib/market.ts'

test('rejects unverified book messages', () => {
  assert.equal(isBook({ type: 'book', schema: 'exchangecore.market.v1', verified: false, symbol: 'BTC/USD', session: 1, bids: [], asks: [] }), false)
})
test('accepts validated book shape', () => {
  assert.equal(isBook({ type: 'book', schema: 'exchangecore.market.v1', verified: true, symbol: 'BTC/USD', session: 1, bids: [], asks: [] }), true)
})
test('handles missing price cleanly', () => {
  assert.equal(priceText(null), '—')
  assert.equal(midpoint(undefined), null)
})
test('sums quantity without formatting assumptions', () => {
  assert.equal(totalDepth([{ price: '10', qty: '0.10000' }, { price: '11', qty: '1.25' }]), 1.35)
})
test('resets history at session change', () => {
  assert.deepEqual(appendHistory([{ at: 1, mid: 10, session: 1 }], { at: 2, mid: 20, session: 2 }), [{ at: 2, mid: 20, session: 2 }])
})
test('prepends newest trades', () => {
  const result = mergeTrades([{ symbol: 'BTC/USD', price: '1', qty: '1', received_at: 'a', session: 1 }], [{ symbol: 'BTC/USD', price: '2', qty: '1', received_at: 'b', session: 1 }])
  assert.equal(result[0].price, '2')
})

test('formats replay time with minute and hour context', () => {
  assert.equal(replayTime(62.9), '1:02')
  assert.equal(replayTime(3662), '1:01:02')
})
test('formats source event time in UTC rather than local browser timezone', () => {
  assert.equal(utcText('2026-11-02T13:25:02Z'), '13:25:02 UTC')
})
