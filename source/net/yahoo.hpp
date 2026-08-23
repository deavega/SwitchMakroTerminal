// yahoo.hpp - build URLs for and parse responses from Yahoo Finance's
// public chart endpoint (query1.finance.yahoo.com/v8/finance/chart).
#pragma once
#include <string>
#include <vector>
#include "data/models.hpp"

namespace yahoo {

// ---- pure helpers (no network; unit-testable on host) --------------------
std::string urlEncode(const std::string& s);
std::string chartUrl(const std::string& symbol,
                     const std::string& range,
                     const std::string& interval);

// Parse a v8 chart JSON body into a Quote (price/prevClose/change + sparkline).
Quote parseQuote(const std::string& body,
                 const std::string& symbol,
                 const std::string& label);

// Parse a v8 chart JSON body into a (timestamp,close) time series.
std::vector<HistoryPoint> parseHistory(const std::string& body);

// Return the value in `hist` whose timestamp is closest to targetUnix.
// `ok` is set false if history is empty.
double pickClosest(const std::vector<HistoryPoint>& hist, int64_t targetUnix, bool& ok);

// ---- network calls -------------------------------------------------------
// One chart request that yields both a live quote and a sparkline.
Quote fetchQuote(const std::string& symbol,
                 const std::string& label,
                 const std::string& range = "1mo",
                 const std::string& interval = "1d");

std::vector<HistoryPoint> fetchHistory(const std::string& symbol,
                                       const std::string& range = "1y",
                                       const std::string& interval = "1d");

} // namespace yahoo
