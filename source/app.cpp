#include "app.hpp"
#include "ui/widgets.hpp"
#include "ui/screens/overview_screen.hpp"
#include "ui/screens/stocks_screen.hpp"
#include "ui/screens/yields_screen.hpp"
#include <SDL2/SDL2_gfxPrimitives.h>
#include <switch.h>
#include <ctime>
#include <cstdio>

static PadState g_pad;

App::App(Renderer& R, const Config& cfg) : R_(R), cfg_(cfg), data_(cfg) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    screens_.push_back(std::make_unique<OverviewScreen>());
    screens_.push_back(std::make_unique<StocksScreen>());
    screens_.push_back(std::make_unique<YieldsScreen>());

    data_.refreshAll();
    screens_[active_]->onEnter(data_);
    lastAutoRefresh_ = lastAutoRefreshYields_ = (int64_t)std::time(nullptr);
}

App::~App() {}

void App::switchTab(int delta) {
    int n = (int)screens_.size();
    active_ = ((active_ + delta) % n + n) % n;
    screens_[active_]->onEnter(data_);
}

void App::handleGlobalInput(uint64_t k) {
    if (k & HidNpadButton_Plus) { running_ = false; return; }
    if ((k & HidNpadButton_R) || (k & HidNpadButton_ZR)) switchTab(+1);
    if ((k & HidNpadButton_L) || (k & HidNpadButton_ZL)) switchTab(-1);
}

void App::maybeAutoRefresh() {
    int64_t now = (int64_t)std::time(nullptr);
    // Overview + stocks on the short timer (Yahoo, no key).
    if (cfg_.refreshSeconds > 0 && now - lastAutoRefresh_ >= cfg_.refreshSeconds) {
        data_.refreshOverview();
        data_.refreshStocks();
        lastAutoRefresh_ = now;
    }
    // Yields on a much slower timer to spare the Trading Economics quota.
    if (cfg_.yieldsRefreshSeconds > 0 &&
        now - lastAutoRefreshYields_ >= cfg_.yieldsRefreshSeconds) {
        data_.refreshYields();
        lastAutoRefreshYields_ = now;
    }
}

static std::string clockString(int tzOffset) {
    time_t now = time(nullptr);
    now += (time_t)tzOffset * 3600;
    struct tm t;
    gmtime_r(&now, &t);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d WIB", t.tm_hour, t.tm_min, t.tm_sec);
    return std::string(buf);
}

static std::string agoString(int64_t updated, int tzOffset) {
    if (updated == 0) return "never";
    time_t now = time(nullptr);
    long d = (long)(now - updated);
    if (d < 5)  return "just now";
    if (d < 60) return std::to_string(d) + "s ago";
    if (d < 3600) return std::to_string(d / 60) + "m ago";
    (void)tzOffset;
    return std::to_string(d / 3600) + "h ago";
}

void App::renderHeader() {
    SDL_Rect bar = { 0, 0, theme::kW, 64 };
    R_.fillRect(bar, theme::bg1);
    R_.hline(0, theme::kW, 64, theme::border);

    // accent square + wordmark
    R_.roundedRect({ 32, 18, 28, 28 }, 6, theme::accent);
    R_.text("MAKRO", 72, 16, FontSize::Medium, theme::text, Align::Left, true);
    R_.text("TERMINAL", 72 + R_.measure("MAKRO ", FontSize::Medium, true) + 6, 16,
            FontSize::Medium, theme::accent, Align::Left, true);
    R_.text("Nintendo Switch \xE2\x80\xA2 live market data", 72, 44, FontSize::Small, theme::textFaint);

    // clock (right)
    R_.text(clockString(cfg_.tzOffsetHours), theme::kW - 32, 14, FontSize::Body,
            theme::text, Align::Right, true);

    // freshness + activity indicator
    bool busy = data_.overviewLoading() || data_.stocksLoading() || data_.yieldsLoading();
    theme::Col dot = busy ? theme::amber : theme::up;
    int64_t updated = data_.overviewUpdated();
    std::string s = std::string(busy ? "syncing" : "updated ") +
                    (busy ? "" : agoString(updated, cfg_.tzOffsetHours));
    filledCircleRGBA(R_.sdl(), theme::kW - 32 - R_.measure(s, FontSize::Small) - 12, 52,
                     4, dot.r, dot.g, dot.b, 255);
    R_.text(s, theme::kW - 32, 42, FontSize::Small, theme::textDim, Align::Right);
}

void App::renderTabBar() {
    int barY = 648, barH = 56;
    SDL_Rect bar = { 0, barY, theme::kW, barH };
    R_.fillRect(bar, theme::bg1);
    R_.hline(0, theme::kW, barY, theme::border);

    int n = (int)screens_.size();
    int tabW = 190;
    int totalW = tabW * n;
    int startX = (theme::kW - totalW) / 2;
    for (int i = 0; i < n; i++) {
        int x = startX + i * tabW;
        bool activeTab = (i == active_);
        theme::Col col = activeTab ? theme::text : theme::textFaint;
        R_.text(screens_[i]->title(), x + tabW / 2, barY + 12, FontSize::Body, col,
                Align::Center, activeTab);
        if (activeTab) {
            int tw = R_.measure(screens_[i]->title(), FontSize::Body, true);
            SDL_Rect ul = { x + tabW / 2 - tw / 2, barY + barH - 8, tw, 3 };
            R_.roundedRect(ul, 2, theme::accent);
        }
    }

    // control hints
    R_.text("L / R  switch view      A  refresh      + exit", theme::kW - 24, barY + 18,
            FontSize::Small, theme::textFaint, Align::Right);
}

void App::run() {
    SDL_Rect content = { 32, 84, theme::kW - 64, 552 };

    while (appletMainLoop() && running_) {
        padUpdate(&g_pad);
        uint64_t kDown = padGetButtonsDown(&g_pad);

        handleGlobalInput(kDown);
        if (!running_) break;
        screens_[active_]->handleInput(kDown, data_);
        maybeAutoRefresh();

        R_.clearBackground();
        renderHeader();
        screens_[active_]->render(R_, data_, content);
        renderTabBar();

        SDL_RenderPresent(R_.sdl());
    }
}
