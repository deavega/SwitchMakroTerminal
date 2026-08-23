#include "json.hpp"
#include "net/yahoo.hpp"
#include <cstdio>
#include <cassert>
#include <cmath>

static const char* kSample = R"JSON(
{
  "chart": {
    "result": [
      {
        "meta": {
          "currency": "IDR",
          "symbol": "IDR=X",
          "regularMarketPrice": 16345.5,
          "chartPreviousClose": 16320.0,
          "previousClose": 16320.0
        },
        "timestamp": [1719792000, 1719878400, 1719964800, 1720051200],
        "indicators": {
          "quote": [
            { "close": [16300.0, 16330.5, null, 16345.5] }
          ]
        }
      }
    ],
    "error": null
  }
}
)JSON";

int main() {
    // 1) raw JSON parser sanity
    auto j = mj::Json::parse(kSample);
    assert(j.isObject());
    assert(j["chart"]["result"].isArray());
    assert(j["chart"]["result"].size() == 1);
    assert(std::fabs(j["chart"]["result"][(size_t)0]["meta"]["regularMarketPrice"].asDouble() - 16345.5) < 1e-6);

    // 2) escape + unicode handling
    auto e = mj::Json::parse(R"({"a":"line1\nline2","u":"caf\u00e9","emoji":"\uD83D\uDE00"})");
    assert(e["a"].asString() == "line1\nline2");
    assert(e["u"].asString() == "caf\xC3\xA9");           // café in UTF-8
    assert(e["emoji"].asString() == "\xF0\x9F\x98\x80");  // 😀

    // 3) quote parsing
    Quote q = yahoo::parseQuote(kSample, "IDR=X", "USD / IDR");
    assert(q.valid);
    assert(std::fabs(q.price - 16345.5) < 1e-6);
    assert(std::fabs(q.prevClose - 16320.0) < 1e-6);
    assert(std::fabs(q.change - 25.5) < 1e-6);
    assert(q.currency == "IDR");
    // nulls skipped -> 3 spark points
    assert(q.spark.size() == 3);

    // 4) history parsing (null close skipped)
    auto h = yahoo::parseHistory(kSample);
    assert(h.size() == 3);
    assert(h[0].t == 1719792000);
    assert(std::fabs(h.back().v - 16345.5) < 1e-6);

    // 5) pickClosest
    bool ok = false;
    double v = yahoo::pickClosest(h, 1719878400, ok);
    assert(ok);
    assert(std::fabs(v - 16330.5) < 1e-6);

    // 6) url encoding of symbols with = and ^
    assert(yahoo::urlEncode("IDR=X") == "IDR%3DX");
    assert(yahoo::urlEncode("^JKSE") == "%5EJKSE");

    std::printf("ALL TESTS PASSED\n");
    std::printf("  price=%.2f change=%.2f (%.3f%%) spark=%zu hist=%zu\n",
                q.price, q.change, q.changePct, q.spark.size(), h.size());
    return 0;
}
