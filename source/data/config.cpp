#include "data/config.hpp"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <cstdio>

Config Config::defaults() {
    Config c;
    c.overview = {
        { "IDR=X", "USD / IDR", 2 },
        { "^JKSE", "IHSG \xE2\x80\xA2 Jakarta Composite", 2 },
    };
    c.stocks = {
        { "BBCA.JK", "Bank Central Asia", 0 },
        { "BBRI.JK", "Bank Rakyat Indonesia", 0 },
        { "BMRI.JK", "Bank Mandiri", 0 },
        { "TLKM.JK", "Telkom Indonesia", 0 },
        { "ASII.JK", "Astra International", 0 },
        { "GOTO.JK", "GoTo Gojek Tokopedia", 0 },
    };
    // Indonesia curve: SAMPLE values (Yahoo has no full INDOGB curve).
    YieldSourceCfg indo;
    indo.name = "Indonesia INDOGB";
    indo.sample = true;
    indo.tenors = {
        { "1Y", 1 }, { "3Y", 3 }, { "5Y", 5 }, { "7Y", 7 },
        { "10Y", 10 }, { "15Y", 15 }, { "20Y", 20 }, { "30Y", 30 },
    };
    indo.current = { 6.05, 6.28, 6.42, 6.55, 6.68, 6.92, 7.05, 7.18 };
    indo.month   = { 6.20, 6.45, 6.60, 6.72, 6.85, 7.05, 7.16, 7.28 };
    indo.year    = { 6.35, 6.55, 6.70, 6.80, 6.62, 6.98, 7.10, 7.22 };

    // US Treasury: fully LIVE via Yahoo tenor tickers.
    YieldSourceCfg ust;
    ust.name = "US Treasury (live)";
    ust.live = true;
    ust.tenors = {
        { "3M", 0.25, "^IRX" },
        { "5Y",  5.0, "^FVX" },
        { "10Y", 10.0, "^TNX" },
        { "30Y", 30.0, "^TYX" },
    };

    c.yieldSources = { indo, ust };
    return c;
}

static std::vector<double> readDoubleArray(const mj::Json& a) {
    std::vector<double> v;
    if (!a.isArray()) return v;
    for (size_t i = 0; i < a.size(); i++) v.push_back(a[i].asDouble(0.0));
    return v;
}

Config Config::load(const std::string& path) {
    Config c = defaults();

    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        std::printf("[config] %s not found, using defaults\n", path.c_str());
        return c;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();
    if (body.empty()) return c;

    mj::Json j = mj::Json::parse(body);
    if (!j.isObject()) return c;

    if (j.contains("tz_offset_hours")) c.tzOffsetHours  = (int)j["tz_offset_hours"].asDouble(7);
    if (j.contains("refresh_seconds")) c.refreshSeconds  = (int)j["refresh_seconds"].asDouble(120);
    if (j.contains("yields_refresh_seconds"))
        c.yieldsRefreshSeconds = (int)j["yields_refresh_seconds"].asDouble(900);
    if (j["fonts"].isObject()) {
        c.fontRegular = j["fonts"]["regular"].asString(c.fontRegular);
        c.fontBold    = j["fonts"]["bold"].asString(c.fontBold);
    }

    if (j["overview"].isArray()) {
        c.overview.clear();
        const mj::Json& arr = j["overview"];
        for (size_t i = 0; i < arr.size(); i++) {
            SymbolCfg s;
            s.symbol   = arr[i]["symbol"].asString("");
            s.label    = arr[i]["label"].asString(s.symbol);
            s.decimals = (int)arr[i]["decimals"].asDouble(2);
            if (!s.symbol.empty()) c.overview.push_back(s);
        }
    }

    if (j["stocks"].isArray()) {
        c.stocks.clear();
        const mj::Json& arr = j["stocks"];
        for (size_t i = 0; i < arr.size(); i++) {
            SymbolCfg s;
            s.symbol   = arr[i]["symbol"].asString("");
            s.label    = arr[i]["label"].asString(s.symbol);
            s.decimals = (int)arr[i]["decimals"].asDouble(0);
            if (!s.symbol.empty()) c.stocks.push_back(s);
        }
    }

    if (j["yield_sources"].isArray()) {
        c.yieldSources.clear();
        const mj::Json& arr = j["yield_sources"];
        for (size_t i = 0; i < arr.size(); i++) {
            const mj::Json& src = arr[i];
            YieldSourceCfg y;
            y.name     = src["name"].asString("Curve");
            y.sample   = src["sample"].asBool(false);
            y.live     = src["live"].asBool(false);
            y.provider = src["provider"].asString("yahoo");
            y.apiKey   = src["apikey"].asString("");
            y.keyFile  = src["key_file"].asString("");
            y.teCountry = src["te_country"].asString("");
            y.teAuto    = src["te_auto"].asBool(false);
            const mj::Json& ts = src["tenors"];
            if (ts.isArray()) {
                for (size_t k = 0; k < ts.size(); k++) {
                    TenorCfg t;
                    t.label    = ts[k]["label"].asString("");
                    t.years    = ts[k]["years"].asDouble(0.0);
                    t.ticker   = ts[k]["ticker"].asString("");
                    t.teSymbol = ts[k]["te"].asString("");
                    y.tenors.push_back(t);
                }
            }
            y.current = readDoubleArray(src["current"]);
            y.month   = readDoubleArray(src["month"]);
            y.year    = readDoubleArray(src["year"]);
            c.yieldSources.push_back(y);
        }
    }

    return c;
}
