#include "transparency_sampling.hpp"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

struct RenderTargetReset {
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  previous = nullptr;
    ~RenderTargetReset() {
        if (renderer) {
            SDL_SetRenderTarget(renderer, previous);
        }
    }
};

std::atomic<std::uint64_t>& attempts_counter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

std::atomic<std::uint64_t>& successes_counter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

std::atomic<std::uint64_t>& failures_counter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

std::atomic<std::uint64_t>& consecutive_failures_counter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

void record_result(bool success) {
    attempts_counter().fetch_add(1, std::memory_order_relaxed);
    if (success) {
        successes_counter().fetch_add(1, std::memory_order_relaxed);
        consecutive_failures_counter().store(0, std::memory_order_relaxed);
    } else {
        failures_counter().fetch_add(1, std::memory_order_relaxed);
        consecutive_failures_counter().fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace

namespace vibble::render {

TransparencySampleResult sample_texture_transparency(SDL_Renderer* renderer, SDL_Texture* texture) {
    TransparencySampleResult result{};
    if (!renderer) {
        result.error_message = "renderer unavailable";
        record_result(false);
        return result;
    }
    if (!texture) {
        result.error_message = "texture unavailable";
        record_result(false);
        return result;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0) {
        result.error_message = std::string("SDL_QueryTexture failed: ") + SDL_GetError();
        record_result(false);
        return result;
    }
    if (tex_w <= 0 || tex_h <= 0) {
        result.error_message = "texture has non-positive dimensions";
        record_result(false);
        return result;
    }

    const std::size_t total_pixels =
        static_cast<std::size_t>(tex_w) * static_cast<std::size_t>(tex_h);
    if (total_pixels == 0) {
        result.error_message = "texture reported zero pixels";
        record_result(false);
        return result;
    }
    if (tex_w != 0 && total_pixels / static_cast<std::size_t>(tex_w) != static_cast<std::size_t>(tex_h)) {
        result.error_message = "texture dimensions overflow sample area";
        record_result(false);
        return result;
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        result.error_message = std::string("SDL_SetRenderTarget failed: ") + SDL_GetError();
        record_result(false);
        return result;
    }
    RenderTargetReset reset{renderer, previous_target};

    std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> fmt(
        SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888), &SDL_FreeFormat);
    if (!fmt) {
        result.error_message = "SDL_AllocFormat failed";
        record_result(false);
        return result;
    }

    constexpr int kMaxReadTileEdge = 256;
    std::vector<std::uint32_t> tile_buffer;
    tile_buffer.reserve(static_cast<std::size_t>(kMaxReadTileEdge) * static_cast<std::size_t>(kMaxReadTileEdge));

    double accum_transparency = 0.0;
    double min_transparency   = 1.0;
    double max_transparency   = 0.0;
    bool   have_sample        = false;
    const double inv_255      = 1.0 / 255.0;

    for (int y = 0; y < tex_h; y += kMaxReadTileEdge) {
        const int tile_h = std::min(kMaxReadTileEdge, tex_h - y);
        for (int x = 0; x < tex_w; x += kMaxReadTileEdge) {
            const int tile_w = std::min(kMaxReadTileEdge, tex_w - x);
            const std::size_t tile_pixels =
                static_cast<std::size_t>(tile_w) * static_cast<std::size_t>(tile_h);

            tile_buffer.resize(tile_pixels);

            SDL_Rect rect{x, y, tile_w, tile_h};
            const int pitch = tile_w * static_cast<int>(sizeof(std::uint32_t));
            if (SDL_RenderReadPixels(renderer,
                                     &rect,
                                     SDL_PIXELFORMAT_RGBA8888,
                                     tile_buffer.data(),
                                     pitch) != 0) {
                result.error_message = std::string("SDL_RenderReadPixels failed: ") + SDL_GetError();
                record_result(false);
                return result;
            }

            for (std::uint32_t pixel : tile_buffer) {
                Uint8 alpha = 255;
                SDL_GetRGBA(pixel, fmt.get(), nullptr, nullptr, nullptr, &alpha);
                const double transparency = 1.0 - static_cast<double>(alpha) * inv_255;
                accum_transparency += transparency;
                if (!have_sample) {
                    min_transparency = transparency;
                    max_transparency = transparency;
                    have_sample      = true;
                } else {
                    min_transparency = std::min(min_transparency, transparency);
                    max_transparency = std::max(max_transparency, transparency);
                }
            }
        }
    }

    if (!have_sample) {
        result.error_message = "no pixels sampled";
        record_result(false);
        return result;
    }

    const double average = accum_transparency / static_cast<double>(total_pixels);
    result.average       = static_cast<float>(std::clamp(average, 0.0, 1.0));
    result.min           = static_cast<float>(std::clamp(min_transparency, 0.0, 1.0));
    result.max           = static_cast<float>(std::clamp(max_transparency, 0.0, 1.0));
    result.success       = true;
    record_result(true);
    return result;
}

TransparencyReadbackStats transparency_readback_stats() {
    TransparencyReadbackStats stats{};
    stats.attempts             = attempts_counter().load(std::memory_order_relaxed);
    stats.successes            = successes_counter().load(std::memory_order_relaxed);
    stats.failures             = failures_counter().load(std::memory_order_relaxed);
    stats.consecutive_failures = consecutive_failures_counter().load(std::memory_order_relaxed);
    return stats;
}

void reset_transparency_readback_stats() {
    attempts_counter().store(0, std::memory_order_relaxed);
    successes_counter().store(0, std::memory_order_relaxed);
    failures_counter().store(0, std::memory_order_relaxed);
    consecutive_failures_counter().store(0, std::memory_order_relaxed);
}

}  // namespace vibble::render

