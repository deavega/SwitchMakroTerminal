#pragma once
#include "ui/screens/screen.hpp"

class StocksScreen : public Screen {
public:
    const char* title() const override { return "STOCKS"; }
    void onEnter(DataService& data) override;
    void handleInput(uint64_t buttonsDown, DataService& data) override;
    void render(Renderer& R, DataService& data, SDL_Rect content) override;

private:
    int selected_ = 0;
    int cols_ = 3;
    int count_ = 0;
    bool showDetail_ = false;
};
