#include "data/data_service.hpp"
#include "net/yahoo.hpp"
#include <thread>
#include <chrono>
#include <ctime>

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
    std::thread([this]() {
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
    }).detach();
}

void DataService::refreshStocks() {
    if (!tryBegin(stocksLoading_)) return;
    inFlight_++;
    std::thread([this]() {
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
    }).detach();
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

    auto anyValid = [](const YieldCurve& c) {
        for (auto& p : c.points) if (p.valid) return true;
        return false;
    };
    b.current.valid = anyValid(b.current);
    b.month.valid   = anyValid(b.month);
    b.year.valid    = anyValid(b.year);
    return b;
}

void DataService::refreshYields() {
    if (!tryBegin(yieldsLoading_)) return;
    inFlight_++;
    std::thread([this]() {
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
    }).detach();
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
