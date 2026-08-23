// config.hpp - runtime configuration loaded from romfs:/config.json.
#pragma once
#include <string>
#include <vector>

struct SymbolCfg {
    std::string symbol;
    std::string label;
    int decimals = 2;
};

struct TenorCfg {
    std::string label;    // "10Y"
    double years = 0.0;
    std::string ticker;   // optional Yahoo ticker for live fetch (e.g. "^TNX")
    std::string teSymbol; // optional Trading Economics symbol (e.g. "GIDN10Y:GOV")
};

struct YieldSourceCfg {
    std::string name;
    bool sample = false;             // values are placeholder
    bool live   = false;             // fetch per-tenor via ticker
    std::string provider = "yahoo";  // "yahoo" | "tradingeconomics"
    std::string apiKey;              // literal key (client:secret) if given inline
    std::string keyFile;             // path to read key from, e.g. "sdmc:/te_apikey.txt"
    std::string teCountry;           // for TE symbol discovery, e.g. "indonesia"
    bool teAuto = false;             // auto-build curve from /markets/bond/{country}
    std::vector<TenorCfg> tenors;
    std::vector<double> current;     // used when not live (or as fallback)
    std::vector<double> month;
    std::vector<double> year;
};

struct Config {
    int tzOffsetHours = 7;           // WIB
    int refreshSeconds = 120;        // overview/stocks auto-refresh
    int yieldsRefreshSeconds = 900;  // yields auto-refresh (slower; bonds move slowly)
    std::string fontRegular = "romfs:/fonts/main.ttf";
    std::string fontBold    = "romfs:/fonts/bold.ttf";
    std::vector<SymbolCfg> overview;
    std::vector<SymbolCfg> stocks;
    std::vector<YieldSourceCfg> yieldSources;

    // Load from a JSON file path. Returns a fully-populated Config, using
    // built-in defaults for anything missing/absent.
    static Config load(const std::string& path);
    static Config defaults();
};
