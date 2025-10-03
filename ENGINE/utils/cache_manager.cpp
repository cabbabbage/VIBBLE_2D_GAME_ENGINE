#include "cache_manager.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void ensure_dirs_for(const std::string& path) {
    try {
        fs::path p(path);
        if (p.has_parent_path()) fs::create_directories(p.parent_path());
    } catch (...) {}
}

static SDL_Surface* to_rgba8888(SDL_Surface* s) {
    if (!s) return nullptr;
    if (s->format->format == SDL_PIXELFORMAT_RGBA8888) {
        SDL_Surface* copy = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!copy) return nullptr;
        SDL_Rect r{0, 0, s->w, s->h};
        SDL_BlitSurface(s, &r, copy, &r);
        return copy;
    }
    return SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA8888, 0);
}

static void set_best_scale_hint() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
}

bool CacheManager::load_metadata(const std::string& meta_file, nlohmann::json& out_meta) {
    if (!fs::exists(meta_file)) return false;
    std::ifstream in(meta_file);
    if (!in) return false;
    try {
        in >> out_meta;
        return true;
    } catch (...) {
        return false;
    }
}

bool CacheManager::save_metadata(const std::string& meta_file, const nlohmann::json& meta) {
    try {
        ensure_dirs_for(meta_file);
        std::ofstream out(meta_file);
        out << meta.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

SDL_Surface* CacheManager::load_surface(const std::string& path) {
    return IMG_Load(path.c_str());
}

bool CacheManager::save_surface_as_png(SDL_Surface* surface, const std::string& path) {
    if (!surface) return false;
#if SDL_IMAGE_VERSION_ATLEAST(2,0,0)
    ensure_dirs_for(path);
    if (IMG_SavePNG(surface, path.c_str()) == 0) return true;
#endif
    std::string bmp = path;
    if (fs::path(path).extension() != ".bmp") {
        bmp = fs::path(path).replace_extension(".bmp").string();
    }
    ensure_dirs_for(bmp);
    return SDL_SaveBMP(surface, bmp.c_str()) == 0;
}

bool CacheManager::load_surface_sequence(const std::string& folder, int frame_count, std::vector<SDL_Surface*>& surfaces) {
    surfaces.clear();
    for (int i = 0; i < frame_count; ++i) {
        std::string png = folder + "/" + std::to_string(i) + ".png";
        std::string bmp = folder + "/" + std::to_string(i) + ".bmp";
        SDL_Surface* s = nullptr;
        if (fs::exists(png)) {
            s = IMG_Load(png.c_str());
        } else if (fs::exists(bmp)) {
            s = IMG_Load(bmp.c_str());
        } else {
            // Missing frame
            for (SDL_Surface* t : surfaces) if (t) SDL_FreeSurface(t);
            surfaces.clear();
            return false;
        }
        if (!s) {
            for (SDL_Surface* t : surfaces) if (t) SDL_FreeSurface(t);
            surfaces.clear();
            return false;
        }
        surfaces.push_back(s);
    }
    return true;
}

// Save sequence as PNGs. If PNG save fails, individual frames fall back to BMP.
bool CacheManager::save_surface_sequence(const std::string& folder, const std::vector<SDL_Surface*>& surfaces) {
    try {
        fs::remove_all(folder);
    } catch (...) {}
    try {
        fs::create_directories(folder);
    } catch (...) {}

    for (size_t i = 0; i < surfaces.size(); ++i) {
        std::string out_path = folder + "/" + std::to_string(i) + ".png";
        if (!save_surface_as_png(surfaces[i], out_path)) {
            return false;
        }
    }
    return true;
}

SDL_Surface* CacheManager::load_and_scale_surface(const std::string& path, float scale, int& out_w, int& out_h) {
    out_w = 0; out_h = 0;

    if (scale <= 0.0f) scale = 1.0f;

    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) return nullptr;

    SDL_Surface* src = to_rgba8888(loaded);
    SDL_FreeSurface(loaded);
    if (!src) return nullptr;

    const int src_w = src->w;
    const int src_h = src->h;

    int final_w = std::max(1, static_cast<int>(std::lround(src_w * scale)));
    int final_h = std::max(1, static_cast<int>(std::lround(src_h * scale)));

    // If basically 1:1, return a copy in RGBA8888
    if (final_w == src_w && final_h == src_h) {
        out_w = src_w; out_h = src_h;
        return src; // already RGBA8888 copy
    }

    set_best_scale_hint();

    auto make_rgba = [](int w, int h) {
        return SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA8888);
    };

    SDL_Surface* current = src;          // owned
    int cur_w = src_w;
    int cur_h = src_h;

    // For strong downscales, shrink by halves to reduce ringing and aliasing
    const float down_ratio = std::min(final_w / float(src_w), final_h / float(src_h));
    if (down_ratio < 0.5f) {
        while (true) {
            int next_w = std::max(1, cur_w / 2);
            int next_h = std::max(1, cur_h / 2);
            // Stop if halving goes too far below target
            if (next_w < final_w * 1.25f || next_h < final_h * 1.25f) break;

            SDL_Surface* half = make_rgba(next_w, next_h);
            if (!half) break;

            SDL_Rect srect{0, 0, cur_w, cur_h};
            SDL_Rect drect{0, 0, next_w, next_h};
            if (SDL_BlitScaled(current, &srect, half, &drect) != 0) {
                SDL_FreeSurface(half);
                break;
            }

            SDL_FreeSurface(current);
            current = half;
            cur_w = next_w;
            cur_h = next_h;

            if (cur_w <= std::max(1, int(final_w * 1.1f)) &&
                cur_h <= std::max(1, int(final_h * 1.1f))) {
                break;
            }
        }
    }

    SDL_Surface* dst = make_rgba(final_w, final_h);
    if (!dst) {
        SDL_FreeSurface(current);
        return nullptr;
    }

    SDL_Rect srect{0, 0, cur_w, cur_h};
    SDL_Rect drect{0, 0, final_w, final_h};
    if (SDL_BlitScaled(current, &srect, dst, &drect) != 0) {
        SDL_FreeSurface(current);
        SDL_FreeSurface(dst);
        return nullptr;
    }

    SDL_FreeSurface(current);

    out_w = final_w;
    out_h = final_h;
    return dst; // caller frees
}

SDL_Texture* CacheManager::surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface) {
    if (!renderer || !surface) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeBest);
#endif
    }
    return tex;
}

std::vector<SDL_Texture*> CacheManager::surfaces_to_textures(SDL_Renderer* renderer, const std::vector<SDL_Surface*>& surfaces) {
    std::vector<SDL_Texture*> textures;
    textures.reserve(surfaces.size());
    for (SDL_Surface* surf : surfaces) {
        SDL_Texture* tex = surface_to_texture(renderer, surf);
        if (tex) textures.push_back(tex);
    }
    return textures;
}
