// SwitchMakroTerminal - a macro/market dashboard homebrew for Nintendo Switch.
#define SDL_MAIN_HANDLED
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <string>
#include <exception>

#include "ui/renderer.hpp"
#include "ui/theme.hpp"
#include "data/config.hpp"
#include "net/http.hpp"
#include "app.hpp"
#include "log.hpp"
#include "app_paths.hpp"

// A tiny console fallback so failures are visible on-screen instead of a black screen.
static void fatalConsole(const std::string& msg) {
    consoleInit(nullptr);
    printf("\n  SwitchMakroTerminal - startup error\n\n  %s\n\n", msg.c_str());
    printf("  Press + to exit.\n");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    applog::reset();
    applog::line("boot: start");

    paths::initFromArgv0(argc > 0 ? argv[0] : nullptr);
    applog::line("boot: app dir = '%s'", paths::appDir().c_str());

    // libnx services
    socketInitializeDefault();
    applog::line("boot: socket ok");
    romfsInit();
    applog::line("boot: romfs ok");
    http::globalInit();
    applog::line("boot: curl ok");

    // config from romfs (falls back to sensible defaults)
    Config cfg = Config::load("romfs:/config.json");
    applog::line("boot: config ok (overview=%zu stocks=%zu sources=%zu)",
                 cfg.overview.size(), cfg.stocks.size(), cfg.yieldSources.size());

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        http::globalCleanup(); romfsExit(); socketExit();
        fatalConsole(std::string("SDL_Init failed: ") + SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        SDL_Quit(); http::globalCleanup(); romfsExit(); socketExit();
        fatalConsole(std::string("TTF_Init failed: ") + TTF_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("SwitchMakroTerminal", 0, 0,
                                       theme::kW, theme::kH, 0);
    if (!win) {
        TTF_Quit(); SDL_Quit(); http::globalCleanup(); romfsExit(); socketExit();
        fatalConsole(std::string("CreateWindow failed: ") + SDL_GetError());
        return 1;
    }

    SDL_Renderer* sdlRen = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdlRen) {
        SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit();
        http::globalCleanup(); romfsExit(); socketExit();
        fatalConsole(std::string("CreateRenderer failed: ") + SDL_GetError());
        return 1;
    }
    applog::line("boot: sdl+window+renderer ok");
    SDL_SetRenderDrawBlendMode(sdlRen, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(sdlRen, theme::kW, theme::kH); // scale to handheld/docked

    Renderer R;
    if (!R.init(sdlRen, cfg.fontRegular, cfg.fontBold)) {
        applog::line("boot: FONT LOAD FAILED (regular=%s bold=%s)",
                     cfg.fontRegular.c_str(), cfg.fontBold.c_str());
        SDL_DestroyRenderer(sdlRen); SDL_DestroyWindow(win);
        TTF_Quit(); SDL_Quit(); http::globalCleanup(); romfsExit(); socketExit();
        fatalConsole("Failed to load fonts.\n  Put a TTF at romfs/fonts/main.ttf (and bold.ttf)\n"
                     "  then rebuild. See README.");
        return 1;
    }

    applog::line("boot: fonts ok -> entering app loop");
    try {
        App app(R, cfg);
        app.run();
    } catch (const std::exception& e) {
        applog::line("FATAL in app loop: %s", e.what());
    } catch (...) {
        applog::line("FATAL in app loop: unknown exception");
    }
    applog::line("app loop exited cleanly");

    R.shutdown();
    SDL_DestroyRenderer(sdlRen);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    http::globalCleanup();
    romfsExit();
    socketExit();
    return 0;
}
