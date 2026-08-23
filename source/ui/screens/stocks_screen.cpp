#include "ui/screens/stocks_screen.hpp"
#include "ui/widgets.hpp"
#include <switch.h>
#include <algorithm>

void StocksScreen::onEnter(DataService& data) {
    showDetail_ = false; // always land on the grid
    if (data.stocks().empty() && !data.stocksLoading())
        data.refreshStocks();
}

void StocksScreen::handleInput(uint64_t k, DataService& data) {
    // Mode toggles
    if (showDetail_) {
        if (k & HidNpadButton_B) showDetail_ = false;
    } else {
        if (k & HidNpadButton_A) showDetail_ = true; // open details for selection
    }
    if (k & HidNpadButton_X) data.refreshStocks();    // refresh in either mode

    if (count_ <= 0) return;

    int row  = selected_ / cols_;
    int rows = (count_ + cols_ - 1) / cols_;

    // Left/Right flips the selection (and, in detail mode, the shown stock).
    if ((k & HidNpadButton_Right) || (k & HidNpadButton_StickLRight))
        selected_ = std::min(selected_ + 1, count_ - 1);
    if ((k & HidNpadButton_Left) || (k & HidNpadButton_StickLLeft))
        selected_ = std::max(selected_ - 1, 0);
    // Up/Down only meaningful on the grid.
    if (!showDetail_) {
        if ((k & HidNpadButton_Down) || (k & HidNpadButton_StickLDown)) {
            if (row < rows - 1) selected_ = std::min(selected_ + cols_, count_ - 1);
        }
        if ((k & HidNpadButton_Up) || (k & HidNpadButton_StickLUp)) {
            if (row > 0) selected_ = selected_ - cols_;
        }
    }
}

void StocksScreen::render(Renderer& R, DataService& data, SDL_Rect c) {
    auto st = data.stocks();
    count_ = (int)st.size();
    if (count_ > 0) {
        if (selected_ >= count_) selected_ = count_ - 1;
        if (selected_ < 0) selected_ = 0;
    }

    if (st.empty()) {
        const char* msg = data.stocksLoading() ? "Loading equities\xE2\x80\xA6" : "No data";
        R.text(msg, c.x, c.y + 4, FontSize::Body, theme::textFaint);
        return;
    }

    // ---- detail mode ----
    if (showDetail_ && selected_ < count_) {
        const std::string& symbol = st[selected_].symbol;
        data.requestSummary(symbol);                 // lazy background fetch
        StockSummary sum;
        bool has = data.getSummary(symbol, sum);
        bool loading = data.summaryLoading(symbol);
        widgets::fundamentalsCard(R, c, st[selected_], has ? &sum : nullptr, loading);
        R.text("B  back       Left / Right  prev / next       X  refresh",
               c.x + c.w - 8, c.y + c.h - 30, FontSize::Small, theme::textFaint,
               Align::Right);
        return;
    }

    // ---- grid mode ----
    int gap = 16;
    int rows = (count_ + cols_ - 1) / cols_;
    if (rows < 1) rows = 1;
    int tileW = (c.w - gap * (cols_ - 1)) / cols_;
    int tileH = (c.h - gap * (rows - 1)) / rows;
    tileH = std::min(tileH, 150);
    tileH = std::max(tileH, 120);

    for (int i = 0; i < count_; i++) {
        int row = i / cols_, col = i % cols_;
        SDL_Rect r = { c.x + col * (tileW + gap), c.y + row * (tileH + gap), tileW, tileH };
        widgets::stockTile(R, r, st[i], i == selected_);
    }

    R.text("A  details       X  refresh", c.x + c.w - 8, c.y + c.h - 26,
           FontSize::Small, theme::textFaint, Align::Right);
}
