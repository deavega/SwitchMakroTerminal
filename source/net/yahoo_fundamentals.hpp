// yahoo_fundamentals.hpp - fetch richer fundamentals from Yahoo's
// quoteSummary endpoint. This endpoint requires a cookie + "crumb" token
// (the same dance yfinance does). All network here; parser is separable.
#pragma once
#include <string>
#include "data/models.hpp"

namespace yahoo {

// Parse a quoteSummary JSON body into a StockSummary. Pure / unit-testable.
StockSummary parseSummary(const std::string& body);

// Obtain (and cache) a Yahoo crumb using a shared cookie jar. Empty on failure.
std::string getCrumb();

// Fetch quoteSummary for a symbol (price, summaryDetail, defaultKeyStatistics,
// financialData, assetProfile). Returns an invalid StockSummary on failure.
StockSummary fetchSummary(const std::string& symbol);

} // namespace yahoo
