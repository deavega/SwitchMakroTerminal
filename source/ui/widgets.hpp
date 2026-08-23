// widgets.hpp - composite dashboard widgets built on the Renderer.
#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include "ui/renderer.hpp"
#include "data/models.hpp"

namespace widgets {

// Format a number with thousands separators and given decimals.
std::string fmt(double v, int decimals);
std::string fmtSigned(double v, int decimals);
std::string fmtPct(double v);
// Compact magnitude format: 987K, 12.3M, 4.5B.
std::string fmtCompact(double v);

// A sparkline (mini line chart) inside a rect. Colored by up/down trend.
void sparkline(Renderer& R, SDL_Rect area, const std::vector<double>& data,
               theme::Col color, bool fill = true);

// A large metric card: label, big value, delta pill + sparkline.
void metricCard(Renderer& R, SDL_Rect r, const Quote& q,
                int valueDecimals, bool selected = false);

// A compact stock tile: symbol, price, %chg pill, tiny sparkline.
void stockTile(Renderer& R, SDL_Rect r, const Quote& q, bool selected);

// Multi-series yield-curve chart. Draws up to three curves + legend + axes.
struct CurveStyle { theme::Col color; std::string name; };
void yieldCurveChart(Renderer& R, SDL_Rect r,
                     const std::vector<const YieldCurve*>& curves,
                     const std::vector<CurveStyle>& styles);

// Compare several sources' curves on one numeric maturity (years) x-axis.
// Handles different tenor sets (e.g. INDOGB vs UST) by plotting each point at
// its actual maturity on a sqrt-scaled axis.
void yieldCompareChart(Renderer& R, SDL_Rect r,
                       const std::vector<const YieldCurve*>& curves,
                       const std::vector<CurveStyle>& styles);

// Yield at (or nearest to) a target maturity; false if none within `tol` years.
bool yieldAtTenor(const YieldCurve& c, double targetYears, double tol, double& out);

// A single time-series line chart with axis labels.
void lineChart(Renderer& R, SDL_Rect r, const std::vector<double>& data,
               theme::Col color, const std::string& title);

// Full-screen stock detail: price, 52-week & day range bars, month chart,
// and a stats grid. If `summary` is non-null and valid, also shows valuation
// metrics (P/E, market cap, EPS, dividend yield, etc.) and sector/industry.
void fundamentalsCard(Renderer& R, SDL_Rect r, const Quote& q,
                      const StockSummary* summary = nullptr,
                      bool summaryLoading = false);

} // namespace widgets
