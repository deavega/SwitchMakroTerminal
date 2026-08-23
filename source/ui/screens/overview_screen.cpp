#include "ui/screens/overview_screen.hpp"
#include "ui/widgets.hpp"
#include <switch.h>
#include <ctime>

void OverviewScreen::onEnter(DataService& data) {
    if (data.overview().empty() && !data.overviewLoading())
        data.refreshOverview();
    if (data.stocks().empty() && !data.stocksLoading())
        data.refreshStocks();
}

void OverviewScreen::handleInput(uint64_t k, DataService& data) {
    if (k & HidNpadButton_A) { data.refreshOverview(); data.refreshStocks(); }
}

void OverviewScreen::render(Renderer& R, DataService& data, SDL_Rect c) {
    auto ov = data.overview();
    auto st = data.stocks();

    // two big metric cards side by side
    int gap = 20;
    int cardW = (c.w - gap) / 2;
    int cardH = 230;
    for (size_t i = 0; i < ov.size() && i < 2; i++) {
        SDL_Rect r = { c.x + (int)i * (cardW + gap), c.y, cardW, cardH };
        int dec = (ov[i].symbol == "IDR=X") ? 2 : 2;
        widgets::metricCard(R, r, ov[i], dec, false);
    }

    if (data.overviewLoading() && ov.empty())
        R.text("Loading market data\xE2\x80\xA6", c.x, c.y + 4, FontSize::Body, theme::textFaint);

    // section label
    int y = c.y + cardH + 22;
    R.text("INDONESIAN EQUITIES", c.x, y, FontSize::Body, theme::textDim, Align::Left, true);
    R.text("press A to refresh", c.x + c.w, y + 2, FontSize::Small, theme::textFaint, Align::Right);
    y += R.lineHeight(FontSize::Body) + 12;

    // row of stock tiles (up to 4)
    int cols = 4;
    int tileGap = 16;
    int tileW = (c.w - tileGap * (cols - 1)) / cols;
    int tileH = c.y + c.h - y;
    if (tileH > 150) tileH = 150;
    for (int i = 0; i < cols && i < (int)st.size(); i++) {
        SDL_Rect r = { c.x + i * (tileW + tileGap), y, tileW, tileH };
        widgets::stockTile(R, r, st[i], false);
    }
}
