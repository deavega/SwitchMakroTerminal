#include "net/yahoo_fundamentals.hpp"
#include "net/yahoo.hpp"     // urlEncode
#include "net/http.hpp"
#include "json.hpp"
#include "log.hpp"
#include <mutex>

namespace yahoo {

// Shared cookie jar on the SD card so the cookie -> getcrumb -> quoteSummary
// requests all share one session.
static const char* kCookieJar = "sdmc:/yahoo_cookies.txt";

static std::mutex g_crumbMtx;
static std::mutex g_fetchMtx;   // serialize summary fetches (shared cookie jar)
static std::string g_crumb;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

// value.raw helper for quoteSummary's { "raw": <num>, "fmt": "<str>" } fields.
static bool rawOf(const mj::Json& mod, const char* field, double& out) {
    const mj::Json& v = mod[field];
    if (v.isNumber()) { out = v.asDouble(); return true; }     // some fields are bare numbers
    if (v.isObject() && v["raw"].isNumber()) { out = v["raw"].asDouble(); return true; }
    return false;
}

static std::string strOf(const mj::Json& mod, const char* field) {
    const mj::Json& v = mod[field];
    if (v.isString()) return v.asString("");
    if (v.isObject() && v["fmt"].isString()) return v["fmt"].asString("");
    return "";
}

std::string getCrumb() {
    std::lock_guard<std::mutex> lk(g_crumbMtx);
    if (!g_crumb.empty()) return g_crumb;

    // 1) Prime cookies. fc.yahoo.com sets the A1/A3 cookie yfinance relies on.
    http::Response c1 = http::get("https://fc.yahoo.com/", 8000, kCookieJar);
    applog::line("YF cookie fc.yahoo: status=%ld", c1.status);
    // Fallback cookie source if fc.yahoo didn't set one.
    http::get("https://finance.yahoo.com/", 8000, kCookieJar);

    // 2) Ask for a crumb using those cookies.
    http::Response r = http::get("https://query2.finance.yahoo.com/v1/test/getcrumb",
                                 8000, kCookieJar);
    applog::line("YF getcrumb: status=%ld bytes=%zu", r.status, r.body.size());
    if (r.ok && !r.body.empty() &&
        r.body.find('<') == std::string::npos &&      // not an HTML/consent page
        r.body.find('{') == std::string::npos) {      // not a JSON error
        g_crumb = trim(r.body);
    }
    if (g_crumb.empty())
        applog::line("YF crumb: FAILED to obtain");
    else
        applog::line("YF crumb: ok (len=%zu)", g_crumb.size());
    return g_crumb;
}

StockSummary parseSummary(const std::string& body) {
    StockSummary s;
    mj::Json root;
    try { root = mj::Json::parse(body); } catch (...) { return s; }

    const mj::Json& res = root["quoteSummary"]["result"];
    if (!res.isArray() || res.size() == 0) return s;
    const mj::Json& r0 = res[(size_t)0];

    const mj::Json& sd = r0["summaryDetail"];
    const mj::Json& ks = r0["defaultKeyStatistics"];
    const mj::Json& fd = r0["financialData"];
    const mj::Json& ap = r0["assetProfile"];
    const mj::Json& pr = r0["price"];

    double v;
    if (rawOf(sd, "trailingPE", v)) { s.peRatio = v; s.hasPE = true; }
    if (rawOf(sd, "forwardPE", v))  { s.forwardPE = v; s.hasFwdPE = true; }
    if (rawOf(sd, "beta", v))       { s.beta = v; s.hasBeta = true; }
    if (rawOf(sd, "dividendYield", v)) { s.dividendYieldPct = v * 100.0; s.hasDivYield = true; }
    if (rawOf(sd, "marketCap", v))  { s.marketCap = v; s.hasMktCap = true; }

    if (!s.hasMktCap && rawOf(pr, "marketCap", v)) { s.marketCap = v; s.hasMktCap = true; }

    if (rawOf(ks, "trailingEps", v)) { s.eps = v; s.hasEps = true; }
    if (rawOf(ks, "priceToBook", v)) { s.priceToBook = v; s.hasPB = true; }
    if (!s.hasBeta && rawOf(ks, "beta", v)) { s.beta = v; s.hasBeta = true; }

    if (rawOf(fd, "targetMeanPrice", v)) { s.targetMean = v; s.hasTarget = true; }
    if (rawOf(fd, "profitMargins", v))   { s.profitMarginsPct = v * 100.0; s.hasMargin = true; }
    if (rawOf(fd, "returnOnEquity", v))  { s.returnOnEquityPct = v * 100.0; s.hasRoe = true; }
    if (rawOf(fd, "revenueGrowth", v))   { s.revenueGrowthPct = v * 100.0; s.hasRevGrowth = true; }
    s.recommendation = strOf(fd, "recommendationKey");

    s.sector   = strOf(ap, "sector");
    s.industry = strOf(ap, "industry");

    s.valid = s.hasPE || s.hasEps || s.hasMktCap || s.hasDivYield || !s.sector.empty();
    return s;
}

StockSummary fetchSummary(const std::string& symbol) {
    std::lock_guard<std::mutex> lk(g_fetchMtx);   // one at a time (shared cookie jar)
    StockSummary empty;
    std::string crumb = getCrumb();
    if (crumb.empty()) return empty;

    std::string url =
        "https://query2.finance.yahoo.com/v10/finance/quoteSummary/" + urlEncode(symbol) +
        "?modules=" + urlEncode("summaryDetail,defaultKeyStatistics,financialData,assetProfile,price") +
        "&crumb=" + urlEncode(crumb);

    http::Response r = http::get(url, 8000, kCookieJar);
    applog::line("YF quoteSummary %s: status=%ld bytes=%zu", symbol.c_str(), r.status, r.body.size());
    if (!r.ok) {
        // A 401 here usually means the crumb went stale; drop it so the next
        // call re-fetches a fresh one.
        if (r.status == 401 || r.status == 403) {
            std::lock_guard<std::mutex> lk(g_crumbMtx);
            g_crumb.clear();
        }
        return empty;
    }
    StockSummary s = parseSummary(r.body);
    applog::line("YF quoteSummary %s: valid=%d pe=%d mktcap=%d",
                 symbol.c_str(), (int)s.valid, (int)s.hasPE, (int)s.hasMktCap);
    return s;
}

} // namespace yahoo
