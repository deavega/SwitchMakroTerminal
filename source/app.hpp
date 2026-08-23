// app.hpp - top-level application: header, tab bar, screen routing, main loop.
#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include "ui/renderer.hpp"
#include "data/data_service.hpp"
#include "data/config.hpp"
#include "ui/screens/screen.hpp"

class App {
public:
    App(Renderer& R, const Config& cfg);
    ~App();

    void run(); // blocks until user exits

private:
    void handleGlobalInput(uint64_t kDown);
    void renderHeader();
    void renderTabBar();
    void maybeAutoRefresh();
    void switchTab(int delta);

    Renderer& R_;
    Config cfg_;
    DataService data_;
    std::vector<std::unique_ptr<Screen>> screens_;
    int active_ = 0;
    bool running_ = true;
    int64_t lastAutoRefresh_ = 0;        // overview/stocks
    int64_t lastAutoRefreshYields_ = 0;  // yields (slower cadence)
};
