// theme.hpp - colors, sizing and layout constants for a dark, professional look.
#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

namespace theme {

// Logical render resolution (SDL scales this to 720p handheld / 1080p docked).
constexpr int kW = 1280;
constexpr int kH = 720;

struct Col { uint8_t r, g, b, a; };

// GitHub-dark-inspired palette: calm, high-contrast, "terminal" feel.
constexpr Col bg0       {  9, 12, 18, 255 };  // window background (top)
constexpr Col bg1       { 13, 17, 23, 255 };  // window background (bottom)
constexpr Col panel     { 22, 27, 34, 255 };  // card fill
constexpr Col panelHi   { 28, 34, 43, 255 };  // hovered/selected card fill
constexpr Col border    { 33, 38, 45, 255 };  // card border
constexpr Col borderHi  { 56, 68, 82, 255 };  // selected border
constexpr Col grid      { 30, 36, 44, 255 };  // chart gridlines

constexpr Col text      { 230, 237, 243, 255 };
constexpr Col textDim   { 139, 148, 158, 255 };
constexpr Col textFaint {  90, 99, 110, 255 };

constexpr Col accent    {  88, 166, 255, 255 }; // blue (neutral highlight)
constexpr Col up        {  63, 185, 80,  255 }; // green
constexpr Col down      { 248, 81,  73,  255 }; // red
constexpr Col amber     { 219, 154, 4,   255 }; // sample/warning
constexpr Col violet    { 163, 113, 247, 255 }; // series accent
constexpr Col teal      {  57, 197, 187, 255 }; // series accent

inline SDL_Color sdl(const Col& c) { return SDL_Color{ c.r, c.g, c.b, c.a }; }

// Series colors used for the three yield curves.
constexpr Col curveCurrent { 88, 166, 255, 255 };
constexpr Col curveMonth   { 57, 197, 187, 255 };
constexpr Col curveYear    { 163, 113, 247, 255 };

} // namespace theme
