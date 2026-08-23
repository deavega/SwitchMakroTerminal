// models.hpp - plain data structures shared across the app.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Extra per-stock detail, all derivable from the /v8/chart endpoint we already
// call (its `meta` block + the daily OHLCV arrays) — no auth-gated endpoint.
struct Fundamentals {
    bool   valid       = false;

    // From chart meta (current session):
    double open        = 0.0;
    double dayHigh     = 0.0;
    double dayLow      = 0.0;
    double week52High  = 0.0;
    double week52Low   = 0.0;
    long long volume   = 0;      // regularMarketVolume
    std::string exchange;        // e.g. "JKSE"
    std::string longName;        // e.g. "Bank Central Asia Tbk"

    // Derived from the fetched daily series (~1 month):
    double avgVolume       = 0.0; // mean daily volume over the window
    double monthChangePct  = 0.0; // first close -> last close
    double monthHigh       = 0.0; // max daily high (or close)
    double monthLow        = 0.0; // min daily low (or close)
    double volatilityAnnPct= 0.0; // annualized stdev of daily log returns (%)

    // Presence flags so the UI can show "—" for anything Yahoo omitted:
    bool hasOpen = false, hasDayRange = false, has52Range = false;
    bool hasVolume = false, hasAvgVolume = false, hasMonthStats = false, hasVol = false;
};

struct Quote {
    std::string symbol;       // Yahoo ticker, e.g. "IDR=X"
    std::string label;        // Friendly label, e.g. "USD / IDR"
    double price      = 0.0;
    double prevClose  = 0.0;
    double change     = 0.0;  // absolute
    double changePct  = 0.0;  // percent
    std::string currency;
    bool   valid      = false;
    std::vector<double> spark; // recent closes for a sparkline (oldest -> newest)
    Fundamentals fund;         // extended detail (see above)
};

// Richer fundamentals from Yahoo's quoteSummary endpoint (needs cookie+crumb).
// Fetched lazily per-symbol when the stock detail view is opened.
struct StockSummary {
    bool valid = false;
    double peRatio = 0, forwardPE = 0, eps = 0, dividendYieldPct = 0;
    double priceToBook = 0, beta = 0, marketCap = 0, targetMean = 0;
    double profitMarginsPct = 0, returnOnEquityPct = 0, revenueGrowthPct = 0;
    bool hasPE=false, hasFwdPE=false, hasEps=false, hasDivYield=false;
    bool hasPB=false, hasBeta=false, hasMktCap=false, hasTarget=false;
    bool hasMargin=false, hasRoe=false, hasRevGrowth=false;
    std::string sector, industry, recommendation;
};

struct HistoryPoint {
    int64_t t = 0;    // unix seconds
    double  v = 0.0;  // close value
};

// One point on a yield curve: a tenor (in years) and its yield (%).
struct YieldPoint {
    double tenorYears = 0.0;
    std::string label;   // e.g. "10Y", "3M"
    double yield = 0.0;   // percent
    bool   valid = false;
};

// A full curve (a series of tenor points) captured at some moment.
struct YieldCurve {
    std::string name;                 // "Current", "1M Ago", "1Y Ago"
    std::vector<YieldPoint> points;
    bool valid = false;
};

// A yields "source" = a named set of three curves over the same tenors.
struct YieldBundle {
    std::string sourceName;           // "Indonesia INDOGB" / "US Treasury"
    bool   live = false;              // whether values came from a live fetch
    bool   sample = false;            // whether values are placeholder/sample
    YieldCurve current;
    YieldCurve month;
    YieldCurve year;
};
