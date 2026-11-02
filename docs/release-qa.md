# ExchangeCore v1.3.0 — Final Release QA

This checklist is a release gate, not a claim that every machine/browser has already been tested. Record the real result of each step; leave unresolved failures open.

## 1. Build and automated tests

- [ ] Fresh clone builds with MSVC on Windows and modern GCC; C++ `ctest` passes.
- [ ] `py -m unittest discover -s market_data/tests -v` passes (includes committed synthetic fixture validation).
- [ ] `cd dashboard; npm install; npm run build; npm run test:unit` passes.
- [ ] GitHub Actions is green for Windows/Linux C++, Python, and dashboard jobs on the PR.
- [ ] `git status` shows no `build*`, `node_modules`, `dist`, or private live market recordings staged for commit.
- [ ] `py scripts/export_benchmark_snapshot.py` reproduces the frozen benchmark JSON from the archived CSVs without changing results.

### One-command local verification

After installing Python, Node and MSYS2 UCRT64 GCC, from the project root:

```powershell
.\scripts\verify_release.ps1
```

The script runs local automated checks but does not validate browser UX or public deployment conditions.

## 2. Manual browser checks

- [ ] Engine Performance opens at `http://127.0.0.1:5173/#performance` **without** Python server.
- [ ] Switch among Passive, Crossing, and Mixed; throughput and latency views change consistently with the archived v0.7 CSVs.
- [ ] Market Overview connects to localhost port 8765 and switches between BTC/USD and ETH/USD without mixing symbol state.
- [ ] Best bid is lower than best ask, midpoint equals half their sum, spread equals ask minus bid, displayed USD value agrees with price × quantity (subject to rounding).
- [ ] Live feed stops reporting fresh prices after disconnect or stale update; fresh session resets old book data.
- [ ] Recorded replay is labeled clearly; pause, seek, resume and speed controls work; seeking never leaves future book state behind.
- [ ] Browser DevTools has no repeating app-specific errors. Some third-party extension/OS warnings are not caused by ExchangeCore.
- [ ] Narrow browser window retains navigation and charts remain legible.
- [ ] User interface preserves read-only status; no live order execution or wallet API is exposed.

## 3. Release and demo

- [ ] Demo screenshots are representative and labeled with the capture date.
- [ ] Optional 60–90-second video shows UI, validated replay, and benchmark methodology; label recorded vs live source.
- [ ] README explains simulated matching vs live Kraken quotes (two distinct data paths).
- [ ] Review public market-data display and licensing terms before **any** public live data deployment.
- [ ] Pull request is reviewed and merged to `main`; tag `v1.3.0` points at the tested merge commit.

## 4. Known boundaries

The public GitHub repository can be viewed without credentials, but a live Kraken dashboard still needs a locally running server. The `market_data/fixtures/` file is intentionally **synthetic** and tiny; it only proves the offline replay path is usable from a clean clone. A full market replay requires user-collected JSONL files. The current repo is not a hosted public trading platform.
