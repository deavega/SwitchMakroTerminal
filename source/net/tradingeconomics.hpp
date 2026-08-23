// tradingeconomics.hpp - fetch government-bond yields from the Trading
// Economics API (api.tradingeconomics.com). Used to make the INDOGB curve live
// via the user's GIDN*:GOV symbols. All parsers are pure and unit-testable.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>
#include "data/models.hpp"

namespace te {

// URL-encode a value, leaving TE-significant chars (":,._-~") intact.
std::string encode(const std::string& s, bool keepSymbolChars);
std::string toUpper(const std::string& s);

// Endpoints. `symbol` may be a single symbol or a comma-separated list.
// `key` is the TE client:secret string.
std::string symbolUrl(const std::string& symbol, const std::string& key);
std::string historyUrl(const std::string& symbol, const std::string& key,
                       const std::string& d1 /*YYYY-MM-DD*/);

// Convert a TE date ("2024-06-01T00:00:00" or "2024-06-01") to unix seconds.
bool dateToUnix(const std::string& date, int64_t& outUnix);

// ---- single-symbol parsers (kept for tests / simple use) ----
bool parseLast(const std::string& body, double& lastOut);
std::vector<HistoryPoint> parseHistory(const std::string& body);

// ---- multi-symbol parsers (batched requests) ----
// markets/symbol (one or many) -> map UPPER(symbol) -> latest yield.
std::map<std::string, double> parseLastMap(const std::string& body);
// markets/historical (one or many) -> map UPPER(symbol) -> (unix,close) series.
std::map<std::string, std::vector<HistoryPoint>> parseHistoryMap(const std::string& body);

// ---- network ----
bool fetchLast(const std::string& symbol, const std::string& key, double& out);
std::vector<HistoryPoint> fetchHistory(const std::string& symbol,
                                       const std::string& key,
                                       const std::string& d1);

// Batched: `csv` is a comma-separated symbol list; results keyed by UPPER(symbol).
std::map<std::string, double> fetchLastBatch(const std::string& csv,
                                             const std::string& key);
std::map<std::string, std::vector<HistoryPoint>> fetchHistoryBatch(
    const std::string& csv, const std::string& key, const std::string& d1);

// Diagnostic: fetch the market list for a country and log every Symbol+Name to
// sdmc:/makro_terminal.log, so the exact TE symbol strings can be copied into
// config.json. Does nothing if country/key is empty.
void logAvailableSymbols(const std::string& country, const std::string& key);

// A government-bond row from /markets/bond/{country}.
struct BondRow {
    std::string symbol;      // e.g. real TE symbol for INDOGB 10Y
    std::string name;        // e.g. "Indonesia 10Y"
    double last = 0.0;       // latest yield
    bool   hasLast = false;
    double tenorYears = 0.0; // parsed from the name/ticker/symbol
    bool   hasTenor = false;
};

// Parse a maturity in years from a string like "Indonesia 10Y", "3M", "10YR",
// "5 Year". Returns false if no tenor token is found.
bool parseTenorYears(const std::string& s, double& years);

// Fetch all government-bond tenors for a country (the TE Python client's
// getMarketsData(marketsField='bond', country=...)). Rows include the real
// Symbol and parsed maturity.
std::vector<BondRow> fetchCountryBonds(const std::string& country, const std::string& key);

} // namespace te

