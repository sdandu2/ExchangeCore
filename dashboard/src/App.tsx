import { useEffect, useMemo, useState, type ReactNode } from 'react'
import {
  Activity, ArrowDownRight, ArrowUpRight,
  BarChart3, CheckCircle2, ChevronDown, CircleAlert, Clock3, Cpu,
  Database, ExternalLink, Github, Layers3, LockKeyhole, Radio,
  RefreshCw, Server, ShieldCheck, Signal, Terminal, WifiOff, Pause, Play,
} from 'lucide-react'
import { useMarketStream } from './useMarketStream'
import {
  integerText, midpoint, priceChange, priceText, quantityText, safeNumber,
  timeText, utcText, replayTime, totalDepth, type Book, type FeedStatus, type Level, type PricePoint, type TapeTrade, type ReplayState,
} from './lib/market'

function cx(...values: (string | false | undefined | null)[]) { return values.filter(Boolean).join(' ') }
function formatDuration(age: number | undefined) {
  if (age === undefined || !Number.isFinite(age)) return '—'
  return age < 1000 ? `${Math.max(0, Math.round(age))} ms` : `${(age / 1000).toFixed(1)} s`
}

function PanelHeader({ icon: Icon, title, note, right }: {
  icon: typeof Activity; title: string; note?: string; right?: ReactNode
}) {
  return <div className="panel-header">
    <div className="panel-title-wrap"><Icon size={17} strokeWidth={1.8} /><div><h2>{title}</h2>{note && <p>{note}</p>}</div></div>
    {right && <div className="panel-header-right">{right}</div>}
  </div>
}

function LineChart({ points, symbol }: { points: PricePoint[]; symbol: string }) {
  const width = 840
  const height = 260
  const left = 56, right = 14, top = 22, bottom = 33
  const plotHeight = height - top - bottom
  const plotWidth = width - left - right
  const data = useMemo(() => {
    if (points.length < 2) return null
    const values = points.map(p => p.mid)
    const lowest = Math.min(...values), highest = Math.max(...values)
    const padding = Math.max((highest - lowest) * 0.18, highest * 0.000007)
    const min = lowest - padding, max = highest + padding
    const firstTime = points[0].at, lastTime = points.at(-1)!.at
    const x = (i: number) => left + (lastTime > firstTime ? (points[i].at - firstTime) / (lastTime - firstTime) : i / Math.max(1, points.length - 1)) * plotWidth
    const y = (value: number) => top + (1 - (value - min) / (max - min)) * plotHeight
    const pointsString = points.map((p, i) => `${x(i).toFixed(2)},${y(p.mid).toFixed(2)}`).join(' ')
    return { min, max, pointsString, lastX: x(points.length - 1), lastY: y(points.at(-1)!.mid), y, firstTime, lastTime }
  }, [points])
  return <div className="chart-frame">
    <svg className="line-chart" role="img" aria-label={`${symbol} observed mid-price from locally received verified book snapshots`} viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none">
      <defs>
        <linearGradient id="midFill" x1="0" x2="0" y1="0" y2="1"><stop offset="0%" stopColor="#78e5bd" stopOpacity=".20"/><stop offset="100%" stopColor="#78e5bd" stopOpacity="0"/></linearGradient>
        <filter id="glow"><feGaussianBlur stdDeviation="3" /></filter>
      </defs>
      {[0, 1, 2, 3, 4].map(i => <g key={i}>
        <line x1={left} x2={width - right} y1={top + plotHeight * i / 4} y2={top + plotHeight * i / 4} stroke="#253241" strokeDasharray="3 6" strokeWidth="1"/>
        {data && <text x={left - 10} y={top + plotHeight * i / 4 + 4} textAnchor="end" className="chart-label">{priceText(data.max - (data.max - data.min) * i / 4, 1)}</text>}
      </g>)}
      {[0, 1, 2, 3, 4, 5, 6].map(i => <line key={i} x1={left + plotWidth * i / 6} x2={left + plotWidth * i / 6} y1={top} y2={height - bottom} stroke="#202b38" strokeDasharray="2 7" />)}
      {data && <>
        <polygon points={`${left},${height - bottom} ${data.pointsString} ${data.lastX},${height - bottom}`} fill="url(#midFill)" />
        <polyline points={data.pointsString} fill="none" stroke="#70e2b6" strokeWidth="7" opacity=".20" filter="url(#glow)" vectorEffect="non-scaling-stroke"/>
        <polyline points={data.pointsString} fill="none" stroke="#78e5bd" strokeWidth="2.5" strokeLinejoin="round" strokeLinecap="round" vectorEffect="non-scaling-stroke"/>
        <line x1={data.lastX} x2={data.lastX} y1={top} y2={height - bottom} stroke="#78e5bd" strokeDasharray="4 5" opacity=".26"/>
        <circle cx={data.lastX} cy={data.lastY} r="5" fill="#78e5bd" stroke="#0e1b24" strokeWidth="3" />
      </>}
      <text x={left} y={height - 7} className="chart-label">{data ? utcText(new Date(data.firstTime).toISOString()) : 'FIRST OBSERVED'}</text>
      <text x={width - right} y={height - 7} textAnchor="end" className="chart-label">{data ? utcText(new Date(data.lastTime).toISOString()) : 'LATEST OBSERVED'}</text>
    </svg>
    {!data && <div className="chart-empty"><Activity size={23}/><strong>Awaiting market snapshots</strong><span>Verified mid-price observations will appear here.</span></div>}
  </div>
}

function DepthChart({ book }: { book?: Book }) {
  const bids = book?.bids || []
  const asks = book?.asks || []
  const max = Math.max(totalDepth(bids), totalDepth(asks), 0.001)
  function makePoints(levels: Level[], isBid: boolean) {
    let sum = 0
    const data = levels.map(level => { sum += safeNumber(level.qty) || 0; return sum })
    const ordered = isBid ? [...data].reverse() : data
    return ordered.map((quantity, i) => `${isBid ? 50 - (ordered.length - i) * (44 / Math.max(ordered.length, 1)) : 50 + (i + 1) * (44 / Math.max(ordered.length, 1))},${89 - quantity / max * 72}`).join(' ')
  }
  return <div className="depth-chart">
    <div className="depth-topline"><span>BID LIQUIDITY</span><span>ASK LIQUIDITY</span></div>
    <svg role="img" aria-label="Cumulative bid and ask liquidity across received levels" viewBox="0 0 100 100" preserveAspectRatio="none">
      <line x1="50" y1="7" x2="50" y2="92" stroke="#52616e" strokeDasharray="2 2" strokeWidth=".3"/>
      <line x1="5" y1="89" x2="95" y2="89" stroke="#33404c" strokeWidth=".5"/>
      {!!book && <>
        <polygon points={`6,89 ${makePoints(bids, true)} 50,89`} fill="#3fd5a5" fillOpacity=".17" />
        <polygon points={`50,89 ${makePoints(asks, false)} 94,89`} fill="#ff7888" fillOpacity=".17" />
        <polyline points={makePoints(bids, true)} fill="none" stroke="#45d5aa" strokeWidth=".8" vectorEffect="non-scaling-stroke"/>
        <polyline points={makePoints(asks, false)} fill="none" stroke="#f37d91" strokeWidth=".8" vectorEffect="non-scaling-stroke"/>
      </>}
    </svg>
    {!book && <div className="depth-empty">No validated depth available</div>}
    <div className="depth-totals"><span>{quantityText(totalDepth(bids))} {book?.symbol.split('/')[0] || ''}</span><span>{quantityText(totalDepth(asks))} {book?.symbol.split('/')[0] || ''}</span></div>
  </div>
}

function Ladder({ book }: { book?: Book }) {
  const bids = book?.bids || []
  const asks = [...(book?.asks || [])].reverse()
  const max = Math.max(0.001, ...[...bids, ...asks].map(level => safeNumber(level.qty) || 0))
  return <div className="ladder">
    <div className="ladder-head"><span>PRICE (USD)</span><span>AMOUNT</span><span>VALUE (USD)</span></div>
    <div className="ladder-side">
      {asks.length ? asks.map(level => <LadderRow key={`ask-${level.price}`} level={level} max={max} side="ask" />) : <div className="ladder-wait">Waiting for ask levels</div>}
    </div>
    <div className="spread-row"><span>SPREAD</span><strong>{book?.spread ? `$${priceText(book.spread, 2)}` : '—'}</strong><span>USD</span></div>
    <div className="ladder-side">
      {bids.length ? bids.map(level => <LadderRow key={`bid-${level.price}`} level={level} max={max} side="bid" />) : <div className="ladder-wait">Waiting for bid levels</div>}
    </div>
    <div className="ladder-foot"><CheckCircle2 size={12}/><span>Verified L2 snapshots · Aggregated levels</span></div>
  </div>
}
function LadderRow({ level, side, max }: { level: Level; side: 'bid' | 'ask'; max: number }) {
  const price = safeNumber(level.price) || 0
  const qty = safeNumber(level.qty) || 0
  return <div className={cx('ladder-row', side)}>
    <span className="ladder-bar" style={{ width: `${Math.min(98, qty / max * 96)}%` }}/>
    <span className="mono price">{priceText(price)}</span><span className="mono">{quantityText(qty)}</span><span className="mono muted">{priceText(price * qty)}</span>
  </div>
}

function TradeTape({ trades, symbol }: { trades: TapeTrade[]; symbol: string }) {
  const visible = trades.filter(item => item.symbol === symbol).slice(0, 9)
  return <div className="tape">
    <div className="tape-head"><span>TIME</span><span>PRICE (USD)</span><span>QUANTITY</span><span>SIDE</span></div>
    {visible.length ? visible.map((trade, i) => {
      const side = (trade.side || '').toLowerCase()
      return <div className="tape-row" key={`${trade.session}-${trade.trade_id ?? trade.received_at}-${i}`}>
        <span className="mono muted">{timeText(trade.timestamp || trade.received_at)}</span>
        <span className={cx('mono', side === 'buy' ? 'green' : side === 'sell' ? 'red' : '')}>{priceText(trade.price)}</span>
        <span className="mono">{quantityText(trade.qty)}</span>
        <span className={cx('side-tag', side === 'buy' ? 'buy' : side === 'sell' ? 'sell' : 'unknown')}>{side === 'buy' ? 'BUY' : side === 'sell' ? 'SELL' : '—'}</span>
      </div>
    }) : <div className="tape-empty">Waiting for verified trade messages for {symbol}.</div>}
  </div>
}

function StatusPanel({ status, connected, lastReceived, selectedSymbol, verified, onReconnect }: {
  status: FeedStatus | null; connected: boolean; lastReceived: number | null;
  selectedSymbol: string; verified: boolean; onReconnect: () => void
}) {
  const feedReady = connected && Boolean(status?.healthy) && Boolean(status?.ready_symbols.includes(selectedSymbol))
  const pausedReplay = status?.mode === 'replay' && status.phase === 'paused' 
  const lines = [
    ['Socket', connected ? 'Connected' : 'Disconnected'],
    ['Feed phase', status?.phase || 'Waiting'],
    ['Session', status ? `#${status.session}` : '—'],
    ['Market data age', status?.mode === 'replay' ? 'Recorded session' : formatDuration(status?.book_age_ms[selectedSymbol])],
    ['Book events', integerText(status?.book_messages)],
    ['Individual trades', integerText(status?.trades)],
    ['Subscribers', integerText(status?.clients)],
    ['Last local receive', lastReceived ? new Date(lastReceived).toLocaleTimeString('en-US', { hour12: false }) : '—'],
  ]
  return <section className="panel system-panel" id="health">
    <PanelHeader icon={Server} title="System health" right={<button className="icon-button" title="Reconnect local socket" aria-label="Reconnect local socket" onClick={onReconnect}><RefreshCw size={16}/></button>}/>
    <div className={cx('health-banner', feedReady ? 'good' : 'warn')}>
      {feedReady ? <ShieldCheck size={19}/> : <CircleAlert size={19}/>}
      <div><strong>{pausedReplay ? 'Verified replay paused' : feedReady ? 'Validated stream active' : connected ? 'Market stream not ready' : 'Local stream offline'}</strong><span>{pausedReplay ? 'Historical market book held at a recorded point' : feedReady ? 'Receiving checksum-verified L2 book' : connected ? 'Waiting for fresh validated snapshots' : 'Start the stream server on port 8765'}</span></div>
    </div>
    <div className="health-list">{lines.map(([key, value]) => <div className="health-line" key={key}><span>{key}</span><strong className={key === 'Socket' ? connected ? 'green' : 'muted' : ''}>{value}</strong></div>)}</div>
    <div className="health-footer"><span className={cx('small-dot', verified ? 'active' : 'inactive')}/><span>{verified ? 'Selected book checksum verified' : 'No currently validated selected book'}</span></div>
  </section>
}

function ReplayControls({ replay, send }: { replay: ReplayState; send: (action: string, value?: number) => void }) {
  const [seekDraft, setSeekDraft] = useState(0)
  useEffect(() => setSeekDraft(Math.round(replay.progress * 100)), [replay.progress])
  return <section className="replay-controls" aria-label="Recorded market replay controls">
    <div className="replay-top"><strong>HISTORICAL REPLAY</strong><span>Recorded Kraken data · No real trading</span></div>
    <div className="replay-actions">
      <button className="button secondary" onClick={() => send(replay.paused ? 'resume' : 'pause')}>
        {replay.paused ? <Play size={15}/> : <Pause size={15}/>} {replay.paused ? 'Resume' : 'Pause'}
      </button>
      <label className="speed-label">Speed
        <select value={replay.speed} onChange={e => send('speed', Number(e.target.value))} aria-label="Replay speed">
          {[0.25,0.5,1,2,4,8].map(speed => <option value={speed} key={speed}>{speed}×</option>)}
        </select>
      </label>
      <label className="seek-label">Replay position
        <input type="range" min="0" max="100" value={seekDraft} onChange={e => setSeekDraft(Number(e.target.value))} aria-label="Seek to percentage of recorded session" />
      </label>
      <button className="button secondary" onClick={() => send('seek', seekDraft / 100)}>Jump to {seekDraft}%</button>
      <span className="replay-clock mono">{replayTime(replay.elapsed_seconds)} / {replayTime(replay.duration_seconds)}</span>
    </div>
    <span className="replay-context">{replay.paused ? 'Paused' : replay.finished ? 'Reached end — seek or resume to restart' : 'Playing'} · {replay.cursor.toLocaleString()} / {replay.total.toLocaleString()} events · Last recorded {utcText(replay.source_time || undefined)}</span>
  </section>
}

export default function App() {
  const stream = useMarketStream()
  const [selectedSymbol, setSelectedSymbol] = useState('BTC/USD')
  const [clock, setClock] = useState(Date.now())
  const [menuOpen, setMenuOpen] = useState(false)
  const [tapePaused, setTapePaused] = useState(false)
  const [visibleTrades, setVisibleTrades] = useState<TapeTrade[]>([])
  useEffect(() => { const interval = window.setInterval(() => setClock(Date.now()), 1000); return () => window.clearInterval(interval) }, [])
  useEffect(() => { if (!tapePaused) setVisibleTrades(stream.trades) }, [stream.trades, tapePaused])
  useEffect(() => setTapePaused(false), [stream.status?.session])
  const available = stream.status?.expected_symbols?.length ? stream.status.expected_symbols : ['BTC/USD', 'ETH/USD']
  useEffect(() => { if (!available.includes(selectedSymbol)) setSelectedSymbol(available[0]) }, [available.join('|'), selectedSymbol])
  const book = stream.books[selectedSymbol]
  const points = stream.history[selectedSymbol] || []
  const mid = midpoint(book)
  const change = priceChange(points)
  const fresh = stream.connected && stream.lastReceived !== null && clock - stream.lastReceived < 8000 && Boolean(stream.status?.healthy) && Boolean(stream.status?.ready_symbols.includes(selectedSymbol)) &&
    !stream.status?.stale_symbols?.includes(selectedSymbol) && Boolean(book) && book?.session === stream.status?.session
  const isReplay = stream.status?.mode === 'replay' || book?.mode === 'replay'
  const finished = stream.status?.phase === 'finished'
  const stateLabel = !stream.connected ? 'OFFLINE' : finished ? 'COMPLETE' : isReplay && stream.replay?.paused ? 'PAUSED REPLAY' : fresh ? isReplay ? 'REPLAYING' : 'LIVE VERIFIED' : 'AWAITING FEED'
  const badgeClass = fresh ? 'online' : finished ? 'neutral' : 'offline'
  return <div className="app">
    <aside className="sidebar">
      <a className="brand" href="#overview" title="Market overview" aria-label="ExchangeCore home"><div className="brand-mark"><span>∆</span></div><div><strong>EXCHANGE<span>CORE</span></strong><small>MARKET SYSTEMS</small></div></a>
      <div className="side-section-title">WORKSPACE</div>
      <nav className="side-nav" aria-label="Dashboard sections">
        <a className="nav-item active" href="#overview" title="Market overview"><BarChart3 size={18}/> Market overview<span className="nav-accent"/></a>
        <a className="nav-item" href="#orderbook" title="Order book"><Layers3 size={18}/> Order book</a>
        <a className="nav-item" href="#trades" title="Trade tape"><Activity size={18}/> Trade tape</a>
        <a className="nav-item" href="#health" title="Feed health"><Signal size={18}/> Feed health</a>
      </nav>
      <div className="side-section-title lower">INTEGRATION</div>
      <div className="source-row"><span className="source-icon"><Radio size={16}/></span><div><strong>Kraken Spot</strong><small>Public WebSocket v2</small></div>{stream.status?.healthy ? <CheckCircle2 size={15} className="source-check"/> : <CircleAlert size={15} color="#f0bf78"/>}</div>
      <div className="side-bottom"><div className="side-note"><LockKeyhole size={15}/><span>Read-only local terminal<br/>No real orders or funds</span></div><a className="github-link" href="https://github.com/sdandu2/ExchangeCore" target="_blank" rel="noreferrer"><Github size={17}/> View source <ExternalLink size={13}/></a><div className="version">EXCHANGECORE / v1.2.0</div></div>
    </aside>

    <div className="content">
      <header className="topbar"><div className="crumb"><span>WORKSPACE</span><span className="slash">/</span><strong>Market overview</strong></div><div className="topbar-right"><span className="top-time mono"><Clock3 size={14}/> {new Date(clock).toLocaleTimeString('en-GB', { timeZone: 'UTC', hour12: false })} UTC</span><span className="top-divider"/><span className={cx('connection-pill', stream.connected && 'connected')}><span/>{stream.connected ? 'LOCAL SOCKET CONNECTED' : 'SOCKET DISCONNECTED'}</span></div></header>
      <main className="main" id="overview">
        <div className="heading-row"><div><div className="eyebrow"><span className="eyebrow-line"/>REAL-TIME MARKET INFRASTRUCTURE</div><h1>Market Overview<span className="heading-dot">.</span></h1><p className="subtitle">{isReplay ? 'Explore a recorded market session with verified snapshots.' : 'A live window into verified market depth and trade activity.'}</p></div><div className="heading-actions"><span className="local-only"><LockKeyhole size={14}/> LOCAL ENVIRONMENT</span><button className="button secondary" onClick={stream.reconnect}><RefreshCw size={15}/> Reconnect</button></div></div>

        <div className="instrument-bar"><div className="instrument-left"><div className="symbol-avatar">{selectedSymbol.slice(0, 1)}</div><div className="symbol-dropdown-wrap"><button className="symbol-select" onClick={() => setMenuOpen((current) => !current)} aria-expanded={menuOpen} aria-label="Select market"><strong>{selectedSymbol}</strong><ChevronDown size={16}/></button><div className="instrument-desc">Kraken / Spot · USD quoted</div>{menuOpen && <div className="symbol-menu" role="menu">{available.map(symbol => <button role="menuitem" onClick={() => { setSelectedSymbol(symbol); setMenuOpen(false) }} key={symbol}><span>{symbol}</span>{selectedSymbol === symbol && <CheckCircle2 size={15}/>}</button>)}</div>}</div><span className={cx('market-state', badgeClass)}><span className="pulse-dot"/>{stateLabel}</span>{isReplay && <span className="replay-label">RECORDED DATA</span>}</div><div className="instrument-right"><span><ShieldCheck size={16}/> Checksum-validated L2</span><span className="session-id mono">SESSION {stream.status?.session ?? '—'} · LAST BOOK {utcText(book?.received_at)}</span></div></div>

        {stream.replay && isReplay && <ReplayControls replay={stream.replay} send={stream.replayCommand}/>}
        {book && !fresh && !finished && !stream.replay?.paused && <div className="warning-bar" role="status">Market updates aren't currently verified as fresh. Quotes are shown for reference only.</div>}
        <section className="stats-grid" aria-label="Market statistics">
          <div className="stat-card"><div className="stat-label">MID PRICE <Activity size={14}/></div><div className="stat-value">{mid !== null ? `$${priceText(mid)}` : '—'}</div><div className="stat-foot">{change ? <span className={cx('delta', change.delta >= 0 ? 'green' : 'red')}>{change.delta >= 0 ? <ArrowUpRight size={14}/> : <ArrowDownRight size={14}/>} {change.delta >= 0 ? '+' : ''}{change.pct.toFixed(3)}%</span> : <span className="muted">Awaiting observations</span>} <span className="foot-sep">·</span> This session</div></div>
          <div className="stat-card"><div className="stat-label">BEST BID <span className="indicator-dot green-bg"/></div><div className="stat-value">{book?.best_bid != null ? `$${priceText(book.best_bid)}` : '—'}</div><div className="stat-foot"><span className="green">Highest buy price</span><span className="foot-sep">·</span> USD</div></div>
          <div className="stat-card"><div className="stat-label">BEST ASK <span className="indicator-dot red-bg"/></div><div className="stat-value">{book?.best_ask != null ? `$${priceText(book.best_ask)}` : '—'}</div><div className="stat-foot"><span className="red">Lowest sell price</span><span className="foot-sep">·</span> USD</div></div>
          <div className="stat-card"><div className="stat-label">BID-ASK SPREAD <ExternalLink size={13}/></div><div className="stat-value">{book?.spread != null ? `$${priceText(book.spread)}` : '—'}</div><div className="stat-foot"><span className="muted">Displayed aggregated market depth</span></div></div>
        </section>

        <div className="layout-grid"><div className="primary-column">
          <section className="panel price-panel"><PanelHeader icon={Activity} title="Mid-price movement" note="Recent verified snapshots · Latest 140" right={<><span className="small-outline"><span className="green-dot"/> L2 MIDPOINT</span><span className="small-outline mono">{points.length} SAMPLES</span></>}/><div className="chart-topline"><strong>{mid == null ? 'No price available' : `$${priceText(mid)}`}</strong><span>{change ? `${change.delta >= 0 ? '+' : ''}${priceText(change.delta)} USD since first observation` : 'Waiting for session data'}</span></div><LineChart points={points} symbol={selectedSymbol}/><div className="chart-caption"><span><span className="caption-dot"/> Midpoint = (best bid + best ask) / 2</span><span>Timeline uses recorded receive times · Not OHLC</span></div></section>
          <div className="lower-grid">
            <section className="panel" id="trades"><PanelHeader icon={Activity} title="Recent market trades" note={tapePaused ? 'Paused locally; backend still receiving' : 'Exchange-reported executions'} right={<><span className="small-outline">{tapePaused ? 'PAUSED' : 'LATEST 9'}</span><button className="icon-button" aria-label={tapePaused ? 'Resume trade tape' : 'Pause trade tape'} title={tapePaused ? 'Resume trade tape' : 'Pause trade tape'} onClick={() => setTapePaused(v => !v)}>{tapePaused ? <Play size={14}/> : <Pause size={14}/>}</button></>}/><TradeTape trades={visibleTrades} symbol={selectedSymbol}/></section>
            <StatusPanel status={stream.status} connected={stream.connected} lastReceived={stream.lastReceived} selectedSymbol={selectedSymbol} verified={Boolean(fresh && book?.verified)} onReconnect={stream.reconnect}/>
          </div>
        </div><div className="secondary-column">
          <section className="panel book-panel" id="orderbook"><PanelHeader icon={Layers3} title="Order book" note="Level 2 · Aggregated depth" right={<span className="small-outline mono">{book?.depth ?? '—'} LEVELS</span>}/><Ladder book={book}/></section>
          <section className="panel liquidity-panel"><PanelHeader icon={BarChart3} title="Market depth" note="Cumulative liquidity by depth level (schematic, not price-scaled)"/><DepthChart book={book}/></section>
          <div className="data-disclaimer"><ShieldCheck size={18}/><div><strong>Market integrity first</strong><span>Real exchange L2 data is displayed separately from ExchangeCore's simulated C++ matching engine. No live orders are placed.</span></div></div>
        </div></div>

        {stream.error && !stream.connected && <div className="error-bar" role="alert"><WifiOff size={17}/>{stream.error}<button onClick={stream.reconnect}>Retry</button></div>}
        <footer className="footer"><span><Terminal size={14}/> ExchangeCore • C++20 matching engine + Python market stream</span><span><Database size={14}/> SOURCE: KRAKEN SPOT WS V2</span><span><Cpu size={14}/> LOCALHOST ONLY</span></footer>
      </main>
    </div>
  </div>
}
