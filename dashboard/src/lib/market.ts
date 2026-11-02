export type Mode = 'live' | 'replay'
export type Level = { price: string; qty: string }
export type Book = {
  schema: string; type: 'book'; mode: Mode; session: number; symbol: string
  verified: boolean; depth: number; updates: number; received_at: string
  best_bid: string | null; best_ask: string | null; spread: string | null
  bids: Level[]; asks: Level[]
}
export type FeedStatus = {
  schema: string; type: 'status'; mode: Mode; phase: string; session: number
  healthy: boolean; stale: boolean; stale_symbols: string[]
  ready_symbols: string[]; expected_symbols: string[]
  book_age_ms: Record<string, number>; book_messages: number; trade_messages: number
  trades: number; clients: number; slow_clients_disconnected: number
  reason: string | null; at: string
}
export type ReplayState = {
  schema: string; type: 'replay_state'; mode: 'replay'
  cursor: number; total: number; progress: number
  paused: boolean; finished: boolean; speed: number
  elapsed_seconds: number; duration_seconds: number; source_time: string | null
}
export function replayTime(seconds: number): string {
  if (!Number.isFinite(seconds)) return '—'
  const value = Math.max(0, Math.floor(seconds))
  const hrs = Math.floor(value / 3600)
  const mins = Math.floor(value % 3600 / 60)
  const secs = value % 60
  return hrs ? `${hrs}:${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}` : `${mins}:${String(secs).padStart(2, '0')}`
}
export function utcText(raw?: string): string {
  if (!raw) return '—'
  const date = new Date(raw)
  return Number.isFinite(date.getTime()) ? date.toLocaleTimeString('en-GB', {timeZone: 'UTC', hour12: false}) + ' UTC' : '—'
}
export type TapeTrade = {
  symbol: string; price: string; qty: string
  side?: string; timestamp?: string; trade_id?: string | number
  received_at: string; session: number
}
export type PricePoint = { at: number; mid: number; session: number }
export type WireMessage = { type?: string; schema?: string; [key: string]: unknown }
export const SOCKET_URL = 'ws://127.0.0.1:8765'
export const SCHEMA = 'exchangecore.market.v1'
export const MAX_TRADES = 80
export const MAX_HISTORY = 140

export function safeNumber(input: string | number | null | undefined): number | null {
  if (input === null || input === undefined || input === '') return null
  const number = Number(input)
  return Number.isFinite(number) ? number : null
}
export function priceText(value: string | number | null | undefined, precision = 2): string {
  const number = safeNumber(value)
  return number === null ? '—' : number.toLocaleString('en-US', { minimumFractionDigits: precision, maximumFractionDigits: precision })
}
export function quantityText(value: string | number | null | undefined): string {
  const number = safeNumber(value)
  return number === null ? '—' : number.toLocaleString('en-US', { maximumFractionDigits: 5 })
}
export function integerText(value: number | null | undefined): string {
  return value == null ? '—' : value.toLocaleString('en-US')
}
export function timeText(raw?: string): string {
  if (!raw) return '—'
  const stamp = Date.parse(raw)
  return Number.isFinite(stamp) ? new Date(stamp).toLocaleTimeString('en-US', { hour12: false }) : '—'
}
export function isBook(raw: WireMessage): raw is WireMessage & Book {
  return raw.schema === SCHEMA && raw.type === 'book' && raw.verified === true &&
    typeof raw.symbol === 'string' && Array.isArray(raw.bids) && Array.isArray(raw.asks) &&
    typeof raw.session === 'number'
}
export function isStatus(raw: WireMessage): raw is WireMessage & FeedStatus {
  return raw.schema === SCHEMA && raw.type === 'status' &&
    Array.isArray(raw.expected_symbols) && typeof raw.session === 'number' &&
    typeof raw.healthy === 'boolean'
}
export function midpoint(book: Book | undefined): number | null {
  if (!book) return null
  const bid = safeNumber(book.best_bid)
  const ask = safeNumber(book.best_ask)
  if (bid === null || ask === null || bid > ask) return null
  return (bid + ask) / 2
}
export function totalDepth(levels: Level[]): number {
  return levels.reduce((total, level) => total + (safeNumber(level.qty) ?? 0), 0)
}
export function mergeTrades(existing: TapeTrade[], incoming: TapeTrade[]): TapeTrade[] {
  return [...incoming.slice().reverse(), ...existing].slice(0, MAX_TRADES)
}
export function appendHistory(history: PricePoint[], point: PricePoint): PricePoint[] {
  const previous = history.at(-1)
  if (previous && (previous.session !== point.session || point.at <= previous.at)) {
    return previous.session !== point.session ? [point] : history
  }
  return [...history, point].slice(-MAX_HISTORY)
}
export function priceChange(history: PricePoint[]): { delta: number; pct: number } | null {
  if (history.length < 2 || history[0].mid === 0) return null
  const delta = history[history.length - 1].mid - history[0].mid
  return { delta, pct: (delta / history[0].mid) * 100 }
}
