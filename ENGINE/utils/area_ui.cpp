#include "area.hpp"
#include <SDL.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {
struct EditorResult {
    std::vector<Area::Point> points;
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
static EditorResult run_area_editor(SDL_Texture* background,
                                   SDL_Renderer* src_renderer,
                                   int window_w,
                                   int window_h,
                                   const Area* initial_area = nullptr) {
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
    
    SDL_Surface* mask = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask) {
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        throw std::runtime_error("[Area Editor] Failed to allocate mask surface");
    }
    
    SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0));
    
    
    SDL_Texture* bg_local = nullptr;
    if (src_renderer) {
        
        
        SDL_Texture* tmp = SDL_CreateTexture(src_renderer, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
        if (tmp) {
            
            SDL_Texture* prev = SDL_GetRenderTarget(src_renderer);
            SDL_Rect prev_vp; SDL_RenderGetViewport(src_renderer, &prev_vp);
            SDL_Rect prev_clip; SDL_RenderGetClipRect(src_renderer, &prev_clip);
            float prev_sx = 1.0f, prev_sy = 1.0f; SDL_RenderGetScale(src_renderer, &prev_sx, &prev_sy);
            SDL_SetRenderTarget(src_renderer, tmp);
            SDL_RenderSetViewport(src_renderer, nullptr);
            SDL_RenderSetClipRect(src_renderer, nullptr);
            SDL_RenderSetScale(src_renderer, 1.0f, 1.0f);
            SDL_SetRenderDrawBlendMode(src_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(src_renderer, 0, 0, 0, 0);
            SDL_RenderClear(src_renderer);
            SDL_Rect full{0,0,tex_w,tex_h};
            SDL_RenderCopy(src_renderer, background, nullptr, &full);
            SDL_Surface* bg_surf = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA8888);
            if (bg_surf) {
                if (SDL_RenderReadPixels(src_renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, bg_surf->pixels, bg_surf->pitch) == 0) {
                    bg_local = SDL_CreateTextureFromSurface(rend, bg_surf);
                }
                SDL_FreeSurface(bg_surf);
            }
            
            SDL_SetRenderTarget(src_renderer, prev);
            SDL_RenderSetViewport(src_renderer, &prev_vp);
            SDL_RenderSetClipRect(src_renderer, &prev_clip);
            SDL_RenderSetScale(src_renderer, prev_sx, prev_sy);
            SDL_DestroyTexture(tmp);
        }
    }
    if (!bg_local) {
        
        bg_local = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
        if (bg_local) {
            SDL_Texture* prev_t = SDL_GetRenderTarget(rend);
            SDL_SetRenderTarget(rend, bg_local);
            SDL_SetRenderDrawColor(rend, 40, 40, 40, 255);
            SDL_RenderClear(rend);
            SDL_SetRenderTarget(rend, prev_t);
        }
    }
    
    
    const double fit_scale = std::min(static_cast<double>(win_w) / tex_w, static_cast<double>(win_h) / tex_h);
    double view_scale = std::min(1.0, fit_scale);
    int pan_x = 0, pan_y = 0;
    auto recompute_layout = [&](int& draw_w, int& draw_h, int& off_x, int& off_y){
        draw_w = static_cast<int>(std::round(tex_w * view_scale));
        draw_h = static_cast<int>(std::round(tex_h * view_scale));
        
        off_x = (win_w - draw_w) / 2 + pan_x;
        off_y = (win_h - draw_h) / 2 + pan_y;
    };
    int draw_w = 0, draw_h = 0, off_x = 0, off_y = 0;
    recompute_layout(draw_w, draw_h, off_x, off_y);
    
    int area_minx = 0, area_miny = 0, area_maxx = 0, area_maxy = 0;
    if (initial_area) {
        std::tie(area_minx, area_miny, area_maxx, area_maxy) = initial_area->get_bounds();
    }
    
    int brush = 10;
    bool drawing = false;
    bool erasing = false;
    bool panning = false;
    int last_mx = 0, last_my = 0;
    bool quit = false;
    bool draw_mode = true;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { quit = true; }
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) { quit = true; }
                else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_s) { quit = true; }
                else if (e.key.keysym.sym == SDLK_d) { draw_mode = true; }
                else if (e.key.keysym.sym == SDLK_e) { draw_mode = false; }
                else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS) { brush = std::min(300, brush + 2); }
                else if (e.key.keysym.sym == SDLK_MINUS) { brush = std::max(1, brush - 2); }
                else if (e.key.keysym.sym == SDLK_c) { SDL_FillRect(mask, nullptr, SDL_MapRGBA(mask->format, 255, 0, 0, 0)); }
                else if (e.key.keysym.sym == SDLK_f) {
                    view_scale = fit_scale;
                    pan_x = pan_y = 0;
                    recompute_layout(draw_w, draw_h, off_x, off_y);
                } else if (e.key.keysym.sym == SDLK_1) {
                    
                    view_scale = 1.0;
                    pan_x = pan_y = 0;
                    recompute_layout(draw_w, draw_h, off_x, off_y);
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                
                SDL_Rect btn_draw{10,10,80,28};
                SDL_Rect btn_erase{100,10,80,28};
                SDL_Rect btn_save{win_w-100,10,80,28};
                SDL_Point mp{ e.button.x, e.button.y };
                if (SDL_PointInRect(&mp, &btn_draw)) { draw_mode = true; }
                else if (SDL_PointInRect(&mp, &btn_erase)) { draw_mode = false; }
                else if (SDL_PointInRect(&mp, &btn_save)) { quit = true; }
                else {
                    if (e.button.button == SDL_BUTTON_LEFT) drawing = true;
                    if (e.button.button == SDL_BUTTON_RIGHT) erasing = true;
                    if (e.button.button == SDL_BUTTON_MIDDLE) { panning = true; last_mx = e.button.x; last_my = e.button.y; }
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.button == SDL_BUTTON_LEFT) drawing = false;
                if (e.button.button == SDL_BUTTON_RIGHT) erasing = false;
                if (e.button.button == SDL_BUTTON_MIDDLE) panning = false;
            } else if (e.type == SDL_MOUSEWHEEL) {
                
                int mx, my; SDL_GetMouseState(&mx, &my);
                
                double tx = (mx - off_x) / view_scale;
                double ty = (my - off_y) / view_scale;
                if (e.wheel.y > 0)      view_scale = std::min(10.0, view_scale * 1.1);
                else if (e.wheel.y < 0) view_scale = std::max(0.05, view_scale / 1.1);
                
                int new_draw_w = 0, new_draw_h = 0, cx = 0, cy = 0;
                
                new_draw_w = static_cast<int>(std::round(tex_w * view_scale));
                new_draw_h = static_cast<int>(std::round(tex_h * view_scale));
                cx = (win_w - new_draw_w) / 2;
                cy = (win_h - new_draw_h) / 2;
                
                pan_x = (int)std::lround((mx - tx * view_scale) - cx);
                pan_y = (int)std::lround((my - ty * view_scale) - cy);
                recompute_layout(draw_w, draw_h, off_x, off_y);
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x;
                int my = e.motion.y;
                if (panning) {
                    pan_x += (mx - last_mx);
                    pan_y += (my - last_my);
                    last_mx = mx; last_my = my;
                    recompute_layout(draw_w, draw_h, off_x, off_y);
                }
                
                int tx = static_cast<int>(std::round((mx - off_x) / view_scale));
                int ty = static_cast<int>(std::round((my - off_y) / view_scale));
                if (tx >= 0 && tx < tex_w && ty >= 0 && ty < tex_h) {
                    bool er = erasing || (!draw_mode && drawing);
                    if (drawing) {
                        stamp_circle(mask, tx, ty, brush, SDL_MapRGBA(mask->format, 255, 0, 0, er ? 0 : 255));
                    }
                }
            }
        }
        SDL_SetRenderDrawColor(rend, 20, 20, 20, 255);
        SDL_RenderClear(rend);
        
        SDL_Rect dst{ off_x, off_y, draw_w, draw_h };
        if (bg_local) SDL_RenderCopy(rend, bg_local, nullptr, &dst);
        
        if (initial_area) {
            
            
            const bool bg_is_area_bb = (tex_w == (area_maxx - area_minx + 1)) &&
                                       (tex_h == (area_maxy - area_miny + 1));
            const int origin_x = bg_is_area_bb ? area_minx : 0;
            const int origin_y = bg_is_area_bb ? area_miny : 0;
            SDL_SetRenderDrawColor(rend, 0, 200, 255, 180);
            std::vector<SDL_Point> pts;
            pts.reserve(initial_area->get_points().size() + 1);
            for (const auto& p : initial_area->get_points()) {
                int sx = off_x + static_cast<int>(std::round((p.first - origin_x) * view_scale));
                int sy = off_y + static_cast<int>(std::round((p.second - origin_y) * view_scale));
                pts.push_back(SDL_Point{ sx, sy });
            }
            if (!pts.empty()) {
                pts.push_back(pts.front());
                SDL_RenderDrawLines(rend, pts.data(), static_cast<int>(pts.size()));
            }
        }
        
        SDL_Texture* overlay = SDL_CreateTextureFromSurface(rend, mask);
        if (overlay) {
            SDL_SetTextureBlendMode(overlay, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(overlay, 128);
            SDL_RenderCopy(rend, overlay, nullptr, &dst);
            SDL_DestroyTexture(overlay);
        }
        
        auto draw_button = [&](SDL_Rect rct, SDL_Color col){
            SDL_SetRenderDrawColor(rend, col.r, col.g, col.b, 200);
            SDL_RenderFillRect(rend, &rct);
            SDL_SetRenderDrawColor(rend, 255,255,255,255);
            SDL_RenderDrawRect(rend, &rct);
        };
        draw_button(SDL_Rect{10,10,80,28}, draw_mode ? SDL_Color{80,180,80,255} : SDL_Color{60,60,60,255});
        draw_button(SDL_Rect{100,10,80,28}, !draw_mode ? SDL_Color{180,80,80,255} : SDL_Color{60,60,60,255});
        draw_button(SDL_Rect{win_w-100,10,80,28}, SDL_Color{80,80,200,255});
        
        int mx, my; (void)SDL_GetMouseState(&mx, &my);
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 255);
        
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
    res.points = extract_edge_points(mask, 1);
    SDL_FreeSurface(mask);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    if (bg_local) SDL_DestroyTexture(bg_local);
    return res;
}
}


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
    EditorResult er = run_area_editor(bg, renderer, window_w, window_h, &base);
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

Area::Area(const std::string& name, SDL_Texture* background, SDL_Renderer* renderer,
           int window_w, int window_h)
    : area_name_(name)
{
    if (!background) throw std::runtime_error("[Area: editor] Null background texture");
    EditorResult er = run_area_editor(background, renderer, window_w, window_h, nullptr);
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

