#include "net/yahoo.hpp"
#include "net/http.hpp"
#include "json.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace yahoo {

// Pull a JSON array of numbers into a vector<double>, skipping null entries
// (Yahoo pads missing samples with null).
static std::vector<double> numArray(const mj::Json& arr) {
    std::vector<double> v;
    if (!arr.isArray()) return v;
    v.reserve(arr.size());
    for (size_t i = 0; i < arr.size(); i++)
        if (arr[i].isNumber()) v.push_back(arr[i].asDouble());
    return v;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string chartUrl(const std::string& symbol,
                     const std::string& range,
                     const std::string& interval) {
    // URL diubah menjadi query2.finance.yahoo.com
    return "https://query2.finance.yahoo.com/v8/finance/chart/" +
           urlEncode(symbol) +
           "?range=" + urlEncode(range) +
           "&interval=" + urlEncode(interval) +
           "&includePrePost=false";
}

static const mj::Json& result0(const mj::Json& root) {
    static const mj::Json kNull;
    const mj::Json& res = root["chart"]["result"];
    if (!res.isArray() || res.size() == 0) return kNull;
    return res[0];
}

Quote parseQuote(const std::string& body,
                 const std::string& symbol,
                 const std::string& label) {
    Quote q;
    q.symbol = symbol;
    q.label  = label;

    // Yahoo can return HTTP 200 with a non-JSON body (consent / rate-limit
    // page). Never let a parse error escape into the worker thread.
    mj::Json root;
    try {
        root = mj::Json::parse(body);
    } catch (...) {
        q.valid = false;
        return q;
    }
    const mj::Json& r0 = result0(root);
    if (r0.isNull()) return q;

    const mj::Json& meta = r0["meta"];
    if (!meta.isObject()) return q;

    q.price    = meta["regularMarketPrice"].asDouble(0.0);
    q.currency = meta["currency"].asString("");

    double prev = 0.0;
    if (meta.contains("chartPreviousClose"))
        prev = meta["chartPreviousClose"].asDouble(0.0);
    else if (meta.contains("previousClose"))
        prev = meta["previousClose"].asDouble(0.0);
    q.prevClose = prev;

    if (q.price != 0.0 && prev != 0.0) {
        q.change    = q.price - prev;
        q.changePct = (q.change / prev) * 100.0;
    }

    // Sparkline from daily closes (skip nulls).
    const mj::Json& quote0 = r0["indicators"]["quote"][(size_t)0];
    const mj::Json& closes = quote0["close"];
    if (closes.isArray()) {
        for (size_t i = 0; i < closes.size(); i++) {
            const mj::Json& c = closes[i];
            if (c.isNumber()) q.spark.push_back(c.asDouble());
        }
    }
    // If meta didn't provide price, fall back to last spark value.
    if (q.price == 0.0 && !q.spark.empty()) {
        q.price = q.spark.back();
        if (q.spark.size() >= 2 && q.spark[q.spark.size() - 2] != 0.0) {
            q.prevClose = q.spark[q.spark.size() - 2];
            q.change    = q.price - q.prevClose;
            q.changePct = (q.change / q.prevClose) * 100.0;
        }
    }

    q.valid = (q.price != 0.0);

    // ---- Fundamentals (all from data we already have in hand) ----
    Fundamentals& f = q.fund;

    // Session fields from meta.
    if (meta.contains("regularMarketOpen")) {
        f.open = meta["regularMarketOpen"].asDouble(0.0);
        f.hasOpen = (f.open != 0.0);
    }
    double dh = meta["regularMarketDayHigh"].asDouble(0.0);
    double dl = meta["regularMarketDayLow"].asDouble(0.0);
    if (dh != 0.0 && dl != 0.0) { f.dayHigh = dh; f.dayLow = dl; f.hasDayRange = true; }

    double wh = meta["fiftyTwoWeekHigh"].asDouble(0.0);
    double wl = meta["fiftyTwoWeekLow"].asDouble(0.0);
    if (wh != 0.0 && wl != 0.0) { f.week52High = wh; f.week52Low = wl; f.has52Range = true; }

    double vol = meta["regularMarketVolume"].asDouble(0.0);
    if (vol > 0.0) { f.volume = (long long)vol; f.hasVolume = true; }

    if (meta.contains("fullExchangeName"))
        f.exchange = meta["fullExchangeName"].asString("");
    if (f.exchange.empty())
        f.exchange = meta["exchangeName"].asString("");

    if (meta.contains("longName"))  f.longName = meta["longName"].asString("");
    if (f.longName.empty())         f.longName = meta["shortName"].asString("");

    // Derived from the daily series.
    std::vector<double> highs = numArray(quote0["high"]);
    std::vector<double> lows  = numArray(quote0["low"]);
    std::vector<double> vols  = numArray(quote0["volume"]);

    // Month high/low: prefer intraday highs/lows, else fall back to closes.
    if (!highs.empty() && !lows.empty()) {
        f.monthHigh = *std::max_element(highs.begin(), highs.end());
        f.monthLow  = *std::min_element(lows.begin(),  lows.end());
        f.hasMonthStats = true;
    } else if (!q.spark.empty()) {
        f.monthHigh = *std::max_element(q.spark.begin(), q.spark.end());
        f.monthLow  = *std::min_element(q.spark.begin(), q.spark.end());
        f.hasMonthStats = true;
    }

    // Month change: first vs last close in the window.
    if (q.spark.size() >= 2 && q.spark.front() != 0.0) {
        f.monthChangePct = (q.spark.back() - q.spark.front()) / q.spark.front() * 100.0;
    }

    // Average daily volume over the window (skip zero/blank days).
    if (!vols.empty()) {
        double sum = 0.0; int n = 0;
        for (double v : vols) if (v > 0.0) { sum += v; n++; }
        if (n > 0) { f.avgVolume = sum / n; f.hasAvgVolume = true; }
    }

    // Annualized volatility: stdev of daily log returns * sqrt(252).
    if (q.spark.size() >= 3) {
        std::vector<double> rets;
        rets.reserve(q.spark.size());
        for (size_t i = 1; i < q.spark.size(); i++) {
            double a = q.spark[i - 1], b = q.spark[i];
            if (a > 0.0 && b > 0.0) rets.push_back(std::log(b / a));
        }
        if (rets.size() >= 2) {
            double mean = 0.0;
            for (double r : rets) mean += r;
            mean /= rets.size();
            double var = 0.0;
            for (double r : rets) var += (r - mean) * (r - mean);
            var /= (rets.size() - 1);
            f.volatilityAnnPct = std::sqrt(var) * std::sqrt(252.0) * 100.0;
            f.hasVol = true;
        }
    }

    f.valid = q.valid;
    return q;
}

std::vector<HistoryPoint> parseHistory(const std::string& body) {
    std::vector<HistoryPoint> out;
    mj::Json root;
    try {
        root = mj::Json::parse(body);
    } catch (...) {
        return out;   // empty history on any parse failure
    }
    const mj::Json& r0 = result0(root);
    if (r0.isNull()) return out;

    const mj::Json& ts     = r0["timestamp"];
    const mj::Json& closes = r0["indicators"]["quote"][(size_t)0]["close"];
    if (!ts.isArray() || !closes.isArray()) return out;

    size_t n = ts.size() < closes.size() ? ts.size() : closes.size();
    for (size_t i = 0; i < n; i++) {
        const mj::Json& c = closes[i];
        if (!c.isNumber()) continue;
        HistoryPoint p;
        p.t = static_cast<int64_t>(ts[i].asDouble(0.0));
        p.v = c.asDouble();
        out.push_back(p);
    }
    return out;
}

double pickClosest(const std::vector<HistoryPoint>& hist, int64_t targetUnix, bool& ok) {
    ok = false;
    if (hist.empty()) return 0.0;
    double best = hist.front().v;
    int64_t bestDiff = std::llabs(hist.front().t - targetUnix);
    for (const auto& p : hist) {
        int64_t d = std::llabs(p.t - targetUnix);
        if (d < bestDiff) { bestDiff = d; best = p.v; }
    }
    ok = true;
    return best;
}

Quote fetchQuote(const std::string& symbol,
                 const std::string& label,
                 const std::string& range,
                 const std::string& interval) {
    http::Response resp = http::get(chartUrl(symbol, range, interval));
    if (!resp.ok) {
        Quote q; q.symbol = symbol; q.label = label; q.valid = false;
        return q;
    }
    return parseQuote(resp.body, symbol, label);
}

std::vector<HistoryPoint> fetchHistory(const std::string& symbol,
                                       const std::string& range,
                                       const std::string& interval) {
    http::Response resp = http::get(chartUrl(symbol, range, interval));
    if (!resp.ok) return {};
    return parseHistory(resp.body);
}

} // namespace yahoo