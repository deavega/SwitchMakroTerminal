#include "net/tradingeconomics.hpp"
#include "net/http.hpp"
#include "json.hpp"
#include "log.hpp"
#include <cstdio>
#include <cstdlib>

namespace te {

std::string toUpper(const std::string& s) {
    std::string o = s;
    for (char& c : o) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return o;
}

std::string encode(const std::string& s, bool keepSymbolChars) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.' || c == '~';
        bool symbolOk = keepSymbolChars && (c == ':' || c == ',');
        if (unreserved || symbolOk) {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string symbolUrl(const std::string& symbol, const std::string& key) {
    // https://api.tradingeconomics.com/markets/symbol/{sym}?c=KEY&f=json
    return "https://api.tradingeconomics.com/markets/symbol/" +
           encode(symbol, true) + "?c=" + encode(key, true) + "&f=json";
}

std::string historyUrl(const std::string& symbol, const std::string& key,
                       const std::string& d1) {
    // https://api.tradingeconomics.com/markets/historical/{sym}?c=KEY&d1=YYYY-MM-DD&f=json
    return "https://api.tradingeconomics.com/markets/historical/" +
           encode(symbol, true) + "?c=" + encode(key, true) +
           "&d1=" + encode(d1, false) + "&f=json";
}

// Days from civil date (Howard Hinnant's algorithm), valid for any Gregorian date.
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool dateToUnix(const std::string& date, int64_t& outUnix) {
    // Expect at least "YYYY-MM-DD".
    if (date.size() < 10) return false;
    if (date[4] != '-' || date[7] != '-') return false;
    int y = std::atoi(date.substr(0, 4).c_str());
    int m = std::atoi(date.substr(5, 2).c_str());
    int d = std::atoi(date.substr(8, 2).c_str());
    if (y < 1900 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    outUnix = daysFromCivil(y, (unsigned)m, (unsigned)d) * 86400LL;
    return true;
}

bool parseLast(const std::string& body, double& lastOut) {
    mj::Json root;
    try {
        root = mj::Json::parse(body);
    } catch (...) {
        return false;
    }
    // TE returns a JSON array of one object for a single symbol.
    const mj::Json* obj = nullptr;
    if (root.isArray() && root.size() > 0) obj = &root[(size_t)0];
    else if (root.isObject())              obj = &root;
    if (!obj) return false;

    const mj::Json& o = *obj;
    if (o["Last"].isNumber())  { lastOut = o["Last"].asDouble();  return true; }
    if (o["Close"].isNumber()) { lastOut = o["Close"].asDouble(); return true; }
    return false;
}

std::vector<HistoryPoint> parseHistory(const std::string& body) {
    std::vector<HistoryPoint> out;
    mj::Json root;
    try {
        root = mj::Json::parse(body);
    } catch (...) {
        return out;
    }
    if (!root.isArray()) return out;
    for (size_t i = 0; i < root.size(); i++) {
        const mj::Json& e = root[i];
        double close;
        if (e["Close"].isNumber())      close = e["Close"].asDouble();
        else if (e["Value"].isNumber()) close = e["Value"].asDouble();
        else continue;
        int64_t t;
        if (!dateToUnix(e["Date"].asString(""), t)) continue;
        HistoryPoint p;
        p.t = t;
        p.v = close;
        out.push_back(p);
    }
    return out;
}

bool fetchLast(const std::string& symbol, const std::string& key, double& out) {
    if (symbol.empty() || key.empty()) return false;
    http::Response r = http::get(symbolUrl(symbol, key));
    if (!r.ok) return false;
    return parseLast(r.body, out);
}

std::vector<HistoryPoint> fetchHistory(const std::string& symbol,
                                       const std::string& key,
                                       const std::string& d1) {
    std::vector<HistoryPoint> empty;
    if (symbol.empty() || key.empty()) return empty;
    http::Response r = http::get(historyUrl(symbol, key, d1));
    if (!r.ok) return empty;
    return parseHistory(r.body);
}

std::map<std::string, double> parseLastMap(const std::string& body) {
    std::map<std::string, double> out;
    mj::Json root;
    try { root = mj::Json::parse(body); } catch (...) { return out; }
    if (!root.isArray()) {
        if (root.isObject()) {
            std::string sym = root["Symbol"].asString("");
            double v;
            if (!sym.empty() && (root["Last"].isNumber() || root["Close"].isNumber())) {
                v = root["Last"].isNumber() ? root["Last"].asDouble() : root["Close"].asDouble();
                out[toUpper(sym)] = v;
            }
        }
        return out;
    }
    for (size_t i = 0; i < root.size(); i++) {
        const mj::Json& o = root[i];
        std::string sym = o["Symbol"].asString("");
        if (sym.empty()) continue;
        if (o["Last"].isNumber())       out[toUpper(sym)] = o["Last"].asDouble();
        else if (o["Close"].isNumber()) out[toUpper(sym)] = o["Close"].asDouble();
    }
    return out;
}

std::map<std::string, std::vector<HistoryPoint>> parseHistoryMap(const std::string& body) {
    std::map<std::string, std::vector<HistoryPoint>> out;
    mj::Json root;
    try { root = mj::Json::parse(body); } catch (...) { return out; }
    if (!root.isArray()) return out;
    for (size_t i = 0; i < root.size(); i++) {
        const mj::Json& e = root[i];
        std::string sym = e["Symbol"].asString("");
        if (sym.empty()) continue;
        double close;
        if (e["Close"].isNumber())      close = e["Close"].asDouble();
        else if (e["Value"].isNumber()) close = e["Value"].asDouble();
        else continue;
        int64_t t;
        if (!dateToUnix(e["Date"].asString(""), t)) continue;
        HistoryPoint p; p.t = t; p.v = close;
        out[toUpper(sym)].push_back(p);
    }
    return out;
}

std::map<std::string, double> fetchLastBatch(const std::string& csv, const std::string& key) {
    std::map<std::string, double> empty;
    if (csv.empty() || key.empty()) return empty;
    http::Response r = http::get(symbolUrl(csv, key));
    applog::line("TE markets/symbol: ok=%d status=%ld bytes=%zu err='%s'",
                 (int)r.ok, r.status, r.body.size(), r.error.c_str());
    if (!r.body.empty())
        applog::line("TE symbol body[0:200]: %.200s", r.body.c_str());
    if (!r.ok) return empty;
    auto m = parseLastMap(r.body);
    applog::line("TE symbol parsed %zu quotes", m.size());
    return m;
}

std::map<std::string, std::vector<HistoryPoint>> fetchHistoryBatch(
    const std::string& csv, const std::string& key, const std::string& d1) {
    std::map<std::string, std::vector<HistoryPoint>> empty;
    if (csv.empty() || key.empty()) return empty;
    http::Response r = http::get(historyUrl(csv, key, d1));
    applog::line("TE markets/historical: ok=%d status=%ld bytes=%zu",
                 (int)r.ok, r.status, r.body.size());
    if (!r.ok) {
        if (!r.body.empty()) applog::line("TE hist body[0:200]: %.200s", r.body.c_str());
        return empty;
    }
    return parseHistoryMap(r.body);
}

bool parseTenorYears(const std::string& s, double& years) {
    std::string u = toUpper(s);
    for (size_t i = 0; i < u.size();) {
        if (u[i] >= '0' && u[i] <= '9') {
            double num = 0;
            size_t j = i;
            while (j < u.size() && u[j] >= '0' && u[j] <= '9') { num = num * 10 + (u[j] - '0'); j++; }
            // allow a separator ('-' or space) before the unit
            size_t k = j;
            while (k < u.size() && (u[k] == '-' || u[k] == ' ')) k++;
            if (k < u.size()) {
                char c = u[k];
                if (c == 'Y') { years = num;          return true; } // 10Y, 10YR, 10 YEAR
                if (c == 'M') { years = num / 12.0;   return true; } // 3M, 6 MONTH
                if (c == 'W') { years = num / 52.0;   return true; } // 52W
                if (c == 'D') { years = num / 365.0;  return true; } // 90D
            }
            i = j; // no unit matched right after this number; keep scanning
        } else {
            i++;
        }
    }
    return false;
}

// Normalize a country name for comparison: uppercase, hyphens->spaces.
static std::string normCountry(const std::string& s) {
    std::string o = toUpper(s);
    for (char& c : o) if (c == '-') c = ' ';
    return o;
}

static BondRow rowFromJson(const mj::Json& o) {
    BondRow br;
    br.symbol = o["Symbol"].asString("");
    br.name   = o["Name"].asString("");
    std::string tick = o["Ticker"].asString("");
    if (o["Last"].isNumber())       { br.last = o["Last"].asDouble();  br.hasLast = true; }
    else if (o["Close"].isNumber()) { br.last = o["Close"].asDouble(); br.hasLast = true; }
    double y = 0;
    if      (parseTenorYears(br.name, y))  { br.tenorYears = y; br.hasTenor = true; }
    else if (parseTenorYears(tick, y))     { br.tenorYears = y; br.hasTenor = true; }
    else if (parseTenorYears(br.symbol, y)){ br.tenorYears = y; br.hasTenor = true; }
    return br;
}

// Standard TE bond maturities (per docs) used as a last-resort per-type probe.
static const char* kBondTypes[] = {
    "1M","3M","6M","52W","2Y","3Y","5Y","7Y","10Y","15Y","20Y","30Y"
};

static void appendRows(const mj::Json& root, std::vector<BondRow>& out,
                       const std::string* countryFilter) {
    if (!root.isArray()) return;
    std::string want = countryFilter ? normCountry(*countryFilter) : "";
    for (size_t i = 0; i < root.size(); i++) {
        if (countryFilter &&
            normCountry(root[i]["Country"].asString("")) != want) continue;
        out.push_back(rowFromJson(root[i]));
    }
}

std::vector<BondRow> fetchCountryBonds(const std::string& country, const std::string& key) {
    std::vector<BondRow> out;
    if (country.empty() || key.empty()) return out;
    std::string k   = encode(key, true);
    std::string cty = encode(country, false);

    // Strategy 1: documented "Bonds by Country" -> /markets/bond?country={c}
    {
        std::string url = "https://api.tradingeconomics.com/markets/bond?country=" +
                          cty + "&c=" + k + "&f=json";
        http::Response r = http::get(url, 12000);
        applog::line("TE bond?country=%s: status=%ld bytes=%zu", country.c_str(),
                     r.status, r.body.size());
        if (r.ok) {
            mj::Json root; try { root = mj::Json::parse(r.body); } catch (...) { root = mj::Json(); }
            appendRows(root, out, nullptr);
            applog::line("  -> %zu rows", out.size());
            if (out.size() >= 2) return out;
        } else if (!r.body.empty()) {
            applog::line("  body[0:140]: %.140s", r.body.c_str());
        }
    }

    // Strategy 2: full bond snapshot (all countries), filter by Country. Big
    // payload -> generous timeout.
    {
        std::string url = "https://api.tradingeconomics.com/markets/bond?c=" + k + "&f=json";
        http::Response r = http::get(url, 20000);
        applog::line("TE bond(all): status=%ld bytes=%zu", r.status, r.body.size());
        if (r.ok) {
            mj::Json root; try { root = mj::Json::parse(r.body); } catch (...) { root = mj::Json(); }
            std::vector<BondRow> filtered;
            appendRows(root, filtered, &country);
            applog::line("  -> %zu rows match '%s'", filtered.size(), country.c_str());
            if (filtered.size() > out.size()) out = filtered;
            if (out.size() >= 2) return out;
        } else if (!r.body.empty()) {
            applog::line("  body[0:140]: %.140s", r.body.c_str());
        }
    }

    // Strategy 3: per-maturity probe (mirrors getMarketsData bond+country+type).
    // Only reached if the above didn't yield a curve; each call is small.
    {
        std::vector<BondRow> perType;
        for (const char* t : kBondTypes) {
            std::string url = "https://api.tradingeconomics.com/markets/bond?country=" +
                              cty + "&type=" + t + "&c=" + k + "&f=json";
            http::Response r = http::get(url, 8000);
            if (!r.ok) continue;
            mj::Json root; try { root = mj::Json::parse(r.body); } catch (...) { continue; }
            appendRows(root, perType, nullptr);
        }
        applog::line("TE bond per-type: %zu rows", perType.size());
        if (perType.size() > out.size()) out = perType;
    }

    return out;
}

void logAvailableSymbols(const std::string& country, const std::string& key) {
    auto rows = fetchCountryBonds(country, key);
    applog::line("TE discover '%s': %zu rows", country.c_str(), rows.size());
    int n = 0;
    for (const auto& r : rows) {
        applog::line("  BOND sym='%s' name='%s' last=%.3f tenorY=%.3f",
                     r.symbol.c_str(), r.name.c_str(), r.last,
                     r.hasTenor ? r.tenorYears : -1.0);
        if (++n >= 80) { applog::line("  (truncated)"); break; }
    }
}

} // namespace te
