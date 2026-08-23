// renderer.hpp - thin drawing layer over SDL2 + SDL2_gfx + SDL2_ttf.
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <map>
#include <vector>
#include "ui/theme.hpp"

enum class FontSize { Small, Body, Medium, Large, Huge };
enum class Align { Left, Center, Right };

class Renderer {
public:
    bool init(SDL_Renderer* r, const std::string& regularPath, const std::string& boldPath);
    void shutdown();

    SDL_Renderer* sdl() const { return ren_; }

    // ---- primitives ----
    void clearBackground();
    void fillRect(SDL_Rect r, theme::Col c);
    void roundedRect(SDL_Rect r, int rad, theme::Col c);
    void roundedBorder(SDL_Rect r, int rad, theme::Col c);
    void card(SDL_Rect r, bool selected = false);          // shadow + fill + border
    void hline(int x1, int x2, int y, theme::Col c);
    void vline(int x, int y1, int y2, theme::Col c);
    void line(int x1, int y1, int x2, int y2, theme::Col c, int thickness = 1);
    void pillTag(int x, int y, const std::string& s, theme::Col fg, theme::Col bg);

    // ---- text ----
    // Returns pixel width drawn.
    int  text(const std::string& s, int x, int y, FontSize sz,
              theme::Col c, Align a = Align::Left, bool bold = false);
    int  measure(const std::string& s, FontSize sz, bool bold = false);
    int  lineHeight(FontSize sz) const;

    void purgeTextCache();

private:
    TTF_Font* font(FontSize sz, bool bold);

    SDL_Renderer* ren_ = nullptr;
    TTF_Font* reg_[5]  = { nullptr, nullptr, nullptr, nullptr, nullptr };
    TTF_Font* bold_[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };

    struct CachedText { SDL_Texture* tex; int w; int h; };
    std::map<std::string, CachedText> cache_;
};
