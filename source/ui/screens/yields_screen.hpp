#pragma once
#include "ui/screens/screen.hpp"

class YieldsScreen : public Screen {
public:
    const char* title() const override { return "BONDS"; }
    void onEnter(DataService& data) override;
    void handleInput(uint64_t buttonsDown, DataService& data) override;
    void render(Renderer& R, DataService& data, SDL_Rect content) override;

private:
    int  sourceIndex_ = 0;
    bool compare_ = false;   // overlay all sources on one maturity axis
};
