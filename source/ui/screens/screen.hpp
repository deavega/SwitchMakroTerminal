// screen.hpp - abstract base for a dashboard view.
#pragma once
#include <cstdint>
#include <string>
#include "ui/renderer.hpp"
#include "data/data_service.hpp"

class Screen {
public:
    virtual ~Screen() = default;

    virtual const char* title() const = 0;

    // Called when this screen becomes active (trigger a refresh if stale).
    virtual void onEnter(DataService& data) = 0;

    // buttonsDown are libnx HidNpadButton bits pressed this frame.
    virtual void handleInput(uint64_t buttonsDown, DataService& data) = 0;

    // content rect is the area between header and tab bar.
    virtual void render(Renderer& R, DataService& data, SDL_Rect content) = 0;
};
