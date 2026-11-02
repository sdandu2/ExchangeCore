import { useMemo, useState } from 'react'
import { Activity, BarChart3, CheckCircle2, ChevronDown, Cpu, Database, ExternalLink, Github, Info, Layers3, ShieldCheck, Timer, TrendingUp } from 'lucide-react'
import snapshot from './data/benchmarks.json'

type Config = (typeof snapshot.workloads)[number]['configs'][number]
const nf = new Intl.NumberFormat('en-US')
const millions = (v: number) => `${(v / 1_000_000).toFixed(2)}M`
const gain = (newValue: number, base: number) => (100 * (newValue / base - 1)).toFixed(1)
const perfColors = ['#597b8a', '#54b9d6', '#74e5b7']

function ThroughputBars({ configs, selected, onSelect }: { configs: Config[]; selected: number; onSelect: (n: number) => void }) {
  const max = Math.max(...configs.map(c => c.median)) * 1.13
  return <div className="perf-bars" aria-label="Median throughput across reservation strategies">
    {configs.map((config, i) => <button key={config.key} className={`perf-bar-row ${selected === i ? 'active' : ''}`} onClick={() => onSelect(i)} aria-pressed={selected === i}>
      <div className="perf-bar-copy"><span>{config.label}</span><strong className="mono">{millions(config.median)} <small>ops/s</small></strong></div>
      <div className="perf-bar-track"><span style={{ width: `${config.median / max * 100}%`, background: perfColors[i % perfColors.length] }} /></div>
      <span className="perf-bar-foot">{nf.format(config.reserve)} IDs reserved</span>
    </button>)}
  </div>
}

function LatencyTimeline({ config }: { config: Config }) {
  const { deciles } = config
  const width = 860, height = 220, left = 54, right = 20, top = 20, bottom = 38
  const vals = deciles.map(d => d.p999Ns / 1000)
  const upper = Math.max(...vals, 0.1) * 1.15
  const x = (i: number) => left + i / 9 * (width - left - right)
  const y = (v: number) => top + (1 - v / upper) * (height - top - bottom)
  const coords = vals.map((v,i) => `${x(i)},${y(v)}`).join(' ')
  return <div className="perf-timeline-wrap">
    <svg viewBox={`0 0 ${width} ${height}`} className="perf-timeline" role="img" aria-label="Within-decile p99.9 operation latency, in microseconds">
      {[0,1,2,3,4].map(i => <g key={i}><line x1={left} x2={width-right} y1={y(upper * i/4)} y2={y(upper * i/4)} stroke="#2a3d48" strokeDasharray="4 7"/><text x={left-8} y={y(upper*i/4)+4} textAnchor="end" className="perf-axis-label">{(upper*i/4).toFixed(1)}</text></g>)}
      <polyline fill="none" stroke="#72ddb5" strokeWidth="2.5" strokeLinejoin="round" strokeLinecap="round" points={coords}/>
      {vals.map((v,i) => <g key={i}><circle cx={x(i)} cy={y(v)} r="5.5" fill="#74e6b7" stroke="#12232c" strokeWidth="2"><title>{`Decile ${i + 1}: ${v.toFixed(2)} µs; ${deciles[i].rehashes} rehashes; ${deciles[i].over10us} operations over 10 µs`}</title></circle><text x={x(i)} y={height-10} className="perf-axis-label" textAnchor="middle">{i+1}</text></g>)}
      <text x="12" y="13" className="perf-axis-label">µs</text>
    </svg>
    <div className="perf-chart-foot"><span>Beginning Of Replay</span><span>Replay Decile (100K Operations Each)</span><span>End Of Replay</span></div>
  </div>
}

function MetricCard({ label, value, sub, accent = false }: { label: string; value: string; sub: string; accent?: boolean }) {
  return <div className="perf-metric"><span className="perf-metric-label">{label}</span><strong className={`perf-metric-value ${accent ? 'positive' : ''}`}>{value}</strong><small>{sub}</small></div>
}

export default function PerformancePage() {
  const [workloadId, setWorkloadId] = useState('mixed')
  const [selection, setSelection] = useState('best')
  const workload = snapshot.workloads.find(w => w.key === workloadId) ?? snapshot.workloads[0]
  const baseline = workload.configs.find(c => c.reserve === 0)!
  const best = useMemo(() => workload.configs.reduce((a,b) => a.median >= b.median ? a : b), [workload])
  const configIdx = selection === 'best' ? workload.configs.indexOf(best) : Math.max(0, workload.configs.findIndex(c => c.key === selection))
  const selectedConfig = workload.configs[configIdx]
  const [detailsOpen, setDetailsOpen] = useState(false)
  const totalRehashes = selectedConfig.deciles.reduce((sum,d) => sum + d.rehashes, 0)
  const worst = Math.max(...selectedConfig.deciles.map(d => d.maxNs)) / 1000
  const allMatch = workload.configs.every(c => c.checksum === baseline.checksum)
  return <>
    <div className="heading-row perf-heading"><div><div className="eyebrow"><span className="eyebrow-line"/> ENGINEERING / QUANT DEVELOPMENT</div><h1>Engine Performance<span className="heading-dot">.</span></h1><p className="subtitle">Real, reproducible benchmark measurements from the ExchangeCore C++20 matching engine.</p></div><a className="button secondary" href="https://github.com/sdandu2/ExchangeCore" target="_blank" rel="noreferrer"><Github size={15}/> View Source <ExternalLink size={13}/></a></div>
    <section className="perf-provenance"><div className="perf-provenance-icon"><Cpu size={22}/></div><div className="perf-provenance-text"><strong>Measured On Your Hardware</strong><span>{snapshot.meta.machine} · {snapshot.meta.compiler} · {snapshot.meta.build} · Seed {snapshot.meta.seed} · {snapshot.meta.rounds} Runs</span></div><div className="perf-measured-tag"><CheckCircle2 size={14}/> MEASURED DATA</div></section>
    <div className="perf-selector"><div><label htmlFor="perf-workload">Benchmark Workload</label><div className="perf-selector-note">Choose The Type Of Order Flow</div></div><div className="perf-workload-buttons" role="group" aria-label="Select benchmark workload">{snapshot.workloads.map(w => <button key={w.key} className={w.key === workloadId ? 'selected' : ''} aria-pressed={w.key === workloadId} onClick={() => {setWorkloadId(w.key);setSelection('best')}}>{w.label}</button>)}</div></div>
    <div className="perf-context"><Layers3 size={15}/><span><strong>{workload.label}</strong> — {workload.desc} · {nf.format(workload.operations)} In-Process Operations</span></div>
    <section className="perf-metrics-grid" aria-label="Performance summary">
      <MetricCard label="Median Throughput" value={millions(selectedConfig.median)} sub="Operations / Second" accent/>
      <MetricCard label="Versus No Reserve" value={`${Number(gain(selectedConfig.median,baseline.median)) >= 0 ? '+' : ''}${gain(selectedConfig.median, baseline.median)}%`} sub="Median Throughput Change" accent={selectedConfig.median >= baseline.median}/>
      <MetricCard label="Peak Live Orders" value={nf.format(workload.peakLive)} sub="Simultaneous Resting Orders"/>
      <MetricCard label="Index Rehashes" value={nf.format(totalRehashes)} sub="Observed During Instrumented Replay"/>
    </section>
    <div className="perf-main-grid">
      <section className="panel perf-panel"><div className="panel-header"><div className="panel-title-wrap"><BarChart3 size={18}/><div><h2>Throughput Comparison</h2><p>Median Across Five Runs · Higher Is Better</p></div></div><span className="small-outline">{millions(best.median)} BEST</span></div><ThroughputBars configs={workload.configs} selected={configIdx} onSelect={i => setSelection(workload.configs[i].key)}/><div className="perf-panel-footer"><Info size={14}/><span>Bars Show Measured Medians. Click A Strategy To Inspect Its Latency And Memory Behavior.</span></div></section>
      <section className="panel perf-panel"><div className="panel-header"><div className="panel-title-wrap"><Timer size={18}/><div><h2>Latency Diagnostics</h2><p>Chronological Replay · Selected Configuration</p></div></div><span className="small-outline">p99.9 BY DECILE</span></div><div className="perf-selected"><span>{selectedConfig.label}</span><span className="mono">{nf.format(selectedConfig.reserve)} Reserved IDs</span></div><LatencyTimeline config={selectedConfig}/><div className="perf-panel-footer"><Info size={14}/><span>Each Point Is The p99.9 Latency Within One 100K-Operation Decile; It Is Not The Overall p99.9.</span></div></section>
    </div>
    <div className="perf-main-grid perf-bottom-grid">
      <section className="panel perf-panel"><div className="panel-header"><div className="panel-title-wrap"><Activity size={18}/><div><h2>Memory And Tail Latency</h2><p>Diagnostic Counters For The Selected Strategy</p></div></div></div><div className="perf-fact-list"><div><span>Order-ID Capacity Hint</span><strong className="mono">{nf.format(selectedConfig.reserve)}</strong></div><div><span>Median Run Range</span><strong className="mono">{millions(selectedConfig.min)} – {millions(selectedConfig.max)}</strong></div><div><span>Worst Observed Operation</span><strong className="mono">{worst.toFixed(1)} µs</strong></div><div><span>Operations Over 10 µs</span><strong className="mono">{nf.format(selectedConfig.deciles.reduce((sum,d)=>sum+d.over10us,0))}</strong></div><div><span>Replay Checksum Agreement</span><strong className={allMatch?'positive':''}>{allMatch ? 'Matched Across Strategies' : 'Needs Investigation'}</strong></div></div></section>
      <section className="panel perf-panel"><div className="panel-header"><div className="panel-title-wrap"><ShieldCheck size={18}/><div><h2>Benchmark Methodology</h2><p>What These Results Can And Cannot Prove</p></div></div></div><div className="perf-method"><p><strong>What Is Measured:</strong> Single-process C++ order-book submissions and cancellations in controlled deterministic workloads, using a GCC Release build.</p><p><strong>Throughput:</strong> Median Of Five Independently Timed Runs. Latency Measurements Come From A Separate, Instrumented Replay.</p><p><strong>Not Measured:</strong> Network Round Trip, Kraken Exchange Execution, End-To-End Trading Latency, Or Financial Profitability.</p><p><strong>Integrity:</strong> Replay Checksums Match Across Tested Reservation Strategies For This Workload. Matching Correctness Is Also Covered By Separate Automated Tests.</p></div></section>
    </div>
    <section className="perf-disclosure"><div><Database size={16}/><strong>Frozen Benchmark Snapshot · v0.7</strong></div><p>These Numbers Are Not Real-Time Feed Measurements. Source: <code>benchmarks/snapshots/v0.7/</code>, Collected {snapshot.meta.date}. The Dashboard Uses A Generated JSON Snapshot; Rerun The Included Export Script After Archiving New Measurements.</p><button className="perf-disclosure-toggle" aria-expanded={detailsOpen} onClick={()=>setDetailsOpen(v=>!v)}>Inspect Measurement Sources <ChevronDown size={14}/></button>{detailsOpen && <div className="perf-disclosure-detail"><code>v0.7-{workload.key}-throughput.csv</code><code>v0.7-{workload.key}-latency.csv</code><div>Compiler: {snapshot.meta.compiler} · Build: {snapshot.meta.build} · Operations: {nf.format(workload.operations)} · Seed: {snapshot.meta.seed} · Rounds: {snapshot.meta.rounds}</div></div>}</section>
    <footer className="footer"><span><Cpu size={14}/> ExchangeCore • C++20 Matching Engine</span><span><Database size={14}/> SOURCE: RECORDED v0.7 BENCHMARK CSV</span><span><TrendingUp size={14}/> IN-PROCESS MEASUREMENTS ONLY</span></footer>
  </>
}
