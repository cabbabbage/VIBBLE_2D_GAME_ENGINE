#include "area.hpp"

#include <SDL.h>
#include <cmath>
#include <stdexcept>
#include <vector>

// --- Interactive Area Editor implementation split from area.cpp ---
namespace {
struct EditorResult {
    std::vector<Area::Point> points; // in background pixel coordinates (top-left origin)
    int bg_w = 0;
    int bg_h = 0;
};

static void stamp_circle(SDL_Surface* surf, int cx, int cy, int r, Uint32 color) {
    if (!surf) return;
    SDL_LockSurface(surf);
    Uint8* pixels = static_cast<Uint8*>(surf->pixels);
    int pitch = surf->pitch;
    for (int y = -r; y <= r; ++y) {
        int yy = cy + y;
        if (yy < 0 || yy >= surf->h) continue;
        int xr = static_cast<int>(std::sqrt(static_cast<double>(r * r - y * y)));
        for (int x = -xr; x <= xr; ++x) {
            int xx = cx + x;
            if (xx < 0 || xx >= surf->w) continue;
            Uint32* p = reinterpret_cast<Uint32*>(pixels + yy * pitch) + xx;
            *p = color;
        }
    }
    SDL_UnlockSurface(surf);
}

static std::vector<Area::Point> extract_edge_points(SDL_Surface* surf, int step = 1) {
    std::vector<Area::Point> out;
    if (!surf) return out;
    SDL_LockSurface(surf);
    const Uint8* pixels = static_cast<const Uint8*>(surf->pixels);
    const int pitch = surf->pitch;
    auto getA = [&](int x, int y) -> Uint8 {
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint32 v = *p;
        return static_cast<Uint8>((v >> 24) & 0xFF);
    };
    for (int y = 1; y < surf->h - 1; y += step) {
        for (int x = 1; x < surf->w - 1; x += step) {
            Uint8 a = getA(x, y);
            if (a == 0) continue;
            if (getA(x - 1, y) == 0 || getA(x + 1, y) == 0 || getA(x, y - 1) == 0 || getA(x, y + 1) == 0) {
                out.emplace_back(x, y);
            }
        }
    }
    SDL_UnlockSurface(surf);
    return out;
}

static EditorResult run_area_editor(SDL_Texture* background, int window_w, int window_h) {
    if (!background) throw std::runtime_error("[Area Editor] No background texture provided");

    int tex_w = 0, tex_h = 0;
    SDL_QueryTexture(background, nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w <= 0 || tex_h <= 0) throw std::runtime_error("[Area Editor] Invalid background texture size");

    const int minW = 800, minH = 600;
    int win_w = window_w > 0 ? window_w : std::max(minW, tex_w);
    int win_h = window_h > 0 ? window_h : std::max(minH, tex_h);

    SDL_Window* win = SDL_CreateWindow("Area Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      win_w, win_h, SDL_WINDOW_SHOWN);
    if (!win) throw std::runtime_error("[Area Editor] Failed to create window");
    SDL_Renderer* rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rend) {
        SDL_DestroyWindow(win);
        throw std::runtime_error("[Area Editor] Failed to create renderer");
    }

    // Prepare mask surface (ARGB): we'll use the alpha channel as the mask value
    SDL_Surface* mask = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask) {
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        throw std::runtime_error("[Area Editor] Failed to allocate mask surface");
    }
    // Clear mask to transparent
    SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0));

    // Precompute scale/offset to fit background centered
    double scale = std::min(static_cast<double>(win_w) / tex_w, static_cast<double>(win_h) / tex_h);
    int draw_w = static_cast<int>(std::round(tex_w * scale));
    int draw_h = static_cast<int>(std::round(tex_h * scale));
    int off_x = (win_w - draw_w) / 2;
    int off_y = (win_h - draw_h) / 2;

    // Brush settings
    int brush = 10;
    bool drawing = false;
    bool erasing = false;

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { quit = true; }
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) { quit = true; }
                else if (e.key.keysym.sym == SDLK_RETURN) { quit = true; }
                else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS) { brush = std::min(300, brush + 2); }
                else if (e.key.keysym.sym == SDLK_MINUS) { brush = std::max(1, brush - 2); }
                else if (e.key.keysym.sym == SDLK_c) { SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0)); }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) drawing = true;
                if (e.button.button == SDL_BUTTON_RIGHT) erasing = true;
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.button == SDL_BUTTON_LEFT) drawing = false;
                if (e.button.button == SDL_BUTTON_RIGHT) erasing = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x;
                int my = e.motion.y;
                // Map to texture space
                int tx = static_cast<int>(std::round((mx - off_x) / scale));
                int ty = static_cast<int>(std::round((my - off_y) / scale));
                if (tx >= 0 && tx < tex_w && ty >= 0 && ty < tex_h) {
                    if (drawing) {
                        stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, 255));
                    } else if (erasing) {
                        stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, 0));
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(rend, 20, 20, 20, 255);
        SDL_RenderClear(rend);
        // Draw background centered
        SDL_Rect dst{ off_x, off_y, draw_w, draw_h };
        SDL_RenderCopy(rend, background, nullptr, &dst);

        // Draw red overlay from mask
        SDL_Texture* overlay = SDL_CreateTextureFromSurface(rend, mask);
        if (overlay) {
            SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(overlay, 128);
            SDL_RenderCopy(rend, overlay, nullptr, &dst);
            SDL_DestroyTexture(overlay);
        }

        // Draw brush preview
        int mx, my; (void)SDL_GetMouseState(&mx, &my);
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        // Approximate circle preview
        for (int a = 0; a < 360; a += 6) {
            double rad = a * M_PI / 180.0;
            int x = mx + static_cast<int>(std::cos(rad) * brush);
            int y = my + static_cast<int>(std::sin(rad) * brush);
            SDL_RenderDrawPoint(rend, x, y);
        }

        SDL_RenderPresent(rend);
    }

    EditorResult res;
    res.bg_w = tex_w;
    res.bg_h = tex_h;
    res.points = extract_edge_points(mask, /*step=*/1);

    SDL_FreeSurface(mask);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    return res;
}
} // namespace

// Interactive constructors moved here
Area::Area(const std::string& name, const Area& base, SDL_Renderer* renderer,
           int window_w, int window_h)
    : area_name_(name)
{
    SDL_Texture* bg = base.get_texture();
    if (!bg && renderer) {
        const_cast<Area&>(base).create_area_texture(renderer);
        bg = base.get_texture();
    }
    if (!bg) throw std::runtime_error("[Area: editor] Base area has no background texture");

    EditorResult er = run_area_editor(bg, window_w, window_h);
    if (er.points.empty()) {
        throw std::runtime_error("[Area: editor] No points drawn");
    }

    // Construct absolute points in image pixel space; pivot is bottom-center
    int pivot_x = er.bg_w / 2;
    int pivot_y = er.bg_h;

    points.clear();
    points.reserve(er.points.size());
    for (const auto& p : er.points) {
        points.emplace_back(p.first, p.second);
    }
    pos_X = pivot_x;
    pos_Y = pivot_y;
    update_geometry_data();
}

Area::Area(const std::string& name, SDL_Texture* background, SDL_Renderer* /*renderer*/,
           int window_w, int window_h)
    : area_name_(name)
{
    if (!background) throw std::runtime_error("[Area: editor] Null background texture");
    EditorResult er = run_area_editor(background, window_w, window_h);
    if (er.points.empty()) {
        throw std::runtime_error("[Area: editor] No points drawn");
    }
    int pivot_x = er.bg_w / 2;
    int pivot_y = er.bg_h;
    points.clear();
    points.reserve(er.points.size());
    for (const auto& p : er.points) {
        points.emplace_back(p.first, p.second);
    }
    pos_X = pivot_x;
    pos_Y = pivot_y;
    update_geometry_data();
}

