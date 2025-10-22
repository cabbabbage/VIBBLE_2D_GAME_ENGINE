// Unified Chunk + LightMap implementation
#include "world/chunk.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "utils/log.hpp"

#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "render/transparency_sampling.hpp"
#include "render/global_light_source.hpp"
#include "world/grid.hpp"

namespace world {

Chunk::~Chunk() {
    releaseLightingArtifacts();
}

void Chunk::releaseLightingArtifacts() {
    if (static_light_mask) {
        SDL_DestroyTexture(static_light_mask);
        static_light_mask = nullptr;
    }
    lighting_preloaded = false;
    static_clean       = false;
    needs_retry        = true;
}

float static_brightness_for_opacity(const Chunk& chunk, float screen_opacity) {
    const float t = std::clamp(screen_opacity, 0.0f, 1.0f);

    float min_strength = chunk.lighting.min_static_avg_strength;
    float max_strength = chunk.lighting.max_static_avg_strength;

    const bool min_ready = std::isfinite(min_strength);
    const bool max_ready = std::isfinite(max_strength);

    auto log_fallback = [&](const char* reason) {
        vibble::log::debug(std::string{"[Lighting] static brightness fallback for chunk("} +
                           std::to_string(chunk.i) + "," + std::to_string(chunk.j) + "): " + reason);
    };

    if (!min_ready && !max_ready) {
        log_fallback("missing min and max values");
        Chunk& mutable_chunk = const_cast<Chunk&>(chunk);
        mutable_chunk.lighting.needs_update = true;
        mutable_chunk.needs_retry          = true;
        mutable_chunk.static_clean         = false;
        return 0.0f;
    }

    if (!min_ready) {
        log_fallback("missing min value");
        return std::clamp(max_strength, 0.0f, 1.0f);
    }

    if (!max_ready) {
        log_fallback("missing max value");
        return std::clamp(min_strength, 0.0f, 1.0f);
    }

    min_strength = std::clamp(min_strength, 0.0f, 1.0f);
    max_strength = std::clamp(max_strength, 0.0f, 1.0f);
    if (max_strength < min_strength) {
        std::swap(min_strength, max_strength);
    }

    const float brightness = min_strength + (max_strength - min_strength) * t;
    return std::clamp(brightness, 0.0f, 1.0f);
}

} // namespace world

namespace {
constexpr const char* kEnableChunkLightingEnv  = "VIBBLE_ENABLE_CHUNK_LIGHTING";
constexpr const char* kDisableChunkLightingEnv = "VIBBLE_DISABLE_CHUNK_LIGHTING";

bool env_truthy(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '1' || c == 'y' || c == 'Y' || c == 't' || c == 'T';
}

bool env_falsey(const char* value) {
    if (!value || !value[0]) {
        return false;
    }
    const char c = value[0];
    return c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F';
}

bool chunk_lighting_suspended_flag() {
    if (env_truthy(std::getenv(kDisableChunkLightingEnv))) {
        return true;
    }
    if (const char* value = std::getenv(kEnableChunkLightingEnv)) {
        if (env_falsey(value)) {
            return true;
        }
        if (env_truthy(value)) {
            return false;
        }
    }
    return false;
}

Uint8 clamp_alpha(float value) {
    return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

template <typename Callback>
void for_each_static_light_draw(SDL_Renderer* renderer,
                                const Assets* assets,
                                const world::Chunk& chunk,
                                Callback&& callback) {
    if (!renderer || !assets) {
        return;
    }

    const auto& static_lights = assets->getActiveStaticLightAssets();
    for (const Asset* asset : static_lights) {
        if (!asset || !asset->info) {
            continue;
        }
        if (asset->info->light_sources.empty()) {
            continue;
        }

        for (const auto& light : asset->info->light_sources) {
            SDL_Texture* tex = light.texture;
            if (!tex) {
                continue;
            }

            int src_w = light.cached_w > 0 ? light.cached_w : 0;
            int src_h = light.cached_h > 0 ? light.cached_h : 0;
            if (src_w <= 0 || src_h <= 0) {
                SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
            }
            if (src_w <= 0 || src_h <= 0) {
                continue;
            }

            const int draw_w = std::max(1, src_w);
            const int draw_h = std::max(1, src_h);

            SDL_Point world_center{asset->pos.x + light.offset_x, asset->pos.y + light.offset_y};
            SDL_Rect  world_dst{world_center.x - draw_w / 2,
                                world_center.y - draw_h / 2,
                                draw_w,
                                draw_h};

            SDL_Rect intersection{};
            if (!SDL_IntersectRect(&world_dst, &chunk.world_bounds, &intersection)) {
                continue;
            }

            SDL_Rect src_rect{};
            src_rect.x = std::clamp(intersection.x - world_dst.x, 0, std::max(0, draw_w));
            src_rect.y = std::clamp(intersection.y - world_dst.y, 0, std::max(0, draw_h));
            src_rect.w = intersection.w;
            src_rect.h = intersection.h;

            if (src_rect.x >= draw_w || src_rect.y >= draw_h) {
                continue;
            }

            src_rect.w = std::min(src_rect.w, draw_w - src_rect.x);
            src_rect.h = std::min(src_rect.h, draw_h - src_rect.y);

            SDL_Rect local_dst{};
            local_dst.x = std::max(0, intersection.x - chunk.world_bounds.x);
            local_dst.y = std::max(0, intersection.y - chunk.world_bounds.y);
            local_dst.w = src_rect.w;
            local_dst.h = src_rect.h;

            if (src_rect.w <= 0 || src_rect.h <= 0 || local_dst.w <= 0 || local_dst.h <= 0) {
                continue;
            }

            callback(tex, src_rect, local_dst);
        }
    }
}

class RenderTargetSetter {
public:
    RenderTargetSetter(SDL_Renderer* renderer, SDL_Texture* target) : renderer_(renderer) {
        if (!renderer_) {
            return;
        }
        previous_ = SDL_GetRenderTarget(renderer_);
        if (SDL_SetRenderTarget(renderer_, target) == 0) {
            valid_ = true;
        }
    }

    RenderTargetSetter(const RenderTargetSetter&) = delete;
    RenderTargetSetter& operator=(const RenderTargetSetter&) = delete;

    RenderTargetSetter(RenderTargetSetter&& other) noexcept
        : renderer_(other.renderer_), previous_(other.previous_), valid_(other.valid_) {
        other.renderer_ = nullptr;
        other.previous_ = nullptr;
        other.valid_    = false;
    }

    RenderTargetSetter& operator=(RenderTargetSetter&& other) noexcept {
        if (this != &other) {
            restore();
            renderer_ = other.renderer_;
            previous_ = other.previous_;
            valid_    = other.valid_;
            other.renderer_ = nullptr;
            other.previous_ = nullptr;
            other.valid_    = false;
        }
        return *this;
    }

    ~RenderTargetSetter() { restore(); }

    bool valid() const { return valid_; }

private:
    void restore() {
        if (renderer_) {
            SDL_SetRenderTarget(renderer_, previous_);
            renderer_ = nullptr;
            previous_ = nullptr;
            valid_    = false;
        }
    }

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  previous_ = nullptr;
    bool          valid_    = false;
};

class ChunkBrightnessPreviewBuilder {
public:
    ChunkBrightnessPreviewBuilder(SDL_Renderer* renderer,
                                  const Assets* assets,
                                  world::Chunk& chunk)
        : renderer_(renderer), assets_(assets), chunk_(chunk) {}

    bool build(float& out_min_strength, float& out_max_strength) {
        width_  = std::max(1, chunk_.world_bounds.w);
        height_ = std::max(1, chunk_.world_bounds.h);
        if (width_ <= 0 || height_ <= 0) {
            return false;
        }
        if (!create_textures()) {
            return false;
        }
        if (!render_base()) {
            return false;
        }
        const float max_avg = compute_average(base_texture_.get());
        if (!std::isfinite(max_avg) || max_avg < 0.0f) {
            return false;
        }
        if (!render_min()) {
            return false;
        }
        const float min_avg = compute_average(min_texture_.get());
        if (!std::isfinite(min_avg) || min_avg < 0.0f) {
            return false;
        }

        out_min_strength = std::clamp(min_avg, 0.0f, 1.0f);
        out_max_strength = std::clamp(max_avg, 0.0f, 1.0f);
        return true;
    }

private:
    class TexturePtr {
    public:
        TexturePtr() = default;
        explicit TexturePtr(SDL_Texture* texture) : texture_(texture) {}
        ~TexturePtr() { reset(); }

        TexturePtr(const TexturePtr&) = delete;
        TexturePtr& operator=(const TexturePtr&) = delete;

        TexturePtr(TexturePtr&& other) noexcept : texture_(other.texture_) { other.texture_ = nullptr; }

        TexturePtr& operator=(TexturePtr&& other) noexcept {
            if (this != &other) {
                reset();
                texture_ = other.texture_;
                other.texture_ = nullptr;
            }
            return *this;
        }

        SDL_Texture* get() const { return texture_; }

        explicit operator bool() const { return texture_ != nullptr; }

        SDL_Texture* release() {
            SDL_Texture* tmp = texture_;
            texture_ = nullptr;
            return tmp;
        }

        void reset(SDL_Texture* texture = nullptr) {
            if (texture_) {
                SDL_DestroyTexture(texture_);
            }
            texture_ = texture;
        }

    private:
        SDL_Texture* texture_ = nullptr;
    };

    bool create_textures() {
        TexturePtr base(SDL_CreateTexture(renderer_,
                                          SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET,
                                          width_,
                                          height_));
        if (!base) {
            vibble::log::warn(std::string("[LightMap] Failed to create base preview texture: ") + SDL_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(base.get(), SDL_BLENDMODE_BLEND);

        TexturePtr min(SDL_CreateTexture(renderer_,
                                         SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         width_,
                                         height_));
        if (!min) {
            vibble::log::warn(std::string("[LightMap] Failed to create min preview texture: ") + SDL_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(min.get(), SDL_BLENDMODE_BLEND);

        base_texture_ = std::move(base);
        min_texture_  = std::move(min);
        return true;
    }

    bool render_base() {
        RenderTargetSetter scope(renderer_, base_texture_.get());
        if (!scope.valid()) {
            vibble::log::warn(std::string("[LightMap] Failed to bind base preview target: ") + SDL_GetError());
            return false;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

        for_each_static_light_draw(renderer_, assets_, chunk_, [&](SDL_Texture* tex, const SDL_Rect& src, const SDL_Rect& dst) {
            Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
            SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
            SDL_GetTextureAlphaMod(tex, &save_a);
            SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(tex, &save_bm);

            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, 255);

            if (SDL_RenderCopy(renderer_, tex, &src, &dst) != 0) {
                vibble::log::warn(std::string("[LightMap] SDL_RenderCopy failed while building base preview: ") + SDL_GetError());
            }

            SDL_SetTextureBlendMode(tex, save_bm);
            SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
            SDL_SetTextureAlphaMod(tex, save_a);
        });

        return true;
    }

    bool render_min() {
        RenderTargetSetter scope(renderer_, min_texture_.get());
        if (!scope.valid()) {
            vibble::log::warn(std::string("[LightMap] Failed to bind min preview target: ") + SDL_GetError());
            return false;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

        if (SDL_RenderCopy(renderer_, base_texture_.get(), nullptr, nullptr) != 0) {
            vibble::log::warn(std::string("[LightMap] Failed to copy base into min preview: ") + SDL_GetError());
            return false;
        }

        if (SDL_Texture* mask = chunk_.static_light_mask) {
            Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
            SDL_GetTextureColorMod(mask, &save_r, &save_g, &save_b);
            SDL_GetTextureAlphaMod(mask, &save_a);
            SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(mask, &save_bm);

            SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(mask, 255, 255, 255);
            SDL_SetTextureAlphaMod(mask, 255);

            if (SDL_RenderCopy(renderer_, mask, nullptr, nullptr) != 0) {
                vibble::log::warn(std::string("[LightMap] Failed to overlay mask into min preview: ") + SDL_GetError());
            }

            SDL_SetTextureBlendMode(mask, save_bm);
            SDL_SetTextureColorMod(mask, save_r, save_g, save_b);
            SDL_SetTextureAlphaMod(mask, save_a);
        }

        return true;
    }

    float compute_average(SDL_Texture* texture) const {
        if (!texture) {
            return -1.0f;
        }
        RenderTargetSetter scope(renderer_, texture);
        if (!scope.valid()) {
            vibble::log::warn(std::string("[LightMap] Failed to bind texture for brightness sampling: ") + SDL_GetError());
            return -1.0f;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        if (pixel_count == 0) {
            return 0.0f;
        }

        std::vector<Uint32> pixels(pixel_count);
        if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA8888, pixels.data(), width_ * sizeof(Uint32)) != 0) {
            vibble::log::warn(std::string("[LightMap] SDL_RenderReadPixels failed during preview sampling: ") + SDL_GetError());
            return -1.0f;
        }

        SDL_PixelFormat* format = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
        if (!format) {
            vibble::log::warn("[LightMap] SDL_AllocFormat failed during preview sampling");
            return -1.0f;
        }

        double accum = 0.0;
        for (Uint32 pixel : pixels) {
            Uint8 r = 0, g = 0, b = 0, a = 0;
            SDL_GetRGBA(pixel, format, &r, &g, &b, &a);
            accum += (static_cast<double>(r) + static_cast<double>(g) + static_cast<double>(b)) / (3.0 * 255.0);
        }

        SDL_FreeFormat(format);
        return static_cast<float>(accum / static_cast<double>(pixel_count));
    }

private:
    SDL_Renderer* renderer_ = nullptr;
    const Assets* assets_    = nullptr;
    world::Chunk& chunk_;
    int width_  = 0;
    int height_ = 0;
    TexturePtr base_texture_{};
    TexturePtr min_texture_{};
};

SDL_Rect world_rect_from_screen(const camera& cam, const SDL_Rect& screen_rect) {
    SDL_Point top_left     = cam.screen_to_map({screen_rect.x, screen_rect.y});
    SDL_Point bottom_right = cam.screen_to_map({screen_rect.x + screen_rect.w, screen_rect.y + screen_rect.h});
    SDL_Rect result{};
    result.x = std::min(top_left.x, bottom_right.x);
    result.y = std::min(top_left.y, bottom_right.y);
    result.w = std::abs(bottom_right.x - top_left.x);
    result.h = std::abs(bottom_right.y - top_left.y);
    return result;
}

bool intersects(const SDL_Rect& a, const SDL_Rect& b) {
    return SDL_HasIntersection(&a, &b) == SDL_TRUE;
}

void log_transparency_failure(const char* context,
                              const world::Chunk& chunk,
                              const vibble::render::TransparencySampleResult& sample) {
    const auto stats = vibble::render::transparency_readback_stats();
    std::string message = std::string(context) +
                          " transparency readback failed for chunk(" + std::to_string(chunk.i) +
                          "," + std::to_string(chunk.j) + "): " +
                          (sample.error_message.empty() ? std::string("unknown error") : sample.error_message) +
                          " [attempts=" + std::to_string(stats.attempts) +
                          ", successes=" + std::to_string(stats.successes) +
                          ", failures=" + std::to_string(stats.failures) +
                          ", consecutive_failures=" + std::to_string(stats.consecutive_failures) + "] (using cached lighting)";
    vibble::log::warn(message);
}

void update_chunk_static_brightness_extrema(SDL_Renderer* renderer, const Assets* assets, world::Chunk& chunk) {
    if (!renderer || !assets) {
        chunk.needs_retry = true;
        return;
    }

    if (!chunk.static_light_mask) {
        chunk.needs_retry = true;
        return;
    }

    float min_strength = 0.0f;
    float max_strength = 0.0f;
    ChunkBrightnessPreviewBuilder builder(renderer, assets, chunk);
    if (!builder.build(min_strength, max_strength)) {
        chunk.needs_retry = true;
        return;
    }

    min_strength = std::clamp(min_strength, 0.0f, 1.0f);
    max_strength = std::clamp(std::max(min_strength, max_strength), 0.0f, 1.0f);

    chunk.lighting.min_static_avg_strength = min_strength;
    chunk.lighting.max_static_avg_strength = max_strength;
    chunk.lighting.needs_update            = true;
    chunk.static_clean                     = true;
    chunk.needs_retry                      = false;
}

template <typename T>
T lerp(T a, T b, float t) {
    return static_cast<T>(a + (b - a) * t);
}

std::pair<float, float> compute_brightness_gradient(const world::Chunk& center,
                                                    const world::Grid& grid,
                                                    int radius,
                                                    float falloff_x,
                                                    float falloff_y,
                                                    float screen_light_opacity) {
    if (radius <= 0) return {0.0f, 0.0f};
    const float cb = world::static_brightness_for_opacity(center, screen_light_opacity);
    float gx = 0.0f, gy = 0.0f;
    for (int dj = -radius; dj <= radius; ++dj) {
        for (int di = -radius; di <= radius; ++di) {
            if (di == 0 && dj == 0) continue;
            const int ni = center.i + di;
            const int nj = center.j + dj;
            const world::Chunk* n = grid.find_chunk_ij(ni, nj);
            const float nb = n ? world::static_brightness_for_opacity(*n, screen_light_opacity) : 0.0f;
            const float db = nb - cb;
            const float dx = static_cast<float>(di);
            const float dy = static_cast<float>(dj);
            const float dist = std::max(1.0f, std::sqrt(dx*dx + dy*dy));
            const float wx = falloff_x / dist;
            const float wy = falloff_y / dist;
            gx += db * (dx / dist) * wx;
            gy += db * (dy / dist) * wy;
        }
    }
    return {gx, gy};
}

// Compute average brightness in front of the chunk (negative j direction),
// adjusted by anisotropic horizontal/vertical falloff. Returns [0,1].
// Compute weighted averages of light strength in-front (negative j) and behind (positive j).
static std::pair<float, float> compute_directional_average_strengths(const LightMap::ShadowSettings& settings,
                                                                     const world::Grid& grid,
                                                                     const world::Chunk& center,
                                                                     float screen_light_opacity) {
    const int   R  = std::max(0, settings.search_radius_cells);
    const float fh = std::max(0.0f, settings.falloff_horizontal);
    const float fv = std::max(0.0f, settings.falloff_vertical);

    auto sample_dir = [&](int j_begin, int j_end) -> float {
        double accum_w = 0.0;
        double accum_v = 0.0;
        const int step = (j_begin <= j_end) ? 1 : -1;
        for (int dj = j_begin; dj != j_end + step; dj += step) {
            for (int di = -R; di <= R; ++di) {
                if (dj == 0) continue; // skip same row
                const int ni = center.i + di;
                const int nj = center.j + dj;
                const world::Chunk* n = grid.find_chunk_ij(ni, nj);
                if (!n) continue;
                const float sx = std::abs(static_cast<float>(di));
                const float sy = std::abs(static_cast<float>(dj));
                const float w  = 1.0f / (1.0f + sx * fh + sy * fv);
                const float s  = (n->lighting.is_active
                                      ? n->lighting.current_strength
                                      : world::static_brightness_for_opacity(*n, screen_light_opacity));
                accum_w += static_cast<double>(w);
                accum_v += static_cast<double>(w) * static_cast<double>(std::clamp(s, 0.0f, 1.0f));
            }
        }
        if (accum_w <= 1e-8) {
            return std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
        }
        return static_cast<float>(std::clamp(accum_v / accum_w, 0.0, 1.0));
    };

    const float front_avg  = (R > 0) ? sample_dir(-R, -1) : std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    const float behind_avg = (R > 0) ? sample_dir(1,  R)  : std::clamp(center.lighting.current_strength, 0.0f, 1.0f);
    return {front_avg, behind_avg};
}

static void compute_use_shadow_data_for_chunk(const LightMap::ShadowSettings& settings,
                                              const world::Grid& grid,
                                              const std::pair<float,float>& grad,
                                              int map_dir_sign_x,
                                              float map_light_opacity_norm,
                                              world::Chunk& chunk) {
    // Opacity: inverse of front average strength.
    const auto [front_avg, behind_avg] =
        compute_directional_average_strengths(settings, grid, chunk, map_light_opacity_norm);
    chunk.shadow.opacity  = std::clamp(1.0f - front_avg, 0.0f, 1.0f);

    // Scale: grow with front dominance, shrink with behind dominance (nonlinear towards min).
    const float d = std::clamp(front_avg - behind_avg, -1.0f, 1.0f); // [-1,1]
    const int min_p = std::clamp(settings.min_scale_percent, 50, 200);
    const int max_p = std::clamp(settings.max_scale_percent, 50, 200);
    const float base_p = 100.0f;
    float scale_percent = base_p;
    if (d >= 0.0f) {
        const float t = d; // more front light -> larger scale
        scale_percent = base_p + t * (static_cast<float>(max_p) - base_p);
    } else {
        const float b = -d; // more behind light -> smaller scale
        // Ease-out towards min: fast at first, slower as approaching min.
        const float ease_out = 1.0f - std::pow(1.0f - b, 2.0f); // gamma=2.0
        scale_percent = base_p - ease_out * (base_p - static_cast<float>(min_p));
    }
    scale_percent = std::clamp(scale_percent, static_cast<float>(min_p), static_cast<float>(max_p));
    chunk.shadow.scale = std::max(0.0f, scale_percent / 100.0f);

    // Base offset away from brightest direction (opposite brightness gradient)
    float gx = grad.first, gy = grad.second;
    float mag = std::sqrt(gx*gx + gy*gy);
    float nx = (mag > 1e-4f) ? (gx / mag) : 0.0f;
    float ny = (mag > 1e-4f) ? (gy / mag) : 0.0f;

    // Move opposite the gradient (away from brighter areas)
    float px = -nx * 100.0f;
    float py = -ny * 100.0f;

    // Map-light directional X adjustment: push away from map-light direction
    if (map_dir_sign_x != 0) {
        const float dir_push = std::clamp(map_light_opacity_norm, 0.0f, 1.0f) *
                               std::max(0.0f, settings.map_light_dir_offset_strength) * 100.0f;
        // If light direction points +X, push left (negative X), and vice-versa.
        px += static_cast<float>(-map_dir_sign_x) * dir_push;
    }

    chunk.shadow.offset_x_percent = std::clamp(px, -100.0f, 100.0f);
    chunk.shadow.offset_y_percent = std::clamp(py, -100.0f, 100.0f);

    chunk.shadow.parallax_intensity_percent = std::clamp(settings.parallax_percent, 0.0f, 100.0f);
}

} // namespace

namespace {

// Restores the renderer target when leaving the current scope.
class ChunkMaskRenderScope {
public:
    ChunkMaskRenderScope() = default;

    ChunkMaskRenderScope(SDL_Renderer* renderer, SDL_Texture* texture, SDL_Texture* previous)
        : renderer_(renderer), texture_(texture), previous_(previous) {}

    ChunkMaskRenderScope(const ChunkMaskRenderScope&) = delete;
    ChunkMaskRenderScope& operator=(const ChunkMaskRenderScope&) = delete;

    ChunkMaskRenderScope(ChunkMaskRenderScope&& other) noexcept
        : renderer_(other.renderer_), texture_(other.texture_), previous_(other.previous_), restored_(other.restored_) {
        other.renderer_ = nullptr;
        other.texture_  = nullptr;
        other.previous_ = nullptr;
        other.restored_ = true;
    }

    ChunkMaskRenderScope& operator=(ChunkMaskRenderScope&& other) noexcept {
        if (this != &other) {
            restore_target();
            renderer_ = other.renderer_;
            texture_  = other.texture_;
            previous_ = other.previous_;
            restored_ = other.restored_;
            other.renderer_ = nullptr;
            other.texture_  = nullptr;
            other.previous_ = nullptr;
            other.restored_ = true;
        }
        return *this;
    }

    ~ChunkMaskRenderScope() { restore_target(); }

    [[nodiscard]] bool valid() const { return renderer_ && texture_; }

    [[nodiscard]] SDL_Texture* texture() const { return texture_; }

    void restore_target() {
        if (!restored_ && renderer_) {
            SDL_SetRenderTarget(renderer_, previous_);
            restored_ = true;
        }
    }

private:
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    SDL_Texture*  previous_ = nullptr;
    bool          restored_ = false;
};

// Creates the chunk mask texture, prepares the render target, and clears it.
ChunkMaskRenderScope create_chunk_mask_texture(SDL_Renderer* renderer,
                                               world::Chunk& chunk,
                                               int width,
                                               int height) {
    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_TARGET,
                                             width,
                                             height);
    if (!texture) {
        vibble::log::warn(std::string("[LightMap] ensure_chunk_static_texture: SDL_CreateTexture failed: ") + SDL_GetError());
        chunk.releaseLightingArtifacts();
        chunk.lighting_dirty  = true;
        return {};
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_DestroyTexture(texture);
        SDL_SetRenderTarget(renderer, previous_target);
        vibble::log::warn(std::string("[LightMap] ensure_chunk_static_texture: SDL_SetRenderTarget failed: ") + SDL_GetError());
        chunk.releaseLightingArtifacts();
        chunk.lighting_dirty  = true;
        return {};
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    return ChunkMaskRenderScope(renderer, texture, previous_target);
}

// Stamps static light textures into the prepared chunk mask.
void stamp_static_lights_onto_mask(SDL_Renderer* renderer,
                                   const Assets* assets,
                                   const world::Chunk& chunk) {
#if SDL_VERSION_ATLEAST(2, 0, 6)
    const SDL_BlendMode erase_alpha_blend = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE,
        SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        SDL_BLENDOPERATION_ADD);
#else
    const SDL_BlendMode erase_alpha_blend = SDL_BLENDMODE_ADD;
#endif

    if (!assets) {
        return;
    }

    vibble::log::debug(std::string("[LightMap] ensure_chunk_static_texture: stamping static lights into light mask for chunk(") +
                       std::to_string(chunk.i) + "," + std::to_string(chunk.j) + ")");

    for_each_static_light_draw(renderer, assets, chunk, [&](SDL_Texture* tex, const SDL_Rect& src_rect, const SDL_Rect& dst_rect) {
        Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
        SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
        SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
        SDL_GetTextureAlphaMod(tex, &save_a);
        SDL_GetTextureBlendMode(tex, &save_bm);

        SDL_SetTextureBlendMode(tex, erase_alpha_blend);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);

        if (SDL_RenderCopy(renderer, tex, &src_rect, &dst_rect) != 0) {
            vibble::log::warn(std::string("[LightMap] SDL_RenderCopy failed while stamping static light: ") + SDL_GetError());
        }

        SDL_SetTextureBlendMode(tex, save_bm);
        SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
        SDL_SetTextureAlphaMod(tex, save_a);
    });
}

// Finalizes the chunk mask texture and updates cached lighting metadata.
void finalize_chunk_mask_texture(SDL_Renderer* renderer,
                                 const Assets* assets,
                                 world::Chunk& chunk,
                                 ChunkMaskRenderScope& scope) {
    scope.restore_target();

    chunk.releaseLightingArtifacts();
    chunk.static_light_mask  = scope.texture();
    chunk.lighting_preloaded = (chunk.static_light_mask != nullptr);
    chunk.lighting_dirty     = false;
    update_chunk_static_brightness_extrema(renderer, assets, chunk);
    vibble::log::debug(std::string("[LightMap] ensure_chunk_static_texture: COMPLETE light mask for chunk(") +
                       std::to_string(chunk.i) + "," + std::to_string(chunk.j) + ") static range=" +
                       std::to_string(chunk.lighting.min_static_avg_strength) + "-" +
                       std::to_string(chunk.lighting.max_static_avg_strength));
}

} // namespace

// LightMap implementation
// ctor/dtor inlined in header

void LightMap::destroy_chunk_texture(world::Chunk& chunk) const {
    chunk.releaseLightingArtifacts();
    chunk.lighting_dirty        = true;
    chunk.lighting.needs_update = true;
    chunk.static_clean          = false;
    chunk.needs_retry           = true;
}

void LightMap::ensure_chunk_static_texture(SDL_Renderer* renderer, world::Chunk& chunk) const {
    if (!renderer) {
        vibble::log::debug("[LightMap] ensure_chunk_static_texture: missing renderer");
        return;
    }
    if (chunk_lighting_suspended_flag()) {
        return;
    }
    if (!chunk.lighting_dirty && chunk.lighting_preloaded) {
        return;
    }

    destroy_chunk_texture(chunk);

    const int width  = std::max(1, chunk.world_bounds.w);
    const int height = std::max(1, chunk.world_bounds.h);
    vibble::log::debug(std::string("[LightMap] ensure_chunk_static_texture: CREATE light mask size=") +
                       std::to_string(width) + "x" + std::to_string(height) + " for chunk(" +
                       std::to_string(chunk.i) + "," + std::to_string(chunk.j) + ")");

    ChunkMaskRenderScope mask_scope = create_chunk_mask_texture(renderer, chunk, width, height);
    if (!mask_scope.valid()) {
        return;
    }

    stamp_static_lights_onto_mask(renderer, assets_, chunk);

    finalize_chunk_mask_texture(renderer, assets_, chunk, mask_scope);
}

void LightMap::rebuild(SDL_Renderer* /*renderer*/) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    world::Grid& grid = assets_->world_grid();
    for (world::Chunk* chunk : grid.active_chunks()) {
        if (!chunk) {
            continue;
        }
        chunk->lighting_dirty        = true;
        chunk->lighting_preloaded    = false;
        chunk->lighting.needs_update = true;
        chunk->static_clean          = false;
        chunk->needs_retry           = true;
    }
}

void LightMap::update(SDL_Renderer* renderer, std::uint32_t /*delta_ms*/) {
    std::scoped_lock lock(mutex_);
    if (chunk_lighting_suspended_flag()) {
        return;
    }
    const auto& chunks = active_chunks();
    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        ensure_chunk_static_texture(renderer, *chunk);
        if (chunk->static_light_mask && chunk->needs_retry) {
            update_chunk_static_brightness_extrema(renderer, assets_, *chunk);
        }
    }

    if (!assets_) {
        return;
    }

    float screen_light_opacity = 0.0f;
    {
        const Global_Light_Source* gl = assets_->map_light_source();
        if (gl) {
            const int min_a = gl->min_opacity();
            const int max_a = gl->max_opacity();
            const int cur_a = std::clamp(static_cast<int>(gl->get_current_color().a), min_a, max_a);
            const int range = std::max(1, max_a - min_a);
            screen_light_opacity = std::clamp(static_cast<float>(cur_a - min_a) / static_cast<float>(range), 0.0f, 1.0f);
        }
    }

    const bool screen_changed = (std::abs(screen_light_opacity - last_screen_light_opacity_) > 1e-4f);
    last_screen_light_opacity_ = screen_light_opacity;

    int min_i = INT32_MAX, max_i = INT32_MIN, min_j = INT32_MAX, max_j = INT32_MIN;
    for (const world::Chunk* c : chunks) {
        if (!c) continue;
        min_i = std::min(min_i, c->i); max_i = std::max(max_i, c->i);
        min_j = std::min(min_j, c->j); max_j = std::max(max_j, c->j);
    }

    std::unordered_set<world::Chunk*> dedupe_set;
    dedupe_set.reserve(chunks.size() * 2);
    std::vector<world::Chunk*> border_candidates;
    border_candidates.reserve(chunks.size());

    auto add_candidate = [&](world::Chunk* c) {
        if (!c) {
            return;
        }
        // Track chunk membership in O(1) so we avoid repeated linear scans when
        // constructing the final update list.
        dedupe_set.insert(c);
    };
    for (world::Chunk* c : chunks) {
        add_candidate(c);
    }

    const world::Grid& grid = assets_->world_grid();
    for (world::Chunk* c : chunks) {
        if (!c) continue;
        const bool is_edge = (c->i == min_i) || (c->i == max_i) || (c->j == min_j) || (c->j == max_j);
        if (!is_edge) continue;
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                if (world::Chunk* n = grid.find_chunk_ij(c->i + di, c->j + dj)) {
                    border_candidates.push_back(n);
                    add_candidate(n);
                }
            }
        }
    }

    std::vector<world::Chunk*> update_set;
    update_set.reserve(dedupe_set.size());
    // Revisit the deterministic input sequences so the final processing order
    // matches the previous behaviour while still benefiting from O(1) deduping.
    for (world::Chunk* c : chunks) {
        if (dedupe_set.erase(c) > 0) {
            update_set.push_back(c);
        }
    }
    for (world::Chunk* c : border_candidates) {
        if (dedupe_set.erase(c) > 0) {
            update_set.push_back(c);
        }
    }

    const auto& moving = assets_->getActiveMovingLightAssets();
    for (world::Chunk* chunk : update_set) {
        if (!chunk) continue;
        chunk->lighting.is_active = true;
        if (screen_changed) chunk->lighting.needs_update = true;

        bool occupied = false;
        for (const Asset* a : moving) {
            if (!a) continue;
            SDL_Point p{a->pos.x, a->pos.y};
            if (SDL_PointInRect(&p, &chunk->world_bounds)) { occupied = true; break; }
        }
        if (occupied != chunk->lighting.is_occupied_by_moving_source) {
            chunk->lighting.needs_update = true;
        }
        chunk->lighting.is_occupied_by_moving_source = occupied;

        if (!chunk->lighting.needs_update) continue;

        if (chunk->lighting.is_occupied_by_moving_source) {
            float static_avg = world::static_brightness_for_opacity(*chunk, screen_light_opacity);
            if (chunk->static_light_mask) {
                const auto sample = vibble::render::sample_texture_transparency(renderer, chunk->static_light_mask);
                if (sample.success) {
                    static_avg = std::clamp(sample.average, 0.0f, 1.0f);
                } else {
                    log_transparency_failure("[LightMap] moving", *chunk, sample);
                    chunk->needs_retry = true;
                }
            } else {
                chunk->needs_retry = true;
            }
            chunk->lighting.current_strength = static_avg;
        } else {
            chunk->lighting.current_strength =
                world::static_brightness_for_opacity(*chunk, screen_light_opacity);
        }

        const ShadowSettings settings{};
        const int   radius = std::max(0, settings.search_radius_cells);
        const float fx     = std::max(0.0f, settings.falloff_horizontal);
        const float fy     = std::max(0.0f, settings.falloff_vertical);
        const auto grad    = compute_brightness_gradient(*chunk, grid, radius, fx, fy, screen_light_opacity);
        int map_dir_sign_x = 0;
        if (const Global_Light_Source* gl = assets_->map_light_source()) {
            const SDL_Point ref = gl->get_direction_reference();
            const SDL_Point tgt = gl->get_direction_target();
            const int dx = tgt.x - ref.x;
            map_dir_sign_x = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
        }
        compute_use_shadow_data_for_chunk(settings, grid, grad, map_dir_sign_x, screen_light_opacity, *chunk);

        chunk->lighting.needs_update = false;
    }
}

float LightMap::sample_brightness(int world_x,
                                  int world_y,
                                  float static_weight,
                                  float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    (void)dynamic_weight;
    world::Chunk* chunk = ensure_chunk_from_world(SDL_Point{world_x, world_y});
    if (!chunk) {
        vibble::log::warn("[LightMap] sample_brightness missing chunk for world point (" +
                          std::to_string(world_x) + ", " + std::to_string(world_y) + ")");
        return 1.0f;
    }
    const float weight           = std::clamp(static_weight, 0.0f, 1.0f);
    const float static_component = world::static_brightness_for_opacity(*chunk, last_screen_light_opacity_);
    return std::clamp(static_component * weight, 0.0f, 1.0f);
}

float LightMap::sample_brightness_bilinear(float world_x,
                                           float world_y,
                                           float static_weight,
                                           float dynamic_weight) const {
    std::scoped_lock lock(mutex_);
    return sample_brightness(static_cast<int>(std::lround(world_x)),
                             static_cast<int>(std::lround(world_y)),
                             static_weight,
                             dynamic_weight);
}

void LightMap::present_static_previews(SDL_Renderer* renderer) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }

    world::Grid& grid = assets_->world_grid();
    std::vector<world::Chunk*> chunks = grid.all_chunks();
    if (chunks.empty()) {
        return;
    }

    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        ensure_chunk_static_texture(renderer, *chunk);
        if (chunk->static_light_mask && chunk->needs_retry) {
            update_chunk_static_brightness_extrema(renderer, assets_, *chunk);
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    constexpr int kColumns = 1;
    constexpr int kPadding = 8;
    int column_x   = kPadding;
    int column_y   = kPadding;
    int column_max = 0;

    for (world::Chunk* chunk : chunks) {
        if (!chunk) {
            continue;
        }
        SDL_Texture* previews[kColumns] = {
            chunk->static_light_mask,
        };
        if (!previews[0]) {
            continue;
        }

        const int chunk_w = std::max(1, chunk->world_bounds.w);
        const int chunk_h = std::max(1, chunk->world_bounds.h);
        const int required_width = kColumns * chunk_w + (kColumns - 1) * kPadding;

        if (column_y + chunk_h > screen_height_) {
            column_y   = kPadding;
            column_x  += column_max + kPadding;
            column_max = 0;
        }

        if (column_x + required_width > screen_width_) {
            break;
        }

        column_max = std::max(column_max, required_width);

        for (int i = 0; i < kColumns; ++i) {
            SDL_Texture* tex = previews[i];
            if (!tex) {
                continue;
            }
            SDL_Rect dst{column_x + i * (chunk_w + kPadding), column_y, chunk_w, chunk_h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
        }

        column_y += chunk_h + kPadding;
    }

    SDL_RenderPresent(renderer);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer, const SDL_Rect& view_rect) const {
    render_visible_chunks(renderer, view_rect, 1.0f);
}

void LightMap::render_visible_chunks(SDL_Renderer* renderer,
                                        const SDL_Rect& view_rect,
                                        float alpha_multiplier) const {
    std::scoped_lock lock(mutex_);
    if (!renderer || !assets_) {
        return;
    }
    const camera& cam = assets_->getView();

    SDL_Rect world_view = world_rect_from_screen(cam, view_rect);

    for (world::Chunk* chunk : active_chunks()) {
        if (!chunk || !chunk->static_light_mask) {
            continue;
        }
        if (!intersects(chunk->world_bounds, world_view)) {
            continue;
        }

        SDL_Point top_left     = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
        SDL_Point bottom_right = cam.map_to_screen({chunk->world_bounds.x + chunk->world_bounds.w,
                                                    chunk->world_bounds.y + chunk->world_bounds.h});
        SDL_Rect dst{};
        dst.x = std::min(top_left.x, bottom_right.x);
        dst.y = std::min(top_left.y, bottom_right.y);
        dst.w = std::abs(bottom_right.x - top_left.x);
        dst.h = std::abs(bottom_right.y - top_left.y);
        if (dst.w <= 0 || dst.h <= 0) {
            continue;
        }

        const float chunk_alpha_multiplier = std::clamp(chunk->opacity_strength, 0.0f, 1.0f);
        const Uint8 chunk_alpha = clamp_alpha(alpha_multiplier * chunk_alpha_multiplier);

        const float scale_strength = std::max(0.0f, chunk->scale_strength);
        if (std::abs(scale_strength - 1.0f) > 1e-4f) {
            const float center_x = static_cast<float>(dst.x) + static_cast<float>(dst.w) / 2.0f;
            const float center_y = static_cast<float>(dst.y) + static_cast<float>(dst.h) / 2.0f;
            const float scaled_w = static_cast<float>(dst.w) * scale_strength;
            const float scaled_h = static_cast<float>(dst.h) * scale_strength;
            dst.w = std::max(1, static_cast<int>(std::lround(scaled_w)));
            dst.h = std::max(1, static_cast<int>(std::lround(scaled_h)));
            dst.x = static_cast<int>(std::lround(center_x - static_cast<float>(dst.w) / 2.0f));
            dst.y = static_cast<int>(std::lround(center_y - static_cast<float>(dst.h) / 2.0f));
        }

        if (chunk->offset_x != 0 || chunk->offset_y != 0) {
            SDL_Point origin_screen = cam.map_to_screen({chunk->world_bounds.x, chunk->world_bounds.y});
            SDL_Point offset_screen = cam.map_to_screen({chunk->world_bounds.x + chunk->offset_x,
                                                         chunk->world_bounds.y + chunk->offset_y});
            dst.x += offset_screen.x - origin_screen.x;
            dst.y += offset_screen.y - origin_screen.y;
        }

        SDL_SetTextureAlphaMod(chunk->static_light_mask, chunk_alpha);
        SDL_RenderCopy(renderer, chunk->static_light_mask, nullptr, &dst);
    }
}

void LightMap::render_visible_chunks_debug(SDL_Renderer* renderer,
                                              const SDL_Rect& view_rect,
                                              float alpha_multiplier) const {
    render_visible_chunks(renderer, view_rect, alpha_multiplier);
}

void LightMap::mark_region_dirty(const SDL_Rect& screen_rect) {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    const camera& cam     = assets_->getView();
    SDL_Rect      world_r = world_rect_from_screen(cam, screen_rect);
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk && intersects(chunk->world_bounds, world_r)) {
            chunk->lighting.needs_update = true;
        }
    }
}

void LightMap::mark_asset_lights_dirty(const Asset* asset) {
    std::scoped_lock lock(mutex_);
    if (!asset) {
        return;
    }
    if (world::Chunk* chunk = ensure_chunk_from_world(asset->pos)) {
        chunk->lighting.needs_update = true;
    } else {
        vibble::log::warn("[LightMap] mark_asset_lights_dirty missing chunk for asset at (" +
                          std::to_string(asset->pos.x) + ", " + std::to_string(asset->pos.y) + ")");
    }
}

void LightMap::mark_static_cache_dirty() {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return;
    }
    for (world::Chunk* chunk : active_chunks()) {
        if (chunk) {
            chunk->lighting_dirty     = true;
            chunk->lighting_preloaded = false;
            chunk->static_clean       = false;
            chunk->needs_retry        = true;
        }
    }
}

const std::vector<world::Chunk*>& LightMap::active_chunks() const {
    static const std::vector<world::Chunk*> kEmpty{};
    if (!assets_) {
        return kEmpty;
    }
    return assets_->world_grid().active_chunks();
}

world::Chunk* LightMap::chunk_from_world(SDL_Point world_px) const {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().chunk_from_world(world_px);
}

world::Chunk* LightMap::ensure_chunk_from_world(SDL_Point world_px) const {
    std::scoped_lock lock(mutex_);
    if (!assets_) {
        return nullptr;
    }
    return assets_->world_grid().ensure_chunk_from_world(world_px);
}

int LightMap::chunk_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<int>(active_chunks().size());
}

int LightMap::chunk_columns() const {
    std::scoped_lock lock(mutex_);
    const int count = chunk_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    return std::max(1, columns);
}

int LightMap::chunk_rows() const {
    std::scoped_lock lock(mutex_);
    const int count = chunk_count();
    if (count <= 0) {
        return 0;
    }
    const int columns = chunk_columns();
    return std::max(1, (count + columns - 1) / columns);
}

const world::Chunk* LightMap::chunk_at(int index) const {
    std::scoped_lock lock(mutex_);
    const auto& chunks = active_chunks();
    if (index < 0 || static_cast<std::size_t>(index) >= chunks.size()) {
        return nullptr;
    }
    return chunks[static_cast<std::size_t>(index)];
}

SDL_Rect LightMap::chunk_bounds(int index) const {
    std::scoped_lock lock(mutex_);
    if (const world::Chunk* chunk = chunk_at(index)) {
        return chunk->world_bounds;
    }
    return SDL_Rect{0, 0, 0, 0};
}

std::optional<world::Chunk::ChunkShadowParameters> LightMap::get_shadow_data(SDL_FPoint world_or_screen_pos) const {
    std::scoped_lock lock(mutex_);
    world::Chunk* chunk = nullptr;
    if (assets_) {
        chunk = chunk_from_world(SDL_Point{static_cast<int>(std::lround(world_or_screen_pos.x)),
                                           static_cast<int>(std::lround(world_or_screen_pos.y))});
        if (!chunk) {
            const camera& cam = assets_->getView();
            SDL_Point from_screen = cam.screen_to_map({static_cast<int>(std::lround(world_or_screen_pos.x)),
                                                       static_cast<int>(std::lround(world_or_screen_pos.y))});
            chunk = chunk_from_world(from_screen);
        }
    }
    if (!chunk) return std::nullopt;
    return chunk->shadow;
}


