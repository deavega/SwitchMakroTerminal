#include "ui/renderer.hpp"
#include <SDL2/SDL2_gfxPrimitives.h>
#include <cstdio>

static int fontPx(FontSize sz) {
    switch (sz) {
        case FontSize::Small:  return 17;
        case FontSize::Body:   return 22;
        case FontSize::Medium: return 28;
        case FontSize::Large:  return 40;
        case FontSize::Huge:   return 56;
    }
    return 22;
}

bool Renderer::init(SDL_Renderer* r, const std::string& regularPath, const std::string& boldPath) {
    ren_ = r;
    const FontSize sizes[5] = { FontSize::Small, FontSize::Body, FontSize::Medium,
                                FontSize::Large, FontSize::Huge };
    for (int i = 0; i < 5; i++) {
        reg_[i]  = TTF_OpenFont(regularPath.c_str(), fontPx(sizes[i]));
        bold_[i] = TTF_OpenFont(boldPath.c_str(),    fontPx(sizes[i]));
        if (!reg_[i]) {
            std::printf("[renderer] failed to open %s: %s\n", regularPath.c_str(), TTF_GetError());
            return false;
        }
        if (!bold_[i]) bold_[i] = reg_[i]; // fall back to regular if no bold file
        TTF_SetFontHinting(reg_[i],  TTF_HINTING_LIGHT);
        if (bold_[i] != reg_[i]) TTF_SetFontHinting(bold_[i], TTF_HINTING_LIGHT);
    }
    return true;
}

void Renderer::shutdown() {
    purgeTextCache();
    for (int i = 0; i < 5; i++) {
        if (bold_[i] && bold_[i] != reg_[i]) TTF_CloseFont(bold_[i]);
        if (reg_[i]) TTF_CloseFont(reg_[i]);
        reg_[i] = bold_[i] = nullptr;
    }
}

TTF_Font* Renderer::font(FontSize sz, bool bold) {
    int i = static_cast<int>(sz);
    return bold ? bold_[i] : reg_[i];
}

void Renderer::clearBackground() {
    // vertical gradient bg0 -> bg1
    for (int y = 0; y < theme::kH; y++) {
        float t = static_cast<float>(y) / theme::kH;
        Uint8 rr = (Uint8)(theme::bg0.r + (theme::bg1.r - theme::bg0.r) * t);
        Uint8 gg = (Uint8)(theme::bg0.g + (theme::bg1.g - theme::bg0.g) * t);
        Uint8 bb = (Uint8)(theme::bg0.b + (theme::bg1.b - theme::bg0.b) * t);
        SDL_SetRenderDrawColor(ren_, rr, gg, bb, 255);
        SDL_RenderDrawLine(ren_, 0, y, theme::kW, y);
    }
}

void Renderer::fillRect(SDL_Rect r, theme::Col c) {
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren_, &r);
}

void Renderer::roundedRect(SDL_Rect r, int rad, theme::Col c) {
    roundedBoxRGBA(ren_, r.x, r.y, r.x + r.w, r.y + r.h, rad, c.r, c.g, c.b, c.a);
}

void Renderer::roundedBorder(SDL_Rect r, int rad, theme::Col c) {
    roundedRectangleRGBA(ren_, r.x, r.y, r.x + r.w, r.y + r.h, rad, c.r, c.g, c.b, c.a);
}

void Renderer::card(SDL_Rect r, bool selected) {
    // soft shadow
    SDL_Rect sh = { r.x + 2, r.y + 4, r.w, r.h };
    roundedBoxRGBA(ren_, sh.x, sh.y, sh.x + sh.w, sh.y + sh.h, 14, 0, 0, 0, 70);
    roundedRect(r, 14, selected ? theme::panelHi : theme::panel);
    roundedBorder(r, 14, selected ? theme::borderHi : theme::border);
    if (selected) {
        // brighten border a touch with a second pass
        roundedRectangleRGBA(ren_, r.x + 1, r.y + 1, r.x + r.w - 1, r.y + r.h - 1, 13,
                             theme::accent.r, theme::accent.g, theme::accent.b, 120);
    }
}

void Renderer::hline(int x1, int x2, int y, theme::Col c) {
    hlineRGBA(ren_, x1, x2, y, c.r, c.g, c.b, c.a);
}
void Renderer::vline(int x, int y1, int y2, theme::Col c) {
    vlineRGBA(ren_, x, y1, y2, c.r, c.g, c.b, c.a);
}

void Renderer::line(int x1, int y1, int x2, int y2, theme::Col c, int thickness) {
    if (thickness <= 1) {
        aalineRGBA(ren_, x1, y1, x2, y2, c.r, c.g, c.b, c.a);
    } else {
        thickLineRGBA(ren_, x1, y1, x2, y2, thickness, c.r, c.g, c.b, c.a);
    }
}

void Renderer::pillTag(int x, int y, const std::string& s, theme::Col fg, theme::Col bg) {
    int w = measure(s, FontSize::Small, true);
    int padX = 10, padY = 5;
    SDL_Rect r = { x, y, w + padX * 2, lineHeight(FontSize::Small) + padY };
    roundedRect(r, r.h / 2, bg);
    text(s, x + padX, y + padY / 2, FontSize::Small, fg, Align::Left, true);
}

int Renderer::lineHeight(FontSize sz) const {
    int i = static_cast<int>(sz);
    return reg_[i] ? TTF_FontHeight(reg_[i]) : fontPx(sz);
}

int Renderer::measure(const std::string& s, FontSize sz, bool bold) {
    TTF_Font* f = font(sz, bold);
    if (!f || s.empty()) return 0;
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s.c_str(), &w, &h);
    return w;
}

int Renderer::text(const std::string& s, int x, int y, FontSize sz,
                   theme::Col c, Align a, bool bold) {
    if (s.empty()) return 0;
    char key[64];
    std::snprintf(key, sizeof(key), "%d%d%02x%02x%02x%02x|", (int)sz, bold ? 1 : 0,
                  c.r, c.g, c.b, c.a);
    std::string cacheKey = std::string(key) + s;

    CachedText ct;
    auto it = cache_.find(cacheKey);
    if (it != cache_.end()) {
        ct = it->second;
    } else {
        if (cache_.size() > 400) purgeTextCache();
        TTF_Font* f = font(sz, bold);
        SDL_Color col = theme::sdl(c);
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), col);
        if (!surf) return 0;
        ct.tex = SDL_CreateTextureFromSurface(ren_, surf);
        ct.w = surf->w; ct.h = surf->h;
        SDL_FreeSurface(surf);
        cache_[cacheKey] = ct;
    }

    int dx = x;
    if (a == Align::Center) dx = x - ct.w / 2;
    else if (a == Align::Right) dx = x - ct.w;
    SDL_Rect dst = { dx, y, ct.w, ct.h };
    SDL_RenderCopy(ren_, ct.tex, nullptr, &dst);
    return ct.w;
}

void Renderer::purgeTextCache() {
    for (auto& kv : cache_) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    cache_.clear();
}
