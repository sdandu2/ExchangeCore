import { useEffect, useRef, useState } from 'react'
import {
  appendHistory, isBook, isStatus, MAX_TRADES, midpoint, SCHEMA, SOCKET_URL,
  type Book, type FeedStatus, type PricePoint, type TapeTrade, type ReplayState, type WireMessage,
} from './lib/market'

type StreamState = {
  connected: boolean
  status: FeedStatus | null
  books: Record<string, Book>
  trades: TapeTrade[]
  history: Record<string, PricePoint[]>
  lastReceived: number | null
  error: string | null
  replay: ReplayState | null
  replayCommand: (action: string, value?: number) => void
  reconnect: () => void
}

export function useMarketStream(): StreamState {
  const [attempt, setAttempt] = useState(0)
  const [connected, setConnected] = useState(false)
  const [status, setStatus] = useState<FeedStatus | null>(null)
  const [books, setBooks] = useState<Record<string, Book>>({})
  const [history, setHistory] = useState<Record<string, PricePoint[]>>({})
  const [trades, setTrades] = useState<TapeTrade[]>([])
  const [lastReceived, setLastReceived] = useState<number | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [replay, setReplay] = useState<ReplayState | null>(null)
  const currentSocket = useRef<WebSocket | null>(null)
  const pendingBooks = useRef<Record<string, Book>>({})
  const pendingTrades = useRef<TapeTrade[]>([])
  const latestSession = useRef<number | null>(null)
  const reconnectDelay = useRef(0)

  useEffect(() => {
    let active = true
    let socket: WebSocket | null = null
    let retryTimeout: number | undefined
    let flushTimer: number | undefined

    const flush = () => {
      const incoming = pendingBooks.current
      const incomingTrades = pendingTrades.current
      if (Object.keys(incoming).length) {
        pendingBooks.current = {}
        setBooks((previous) => ({ ...previous, ...incoming }))
        setHistory((previous) => {
          const updated = { ...previous }
          for (const book of Object.values(incoming)) {
            const mid = midpoint(book)
            if (mid === null) continue
            const previousPoint = updated[book.symbol]?.at(-1)
            const pointAt = Date.parse(book.received_at) || Date.now()
            updated[book.symbol] = appendHistory(updated[book.symbol] || [], {
              at: Math.max(pointAt, (previousPoint?.at ?? 0) + 1), mid, session: book.session,
            })
          }
          return updated
        })
      }
      if (incomingTrades.length) {
        pendingTrades.current = []
        setTrades((previous) => [...incomingTrades.slice().reverse(), ...previous].slice(0, MAX_TRADES))
      }
    }

    function onReset(session: number) {
      latestSession.current = session
      pendingBooks.current = {}
      pendingTrades.current = []
      setBooks({})
      setHistory({})
      setTrades([])
      setStatus(null)
      setReplay(null)
    }

    const connect = () => {
      if (!active) return
      if (window.location.protocol === 'https:') {
        setError('This local HTTP dashboard cannot attach to a ws:// feed from an HTTPS page.')
        return
      }
      try {
        socket = new WebSocket(SOCKET_URL)
        currentSocket.current = socket
      } catch {
        setError('WebSocket could not be opened. Confirm that the local Python stream is running.')
        return
      }
      socket.onopen = () => {
        if (!active) return
        setConnected(true)
        setError(null)
        reconnectDelay.current = 0
      }
      socket.onmessage = (event: MessageEvent<string>) => {
        if (!active) return
        let message: WireMessage
        try {
          message = JSON.parse(event.data) as WireMessage
          if (!message || typeof message !== 'object' || message.schema !== SCHEMA) return
        } catch {
          setError('Received an unreadable stream message.')
          return
        }
        setLastReceived(Date.now())
        if (message.type === 'reset' && typeof message.session === 'number') {
          onReset(message.session)
        } else if (isStatus(message)) {
          if (latestSession.current !== null && message.session !== latestSession.current &&
              message.phase !== 'starting' && message.phase !== 'connecting') {
            onReset(message.session)
          }
          latestSession.current = message.session
          setStatus(message)
        } else if (message.type === 'replay_state' && message.mode === 'replay' &&
                   typeof message.cursor === 'number' && typeof message.total === 'number') {
          setReplay(message as unknown as ReplayState)
        } else if (isBook(message)) {
          if (latestSession.current !== null && message.session !== latestSession.current) {
            // A reset/status must establish each new session before accepting book state.
            return
          }
          latestSession.current = message.session
          pendingBooks.current[message.symbol] = message
        } else if (message.type === 'trades' && Array.isArray(message.trades) &&
                   typeof message.session === 'number' &&
                   message.session === latestSession.current) {
          const received = typeof message.received_at === 'string' ? message.received_at : ''
          for (const item of message.trades) {
            if (!item || typeof item !== 'object') continue
            const trade = item as Record<string, unknown>
            if (typeof trade.symbol !== 'string' ||
                !['string', 'number'].includes(typeof trade.price) ||
                !['string', 'number'].includes(typeof trade.qty)) continue
            pendingTrades.current.push({
              symbol: trade.symbol, price: String(trade.price), qty: String(trade.qty),
              side: typeof trade.side === 'string' ? trade.side : undefined,
              timestamp: typeof trade.timestamp === 'string' ? trade.timestamp : undefined,
              trade_id: typeof trade.trade_id === 'string' || typeof trade.trade_id === 'number' ? trade.trade_id : undefined,
              received_at: received, session: message.session,
            })
          }
          if (pendingTrades.current.length > 4 * MAX_TRADES) {
            pendingTrades.current = pendingTrades.current.slice(-4 * MAX_TRADES)
          }
        }
      }
      socket.onclose = () => {
        if (!active) return
        setConnected(false)
        // Never display a prior socket's prices as if they belonged to a new session.
        latestSession.current = null
        pendingBooks.current = {}
        pendingTrades.current = []
        setBooks({})
        setHistory({})
        setTrades([])
        setStatus(null)
        setReplay(null)
        const delay = Math.min(800 * 2 ** Math.min(reconnectDelay.current++, 5), 12000)
        retryTimeout = window.setTimeout(connect, delay)
      }
      socket.onerror = () => {
        if (active) setError('Unable to reach ws://127.0.0.1:8765. Start the Python stream server.')
      }
    }

    flushTimer = window.setInterval(flush, 140)
    connect()
    return () => {
      active = false
      window.clearInterval(flushTimer)
      window.clearTimeout(retryTimeout)
      if (currentSocket.current === socket) currentSocket.current = null
      socket?.close(1000, 'Dashboard unmounted')
      pendingBooks.current = {}
      pendingTrades.current = []
    }
  }, [attempt])

  return {
    connected, status, books, trades, history, lastReceived, error, replay,
    replayCommand: (action, value) => {
      if (status?.mode !== 'replay' || currentSocket.current?.readyState !== WebSocket.OPEN) return
      const payload: Record<string, unknown> = {type: 'replay_control', action}
      if (action === 'seek') payload.progress = value
      if (action === 'speed') payload.speed = value
      currentSocket.current.send(JSON.stringify(payload))
    },
    reconnect: () => {
      reconnectDelay.current = 0
      setConnected(false)
      setError(null)
      setAttempt((current) => current + 1)
    },
  }
}
