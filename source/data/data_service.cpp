#include "data/data_service.hpp"
#include "net/yahoo.hpp"
#include "net/yahoo_fundamentals.hpp"
#include "net/tradingeconomics.hpp"
#include "app_paths.hpp"
#include "log.hpp"
#include <thread>
#include <chrono>
#include <ctime>
#include <functional>
#include <pthread.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace {
// Guarantees the loading flag is cleared and the in-flight counter is
// decremented no matter how the worker exits (normal return, exception,
// std::bad_alloc). Without this, a throw in a detached worker would call
// std::terminate() and force-close the app, or leave the service stuck
// "loading" forever.
struct WorkGuard {
    std::atomic<bool>& loading;
    std::atomic<int>&  inflight;
    ~WorkGuard() {
        loading.store(false);
        inflight.fetch_sub(1);
    }
};

// libnx/newlib's std::thread uses a small default stack — far too small for
// the mbedTLS TLS handshake, whose certificate-chain verification recurses
// deeply. On the console that overflows the stack and hard-faults (an
// uncatchable force-close ~2s in, right when HTTPS runs). Launch workers on a
// pthread with an explicit 1 MiB stack instead; that's what the earlier
// project needed too.
constexpr size_t kWorkerStack = 1u << 20; // 1 MiB

void* workerThunk(void* arg) {
    auto* fn = static_cast<std::function<void()>*>(arg);
    try { (*fn)(); } catch (...) {}
    delete fn;
    return nullptr;
}

bool launchWorker(std::function<void()> fn) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return false;
    pthread_attr_setstacksize(&attr, kWorkerStack);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    auto* heapFn = new std::function<void()>(std::move(fn));
    pthread_t th;
    int rc = pthread_create(&th, &attr, workerThunk, heapFn);
    pthread_attr_destroy(&attr);
    if (rc != 0) { delete heapFn; return false; }
    return true;
}
} // namespace

DataService::DataService(const Config& cfg) : cfg_(cfg) {}

DataService::~DataService() {
    // wait for any detached workers to finish touching us
    while (inFlight_.load() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

bool DataService::tryBegin(std::atomic<bool>& flag) {
    bool expected = false;
    return flag.compare_exchange_strong(expected, true);
}

void DataService::refreshOverview() {
    if (!tryBegin(overviewLoading_)) return;
    inFlight_++;
    bool launched = launchWorker([this]() {
        WorkGuard guard{overviewLoading_, inFlight_};
        try {
            std::vector<Quote> out;
            for (const auto& s : cfg_.overview)
                out.push_back(yahoo::fetchQuote(s.symbol, s.label, "1mo", "1d"));
            {
                std::lock_guard<std::mutex> lk(mtx_);
                overview_ = std::move(out);
            }
            overviewUpdated_.store((int64_t)std::time(nullptr));
        } catch (...) {
            // swallow; guard clears the loading flag and in-flight count
        }
    });
    if (!launched) { overviewLoading_.store(false); inFlight_--; }
}

void DataService::refreshStocks() {
    if (!tryBegin(stocksLoading_)) return;
    inFlight_++;
    bool launched = launchWorker([this]() {
        WorkGuard guard{stocksLoading_, inFlight_};
        try {
            std::vector<Quote> out;
            for (const auto& s : cfg_.stocks) {
                Quote q = yahoo::fetchQuote(s.symbol, s.label, "1mo", "1d");
                out.push_back(q);
            }
            {
                std::lock_guard<std::mutex> lk(mtx_);
                stocks_ = std::move(out);
            }
            stocksUpdated_.store((int64_t)std::time(nullptr));
        } catch (...) {
        }
    });
    if (!launched) { stocksLoading_.store(false); inFlight_--; }
}

// Read a file's contents, trimmed of surrounding whitespace/newlines.
static std::string readFileTrim(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

// Format a unix time as UTC "YYYY-MM-DD".
static std::string ymd(int64_t unixSec) {
    time_t tt = (time_t)unixSec;
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return std::string(buf);
}

// Short label for a maturity in years: "3M", "1Y", "10Y", "2.5Y".
static std::string tenorLabel(double y) {
    char buf[16];
    if (y < 0.999) {
        int m = (int)std::lround(y * 12.0);
        if (m < 1) m = 1;
        std::snprintf(buf, sizeof(buf), "%dM", m);
    } else if (std::fabs(y - std::lround(y)) < 0.05) {
        std::snprintf(buf, sizeof(buf), "%dY", (int)std::lround(y));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1fY", y);
    }
    return std::string(buf);
}

YieldBundle DataService::buildBundle(const YieldSourceCfg& src) {
    YieldBundle b;
    b.sourceName = src.name;
    b.live   = src.live;
    b.sample = src.sample;

    b.current.name = "Current";
    b.month.name   = "1M Ago";
    b.year.name    = "1Y Ago";

    const int64_t now = (int64_t)std::time(nullptr);
    const int64_t monthAgo = now - 30LL * 24 * 3600;
    const int64_t yearAgo  = now - 365LL * 24 * 3600;

    auto anyValid = [](const YieldCurve& c) {
        for (auto& p : c.points) if (p.valid) return true;
        return false;
    };
    auto finalize = [&]() {
        b.current.valid = anyValid(b.current);
        b.month.valid   = anyValid(b.month);
        b.year.valid    = anyValid(b.year);
        int vc = 0; for (auto& p : b.current.points) if (p.valid) vc++;
        applog::line("source '%s': current %d/%zu valid, live=%d sample=%d",
                     b.sourceName.c_str(), vc, b.current.points.size(),
                     (int)b.live, (int)b.sample);
        return b;
    };

    // ---- Trading Economics provider (live INDOGB via GIDN*:GOV) ----
    if (src.provider == "tradingeconomics") {
        // Key resolution order: inline apiKey -> te_apikey.txt next to the .nro
        // -> the path configured in key_file. First non-empty wins.
        std::string key = src.apiKey;
        if (key.empty() && !paths::appDir().empty())
            key = readFileTrim(paths::appDir() + "/te_apikey.txt");
        if (key.empty() && !src.keyFile.empty())
            key = readFileTrim(src.keyFile);

        applog::line("TE '%s': key %s (len=%zu), appDir='%s'", src.name.c_str(),
                     key.empty() ? "NOT FOUND" : "found", key.size(),
                     paths::appDir().c_str());

        if (!key.empty()) {
            // ----- AUTO mode: build the whole curve from /markets/bond/{country} -----
            if (src.teAuto && !src.teCountry.empty()) {
                auto rows = te::fetchCountryBonds(src.teCountry, key);
                applog::line("TE auto '%s': %zu bond rows", src.name.c_str(), rows.size());

                std::vector<const te::BondRow*> usable;
                std::string csv;
                for (const auto& row : rows) {
                    if (row.symbol.empty() || !row.hasLast || !row.hasTenor) {
                        applog::line("  skip name='%s' sym='%s' hasLast=%d hasTenor=%d",
                                     row.name.c_str(), row.symbol.c_str(),
                                     (int)row.hasLast, (int)row.hasTenor);
                        continue;
                    }
                    usable.push_back(&row);
                    if (!csv.empty()) csv += ",";
                    csv += row.symbol;
                }
                std::sort(usable.begin(), usable.end(),
                          [](const te::BondRow* a, const te::BondRow* b) {
                              return a->tenorYears < b->tenorYears;
                          });

                // historical (cached ~6h) for the 1M / 1Y curves
                const int64_t kHistTtl = 6 * 3600;
                HistCache& cache = teHistCache_[src.name];
                if (!csv.empty() && (cache.bySymbol.empty() || (now - cache.fetched) > kHistTtl)) {
                    const std::string d1 = ymd(yearAgo - 5LL * 24 * 3600);
                    auto hm = te::fetchHistoryBatch(csv, key, d1);
                    if (!hm.empty()) { cache.bySymbol = std::move(hm); cache.fetched = now; }
                }

                for (const te::BondRow* row : usable) {
                    YieldPoint pc, pm, py;
                    pc.tenorYears = pm.tenorYears = py.tenorYears = row->tenorYears;
                    pc.label = pm.label = py.label = tenorLabel(row->tenorYears);
                    pc.yield = row->last; pc.valid = true;

                    auto itH = cache.bySymbol.find(te::toUpper(row->symbol));
                    if (itH != cache.bySymbol.end() && !itH->second.empty()) {
                        bool okM = false, okY = false;
                        pm.yield = yahoo::pickClosest(itH->second, monthAgo, okM); pm.valid = okM;
                        py.yield = yahoo::pickClosest(itH->second, yearAgo,  okY); py.valid = okY;
                    }
                    b.current.points.push_back(pc);
                    b.month.points.push_back(pm);
                    b.year.points.push_back(py);
                }

                if (anyValid(b.current)) {
                    b.live = true; b.sample = false;
                    applog::line("TE auto '%s': LIVE (%zu tenors)", src.name.c_str(), usable.size());
                    return finalize();
                }
                applog::line("TE auto '%s': nothing usable -> trying explicit symbols", src.name.c_str());
                b.current.points.clear(); b.month.points.clear(); b.year.points.clear();
                // fall through to the explicit-symbol path below
            }
            {
            // ----- EXPLICIT mode: use the per-tenor `te` symbols from config -----
            // One comma-separated symbol list -> one API call for all tenors.
            std::string csv;
            for (const auto& t : src.tenors) {
                if (t.teSymbol.empty()) continue;
                if (!csv.empty()) csv += ",";
                csv += t.teSymbol;
            }
            applog::line("TE symbols: %s", csv.c_str());

            // Current yields: a single batched markets/symbol request.
            std::map<std::string, double> lastMap = te::fetchLastBatch(csv, key);

            // If not every requested symbol resolved, dump the country's real
            // symbol list to the log so config.json can be corrected.
            size_t requested = 0;
            for (const auto& t : src.tenors) if (!t.teSymbol.empty()) requested++;
            if (lastMap.size() < requested && !src.teCountry.empty()) {
                applog::line("TE '%s': %zu/%zu symbols resolved -> listing available symbols",
                             src.name.c_str(), lastMap.size(), requested);
                te::logAvailableSymbols(src.teCountry, key);
            }

            // Historical (for 1M/1Y): cached ~6h so most refreshes skip it.
            const int64_t kHistTtl = 6 * 3600;
            HistCache& cache = teHistCache_[src.name];
            if (cache.bySymbol.empty() || (now - cache.fetched) > kHistTtl) {
                const std::string d1 = ymd(yearAgo - 5LL * 24 * 3600);
                auto hm = te::fetchHistoryBatch(csv, key, d1);
                if (!hm.empty()) { cache.bySymbol = std::move(hm); cache.fetched = now; }
            }

            for (const auto& t : src.tenors) {
                YieldPoint pc, pm, py;
                pc.tenorYears = pm.tenorYears = py.tenorYears = t.years;
                pc.label = pm.label = py.label = t.label;

                if (!t.teSymbol.empty()) {
                    std::string U = te::toUpper(t.teSymbol);
                    auto itL = lastMap.find(U);
                    const std::vector<HistoryPoint>* hist = nullptr;
                    auto itH = cache.bySymbol.find(U);
                    if (itH != cache.bySymbol.end()) hist = &itH->second;

                    if (itL != lastMap.end())            { pc.yield = itL->second;   pc.valid = true; }
                    else if (hist && !hist->empty())     { pc.yield = hist->back().v; pc.valid = true; }

                    if (hist && !hist->empty()) {
                        bool okM = false, okY = false;
                        double vm = yahoo::pickClosest(*hist, monthAgo, okM);
                        double vy = yahoo::pickClosest(*hist, yearAgo, okY);
                        pm.yield = vm; pm.valid = okM;
                        py.yield = vy; py.valid = okY;
                    }
                }
                b.current.points.push_back(pc);
                b.month.points.push_back(pm);
                b.year.points.push_back(py);
            }

            if (anyValid(b.current)) {   // TE succeeded -> genuinely live
                b.live = true;
                b.sample = false;
                applog::line("TE '%s': LIVE (current curve populated)", src.name.c_str());
                return finalize();
            }
            // TE reachable but returned nothing usable: fall back to sample.
            applog::line("TE '%s': no usable data -> SAMPLE fallback", src.name.c_str());
            b.current.points.clear();
            b.month.points.clear();
            b.year.points.clear();
            } // end explicit-mode else
        }
        // No key (or all-empty): fall through to the sample/static arrays below,
        // keeping the SAMPLE badge from src.sample.
    }

    // ---- Yahoo-live per-tenor ticker, or static/sample arrays ----
    for (size_t i = 0; i < src.tenors.size(); i++) {
        const TenorCfg& t = src.tenors[i];
        YieldPoint pc, pm, py;
        pc.tenorYears = pm.tenorYears = py.tenorYears = t.years;
        pc.label = pm.label = py.label = t.label;

        if (src.live && !t.ticker.empty()) {
            auto hist = yahoo::fetchHistory(t.ticker, "1y", "1d");
            Quote q = yahoo::fetchQuote(t.ticker, t.label, "5d", "1d");
            if (q.valid) { pc.yield = q.price; pc.valid = true; }
            else if (!hist.empty()) { pc.yield = hist.back().v; pc.valid = true; }
            bool okM = false, okY = false;
            double vm = yahoo::pickClosest(hist, monthAgo, okM);
            double vy = yahoo::pickClosest(hist, yearAgo, okY);
            pm.yield = vm; pm.valid = okM;
            py.yield = vy; py.valid = okY;
        } else {
            if (i < src.current.size()) { pc.yield = src.current[i]; pc.valid = true; }
            if (i < src.month.size())   { pm.yield = src.month[i];   pm.valid = true; }
            if (i < src.year.size())    { py.yield = src.year[i];    py.valid = true; }
        }

        b.current.points.push_back(pc);
        b.month.points.push_back(pm);
        b.year.points.push_back(py);
    }

    return finalize();
}

void DataService::refreshYields() {
    if (!tryBegin(yieldsLoading_)) return;
    inFlight_++;
    bool launched = launchWorker([this]() {
        WorkGuard guard{yieldsLoading_, inFlight_};
        try {
            std::vector<YieldBundle> out;
            for (const auto& src : cfg_.yieldSources)
                out.push_back(buildBundle(src));
            {
                std::lock_guard<std::mutex> lk(mtx_);
                yields_ = std::move(out);
            }
            yieldsUpdated_.store((int64_t)std::time(nullptr));
        } catch (...) {
        }
    });
    if (!launched) { yieldsLoading_.store(false); inFlight_--; }
}

void DataService::refreshAll() {
    refreshOverview();
    refreshStocks();
    refreshYields();
}

std::vector<Quote> DataService::overview() {
    std::lock_guard<std::mutex> lk(mtx_);
    return overview_;
}
std::vector<Quote> DataService::stocks() {
    std::lock_guard<std::mutex> lk(mtx_);
    return stocks_;
}
std::vector<YieldBundle> DataService::yields() {
    std::lock_guard<std::mutex> lk(mtx_);
    return yields_;
}

bool DataService::getSummary(const std::string& symbol, StockSummary& out) {
    std::lock_guard<std::mutex> lk(summaryMtx_);
    auto it = summaryCache_.find(symbol);
    if (it == summaryCache_.end()) return false;
    out = it->second;
    return true;
}

bool DataService::summaryLoading(const std::string& symbol) {
    std::lock_guard<std::mutex> lk(summaryMtx_);
    auto it = summaryInFlight_.find(symbol);
    return it != summaryInFlight_.end() && it->second;
}

void DataService::requestSummary(const std::string& symbol) {
    {
        std::lock_guard<std::mutex> lk(summaryMtx_);
        if (summaryCache_.count(symbol)) return;   // already have it
        if (summaryInFlight_[symbol]) return;      // already fetching
        summaryInFlight_[symbol] = true;
    }
    inFlight_++;
    std::string sym = symbol;
    bool launched = launchWorker([this, sym]() {
        StockSummary s;
        try { s = yahoo::fetchSummary(sym); } catch (...) {}
        {
            std::lock_guard<std::mutex> lk(summaryMtx_);
            if (s.valid) summaryCache_[sym] = s;   // cache good results; allow retry otherwise
            summaryInFlight_[sym] = false;
        }
        inFlight_--;
    });
    if (!launched) {
        std::lock_guard<std::mutex> lk(summaryMtx_);
        summaryInFlight_[sym] = false;
        inFlight_--;
    }
}
