# 75-Second ExchangeCore Portfolio Demonstration

Record a screen capture on your machine with the local dashboard open. This is a **shot list**, not a finished video.

| Segment | Show | Narration prompt |
|---|---|---|
| 0–10s | Opening Market Overview, labeled Live or Replay | “ExchangeCore combines a simulated C++ matching engine with independently validated public market data.” |
| 10–25s | BTC/USD/ETH/USD selection, L2 ladder and health status | “The Python adapter reconstructs aggregated price levels and checks their CRC32 integrity.” |
| 25–40s | Historical replay pause, jump, resume | “Recorded event sessions can be inspected again, with sessions reset after seeks.” |
| 40–55s | Engine Performance, select Crossing and Mixed | “These are real in-process benchmark results on my Windows machine, not exchange latency.” |
| 55–68s | Throughput comparison and p99.9-by-decile chart | “Memory reservation affects throughput and sometimes tail latency. We show all tested configurations, not only winners.” |
| 68–75s | GitHub README and automated test result | “Reproduce it with CMake and Python tests or run a local read-only terminal.” |

Tips: use the locally captured recording for predictable playback. If a recording contains real exchange prices, label its date and source; do not imply the footage is showing current prices. Do not expose credentials or private file paths on screen. Show the method and limitations instead of presenting it as a profitable automated trading strategy.
