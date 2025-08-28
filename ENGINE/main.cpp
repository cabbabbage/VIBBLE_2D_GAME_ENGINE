
#include "main.hpp"

#include "utils/rebuild_assets.hpp"
#include "ui/main_menu.hpp"
#include "ui/menu_ui.hpp"
#include "asset_loader.hpp"
#include "scene_renderer.hpp"
#include "assets.hpp"
#include "mouse_input.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

extern "C" {
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
    __declspec(dllexport) int NvOptimusEnablement = 0x00000001;
}










static std::vector<fs::path> list_images_in(const fs::path& dir) {
    std::vector<fs::path> out;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;

    for (auto& p : fs::directory_iterator(dir)) {
        if (p.is_regular_file()) {
            auto ext = p.path().extension().string();
            for (auto& c : ext) c = (char)tolower(c);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                out.push_back(p.path());
            }
        }
    }

    std::sort(out.begin(), out.end()); 
    return out;
}


static std::string pick_random_message_from_csv(const fs::path& csv_path) {
    std::vector<std::string> lines;
    std::ifstream in(csv_path);
    if (!in.is_open()) return "";

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    if (lines.empty()) return "";
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, lines.size() - 1);
    return lines[dist(rng)];
}


static void draw_text(SDL_Renderer* r, TTF_Font* font, const std::string& txt, int x, int y, SDL_Color col) {
    SDL_Surface* surf = TTF_RenderText_Blended(font, txt.c_str(), col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    int tw = surf->w, th = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) return;
    SDL_Rect dst{ x, y, tw, th };
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}


static void render_justified_text(SDL_Renderer* r, TTF_Font* font, const std::string& text, const SDL_Rect& rect, SDL_Color col) {
    if (!font || text.empty()) return;

    
    std::vector<std::string> words;
    {
        std::istringstream iss(text);
        std::string w;
        while (iss >> w) words.push_back(w);
    }
    if (words.empty()) return;

    int space_w = 0, space_h = 0;
    TTF_SizeText(font, " ", &space_w, &space_h);

    std::vector<std::vector<std::string>> lines;
    std::vector<std::string> cur;
    int cur_width = 0;

    auto width_of = [&](const std::vector<std::string>& ws) {
        int wsum = 0;
        for (size_t i = 0; i < ws.size(); ++i) {
            int w = 0, h = 0;
            TTF_SizeText(font, ws[i].c_str(), &w, &h);
            wsum += w;
            if (i + 1 < ws.size()) wsum += space_w; 
        }
        return wsum;
    };

    for (size_t i = 0; i < words.size(); ++i) {
        std::vector<std::string> test = cur;
        test.push_back(words[i]);
        int w = width_of(test);
        if (w <= rect.w || cur.empty()) {
            cur = std::move(test);
        } else {
            lines.push_back(cur);
            cur.clear();
            cur.push_back(words[i]);
        }
    }
    if (!cur.empty()) lines.push_back(cur);

    int line_y = rect.y;
    int line_h_max = 0;

    for (size_t li = 0; li < lines.size(); ++li) {
        const auto& l = lines[li];
        
        std::vector<int> ww(l.size(), 0);
        int words_total_w = 0;
        int word_h = 0;
        for (size_t i = 0; i < l.size(); ++i) {
            int w = 0, h = 0;
            TTF_SizeText(font, l[i].c_str(), &w, &h);
            ww[i] = w;
            words_total_w += w;
            word_h = std::max(word_h, h);
        }

        int extra_space = rect.w - words_total_w;
        int gaps = (int)l.size() - 1;
        int gap_space = (gaps > 0) ? space_w : 0;

        
        bool last_line = (li == lines.size() - 1);
        int x = rect.x;

        if (!last_line && gaps > 0) {
            
            int total_min = gaps * space_w;
            int addl = std::max(0, extra_space - total_min);
            int per_gap_extra = gaps > 0 ? addl / gaps : 0;
            int remainder = gaps > 0 ? addl % gaps : 0;

            for (size_t i = 0; i < l.size(); ++i) {
                
                SDL_Surface* surf = TTF_RenderText_Blended(font, l[i].c_str(), col);
                if (!surf) continue;
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                int tw = surf->w, th = surf->h;
                SDL_FreeSurface(surf);
                if (!tex) continue;
                SDL_Rect dst{ x, line_y, tw, th };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);

                x += tw;
                if (i + 1 < l.size()) {
                    int gap = gap_space + per_gap_extra + (remainder-- > 0 ? 1 : 0);
                    x += gap;
                }
                line_h_max = std::max(line_h_max, th);
            }
        } else {
            
            int total_width_with_min_spaces = words_total_w + gaps * space_w;
            x = rect.x + (rect.w - total_width_with_min_spaces) / 2;
            for (size_t i = 0; i < l.size(); ++i) {
                SDL_Surface* surf = TTF_RenderText_Blended(font, l[i].c_str(), col);
                if (!surf) continue;
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                int tw = surf->w, th = surf->h;
                SDL_FreeSurface(surf);
                if (!tex) continue;
                SDL_Rect dst{ x, line_y, tw, th };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);

                x += tw;
                if (i + 1 < l.size()) x += space_w;
                line_h_max = std::max(line_h_max, th);
            }
        }
        line_y += line_h_max;
        if (line_y >= rect.y + rect.h) break;
    }
}

static void render_scaled_center(SDL_Renderer* r, SDL_Texture* tex, int target_w, int target_h, int cx, int cy) {
    if (!tex) return;
    int w = 0, h = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    if (w <= 0 || h <= 0) return;

    double ar = (double)w / (double)h;
    int dw = target_w;
    int dh = (int)(dw / ar);
    if (dh > target_h) {
        dh = target_h;
        dw = (int)(dh * ar);
    }
    SDL_Rect dst{ cx - dw/2, cy - dh/2, dw, dh };
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

static SDL_Texture* gif_first_frame(SDL_Renderer* r, const fs::path& gif_path) {
#if SDL_IMAGE_VERSION_ATLEAST(2,6,0)
    IMG_Animation* anim = IMG_LoadAnimation(gif_path.string().c_str());
    if (!anim) {
        
        SDL_Surface* surf = IMG_Load(gif_path.string().c_str());
        if (!surf) return nullptr;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
        return tex;
    }
    
    SDL_Surface* f0 = anim->frames[0];
    SDL_Texture* tex = f0 ? SDL_CreateTextureFromSurface(r, f0) : nullptr;
    IMG_FreeAnimation(anim);
    return tex;
#else
    SDL_Surface* surf = IMG_Load(gif_path.string().c_str());
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return tex;
#endif
}


static void show_loading_screen(SDL_Renderer* renderer, int screen_w, int screen_h) {
    
    std::vector<fs::path> folders;
    if (fs::exists("loading") && fs::is_directory("loading")) {
        for (auto& p : fs::directory_iterator("loading")) {
            if (p.is_directory()) {
                folders.push_back(p.path());
            }
        }
    }
    if (folders.empty()) {
        std::cerr << "[Loading] No loading folders found!\n";
        return;
    }

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, folders.size() - 1);
    fs::path folder = folders[dist(rng)];

    
    auto images = list_images_in(folder);
    if (images.empty()) {
        std::cerr << "[Loading] No images in " << folder << "\n";
        return;
    }

    
    fs::path image_path = images.front();
    std::string msg = pick_random_message_from_csv(folder / "messages.csv");

    
    TTF_Font* title_font = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 48);
    TTF_Font* body_font  = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", 26);
    SDL_Color white = {255, 255, 255, 255};

    
    SDL_Surface* surf = IMG_Load(image_path.string().c_str());
    if (!surf) {
        std::cerr << "[Loading] Failed to load image: " << image_path << "\n";
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        std::cerr << "[Loading] Failed to create texture from image.\n";
        return;
    }

    
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    
    if (title_font) {
        int tw, th;
        TTF_SizeText(title_font, "LOADING...", &tw, &th);
        int tx = (screen_w - tw) / 2;
        draw_text(renderer, title_font, "LOADING...", tx, 40, white);
    }

    
    render_scaled_center(renderer, tex, screen_w/3, screen_h/3, screen_w/2, screen_h/2);
    SDL_DestroyTexture(tex);

    
    SDL_Rect msg_rect{ screen_w/3, (screen_h*2)/3, screen_w/3, screen_h/4 };
    if (body_font && !msg.empty()) {
        render_justified_text(renderer, body_font, msg, msg_rect, white);
    }

    if (title_font) TTF_CloseFont(title_font);
    if (body_font)  TTF_CloseFont(body_font);

    SDL_RenderPresent(renderer);
}




static std::string show_main_menu(SDL_Renderer* renderer, int screen_w, int screen_h) {
    MainMenu menu(renderer, screen_w, screen_h);
    std::string chosen_map;
    SDL_Event e;
    bool choosing = true;

    while (choosing) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                return "QUIT";
            }
            chosen_map = menu.handle_event(e);
            if (chosen_map == "QUIT") {
                return "QUIT";
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
    return chosen_map;
}


MainApp::MainApp(const std::string& map_path, SDL_Renderer* renderer, int screen_w, int screen_h)
    : map_path_(map_path), renderer_(renderer), screen_w_(screen_w), screen_h_(screen_h) {}

MainApp::~MainApp() {
    if (overlay_texture_)  SDL_DestroyTexture(overlay_texture_);
    if (minimap_texture_)  SDL_DestroyTexture(minimap_texture_);

    delete game_assets_;
    delete scene_;
    delete mouse_input_;
}

void MainApp::init() {
    setup();
    game_loop();
}

void MainApp::setup() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    try {
        loader_ = std::make_unique<AssetLoader>(map_path_, renderer_);

        minimap_texture_ = loader_->createMinimap(200, 200);

        auto assets_uptr = loader_->createAssets(screen_w_, screen_h_);
        game_assets_ = assets_uptr.release();

        mouse_input_ = new MouseInput();
        game_assets_->set_mouse_input(mouse_input_);

    } catch (const std::exception& e) {
        std::cerr << "[MainApp] Setup error: " << e.what() << "\n";
        throw;
    }

    scene_ = new SceneRenderer(renderer_, game_assets_, screen_w_, screen_h_, map_path_);
}

void MainApp::game_loop() {
    constexpr int FRAME_MS = 1000 / 30;

    bool quit = false;
    SDL_Event e;
    std::unordered_set<SDL_Keycode> keys;
    int frame_count = 0;

    while (!quit) {
        Uint32 start = SDL_GetTicks();

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_KEYDOWN) {
                keys.insert(e.key.keysym.sym);
            } else if (e.type == SDL_KEYUP) {
                keys.erase(e.key.keysym.sym);
            }
            if (mouse_input_) mouse_input_->handleEvent(e);
        }

        if (game_assets_ && game_assets_->player) {
            const int px = game_assets_->player->pos_X;
            const int py = game_assets_->player->pos_Y;
            game_assets_->update(keys, px, py);
        }

        if (frame_count >= 80) {
            if (scene_) scene_->render();
            SDL_RenderPresent(renderer_);
        }

        ++frame_count;
        if (mouse_input_) mouse_input_->update();

        Uint32 elapsed = SDL_GetTicks() - start;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }
}




void run(SDL_Window* window, SDL_Renderer* renderer, int screen_w, int screen_h, bool rebuild_cache) {
    while (true) {
        
        std::string chosen = show_main_menu(renderer, screen_w, screen_h);
        if (chosen == "QUIT" || chosen.empty()) {
            break;
        }

        
        if (rebuild_cache) {
            std::cout << "[Main] Rebuilding asset cache...\n";
            RebuildAssets* rebuilder = new RebuildAssets(renderer, chosen);
            delete rebuilder;
            std::cout << "[Main] Asset cache rebuild complete.\n";
        }

        
        show_loading_screen(renderer, screen_w, screen_h);

        
        MenuUI app(renderer, screen_w, screen_h, chosen);
        app.init();

        
        if (app.wants_return_to_main_menu()) {
            continue;
        }

        
        break;
    }
}

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
    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP) & (IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_TIF | IMG_INIT_WEBP))) {
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


    run(window, renderer, screen_width, screen_height, rebuild_cache);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    std::cout << "[Main] Game exited cleanly.\n";
    return 0;
}
