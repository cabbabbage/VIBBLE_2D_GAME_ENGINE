#include "area_ui.hpp"

#include <SDL_ttf.h>
#include <algorithm>
#include <stdexcept>

namespace {

static SDL_Surface* snapshot_texture_to_surface(SDL_Renderer* r, SDL_Texture* tex) {
    if (!r || !tex) return nullptr;
    int tw = 0, th = 0;
    Uint32 fmt = 0; int access = 0;
    SDL_QueryTexture(tex, &fmt, &access, &tw, &th);
    if (tw <= 0 || th <= 0) return nullptr;

    SDL_Texture* prev = SDL_GetRenderTarget(r);
    SDL_Texture* target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, tw, th);
    if (!target) return nullptr;

    SDL_SetRenderTarget(r, target);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, tex, nullptr, nullptr);

    SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (out) {
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, out->pixels, out->pitch);
    }

    SDL_SetRenderTarget(r, prev);
    SDL_DestroyTexture(target);
    return out;
}

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

static bool run_editor(SDL_Surface* bg_surface, int window_w, int window_h, AreaUI::Result& out) {
    if (!bg_surface || bg_surface->w <= 0 || bg_surface->h <= 0) return false;

    const int tex_w = bg_surface->w;
    const int tex_h = bg_surface->h;

    const int minW = 800, minH = 600;
    int win_w = window_w > 0 ? window_w : std::max(minW, tex_w);
    int win_h = window_h > 0 ? window_h : std::max(minH, tex_h);

    SDL_Window* win = SDL_CreateWindow("Area Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      win_w, win_h, SDL_WINDOW_SHOWN);
    if (!win) return false;
    SDL_Renderer* rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rend) { SDL_DestroyWindow(win); return false; }

    SDL_Texture* bg = SDL_CreateTextureFromSurface(rend, bg_surface);
    if (!bg) { SDL_DestroyRenderer(rend); SDL_DestroyWindow(win); return false; }

    SDL_Surface* mask = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask) {
        SDL_DestroyTexture(bg);
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        return false;
    }
    SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0));

    double scale = std::min(static_cast<double>(win_w) / tex_w, static_cast<double>(win_h) / tex_h);
    int draw_w = static_cast<int>(std::round(tex_w * scale));
    int draw_h = static_cast<int>(std::round(tex_h * scale));
    int off_x = (win_w - draw_w) / 2;
    int off_y = (win_h - draw_h) / 2;

    int brush = 10; bool drawing = false; bool erasing = false;

    bool ttf_init_local = false;
    if (TTF_WasInit() == 0) { if (TTF_Init() == 0) ttf_init_local = true; }
    TTF_Font* font = nullptr;
#ifdef _WIN32
    const char* candidates[] = {
        "C:/Windows/Fonts/COPRGTB.TTF",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf"
    };
#else
    const char* candidates[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" };
#endif
    for (const char* p : candidates) { if (!font) font = TTF_OpenFont(p, 20); }

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
                const int btn_w = 120, btn_h = 42, margin = 16;
                SDL_Rect doneRect{ win_w - btn_w - margin, margin, btn_w, btn_h };
                SDL_Point p{ e.button.x, e.button.y };
                const bool overDone = SDL_PointInRect(&p, &doneRect);
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (!overDone) {
                        drawing = true;
                        int mx = e.button.x, my = e.button.y;
                        int tx = static_cast<int>(std::round((mx - off_x) / scale));
                        int ty = static_cast<int>(std::round((my - off_y) / scale));
                        if (tx >= 0 && tx < tex_w && ty >= 0 && ty < tex_h) {
                            stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, 255));
                        }
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (!overDone) {
                        erasing = true;
                        int mx = e.button.x, my = e.button.y;
                        int tx = static_cast<int>(std::round((mx - off_x) / scale));
                        int ty = static_cast<int>(std::round((my - off_y) / scale));
                        if (tx >= 0 && tx < tex_w && ty >= 0 && ty < tex_h) {
                            stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, 0));
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                const int btn_w = 120, btn_h = 42, margin = 16;
                SDL_Rect doneRect{ win_w - btn_w - margin, margin, btn_w, btn_h };
                SDL_Point p{ e.button.x, e.button.y };
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (SDL_PointInRect(&p, &doneRect)) quit = true;
                    drawing = false;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    erasing = false;
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
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
        SDL_Rect dst{ off_x, off_y, draw_w, draw_h };
        SDL_RenderCopy(rend, bg, nullptr, &dst);

        SDL_Texture* overlay = SDL_CreateTextureFromSurface(rend, mask);
        if (overlay) {
            SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(overlay, 128);
            SDL_RenderCopy(rend, overlay, nullptr, &dst);
            SDL_DestroyTexture(overlay);
        }

        int mx, my; SDL_GetMouseState(&mx, &my);
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        for (int a = 0; a < 360; a += 6) {
            double rad = a * M_PI / 180.0;
            int x = mx + static_cast<int>(std::cos(rad) * brush);
            int y = my + static_cast<int>(std::sin(rad) * brush);
            SDL_RenderDrawPoint(rend, x, y);
        }

        const int btn_w = 120, btn_h = 42, margin = 16;
        SDL_Rect doneRect{ win_w - btn_w - margin, margin, btn_w, btn_h };
        SDL_Point mp; SDL_GetMouseState(&mp.x, &mp.y);
        const bool hover = SDL_PointInRect(&mp, &doneRect);
        SDL_SetRenderDrawColor(rend, hover ? 60 : 40, hover ? 140 : 100, 160, 230);
        SDL_RenderFillRect(rend, &doneRect);
        SDL_SetRenderDrawColor(rend, 250, 195, 73, 255);
        SDL_RenderDrawRect(rend, &doneRect);
        if (font) {
            SDL_Color col = hover ? SDL_Color{255,255,255,255} : SDL_Color{230,230,230,255};
            SDL_Surface* text = TTF_RenderText_Blended(font, "Done", col);
            if (text) {
                SDL_Texture* ttex = SDL_CreateTextureFromSurface(rend, text);
                if (ttex) {
                    SDL_Rect td{ doneRect.x + (doneRect.w - text->w)/2,
                                  doneRect.y + (doneRect.h - text->h)/2,
                                  text->w, text->h };
                    SDL_RenderCopy(rend, ttex, nullptr, &td);
                    SDL_DestroyTexture(ttex);
                }
                SDL_FreeSurface(text);
            }
        }

        SDL_RenderPresent(rend);
    }

    out.bg_w = tex_w;
    out.bg_h = tex_h;
    out.points = extract_edge_points(mask, 1);

    SDL_FreeSurface(mask);
    if (font) TTF_CloseFont(font);
    if (ttf_init_local) TTF_Quit();
    SDL_DestroyTexture(bg);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    return true;
}

} // namespace

bool AreaUI::edit_over_area(SDL_Renderer* app_renderer,
                            const Area& base,
                            int window_w,
                            int window_h,
                            Result& out) {
    if (!app_renderer) return false;
    SDL_Texture* bg = base.get_texture();
    if (!bg) {
        const_cast<Area&>(base).create_area_texture(app_renderer);
        bg = base.get_texture();
    }
    if (!bg) return false;
    SDL_Surface* snap = snapshot_texture_to_surface(app_renderer, bg);
    if (!snap) return false;
    bool ok = run_editor(snap, window_w, window_h, out);
    SDL_FreeSurface(snap);
    return ok;
}

bool AreaUI::edit_over_texture(SDL_Renderer* app_renderer,
                               SDL_Texture* texture,
                               int window_w,
                               int window_h,
                               Result& out) {
    if (!app_renderer || !texture) return false;
    SDL_Surface* snap = snapshot_texture_to_surface(app_renderer, texture);
    if (!snap) return false;
    bool ok = run_editor(snap, window_w, window_h, out);
    SDL_FreeSurface(snap);
    return ok;
}

