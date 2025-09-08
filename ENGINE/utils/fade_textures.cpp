#include "fade_textures.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
template <typename T>
T clamp(T value, T min_val, T max_val) {
    return std::max(min_val, std::min(value, max_val));
}

SDL_Surface* blurSurfaceFast(SDL_Surface* src, int radius = 3) {
    if (!src || radius <= 0) return src;
    SDL_Surface* dest = SDL_ConvertSurface(src, src->format, 0);
    if (!dest) return src;
    int w = src->w, h = src->h;
    Uint32* in = (Uint32*)src->pixels;
    Uint32* out = (Uint32*)dest->pixels;
    int pitch = src->pitch / 4;
    std::vector<uint64_t> sumR((w+1)*(h+1), 0);
    std::vector<uint64_t> sumG((w+1)*(h+1), 0);
    std::vector<uint64_t> sumB((w+1)*(h+1), 0);
    std::vector<uint64_t> sumA((w+1)*(h+1), 0);
    auto idx = [&](int x, int y) { return y * (w + 1) + x; };
    for (int y = 1; y <= h; ++y) {
        for (int x = 1; x <= w; ++x) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(in[(y-1)*pitch + (x-1)], src->format, &r, &g, &b, &a);
            sumR[idx(x, y)] = r + sumR[idx(x-1, y)] + sumR[idx(x, y-1)] - sumR[idx(x-1, y-1)];
            sumG[idx(x, y)] = g + sumG[idx(x-1, y)] + sumG[idx(x, y-1)] - sumG[idx(x-1, y-1)];
            sumB[idx(x, y)] = b + sumB[idx(x-1, y)] + sumB[idx(x, y-1)] - sumB[idx(x-1, y-1)];
            sumA[idx(x, y)] = a + sumA[idx(x-1, y)] + sumA[idx(x, y-1)] - sumA[idx(x-1, y-1)];
        }
    }
    for (int y = 0; y < h; ++y) {
        int y0 = std::max(0, y - radius);
        int y1 = std::min(h, y + radius + 1);
        for (int x = 0; x < w; ++x) {
            int x0 = std::max(0, x - radius);
            int x1 = std::min(w, x + radius + 1);
            int area = (x1 - x0) * (y1 - y0);
            if (area == 0) area = 1;
            uint64_t r = sumR[idx(x1, y1)] - sumR[idx(x0, y1)] - sumR[idx(x1, y0)] + sumR[idx(x0, y0)];
            uint64_t g = sumG[idx(x1, y1)] - sumG[idx(x0, y1)] - sumG[idx(x1, y0)] + sumG[idx(x0, y0)];
            uint64_t b = sumB[idx(x1, y1)] - sumB[idx(x0, y1)] - sumB[idx(x1, y0)] + sumB[idx(x0, y0)];
            uint64_t a = sumA[idx(x1, y1)] - sumA[idx(x0, y1)] - sumA[idx(x1, y0)] + sumA[idx(x0, y0)];
            out[y * pitch + x] = SDL_MapRGBA(src->format, r / area, g / area, b / area, a / area);
        }
    }
    return dest;
}

FadeTextureGenerator::FadeTextureGenerator(SDL_Renderer* renderer, SDL_Color color, double expand)
    : renderer_(renderer), color_(color), expand_(expand) {}

std::vector<std::pair<SDL_Texture*, SDL_Rect>> FadeTextureGenerator::generate_all(const std::vector<Area>& areas) {
    std::vector<std::pair<SDL_Texture*, SDL_Rect>> results;
    size_t index = 0;
    for (const Area& area : areas) {
        std::cout << "  [FadeGen " << index << "] Starting...\n";
        auto [ominx, ominy, omaxx, omaxy] = area.get_bounds();
        int ow = omaxx - ominx + 1;
        int oh = omaxy - ominy + 1;
        if (ow <= 0 || oh <= 0) {
            std::cout << "    [FadeGen " << index << "] Invalid area bounds; skipping.\n";
            ++index;
            continue;
        }
        float base_expand = 0.2f * static_cast<float>(std::min(ow, oh));
        base_expand = std::max(base_expand, 1.0f);
        int fw = static_cast<int>(std::ceil(base_expand * expand_));
        int minx = ominx - fw;
        int miny = ominy - fw;
        int maxx = omaxx + fw;
        int maxy = omaxy + fw;
        int w = maxx - minx + 1;
        int h = maxy - miny + 1;
        if (w <= 0 || h <= 0) {
            std::cout << "    [FadeGen " << index << "] Invalid final size; skipping.\n";
            ++index;
            continue;
        }
        std::vector<std::pair<double, double>> poly;
        for (auto& [x, y] : area.get_points())
            poly.emplace_back(x - minx, y - miny);
        auto point_in_poly = [&](double px, double py) {
            bool inside = false;
            size_t n = poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                auto [xi, yi] = poly[i];
                auto [xj, yj] = poly[j];
                bool intersect = ((yi > py) != (yj > py)) &&
                                 (px < (xj - xi) * (py - yi) / (yj - yi + 1e-9) + xi);
                if (intersect) inside = !inside;
            }
            return inside;
        };
        SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!tex) {
            std::cout << "    [FadeGen " << index << "] Texture creation failed; skipping.\n";
            ++index;
            continue;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer_, tex);
        SDL_SetRenderDrawColor(renderer_, color_.r, color_.g, color_.b, color_.a);
        SDL_RenderClear(renderer_);
        const float fade_radius = static_cast<float>(fw + 250);
        const int step = 25;
        for (int y = 0; y < h; y += step) {
            for (int x = 0; x < w; x += step) {
                double gx = x + 0.5;
                double gy = y + 0.5;
                bool inside = point_in_poly(gx, gy);
                float alpha = 0.0f;
                if (inside) {
                    alpha = 1.0f;
                } else {
                    float cx = static_cast<float>(ominx + ow / 2 - minx);
                    float cy = static_cast<float>(ominy + oh / 2 - miny);
                    float dx = gx - cx;
                    float dy = gy - cy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    float falloff = 1.0f - clamp(dist / fade_radius, 0.0f, 1.0f);
                    alpha = falloff * falloff;
                }
                if (alpha > 0.01f) {
                    Uint8 a = static_cast<Uint8>(clamp(alpha, 0.0f, 1.0f) * 255);
                    SDL_SetRenderDrawColor(renderer_, color_.r, color_.g, color_.b, a);
                    for (int dy = 0; dy < step; ++dy)
                        for (int dx = 0; dx < step; ++dx)
                            SDL_RenderDrawPoint(renderer_, x + dx, y + dy);
                }
            }
        }
        SDL_Surface* raw = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_SetRenderTarget(renderer_, tex);
        SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA32, raw->pixels, raw->pitch);
        SDL_SetRenderTarget(renderer_, nullptr);
        SDL_Surface* blurred = blurSurfaceFast(raw, 3);
        SDL_FreeSurface(raw);
        SDL_Texture* blurredTex = SDL_CreateTextureFromSurface(renderer_, blurred);
        SDL_FreeSurface(blurred);
        SDL_SetTextureBlendMode(blurredTex, SDL_BLENDMODE_BLEND);
        SDL_Rect dst = { minx, miny, w, h };
        results.emplace_back(blurredTex, dst);
        SDL_DestroyTexture(tex);
        std::cout << "    [FadeGen " << index << "] Texture stored. Size = " << w << "x" << h << "\n";
        ++index;
    }
    return results;
}
