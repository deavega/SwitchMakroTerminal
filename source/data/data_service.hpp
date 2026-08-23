// data_service.hpp - owns market data and refreshes it on background threads.
#pragma once
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include "data/models.hpp"
#include "data/config.hpp"

class DataService {
public:
    explicit DataService(const Config& cfg);
    ~DataService();

    void refreshOverview();
    void refreshStocks();
    void refreshYields();
    void refreshAll();

    // thread-safe snapshots (return copies)
    std::vector<Quote>       overview();
    std::vector<Quote>       stocks();
    std::vector<YieldBundle> yields();

    bool overviewLoading() const { return overviewLoading_.load(); }
    bool stocksLoading()   const { return stocksLoading_.load(); }
    bool yieldsLoading()   const { return yieldsLoading_.load(); }

    // Richer per-stock fundamentals (Yahoo quoteSummary), fetched on demand.
    // requestSummary() kicks off a background fetch if not already cached/in-flight.
    // getSummary() returns true and fills `out` once available.
    void requestSummary(const std::string& symbol);
    bool getSummary(const std::string& symbol, StockSummary& out);
    bool summaryLoading(const std::string& symbol);

    int64_t overviewUpdated() const { return overviewUpdated_.load(); }
    int64_t stocksUpdated()   const { return stocksUpdated_.load(); }
    int64_t yieldsUpdated()   const { return yieldsUpdated_.load(); }

private:
    YieldBundle buildBundle(const YieldSourceCfg& src);
    void beginJob(std::atomic<bool>& flag);   // returns false if already running
    bool tryBegin(std::atomic<bool>& flag);

    Config cfg_;
    std::mutex mtx_;
    std::vector<Quote>       overview_;
    std::vector<Quote>       stocks_;
    std::vector<YieldBundle> yields_;

    std::atomic<bool> overviewLoading_{false};
    std::atomic<bool> stocksLoading_{false};
    std::atomic<bool> yieldsLoading_{false};
    std::atomic<int64_t> overviewUpdated_{0};
    std::atomic<int64_t> stocksUpdated_{0};
    std::atomic<int64_t> yieldsUpdated_{0};

    std::atomic<int> inFlight_{0};

    // Cached TE historical series (for 1M/1Y curves), keyed by source name.
    // Fetched rarely (see buildBundle) so refreshes cost ~1 API call, not N.
    struct HistCache {
        int64_t fetched = 0;
        std::map<std::string, std::vector<HistoryPoint>> bySymbol; // UPPER(sym) -> series
    };
    std::map<std::string, HistCache> teHistCache_;

    // Per-stock quoteSummary cache + in-flight set (guarded by summaryMtx_).
    std::mutex summaryMtx_;
    std::map<std::string, StockSummary> summaryCache_;
    std::map<std::string, bool> summaryInFlight_;
};
