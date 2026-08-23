#include "ui/widgets.hpp"
#include <SDL2/SDL2_gfxPrimitives.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace widgets {

std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    // insert thousands separators into the integer part
    std::string s(buf);
    bool neg = !s.empty() && s[0] == '-';
    size_t start = neg ? 1 : 0;
    size_t dot = s.find('.');
    size_t intEnd = (dot == std::string::npos) ? s.size() : dot;
    std::string intPart = s.substr(start, intEnd - start);
    std::string rest = s.substr(intEnd);
    std::string grouped;
    int cnt = 0;
    for (int i = (int)intPart.size() - 1; i >= 0; i--) {
        grouped.push_back(intPart[i]);
        if (++cnt % 3 == 0 && i != 0) grouped.push_back(',');
    }
    std::reverse(grouped.begin(), grouped.end());
    return (neg ? "-" : "") + grouped + rest;
}

std::string fmtSigned(double v, int decimals) {
    std::string body = fmt(std::fabs(v), decimals);
    return (v >= 0 ? "+" : "-") + body;
}

std::string fmtPct(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.2f%%", v);
    return std::string(buf);
}

std::string fmtCompact(double v) {
    double a = std::fabs(v);
    char buf[32];
    if (a >= 1e12)      std::snprintf(buf, sizeof(buf), "%.2fT", v / 1e12);
    else if (a >= 1e9)  std::snprintf(buf, sizeof(buf), "%.2fB", v / 1e9);
    else if (a >= 1e6)  std::snprintf(buf, sizeof(buf), "%.2fM", v / 1e6);
    else if (a >= 1e3)  std::snprintf(buf, sizeof(buf), "%.1fK", v / 1e3);
    else                std::snprintf(buf, sizeof(buf), "%.0f", v);
    return std::string(buf);
}

static void minmax(const std::vector<double>& d, double& lo, double& hi) {
    lo = 1e300; hi = -1e300;
    for (double v : d) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (lo > hi) { lo = 0; hi = 1; }
    if (hi - lo < 1e-9) { hi = lo + 1; }
}

void sparkline(Renderer& R, SDL_Rect area, const std::vector<double>& data,
               theme::Col color, bool fill) {
    if (data.size() < 2) return;
    double lo, hi; minmax(data, lo, hi);
    int n = (int)data.size();
    auto X = [&](int i) { return area.x + (int)std::round((double)i / (n - 1) * area.w); };
    auto Y = [&](double v) {
        double t = (v - lo) / (hi - lo);
        return area.y + area.h - (int)std::round(t * area.h);
    };

    if (fill) {
        std::vector<Sint16> vx, vy;
        vx.push_back(X(0)); vy.push_back(area.y + area.h);
        for (int i = 0; i < n; i++) { vx.push_back(X(i)); vy.push_back(Y(data[i])); }
        vx.push_back(X(n - 1)); vy.push_back(area.y + area.h);
        filledPolygonRGBA(R.sdl(), vx.data(), vy.data(), (int)vx.size(),
                          color.r, color.g, color.b, 40);
    }
    for (int i = 1; i < n; i++)
        R.line(X(i - 1), Y(data[i - 1]), X(i), Y(data[i]), color, 2);
}

void metricCard(Renderer& R, SDL_Rect r, const Quote& q, int valueDecimals, bool selected) {
    R.card(r, selected);
    int pad = 22;
    int x = r.x + pad, y = r.y + pad;

    R.text(q.label, x, y, FontSize::Body, theme::textDim, Align::Left, true);
    if (!q.currency.empty())
        R.text(q.currency, r.x + r.w - pad, y, FontSize::Small, theme::textFaint, Align::Right);

    y += R.lineHeight(FontSize::Body) + 10;

    if (!q.valid) {
        R.text("—", x, y, FontSize::Huge, theme::textFaint, Align::Left, true);
        R.text("no data", x, y + R.lineHeight(FontSize::Huge), FontSize::Small,
               theme::textFaint);
        return;
    }

    R.text(fmt(q.price, valueDecimals), x, y, FontSize::Huge, theme::text, Align::Left, true);

    // delta pill
    bool upTrend = q.change >= 0;
    theme::Col dc = upTrend ? theme::up : theme::down;
    std::string arrow = upTrend ? "\xE2\x96\xB2" : "\xE2\x96\xBC"; // ▲ / ▼
    std::string delta = arrow + " " + fmtSigned(q.change, valueDecimals) +
                        "  (" + fmtPct(q.changePct) + ")";
    int py = y + R.lineHeight(FontSize::Huge) + 8;
    theme::Col pillBg = { dc.r, dc.g, dc.b, 40 };
    R.pillTag(x, py, delta, dc, pillBg);

    // sparkline lower-right
    SDL_Rect sp = { r.x + r.w / 2, r.y + r.h - 70, r.w / 2 - pad, 48 };
    sparkline(R, sp, q.spark, dc, true);
}

void stockTile(Renderer& R, SDL_Rect r, const Quote& q, bool selected) {
    R.card(r, selected);
    int pad = 16;
    int x = r.x + pad, y = r.y + pad;

    // strip ".JK" for display
    std::string sym = q.symbol;
    size_t dot = sym.find('.');
    if (dot != std::string::npos) sym = sym.substr(0, dot);

    R.text(sym, x, y, FontSize::Medium, theme::text, Align::Left, true);
    if (!q.label.empty() && q.label != sym)
        R.text(q.label, x, y + R.lineHeight(FontSize::Medium) - 2, FontSize::Small,
               theme::textFaint);

    if (!q.valid) {
        R.text("no data", x, r.y + r.h - 34, FontSize::Small, theme::textFaint);
        return;
    }

    bool upTrend = q.change >= 0;
    theme::Col dc = upTrend ? theme::up : theme::down;
    R.text(fmt(q.price, 0), x, r.y + r.h - 62, FontSize::Body, theme::text, Align::Left, true);
    R.text(fmtPct(q.changePct), r.x + r.w - pad, r.y + r.h - 60, FontSize::Small, dc,
           Align::Right, true);

    SDL_Rect sp = { x, r.y + r.h - 30, r.w - pad * 2, 18 };
    sparkline(R, sp, q.spark, dc, false);
}

void lineChart(Renderer& R, SDL_Rect r, const std::vector<double>& data,
               theme::Col color, const std::string& title) {
    R.card(r, false);
    int pad = 20;
    SDL_Rect plot = { r.x + pad + 46, r.y + pad + 28, r.w - pad * 2 - 46, r.h - pad * 2 - 40 };
    R.text(title, r.x + pad, r.y + pad, FontSize::Body, theme::textDim, Align::Left, true);
    if (data.size() < 2) return;

    double lo, hi; minmax(data, lo, hi);
    for (int i = 0; i <= 4; i++) {
        int gy = plot.y + plot.h - i * plot.h / 4;
        R.hline(plot.x, plot.x + plot.w, gy, theme::grid);
        double val = lo + (hi - lo) * i / 4.0;
        R.text(fmt(val, 0), plot.x - 8, gy - 9, FontSize::Small, theme::textFaint, Align::Right);
    }
    sparkline(R, plot, data, color, true);
}

// -------- yield curve chart --------
void yieldCurveChart(Renderer& R, SDL_Rect r,
                     const std::vector<const YieldCurve*>& curves,
                     const std::vector<CurveStyle>& styles) {
    R.card(r, false);
    int pad = 24;
    SDL_Rect plot = { r.x + pad + 40, r.y + pad + 40, r.w - pad * 2 - 50, r.h - pad * 2 - 78 };

    // bounds across all curves
    double yLo = 1e300, yHi = -1e300, tHi = 0;
    int tenorCount = 0;
    for (auto* c : curves) {
        if (!c || !c->valid) continue;
        tenorCount = std::max(tenorCount, (int)c->points.size());
        for (auto& p : c->points) {
            if (!p.valid) continue;
            yLo = std::min(yLo, p.yield);
            yHi = std::max(yHi, p.yield);
            tHi = std::max(tHi, p.tenorYears);
        }
    }
    if (yLo > yHi) { R.text("no yield data", plot.x, plot.y, FontSize::Body, theme::textFaint); return; }
    // pad the y-range a little
    double range = yHi - yLo; if (range < 0.2) range = 0.2;
    yLo -= range * 0.15; yHi += range * 0.15;

    auto Y = [&](double v) {
        double t = (v - yLo) / (yHi - yLo);
        return plot.y + plot.h - (int)std::round(t * plot.h);
    };

    // horizontal gridlines + y labels (percent)
    for (int i = 0; i <= 4; i++) {
        double val = yLo + (yHi - yLo) * i / 4.0;
        int gy = Y(val);
        R.hline(plot.x, plot.x + plot.w, gy, theme::grid);
        char lbl[16]; std::snprintf(lbl, sizeof(lbl), "%.2f%%", val);
        R.text(lbl, plot.x - 8, gy - 9, FontSize::Small, theme::textFaint, Align::Right);
    }

    // x positions: evenly spaced by tenor index (categorical), labels from first valid curve
    const YieldCurve* base = nullptr;
    for (auto* c : curves) if (c && c->valid) { base = c; break; }
    if (!base) return;
    int n = (int)base->points.size();
    auto X = [&](int i) {
        if (n <= 1) return plot.x + plot.w / 2;
        return plot.x + i * plot.w / (n - 1);
    };
    for (int i = 0; i < n; i++) {
        R.vline(X(i), plot.y, plot.y + plot.h, theme::grid);
        R.text(base->points[i].label, X(i), plot.y + plot.h + 8, FontSize::Small,
               theme::textFaint, Align::Center);
    }

    // draw each curve (bridge across any missing/invalid interior tenors)
    for (size_t s = 0; s < curves.size(); s++) {
        const YieldCurve* c = curves[s];
        if (!c || !c->valid) continue;
        theme::Col col = styles[s].color;
        int m = (int)c->points.size();
        int prev = -1;
        for (int i = 0; i < m; i++) {
            if (!c->points[i].valid) continue;
            if (prev >= 0)
                R.line(X(prev), Y(c->points[prev].yield), X(i), Y(c->points[i].yield), col, 3);
            prev = i;
        }
        for (int i = 0; i < m; i++) {
            if (!c->points[i].valid) continue;
            filledCircleRGBA(R.sdl(), X(i), Y(c->points[i].yield), 4, col.r, col.g, col.b, 255);
            filledCircleRGBA(R.sdl(), X(i), Y(c->points[i].yield), 2, 13, 17, 23, 255);
        }
    }

    // legend (top-right)
    int lx = plot.x + plot.w - 4;
    int ly = r.y + pad;
    for (size_t s = 0; s < styles.size(); s++) {
        if (!curves[s] || !curves[s]->valid) continue;
        std::string nm = styles[s].name;
        int w = R.measure(nm, FontSize::Small, true);
        int rowW = w + 26;
        R.text(nm, lx, ly, FontSize::Small, theme::text, Align::Right, true);
        theme::Col col = styles[s].color;
        int sw = lx - w - 20;
        R.line(sw, ly + 9, sw + 14, ly + 9, col, 3);
        ly += R.lineHeight(FontSize::Small) + 6;
        (void)rowW;
    }
}

bool yieldAtTenor(const YieldCurve& c, double targetYears, double tol, double& out) {
    const YieldPoint* best = nullptr;
    double bd = 1e300;
    for (const auto& p : c.points) {
        if (!p.valid) continue;
        double d = std::fabs(p.tenorYears - targetYears);
        if (d < bd) { bd = d; best = &p; }
    }
    if (!best || bd > tol) return false;
    out = best->yield;
    return true;
}

void yieldCompareChart(Renderer& R, SDL_Rect r,
                       const std::vector<const YieldCurve*>& curves,
                       const std::vector<CurveStyle>& styles) {
    R.card(r, false);
    int pad = 24;
    SDL_Rect plot = { r.x + pad + 40, r.y + pad + 40, r.w - pad * 2 - 50, r.h - pad * 2 - 78 };

    double yLo = 1e300, yHi = -1e300, tLo = 1e300, tHi = -1e300;
    for (auto* c : curves) {
        if (!c) continue;
        for (auto& p : c->points) {
            if (!p.valid) continue;
            yLo = std::min(yLo, p.yield); yHi = std::max(yHi, p.yield);
            tLo = std::min(tLo, p.tenorYears); tHi = std::max(tHi, p.tenorYears);
        }
    }
    if (yLo > yHi) { R.text("no yield data", plot.x, plot.y, FontSize::Body, theme::textFaint); return; }
    double range = yHi - yLo; if (range < 0.2) range = 0.2;
    yLo -= range * 0.15; yHi += range * 0.15;
    if (tLo <= 0.0) tLo = 0.05;
    double sLo = std::sqrt(tLo), sHi = std::sqrt(tHi);
    if (sHi - sLo < 1e-6) sHi = sLo + 1.0;

    auto X = [&](double years) {
        if (years < tLo) years = tLo;
        double t = (std::sqrt(years) - sLo) / (sHi - sLo);
        return plot.x + (int)std::round(t * plot.w);
    };
    auto Y = [&](double v) {
        double t = (v - yLo) / (yHi - yLo);
        return plot.y + plot.h - (int)std::round(t * plot.h);
    };

    // y gridlines + percent labels
    for (int i = 0; i <= 4; i++) {
        double val = yLo + (yHi - yLo) * i / 4.0;
        int gy = Y(val);
        R.hline(plot.x, plot.x + plot.w, gy, theme::grid);
        char lbl[16]; std::snprintf(lbl, sizeof(lbl), "%.2f%%", val);
        R.text(lbl, plot.x - 8, gy - 9, FontSize::Small, theme::textFaint, Align::Right);
    }

    // x ticks: union of maturities across curves, deduped, labels spaced out
    std::vector<std::pair<double, std::string>> ticks;
    for (auto* c : curves) {
        if (!c) continue;
        for (auto& p : c->points) {
            if (!p.valid) continue;
            bool dup = false;
            for (auto& tk : ticks) if (std::fabs(tk.first - p.tenorYears) < 1e-6) { dup = true; break; }
            if (!dup) ticks.push_back({ p.tenorYears, p.label });
        }
    }
    std::sort(ticks.begin(), ticks.end(),
              [](const std::pair<double,std::string>& a, const std::pair<double,std::string>& b) {
                  return a.first < b.first;
              });
    int lastLabelX = -1000;
    for (auto& tk : ticks) {
        int gx = X(tk.first);
        R.vline(gx, plot.y, plot.y + plot.h, theme::grid);
        if (gx - lastLabelX >= 42) {
            R.text(tk.second, gx, plot.y + plot.h + 8, FontSize::Small, theme::textFaint, Align::Center);
            lastLabelX = gx;
        }
    }

    // each curve: connect its valid points in maturity order
    for (size_t s = 0; s < curves.size(); s++) {
        const YieldCurve* c = curves[s];
        if (!c) continue;
        std::vector<const YieldPoint*> pts;
        for (auto& p : c->points) if (p.valid) pts.push_back(&p);
        std::sort(pts.begin(), pts.end(),
                  [](const YieldPoint* a, const YieldPoint* b) { return a->tenorYears < b->tenorYears; });
        theme::Col col = styles[s].color;
        for (size_t i = 1; i < pts.size(); i++)
            R.line(X(pts[i-1]->tenorYears), Y(pts[i-1]->yield),
                   X(pts[i]->tenorYears),   Y(pts[i]->yield), col, 3);
        for (auto* p : pts) {
            filledCircleRGBA(R.sdl(), X(p->tenorYears), Y(p->yield), 4, col.r, col.g, col.b, 255);
            filledCircleRGBA(R.sdl(), X(p->tenorYears), Y(p->yield), 2, 13, 17, 23, 255);
        }
    }

    // legend (top-right)
    int lx = plot.x + plot.w - 4;
    int ly = r.y + pad;
    for (size_t s = 0; s < styles.size(); s++) {
        if (!curves[s]) continue;
        std::string nm = styles[s].name;
        int w = R.measure(nm, FontSize::Small, true);
        R.text(nm, lx, ly, FontSize::Small, theme::text, Align::Right, true);
        theme::Col col = styles[s].color;
        int sw = lx - w - 20;
        R.line(sw, ly + 9, sw + 14, ly + 9, col, 3);
        ly += R.lineHeight(FontSize::Small) + 6;
    }
}

// -------- stock fundamentals detail --------

// A horizontal range bar: where `cur` sits between `lo` and `hi`.
static void rangeBar(Renderer& R, int x, int y, int w, const std::string& title,
                     double lo, double hi, double cur, int decimals,
                     theme::Col accent) {
    R.text(title, x, y, FontSize::Small, theme::textFaint, Align::Left, true);
    int by = y + R.lineHeight(FontSize::Small) + 8;
    int h = 8;
    // track
    roundedBoxRGBA(R.sdl(), x, by, x + w, by + h, h / 2,
                   theme::grid.r, theme::grid.g, theme::grid.b, 255);
    double t = (hi > lo) ? (cur - lo) / (hi - lo) : 0.5;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int cx = x + (int)(t * w);
    // filled portion
    roundedBoxRGBA(R.sdl(), x, by, cx, by + h, h / 2, accent.r, accent.g, accent.b, 150);
    // knob
    filledCircleRGBA(R.sdl(), cx, by + h / 2, 7, accent.r, accent.g, accent.b, 255);
    filledCircleRGBA(R.sdl(), cx, by + h / 2, 3, 13, 17, 23, 255);
    // lo / hi labels beneath
    int ly = by + h + 6;
    R.text(fmt(lo, decimals), x, ly, FontSize::Small, theme::textDim, Align::Left);
    R.text(fmt(hi, decimals), x + w, ly, FontSize::Small, theme::textDim, Align::Right);
}

// One label/value stat cell.
static void statCell(Renderer& R, int x, int y, const std::string& label,
                     const std::string& value, theme::Col valueCol) {
    R.text(label, x, y, FontSize::Small, theme::textFaint, Align::Left);
    R.text(value, x, y + R.lineHeight(FontSize::Small) + 1, FontSize::Medium,
           valueCol, Align::Left, true);
}

void fundamentalsCard(Renderer& R, SDL_Rect r, const Quote& q,
                      const StockSummary* summary, bool summaryLoading) {
    R.card(r, false);
    int pad = 28;
    int x0 = r.x + pad, y0 = r.y + pad;
    int innerW = r.w - pad * 2;

    // strip ".JK" for the big symbol
    std::string sym = q.symbol;
    size_t dot = sym.find('.');
    if (dot != std::string::npos) sym = sym.substr(0, dot);

    const Fundamentals& f = q.fund;
    const StockSummary* s = (summary && summary->valid) ? summary : nullptr;

    // --- header ---
    R.text(sym, x0, y0, FontSize::Huge, theme::text, Align::Left, true);
    std::string name = !f.longName.empty() ? f.longName : q.label;
    if (!name.empty() && name != sym)
        R.text(name, x0, y0 + R.lineHeight(FontSize::Huge) - 6, FontSize::Body,
               theme::textDim, Align::Left);
    // sector • industry subtitle (from quoteSummary), if available
    if (s && (!s->sector.empty() || !s->industry.empty())) {
        std::string si = s->sector;
        if (!s->industry.empty()) si += (si.empty() ? "" : "  \xE2\x80\xA2  ") + s->industry;
        R.text(si, x0, y0 + R.lineHeight(FontSize::Huge) + R.lineHeight(FontSize::Body) - 8,
               FontSize::Small, theme::textFaint, Align::Left);
    }
    // exchange + currency (top-right)
    std::string ex = f.exchange;
    if (!q.currency.empty()) ex += (ex.empty() ? "" : "  \xE2\x80\xA2  ") + q.currency;
    if (!ex.empty())
        R.text(ex, r.x + r.w - pad, y0, FontSize::Small, theme::textFaint, Align::Right);
    if (summaryLoading && !s)
        R.text("memuat fundamental\xE2\x80\xA6", r.x + r.w - pad,
               y0 + R.lineHeight(FontSize::Small) + 4, FontSize::Small, theme::textFaint, Align::Right);

    if (!q.valid) {
        R.text("no data for this symbol", x0, y0 + 120, FontSize::Body, theme::textFaint);
        return;
    }

    // --- columns ---
    int gap = 40;
    int leftW = (int)(innerW * 0.54);
    int rightX = x0 + leftW + gap;
    int rightW = innerW - leftW - gap;

    // Left: big price + change pill
    int py = y0 + R.lineHeight(FontSize::Huge) + 40;
    R.text(fmt(q.price, 2), x0, py, FontSize::Huge, theme::text, Align::Left, true);
    bool up = q.change >= 0;
    theme::Col dc = up ? theme::up : theme::down;
    std::string arrow = up ? "\xE2\x96\xB2" : "\xE2\x96\xBC";
    std::string delta = arrow + " " + fmtSigned(q.change, 2) + "  (" + fmtPct(q.changePct) + ")";
    int pillY = py + R.lineHeight(FontSize::Huge) + 6;
    R.pillTag(x0, pillY, delta, dc, { dc.r, dc.g, dc.b, 40 });

    // Left: range bars
    int barsY = pillY + 52;
    if (f.has52Range) {
        rangeBar(R, x0, barsY, leftW, "52-WEEK RANGE", f.week52Low, f.week52High,
                 q.price, 2, theme::accent);
        barsY += 66;
    }
    if (f.hasDayRange) {
        rangeBar(R, x0, barsY, leftW, "DAY RANGE", f.dayLow, f.dayHigh,
                 q.price, 2, theme::teal);
        barsY += 66;
    }

    // Left: month chart fills the rest of the left column
    int chartY = barsY + 6;
    int chartBottom = r.y + r.h - pad;
    if (chartBottom - chartY > 80) {
        SDL_Rect chart = { x0, chartY, leftW, chartBottom - chartY };
        lineChart(R, chart, q.spark, up ? theme::up : theme::down, "1-MONTH");
    }

    // --- right: stats grid (2 cols) ---
    const char* dash = "\xE2\x80\x94";
    int colGap = 30;
    int cellW = (rightW - colGap) / 2;
    int rowH = 58;
    int gx0 = rightX, gx1 = rightX + cellW + colGap;
    int gy = y0 + 6;

    auto val = [&](bool has, const std::string& s) { return has ? s : std::string(dash); };

    // row 1: Open | Prev Close
    statCell(R, gx0, gy, "OPEN",       val(f.hasOpen, fmt(f.open, 2)), theme::text);
    statCell(R, gx1, gy, "PREV CLOSE", fmt(q.prevClose, 2), theme::text);
    gy += rowH;
    // row 2: Day High | Day Low
    statCell(R, gx0, gy, "DAY HIGH", val(f.hasDayRange, fmt(f.dayHigh, 2)), theme::text);
    statCell(R, gx1, gy, "DAY LOW",  val(f.hasDayRange, fmt(f.dayLow, 2)),  theme::text);
    gy += rowH;
    // row 3: 52W High | 52W Low
    statCell(R, gx0, gy, "52W HIGH", val(f.has52Range, fmt(f.week52High, 2)), theme::text);
    statCell(R, gx1, gy, "52W LOW",  val(f.has52Range, fmt(f.week52Low, 2)),  theme::text);
    gy += rowH;
    // row 4: Volume | Avg Vol
    statCell(R, gx0, gy, "VOLUME",       val(f.hasVolume, fmtCompact((double)f.volume)), theme::text);
    statCell(R, gx1, gy, "AVG VOL (1M)", val(f.hasAvgVolume, fmtCompact(f.avgVolume)),   theme::text);
    gy += rowH;
    // row 5: 1-Mo change | Volatility
    theme::Col mc = (f.monthChangePct >= 0) ? theme::up : theme::down;
    statCell(R, gx0, gy, "1-MONTH", val(q.spark.size() >= 2, fmtPct(f.monthChangePct)), mc);
    statCell(R, gx1, gy, "VOLATILITY (ANN)",
             val(f.hasVol, fmt(f.volatilityAnnPct, 1) + "%"), theme::text);
    gy += rowH;

    // --- valuation metrics from quoteSummary (if available) ---
    if (s) {
        int vy = gy + 4;
        int cardBottom = r.y + r.h - pad;
        R.text("VALUATION", gx0, vy, FontSize::Small, theme::accent, Align::Left, true);
        vy += R.lineHeight(FontSize::Small) + 8;

        // flow label/value chips across the two right columns
        struct Metric { const char* label; std::string value; bool show; };
        std::vector<Metric> ms = {
            { "P/E",        s->hasPE     ? fmt(s->peRatio, 1)          : "", s->hasPE },
            { "FWD P/E",    s->hasFwdPE  ? fmt(s->forwardPE, 1)        : "", s->hasFwdPE },
            { "EPS",        s->hasEps    ? fmt(s->eps, 2)              : "", s->hasEps },
            { "DIV YIELD",  s->hasDivYield ? fmt(s->dividendYieldPct,2)+"%" : "", s->hasDivYield },
            { "P/B",        s->hasPB     ? fmt(s->priceToBook, 2)      : "", s->hasPB },
            { "BETA",       s->hasBeta   ? fmt(s->beta, 2)            : "", s->hasBeta },
            { "MKT CAP",    s->hasMktCap ? fmtCompact(s->marketCap)    : "", s->hasMktCap },
            { "TGT PRICE",  s->hasTarget ? fmt(s->targetMean, 0)       : "", s->hasTarget },
            { "ROE",        s->hasRoe    ? fmt(s->returnOnEquityPct,1)+"%" : "", s->hasRoe },
            { "MARGIN",     s->hasMargin ? fmt(s->profitMarginsPct,1)+"%" : "", s->hasMargin },
        };
        int col = 0;
        for (auto& m : ms) {
            if (!m.show) continue;
            int cx = (col % 2 == 0) ? gx0 : gx1;
            if (vy + 40 > cardBottom) break;
            statCell(R, cx, vy, m.label, m.value, theme::text);
            col++;
            if (col % 2 == 0) vy += 52;
        }
    }
}

} // namespace widgets
