# Makro Terminal — Nintendo Switch homebrew

A sleek, dark "professional dashboard" that pulls **live macro & market data** from
Yahoo Finance's public chart API and renders it with SDL2 on a Switch running custom
firmware / the Homebrew Launcher.

Three views, switchable with the shoulder buttons:

| View | Shows |
|------|-------|
| **OVERVIEW** | USD/IDR (`IDR=X`) and IHSG (`^JKSE`) as big metric cards with sparklines + a strip of top Indonesian equities |
| **STOCKS**   | Grid of Indonesian stocks (`.JK`) — price, % change, sparkline, D-pad selectable. Press **A** for a full fundamentals view |
| **YIELDS**   | Yield curve with **Current vs 1M Ago vs 1Y Ago** overlaid, plus a spread panel that calls out **STEEPENING / FLATTENING** in bps. Press **Y** to switch source, **X** to overlay **INDOGB vs UST** on one maturity axis with per-tenor spreads |

### Stock fundamentals (Stocks → A)

Selecting a stock and pressing **A** opens a detail view with a 52-week range bar,
a day range bar, a 1-month chart, and a stats grid: open, previous close, day
high/low, 52-week high/low, volume, average volume, 1-month change, and annualized
volatility. **Left/Right** flips between stocks, **B** goes back, **X** refreshes.

When the detail view opens, the app also fetches **richer valuation metrics** from
Yahoo's `quoteSummary` endpoint (the same data `yfinance` exposes): **P/E, forward P/E,
EPS, dividend yield, price/book, beta, market cap, mean analyst target, ROE, and profit
margin**, plus the company's **sector/industry**. This needs a Yahoo cookie + "crumb",
which the app obtains automatically and caches for the session (cookies are stored at
`sdmc:/yahoo_cookies.txt`). It's fetched lazily per stock, so it costs one request the
first time you open a given stock. If Yahoo declines the crumb, the base stats still show
and the valuation block is simply omitted.

---

## 1. Prerequisites (one-time)

Install **devkitPro** (see https://devkitpro.org/wiki/Getting_Started), then install the
toolchain + libraries:

```bash
# Switch toolchain + libnx
sudo dkp-pacman -S switch-dev

# libraries this project links against
sudo dkp-pacman -S \
  switch-sdl2 switch-sdl2_gfx switch-sdl2_ttf \
  switch-curl switch-mbedtls \
  switch-freetype switch-harfbuzz switch-libpng switch-bzip2 switch-zlib
```

Make sure `DEVKITPRO` is exported (the installer usually adds this):

```bash
export DEVKITPRO=/opt/devkitpro
export PATH=$DEVKITPRO/tools/bin:$PATH
```

## 2. Add fonts (required)

SDL2_ttf needs a TTF. Drop two files into `romfs/fonts/`:

```
romfs/fonts/main.ttf   # regular
romfs/fonts/bold.ttf   # bold (optional; falls back to main.ttf)
```

Any TTF works. [Inter](https://github.com/rsms/inter) or IBM Plex look great and
include the ▲ ▼ • glyphs the UI uses. Example:

```bash
cp ~/Downloads/Inter-Regular.ttf romfs/fonts/main.ttf
cp ~/Downloads/Inter-Bold.ttf    romfs/fonts/bold.ttf
```

## 3. Build

From the project root:

```bash
make
```

This produces **`SwitchMakroTerminal.nro`**.

## 4. Run

- **On hardware:** copy `SwitchMakroTerminal.nro` to your SD card under `/switch/` and
  launch it from the Homebrew Menu. (Wi-Fi must be connected — it fetches live data.)
- **Over the network (fastest dev loop):** with the Homebrew Menu open on the console:
  ```bash
  nxlink -s SwitchMakroTerminal.nro
  ```
  `-s` streams stdout back to your terminal so you can see logs.

---

## Controls

| Button | Action |
|--------|--------|
| **L / R** (or ZL / ZR) | Switch view (Overview ⇄ Stocks ⇄ Yields) |
| **D-pad / left stick** | Move selection (Stocks grid) |
| **A** | Overview: refresh · Stocks: open fundamentals for the selected stock |
| **B** | Stocks: close the fundamentals view |
| **X** | Stocks: refresh |
| **Y** | Yields view: toggle data source (Indonesia ⇄ US Treasury) |
| **X** | Yields view: toggle compare overlay (INDOGB vs UST on one axis) |
| **+** | Exit |

Data also auto-refreshes every `refresh_seconds` (default 60).

---

## Customizing — `romfs/config.json`

No recompile needed for data changes — edit `romfs/config.json` and rebuild the RomFS
(`make` again). Fields:

- `tz_offset_hours` — clock offset (7 = WIB).
- `refresh_seconds` — auto-refresh interval.
- `overview` / `stocks` — arrays of `{ "symbol", "label", "decimals" }`. Use Yahoo
  tickers: Indonesian equities end in `.JK` (e.g. `BBCA.JK`), FX is `IDR=X`, indices
  start with `^` (e.g. `^JKSE`).
- `yield_sources` — one or more curves. Each has `tenors[]` and either:
  - `"live": true` with a `"ticker"` per tenor (fetched from Yahoo; 1M/1Y points are
    derived from that ticker's 1-year daily history), **or**
  - static `current` / `month` / `year` arrays (aligned to `tenors`), optionally marked
    `"sample": true` to show the amber SAMPLE badge.

### Live INDOGB via Trading Economics

The Indonesia curve can run **live** from the Trading Economics API using your
`GIDN*:GOV` symbols. To enable it, put your API key in a text file named
`te_apikey.txt` in **either** of these places:

```
<the folder your .nro is in>/te_apikey.txt     (e.g. sdmc:/switch/switch_terminal/te_apikey.txt)
sdmc:/te_apikey.txt                             (SD root — fallback)
```

The app checks the folder next to the `.nro` first (it auto-detects its own path at
launch), then the SD root, then whatever `key_file` says in config. Put the key wherever
is tidiest — keeping it next to the `.nro` means everything for the app lives in one
folder. File contents are just the key, e.g. `client:secret`.

On launch the app reads that file; if present, the Indonesia source fetches each
tenor's current yield (`markets/symbol`) plus ~1 year of history (`markets/historical`,
used to derive the 1M-ago and 1Y-ago curves) and the badge switches from amber
**SAMPLE DATA** to green **LIVE**. If the file is missing (or the key/symbols return
nothing), it silently falls back to the built-in sample values — so the app never
breaks over a missing key.

Symbols and the key path are configurable in `romfs/config.json` under the Indonesia
source (`provider: "tradingeconomics"`, `key_file`, and each tenor's `te` field).
Adjust the `GIDN*:GOV` strings to match exactly what your TE subscription exposes —
if a particular tenor isn't in your plan, that point simply shows as a gap and the
rest of the curve still draws. Note this issues two requests per tenor, so a full
8-tenor refresh makes ~16 calls; keep an eye on your TE rate limit.

### About the yield sources

Yahoo Finance does not expose a per-tenor INDOGB curve, which is why the Indonesia
source uses Trading Economics (above) rather than Yahoo. The **US Treasury** source is
live from Yahoo (`^IRX ^FVX ^TNX ^TYX`). Press **Y** on the Yields screen to switch
between sources.

---

## TLS / HTTPS note

Yahoo is HTTPS. By default the app **skips certificate verification** (fine for
read-only public quotes). To verify properly, drop a CA bundle at `romfs/cacert.pem`
(e.g. from https://curl.se/ca/cacert.pem) and rebuild — the app auto-detects it and
enables verification.

---

## Project layout

```
SwitchMakroTerminal/
├── Makefile                     devkitPro/libnx build
├── romfs/
│   ├── config.json              editable tickers, tenors, refresh, tz
│   └── fonts/                    <- put main.ttf / bold.ttf here
├── source/
│   ├── main.cpp                 init libnx + SDL, run loop
│   ├── app.{hpp,cpp}            header, tab bar, navigation, auto-refresh
│   ├── json.hpp                 self-contained JSON parser (no deps)
│   ├── net/
│   │   ├── http.{hpp,cpp}       libcurl HTTPS GET wrapper
│   │   └── yahoo.{hpp,cpp}      Yahoo chart URL build + parse
│   ├── data/
│   │   ├── models.hpp           Quote / YieldCurve / etc.
│   │   ├── config.{hpp,cpp}     config.json loader (+ defaults)
│   │   └── data_service.{hpp,cpp}  threaded background fetching
│   └── ui/
│       ├── theme.hpp            colors + layout constants
│       ├── renderer.{hpp,cpp}   fonts, text cache, primitives
│       ├── widgets.{hpp,cpp}    metric cards, sparklines, curve chart
│       └── screens/             overview / stocks / yields
└── tests/                       host-side unit tests (not built for Switch)
```

## Running the host tests (optional)

The portable core (JSON + Yahoo parsing) has unit tests that build with a normal
desktop compiler:

```bash
g++ -std=c++17 -Isource tests/test_parse.cpp source/net/yahoo.cpp tests/http_stub.cpp -o test_parse
./test_parse
```

---

## Troubleshooting

**If the app closes/crashes**, it writes a staged log to the SD card at
`sdmc:/makro_terminal.log`. Pop the SD card (or use FTP/nxlink) and read the last line —
it names the exact stage reached, e.g.:

```
[10:14:02] boot: start
[10:14:02] boot: socket ok
[10:14:02] boot: romfs ok
[10:14:02] boot: curl ok
[10:14:02] boot: config ok (overview=2 stocks=9 sources=2)
[10:14:03] boot: sdl+window+renderer ok
[10:14:03] boot: fonts ok -> entering app loop
```

- Last line is `boot: FONT LOAD FAILED` → no TTF at `romfs/fonts/main.ttf`. Add one and rebuild.
- Log stops at `boot: curl ok` (no `config ok`) → `romfs/config.json` missing/edited badly.
- You see `FATAL in app loop: ...` → send me that line.

Network fetches run on background threads and **never** crash the app now — if Yahoo
rate-limits or returns a non-JSON page, that quote simply shows as unavailable and the
next refresh retries.

## Publishing this project (API keys, security & quota)

This repo is safe to publish and distribute the `.nro` publicly. A few things make
that true, and a couple you should do:

**No secret is ever in the repo or the build.** The Trading Economics key is read at
runtime from `sdmc:/te_apikey.txt` on each user's own SD card — it is never compiled
into the `.nro` and never committed (`te_apikey.txt` is gitignored). Every user brings
their **own** key; TE keys are per-account and must not be shared or embedded. Without a
key the app simply shows the sample INDOGB curve, so it works for everyone out of the box.

**API-quota friendliness (already built in):**
- Yields (Trading Economics) refresh on a slow timer — `yields_refresh_seconds`,
  default **900s (15 min)**. Bond yields move slowly; there's no reason to poll faster.
- All tenors are fetched in **one batched request** (comma-separated symbols), not one
  call per tenor.
- The 1-month / 1-year history is **cached ~6 hours**, so a routine refresh costs about
  **one** API call, not sixteen. Startup costs two (current + history).
- Overview/stocks (Yahoo, no key) refresh on `refresh_seconds`, default **120s**.

  Net: a user on TE's free tier will make only a few calls per hour. If you want it even
  lighter, raise `yields_refresh_seconds` (e.g. `3600` for hourly). Press **A** on the
  Yields screen any time to force an immediate refresh.

**Turn on TLS verification for public builds.** By default the HTTPS layer skips
certificate verification (fine for anonymous public quotes). But once a user adds their
TE key, the app transmits that key to `api.tradingeconomics.com`. To protect it against
a man-in-the-middle, ship a CA bundle so verification is enabled: download
`https://curl.se/ca/cacert.pem` and place it at `romfs/cacert.pem` before `make`. The
app auto-detects it and verifies certificates. It isn't gitignored, so you can commit it
straight into your public repo (it's a public bundle, not a secret).

**Don't rate-limit other people from one IP.** Yahoo may throttle by IP; that only ever
affects the single console making the requests, never other users. Nothing in this app
phones home or shares data between users.

## Notes & disclaimers

- Uses Yahoo Finance's **unofficial** public endpoint (`query1.finance.yahoo.com/v8/finance/chart`).
  It can rate-limit or change; this is for personal/educational use.
- Homebrew requires a Switch already running CFW / the Homebrew Launcher. This project
  ships only original code — no Nintendo SDK, keys, or copyrighted assets.
