#include "net/yahoo.hpp"
#include "net/http.hpp"
#include "json.hpp"
#include <cmath>
#include <cstdio>

namespace yahoo {

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
    return "https://query1.finance.yahoo.com/v8/finance/chart/" +
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
    const mj::Json& closes = r0["indicators"]["quote"][(size_t)0]["close"];
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
