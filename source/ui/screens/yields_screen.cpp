#include "ui/screens/yields_screen.hpp"
#include "ui/widgets.hpp"
#include <switch.h>
#include <cstdio>
#include <cmath>

void YieldsScreen::onEnter(DataService& data) {
    if (data.yields().empty() && !data.yieldsLoading())
        data.refreshYields();
}

void YieldsScreen::handleInput(uint64_t k, DataService& data) {
    if (k & HidNpadButton_A) data.refreshYields();
    if (k & HidNpadButton_X) compare_ = !compare_;   // toggle overlay
    auto y = data.yields();
    if (!compare_ && !y.empty() && (k & HidNpadButton_Y))
        sourceIndex_ = (sourceIndex_ + 1) % (int)y.size();
}

// Colors used per source in compare mode.
static theme::Col sourceColor(size_t i) {
    static const theme::Col pal[] = { theme::accent, theme::violet, theme::teal, theme::amber };
    return pal[i % 4];
}

// spread = long-end yield minus short-end yield across a curve (in %).
static bool curveSpread(const YieldCurve& c, double& spreadPct) {
    const YieldPoint* first = nullptr;
    const YieldPoint* last  = nullptr;
    for (auto& p : c.points) {
        if (!p.valid) continue;
        if (!first) first = &p;
        last = &p;
    }
    if (!first || !last || first == last) return false;
    spreadPct = last->yield - first->yield;
    return true;
}

void YieldsScreen::render(Renderer& R, DataService& data, SDL_Rect c) {
    auto bundles = data.yields();
    if (bundles.empty()) {
        const char* msg = data.yieldsLoading() ? "Loading bond curves\xE2\x80\xA6" : "No data";
        R.text(msg, c.x, c.y + 4, FontSize::Body, theme::textFaint);
        return;
    }

    // ================= COMPARE MODE (overlay all sources) =================
    if (compare_) {
        R.text("COMPARE CURVES", c.x, c.y, FontSize::Medium, theme::text, Align::Left, true);
        R.text("X: back   \xE2\x80\xA2   A: refresh", c.x + c.w, c.y + 6,
               FontSize::Small, theme::textFaint, Align::Right);
        if (data.yieldsLoading())
            R.text("syncing\xE2\x80\xA6", c.x + R.measure("COMPARE CURVES ", FontSize::Medium, true) + 12,
                   c.y + 4, FontSize::Small, theme::amber);

        int top = c.y + R.lineHeight(FontSize::Medium) + 16;
        int panelW = 320, gap = 20;
        SDL_Rect chartR = { c.x, top, c.w - panelW - gap, c.y + c.h - top };
        SDL_Rect panelR = { c.x + c.w - panelW, top, panelW, c.y + c.h - top };

        // one CURRENT curve per source, each its own color
        std::vector<const YieldCurve*> curves;
        std::vector<widgets::CurveStyle> styles;
        for (size_t i = 0; i < bundles.size(); i++) {
            curves.push_back(&bundles[i].current);
            styles.push_back({ sourceColor(i), bundles[i].sourceName });
        }
        widgets::yieldCompareChart(R, chartR, curves, styles);

        // analytics: spread between the first two valid sources
        R.card(panelR, false);
        int px = panelR.x + 20, py = panelR.y + 20;
        const YieldBundle* A = nullptr; const YieldBundle* B = nullptr;
        for (auto& bb : bundles) {
            if (!bb.current.valid) continue;
            if (!A) A = &bb; else if (!B) { B = &bb; break; }
        }
        if (A && B) {
            // short names for the header
            R.text("SPREAD", px, py, FontSize::Body, theme::textDim, Align::Left, true);
            py += R.lineHeight(FontSize::Body) + 4;
            R.text(A->sourceName + "  \xE2\x88\x92  " + B->sourceName, px, py,
                   FontSize::Small, theme::textFaint);
            py += R.lineHeight(FontSize::Small) + 14;

            // headline 10Y spread
            double a10, b10;
            if (widgets::yieldAtTenor(A->current, 10.0, 1.0, a10) &&
                widgets::yieldAtTenor(B->current, 10.0, 1.0, b10)) {
                char v[48];
                std::snprintf(v, sizeof(v), "10Y  %+.0f bps", (a10 - b10) * 100.0);
                R.pillTag(px, py, v, theme::accent,
                          theme::Col{ theme::accent.r, theme::accent.g, theme::accent.b, 40 });
                py += R.lineHeight(FontSize::Small) + 20;
            }

            R.text("BY MATURITY", px, py, FontSize::Small, theme::textDim, Align::Left, true);
            py += R.lineHeight(FontSize::Small) + 8;
            const double targets[] = { 2, 5, 10, 20, 30 };
            const char*  tlabels[] = { "2Y", "5Y", "10Y", "20Y", "30Y" };
            for (int i = 0; i < 5; i++) {
                double ya, yb;
                bool okA = widgets::yieldAtTenor(A->current, targets[i], 1.0, ya);
                bool okB = widgets::yieldAtTenor(B->current, targets[i], 1.0, yb);
                if (!okA || !okB) continue;
                R.text(tlabels[i], px, py, FontSize::Body, theme::text, Align::Left);
                char v[32]; std::snprintf(v, sizeof(v), "%+.0f bps", (ya - yb) * 100.0);
                R.text(v, panelR.x + panelR.w - 20, py, FontSize::Body, theme::text, Align::Right, true);
                py += R.lineHeight(FontSize::Body) + 8;
            }
        } else {
            R.text("Need two live sources", px, py, FontSize::Small, theme::textFaint);
        }
        return;
    }
    // ================= SINGLE-SOURCE MODE =================
    if (sourceIndex_ >= (int)bundles.size()) sourceIndex_ = 0;
    const YieldBundle& b = bundles[sourceIndex_];

    // header row: source name + toggle hint + sample tag
    R.text(b.sourceName, c.x, c.y, FontSize::Medium, theme::text, Align::Left, true);
    if (b.sample)
        R.pillTag(c.x + R.measure(b.sourceName, FontSize::Medium, true) + 16, c.y + 4,
                  "SAMPLE DATA", theme::amber, theme::Col{ theme::amber.r, theme::amber.g, theme::amber.b, 40 });
    else if (b.live)
        R.pillTag(c.x + R.measure(b.sourceName, FontSize::Medium, true) + 16, c.y + 4,
                  "LIVE", theme::up, theme::Col{ theme::up.r, theme::up.g, theme::up.b, 40 });
    R.text("Y: source   \xE2\x80\xA2   X: compare   \xE2\x80\xA2   A: refresh",
           c.x + c.w, c.y + 6, FontSize::Small, theme::textFaint, Align::Right);

    int top = c.y + R.lineHeight(FontSize::Medium) + 16;

    // left: curve chart ; right: analytics panel
    int panelW = 320;
    int gap = 20;
    SDL_Rect chartR = { c.x, top, c.w - panelW - gap, c.y + c.h - top };
    SDL_Rect panelR = { c.x + c.w - panelW, top, panelW, c.y + c.h - top };

    std::vector<const YieldCurve*> curves = { &b.current, &b.month, &b.year };
    std::vector<widgets::CurveStyle> styles = {
        { theme::curveCurrent, "Current" },
        { theme::curveMonth,   "1M Ago"  },
        { theme::curveYear,    "1Y Ago"  },
    };
    widgets::yieldCurveChart(R, chartR, curves, styles);

    // ---- analytics panel ----
    R.card(panelR, false);
    int px = panelR.x + 20, py = panelR.y + 20;
    R.text("CURVE ANALYTICS", px, py, FontSize::Body, theme::textDim, Align::Left, true);
    py += R.lineHeight(FontSize::Body) + 12;

    double sc = 0, sm = 0, sy = 0;
    bool okc = curveSpread(b.current, sc);
    bool okm = curveSpread(b.month, sm);
    bool oky = curveSpread(b.year, sy);

    auto row = [&](const char* name, bool ok, double spreadPct, theme::Col col) {
        R.line(px, py + 10, px + 14, py + 10, col, 3);
        R.text(name, px + 24, py, FontSize::Body, theme::text, Align::Left);
        if (ok) {
            char v[32]; std::snprintf(v, sizeof(v), "%+.0f bps", spreadPct * 100.0);
            R.text(v, panelR.x + panelR.w - 20, py, FontSize::Body, theme::text, Align::Right, true);
        } else {
            R.text("—", panelR.x + panelR.w - 20, py, FontSize::Body, theme::textFaint, Align::Right);
        }
        py += R.lineHeight(FontSize::Body) + 8;
    };

    R.text("Long\xE2\x88\x92Short spread", px, py, FontSize::Small, theme::textFaint);
    py += R.lineHeight(FontSize::Small) + 8;
    row("Current", okc, sc, theme::curveCurrent);
    row("1M Ago",  okm, sm, theme::curveMonth);
    row("1Y Ago",  oky, sy, theme::curveYear);

    py += 12;
    R.hline(px, panelR.x + panelR.w - 20, py, theme::border);
    py += 16;

    // verdict vs 1M and 1Y
    auto verdict = [&](const char* label, bool okRef, double ref) {
        if (!okc || !okRef) return;
        double d = (sc - ref) * 100.0; // bps change in spread
        const char* word;
        theme::Col col;
        if (d > 2)      { word = "STEEPENING"; col = theme::up; }
        else if (d < -2){ word = "FLATTENING"; col = theme::down; }
        else            { word = "UNCHANGED";  col = theme::textDim; }
        R.text(label, px, py, FontSize::Small, theme::textFaint);
        py += R.lineHeight(FontSize::Small) + 2;
        char v[48]; std::snprintf(v, sizeof(v), "%s  %+.0f bps", word, d);
        R.pillTag(px, py, v, col, theme::Col{ col.r, col.g, col.b, 40 });
        py += R.lineHeight(FontSize::Small) + 18;
    };
    R.text("VS HISTORY", px, py, FontSize::Small, theme::textDim, Align::Left, true);
    py += R.lineHeight(FontSize::Small) + 10;
    verdict("Since 1 month ago", okm, sm);
    verdict("Since 1 year ago",  oky, sy);

    if (data.yieldsLoading())
        R.text("refreshing\xE2\x80\xA6", px, panelR.y + panelR.h - 30, FontSize::Small, theme::accent);
}
