import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
const data = JSON.parse(readFileSync(new URL('../src/data/benchmarks.json', import.meta.url), 'utf8'))
function median(vals) { const s = [...vals].sort((a,b) => a-b); return s[(s.length-1)/2] }
test('all v0.7 workloads are backed by five measured repetitions', () => {
  assert.deepEqual(data.workloads.map(w => w.key), ['passive', 'crossing', 'mixed'])
  for (const w of data.workloads) for (const c of w.configs) {
    assert.equal(w.operations, 1000000)
    assert.equal(c.rounds.length, 5)
    assert.equal(c.median, median(c.rounds))
    assert.ok(c.min <= c.median && c.median <= c.max)
    assert.equal(c.deciles.length, 10)
  }
})
test('each workload preserves the same replay checksum across reservation strategies', () => {
  for (const w of data.workloads) assert.equal(new Set(w.configs.map(c => c.checksum)).size, 1)
})
test('passive replay has no need for a distinct peak-live and full-reservation profile', () => {
  assert.equal(data.workloads.find(w => w.key === 'passive').configs.length, 2)
})
test('each replay decile represents ordered and valid latency measurements', () => {
  for (const w of data.workloads) for (const c of w.configs) {
    assert.deepEqual(c.deciles.map(d => d.decile), [1,2,3,4,5,6,7,8,9,10])
    for (const d of c.deciles) {
      assert.ok(d.p99Ns >= 0 && d.p999Ns >= d.p99Ns)
      assert.ok(d.rehashes >= 0 && d.maxNs >= d.p999Ns)
    }
  }
})
