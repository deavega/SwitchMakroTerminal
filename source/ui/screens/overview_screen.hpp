#pragma once
#include "ui/screens/screen.hpp"

class OverviewScreen : public Screen {
public:
    const char* title() const override { return "OVERVIEW"; }
    void onEnter(DataService& data) override;
    void handleInput(uint64_t buttonsDown, DataService& data) override;
    void render(Renderer& R, DataService& data, SDL_Rect content) override;
};
