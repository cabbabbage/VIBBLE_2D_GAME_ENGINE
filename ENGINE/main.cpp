

#include "main.hpp"
#include "engine.hpp"
#include "utils/rebuild_assets.hpp"
#include "ui\main_menu.hpp"   

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>


extern "C" {
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
    __declspec(dllexport) int NvOptimusEnablement = 0x00000001;
}

void run(const std::string& map_path, SDL_Renderer* renderer, int screen_w, int screen_h);

int main(int argc, char* argv[]) {
    std::cout << "[Main] Starting game engine...\n";

    bool rebuild_cache = (argc > 1 && argv[1] && std::string(argv[1]) == "-r");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "[Main] SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "[Main] Mix_OpenAudio failed: " << Mix_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    if (TTF_Init() < 0) {
        std::cerr << "[Main] TTF_Init failed: " << TTF_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "[Main] IMG_Init failed: " << IMG_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Game Window",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          0, 0,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        std::cerr << "[Main] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        IMG_Quit(); TTF_Quit(); SDL_Quit();
        return 1;
    }


    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "[Main] SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        IMG_Quit(); TTF_Quit(); SDL_Quit();
        return 1;
    }

 
    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    std::cout << "[Main] Renderer: " << (info.name ? info.name : "Unknown") << "\n";

    int screen_width = 0, screen_height = 0;
    SDL_GetRendererOutputSize(renderer, &screen_width, &screen_height);
    std::cout << "[Main] Screen resolution: " << screen_width << "x" << screen_height << "\n";


    MainMenu menu(renderer, screen_width, screen_height);
    std::string chosen_map;
    SDL_Event e;
    bool choosing = true;

    while (choosing) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                IMG_Quit();
                TTF_Quit();
                SDL_Quit();
                return 0;
            }

            chosen_map = menu.handle_event(e);
            if (chosen_map == "QUIT") {
                std::cout << "[Main] Quit selected from menu.\n";
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                IMG_Quit();
                TTF_Quit();
                SDL_Quit();
                return 0;
            }
            if (!chosen_map.empty()) {
                choosing = false;
                break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        menu.render();
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    TTF_Font* font = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 48);
    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* surf = TTF_RenderText_Blended(font, "LOADING...", white);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FreeSurface(surf);

            if (tex) {
                int tw, th;
                SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                SDL_Rect dst{ (screen_width - tw) / 2, (screen_height - th) / 2, tw, th };
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
        }
        TTF_CloseFont(font);
    }
    SDL_RenderPresent(renderer);

    const std::string map_path = chosen_map;
    std::cout << "[Main] Selected map path: " << map_path << "\n";

    if (rebuild_cache) {
        std::cout << "[Main] Rebuilding asset cache...\n";
        RebuildAssets* rebuilder = new RebuildAssets(renderer, map_path);
        delete rebuilder;
        std::cout << "[Main] Asset cache rebuild complete.\n";
    }

    run(map_path, renderer, screen_width, screen_height);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    std::cout << "[Main] Game exited cleanly.\n";
    return 0;
}

void run(const std::string& map_path, SDL_Renderer* renderer, int screen_w, int screen_h) {
    Engine engine(map_path, renderer, screen_w, screen_h);
    engine.init();
}
