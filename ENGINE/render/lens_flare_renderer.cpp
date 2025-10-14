#include "lens_flare_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace {

auto clamp01(float v) -> float {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

auto lerp(float a, float b, float t) -> float {
    return a + (b - a) * t;
}

auto pixel_luma_norm(uint32_t argb) -> float {
    const float r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    const float g = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    const float b = static_cast<float>((argb >> 0) & 0xFF) / 255.0f;
    return clamp01(0.2126f * r + 0.7152f * g + 0.0722f * b);
}

auto nearly_equal(float a, float b, float eps = 1e-4f) -> bool {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool LensFlareRenderer::Settings::operator==(const Settings& other) const {
    if (enabled != other.enabled) return false;
    if (seed_stride_px != other.seed_stride_px) return false;
    if (!nearly_equal(seed_threshold_norm, other.seed_threshold_norm)) return false;
    if (!nearly_equal(seed_pos_ema, other.seed_pos_ema)) return false;
    if (!nearly_equal(ghost_follow_ema, other.ghost_follow_ema)) return false;
    if (!nearly_equal(ghost_spawn_speed, other.ghost_spawn_speed)) return false;
    if (!nearly_equal(ghost_alpha_rise, other.ghost_alpha_rise)) return false;
    if (!nearly_equal(ghost_alpha_fall, other.ghost_alpha_fall)) return false;
    if (!nearly_equal(ghost_drift, other.ghost_drift)) return false;
    if (!nearly_equal(ghost_size_min, other.ghost_size_min)) return false;
    if (!nearly_equal(ghost_size_max, other.ghost_size_max)) return false;
    if (!nearly_equal(ghost_intensity_gain, other.ghost_intensity_gain)) return false;
    if (!nearly_equal(ghost_alpha_cap, other.ghost_alpha_cap)) return false;
    if (!nearly_equal(streak_angle_lean, other.streak_angle_lean)) return false;
    if (!nearly_equal(offscreen_spawn_bias, other.offscreen_spawn_bias)) return false;
    if (max_new_per_frame != other.max_new_per_frame) return false;
    for (std::size_t i = 0; i < axis_factors.size(); ++i) {
        if (!nearly_equal(axis_factors[i], other.axis_factors[i])) {
            return false;
        }
    }
    return true;
}

LensFlareRenderer::LensFlareRenderer(SDL_Renderer* renderer, int screen_width, int screen_height)
    : renderer_(renderer), screen_width_(screen_width), screen_height_(screen_height) {}

LensFlareRenderer::~LensFlareRenderer() {
    destroy_flare_textures();
}

void LensFlareRenderer::set_renderer(SDL_Renderer* renderer) {
    if (renderer_ == renderer) {
        return;
    }
    destroy_flare_textures();
    renderer_ = renderer;
}

void LensFlareRenderer::set_screen_size(int width, int height) {
    screen_width_ = width;
    screen_height_ = height;
}

auto LensFlareRenderer::default_settings() -> Settings {
    return Settings{};
}

auto LensFlareRenderer::sanitize_settings(const Settings& raw) -> Settings {
    Settings out = raw;
    out.seed_stride_px = std::max(1, std::min(raw.seed_stride_px, 256));
    out.seed_threshold_norm = clamp01(raw.seed_threshold_norm);
    out.seed_pos_ema = clamp01(raw.seed_pos_ema);
    out.ghost_follow_ema = clamp01(raw.ghost_follow_ema);
    out.ghost_spawn_speed = std::clamp(raw.ghost_spawn_speed, 0.0f, 500.0f);
    out.ghost_alpha_rise = clamp01(raw.ghost_alpha_rise);
    out.ghost_alpha_fall = clamp01(raw.ghost_alpha_fall);
    out.ghost_drift = std::clamp(raw.ghost_drift, 0.0f, 1.0f);
    out.ghost_size_min = std::clamp(raw.ghost_size_min, 0.0f, 2000.0f);
    out.ghost_size_max = std::clamp(raw.ghost_size_max, out.ghost_size_min, 4000.0f);
    out.ghost_intensity_gain = std::clamp(raw.ghost_intensity_gain, 0.0f, 4.0f);
    out.ghost_alpha_cap = clamp01(raw.ghost_alpha_cap);
    out.streak_angle_lean = std::clamp(raw.streak_angle_lean, -90.0f, 90.0f);
    out.offscreen_spawn_bias = std::clamp(raw.offscreen_spawn_bias, 0.0f, 1000.0f);
    out.max_new_per_frame = std::max(0, std::min(raw.max_new_per_frame, 64));
    for (std::size_t i = 0; i < Settings::kAxisCount; ++i) {
        out.axis_factors[i] = std::clamp(raw.axis_factors[i], -2.0f, 4.0f);
    }
    return out;
}

auto LensFlareRenderer::settings_from_json(const nlohmann::json& data, const Settings& defaults) -> Settings {
    Settings result = defaults;
    if (!data.is_object()) {
        return sanitize_settings(result);
    }

    auto read_float = [&data](const char* key, float fallback) -> float {
        auto it = data.find(key);
        if (it == data.end()) {
            return fallback;
        }
        try {
            if (it->is_number_float()) return static_cast<float>(it->get<double>());
            if (it->is_number_integer()) return static_cast<float>(it->get<int>());
        } catch (...) {
        }
        return fallback;
    };

    auto read_int = [&data](const char* key, int fallback) -> int {
        auto it = data.find(key);
        if (it == data.end()) {
            return fallback;
        }
        try {
            if (it->is_number_integer()) return it->get<int>();
            if (it->is_number_float()) return static_cast<int>(std::lround(it->get<double>()));
        } catch (...) {
        }
        return fallback;
    };

    if (auto it = data.find("enabled"); it != data.end() && it->is_boolean()) {
        result.enabled = it->get<bool>();
    }

    result.seed_stride_px = read_int("seed_stride_px", result.seed_stride_px);
    result.seed_threshold_norm = read_float("seed_threshold_norm", result.seed_threshold_norm);
    result.seed_pos_ema = read_float("seed_pos_ema", result.seed_pos_ema);
    result.ghost_follow_ema = read_float("ghost_follow_ema", result.ghost_follow_ema);
    result.ghost_spawn_speed = read_float("ghost_spawn_speed", result.ghost_spawn_speed);
    result.ghost_alpha_rise = read_float("ghost_alpha_rise", result.ghost_alpha_rise);
    result.ghost_alpha_fall = read_float("ghost_alpha_fall", result.ghost_alpha_fall);
    result.ghost_drift = read_float("ghost_drift", result.ghost_drift);
    result.ghost_size_min = read_float("ghost_size_min", result.ghost_size_min);
    result.ghost_size_max = read_float("ghost_size_max", result.ghost_size_max);
    result.ghost_intensity_gain = read_float("ghost_intensity_gain", result.ghost_intensity_gain);
    result.ghost_alpha_cap = read_float("ghost_alpha_cap", result.ghost_alpha_cap);
    result.streak_angle_lean = read_float("streak_angle_lean", result.streak_angle_lean);
    result.offscreen_spawn_bias = read_float("offscreen_spawn_bias", result.offscreen_spawn_bias);
    result.max_new_per_frame = read_int("max_new_per_frame", result.max_new_per_frame);

    if (auto it = data.find("axis_factors"); it != data.end() && it->is_array()) {
        for (std::size_t i = 0; i < Settings::kAxisCount && i < it->size(); ++i) {
            try {
                result.axis_factors[i] = static_cast<float>((*it)[i].get<double>());
            } catch (...) {
            }
        }
    }

    return sanitize_settings(result);
}

void LensFlareRenderer::settings_to_json(nlohmann::json& out, const Settings& settings) {
    Settings sanitized = sanitize_settings(settings);
    out["enabled"] = sanitized.enabled;
    out["seed_stride_px"] = sanitized.seed_stride_px;
    out["seed_threshold_norm"] = sanitized.seed_threshold_norm;
    out["seed_pos_ema"] = sanitized.seed_pos_ema;
    out["ghost_follow_ema"] = sanitized.ghost_follow_ema;
    out["ghost_spawn_speed"] = sanitized.ghost_spawn_speed;
    out["ghost_alpha_rise"] = sanitized.ghost_alpha_rise;
    out["ghost_alpha_fall"] = sanitized.ghost_alpha_fall;
    out["ghost_drift"] = sanitized.ghost_drift;
    out["ghost_size_min"] = sanitized.ghost_size_min;
    out["ghost_size_max"] = sanitized.ghost_size_max;
    out["ghost_intensity_gain"] = sanitized.ghost_intensity_gain;
    out["ghost_alpha_cap"] = sanitized.ghost_alpha_cap;
    out["streak_angle_lean"] = sanitized.streak_angle_lean;
    out["offscreen_spawn_bias"] = sanitized.offscreen_spawn_bias;
    out["max_new_per_frame"] = sanitized.max_new_per_frame;

    nlohmann::json axis = nlohmann::json::array();
    for (float f : sanitized.axis_factors) {
        axis.push_back(f);
    }
    out["axis_factors"] = std::move(axis);
}

void LensFlareRenderer::apply_settings(const Settings& settings) {
    Settings sanitized = sanitize_settings(settings);
    if (sanitized == settings_) {
        if (!settings_.enabled) {
            ghosts_.clear();
            last_seeds_.clear();
        }
        return;
    }

    const bool was_enabled = settings_.enabled;
    settings_ = sanitized;

    if (!settings_.enabled) {
        ghosts_.clear();
        last_seeds_.clear();
        return;
    }

    if (!was_enabled && settings_.enabled) {
        ghosts_.clear();
        last_seeds_.clear();
    }
}

void LensFlareRenderer::apply_settings_from_json(const nlohmann::json& data) {
    apply_settings(settings_from_json(data));
}

void LensFlareRenderer::draw_after_light_map() {
    if (!renderer_ || screen_width_ <= 0 || screen_height_ <= 0) {
        return;
    }

    if (!settings_.enabled) {
        if (!ghosts_.empty()) {
            ghosts_.clear();
            last_seeds_.clear();
        }
        return;
    }

    std::vector<Seed> seeds;
    const bool found = detect_bright_seeds(seeds, settings_.seed_stride_px, settings_.seed_threshold_norm);
    if (found) {
        smooth_and_track_seeds(seeds);
    }
    spawn_or_update_ghosts(found ? seeds : std::vector<Seed>{});
    step_and_render_ghosts();
}

void LensFlareRenderer::ensure_flare_textures() {
    if (circle_tex_ && streak_tex_ && star_tex_) {
        return;
    }
    make_circle_tex();
    make_streak_tex();
    make_starburst_tex();
}

void LensFlareRenderer::destroy_flare_textures() {
    if (circle_tex_) {
        SDL_DestroyTexture(circle_tex_);
        circle_tex_ = nullptr;
    }
    if (streak_tex_) {
        SDL_DestroyTexture(streak_tex_);
        streak_tex_ = nullptr;
    }
    if (star_tex_) {
        SDL_DestroyTexture(star_tex_);
        star_tex_ = nullptr;
    }
}

void LensFlareRenderer::make_circle_tex() {
    if (circle_tex_ || !renderer_) return;
    constexpr int SZ = 256;
    circle_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SZ, SZ);
    if (!circle_tex_) return;
    SDL_SetTextureBlendMode(circle_tex_, SDL_BLENDMODE_ADD);

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, circle_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    const int cx = SZ / 2;
    const int cy = SZ / 2;
    const float R = SZ * 0.48f;
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = static_cast<float>(x - cx);
            float dy = static_cast<float>(y - cy);
            float r = std::sqrt(dx * dx + dy * dy);
            if (r > R) continue;
            float fall = std::pow(1.0f - (r / R), 1.6f);
            Uint8 alpha = static_cast<Uint8>(std::lround(255.0f * clamp01(fall)));
            if (!alpha) continue;
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
            SDL_RenderDrawPoint(renderer_, x, y);
        }
    }
    SDL_SetRenderTarget(renderer_, prev);
}

void LensFlareRenderer::make_streak_tex() {
    if (streak_tex_ || !renderer_) return;
    constexpr int W = 512;
    constexpr int H = 64;
    streak_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
    if (!streak_tex_) return;
    SDL_SetTextureBlendMode(streak_tex_, SDL_BLENDMODE_ADD);

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, streak_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    const float cx = W * 0.5f;
    const float cy = H * 0.5f;
    for (int y = 0; y < H; ++y) {
        float ny = (static_cast<float>(y) - cy) / (H * 0.5f);
        float vy = ny * ny;
        for (int x = 0; x < W; ++x) {
            float nx = (static_cast<float>(x) - cx) / (W * 0.5f);
            float d = nx * nx * 0.12f + vy;
            float t = std::exp(-4.0f * d);
            Uint8 a = static_cast<Uint8>(std::lround(210.0f * clamp01(t)));
            if (!a) continue;
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, a);
            SDL_RenderDrawPoint(renderer_, x, y);
        }
    }
    SDL_SetRenderTarget(renderer_, prev);
}

void LensFlareRenderer::make_starburst_tex() {
    if (star_tex_ || !renderer_) return;
    constexpr int SZ = 256;
    star_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SZ, SZ);
    if (!star_tex_) return;
    SDL_SetTextureBlendMode(star_tex_, SDL_BLENDMODE_ADD);

    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, star_tex_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);

    const int cx = SZ / 2;
    const int cy = SZ / 2;
    const float R = SZ * 0.48f;
    for (int y = 0; y < SZ; ++y) {
        for (int x = 0; x < SZ; ++x) {
            float dx = static_cast<float>(x - cx);
            float dy = static_cast<float>(y - cy);
            float r = std::sqrt(dx * dx + dy * dy);
            if (r > R) continue;
            float ang = std::atan2(dy, dx);
            float rays = std::pow(std::fabs(std::cos(ang * 6.0f)), 18.0f);
            float core = std::pow(1.0f - (r / R), 0.35f);
            float a = clamp01(0.8f * core + 0.4f * rays * (1.0f - r / R));
            Uint8 alpha = static_cast<Uint8>(std::lround(255.0f * a));
            if (!alpha) continue;
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
            SDL_RenderDrawPoint(renderer_, x, y);
        }
    }
    SDL_SetRenderTarget(renderer_, prev);
}

void LensFlareRenderer::render_sprite(SDL_Texture* tex, float cx, float cy, float intensity, float base_px, float angle_deg, SDL_Color tint) {
    if (!renderer_ || !tex) return;
    int tw = 0;
    int th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    float scale = base_px / static_cast<float>(std::max(tw, th));
    int dw = std::max(1, static_cast<int>(std::lround(tw * scale)));
    int dh = std::max(1, static_cast<int>(std::lround(th * scale)));

    SDL_Rect dst{ static_cast<int>(std::lround(cx - dw / 2.0f)), static_cast<int>(std::lround(cy - dh / 2.0f)), dw, dh };

    const float a = clamp01(intensity) * settings_.ghost_alpha_cap;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(std::lround(a * 255.0f)));
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);

    if (std::fabs(angle_deg) > 0.01f) {
        SDL_RenderCopyEx(renderer_, tex, nullptr, &dst, angle_deg, nullptr, SDL_FLIP_NONE);
    } else {
        SDL_RenderCopy(renderer_, tex, nullptr, &dst);
    }

    SDL_SetTextureAlphaMod(tex, 255);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
}

SDL_Color LensFlareRenderer::warm_tint(float hue_deg, float intensity_scale) const {
    float H = std::fmod(hue_deg, 360.0f) / 60.0f;
    int i = static_cast<int>(std::floor(H));
    float f = H - i;
    float V = 1.0f;
    float S = 0.5f;
    float p = V * (1.0f - S);
    float q = V * (1.0f - S * f);
    float t = V * (1.0f - S * (1.0f - f));
    float r, g, b;
    switch (i) {
        default:
        case 0: r = V; g = t; b = p; break;
        case 1: r = q; g = V; b = p; break;
        case 2: r = p; g = V; b = t; break;
        case 3: r = p; g = q; b = V; break;
        case 4: r = t; g = p; b = V; break;
        case 5: r = V; g = p; b = q; break;
    }
    SDL_Color c{
        static_cast<Uint8>(std::lround(r * 255.0f)),
        static_cast<Uint8>(std::lround(g * 255.0f)),
        static_cast<Uint8>(std::lround(b * 255.0f)),
        static_cast<Uint8>(std::lround(clamp01(intensity_scale) * 255.0f))
    };
    return c;
}

bool LensFlareRenderer::detect_bright_seeds(std::vector<Seed>& out, int stride_px, float threshold_norm) {
    out.clear();
    if (!renderer_) return false;

    std::vector<uint32_t> pixels(static_cast<std::size_t>(screen_width_) * static_cast<std::size_t>(screen_height_));
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), screen_width_ * static_cast<int>(sizeof(uint32_t))) != 0) {
        return false;
    }

    const int stride = std::max(1, stride_px);
    for (int y = stride / 2; y < screen_height_; y += stride) {
        const uint32_t* row = &pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(screen_width_)];
        for (int x = stride / 2; x < screen_width_; x += stride) {
            float lum = pixel_luma_norm(row[x]);
            if (lum < threshold_norm) continue;

            bool is_max = true;
            for (int oy = -1; oy <= 1 && is_max; ++oy) {
                int yy = y + oy * stride;
                if (yy < 0 || yy >= screen_height_) continue;
                const uint32_t* prow = &pixels[static_cast<std::size_t>(yy) * static_cast<std::size_t>(screen_width_)];
                for (int ox = -1; ox <= 1; ++ox) {
                    int xx = x + ox * stride;
                    if (xx < 0 || xx >= screen_width_) continue;
                    if (pixel_luma_norm(prow[xx]) > lum + 1e-5f) {
                        is_max = false;
                        break;
                    }
                }
            }
            if (!is_max) continue;

            Seed s;
            s.x = static_cast<float>(x);
            s.y = static_cast<float>(y);
            s.sx = s.x;
            s.sy = s.y;
            s.strength = clamp01(lum);
            s.valid = true;
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const Seed& a, const Seed& b) { return a.strength > b.strength; });
    return !out.empty();
}

void LensFlareRenderer::smooth_and_track_seeds(std::vector<Seed>& seeds) {
    const float match2 = 160.0f * 160.0f;
    std::vector<bool> matched(seeds.size(), false);

    for (auto& prev : last_seeds_) {
        float best_d2 = match2;
        int best = -1;
        for (int i = 0; i < static_cast<int>(seeds.size()); ++i) {
            if (matched[i] || !seeds[i].valid) continue;
            float dx = seeds[i].x - prev.sx;
            float dy = seeds[i].y - prev.sy;
            float d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best = i;
            }
        }
        if (best >= 0) {
            seeds[best].sx = lerp(prev.sx, seeds[best].x, settings_.seed_pos_ema);
            seeds[best].sy = lerp(prev.sy, seeds[best].y, settings_.seed_pos_ema);
            matched[best] = true;
        }
    }
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        if (!matched[i]) {
            seeds[i].sx = seeds[i].x;
            seeds[i].sy = seeds[i].y;
        }
    }
    last_seeds_ = seeds;
}

void LensFlareRenderer::axis_cascade_points(const Seed& seed, std::vector<SDL_FPoint>& out) const {
    out.clear();
    const SDL_FPoint c = screen_center();
    float vx = seed.sx - c.x;
    float vy = seed.sy - c.y;
    float L = std::sqrt(vx * vx + vy * vy) + 1e-6f;
    float ux = vx / L;
    float uy = vy / L;
    for (float f : settings_.axis_factors) {
        out.push_back(SDL_FPoint{ c.x + ux * L * f, c.y + uy * L * f });
    }
}

bool LensFlareRenderer::on_screen(float x, float y, int margin_px) const {
    return x >= -margin_px && x <= screen_width_ + margin_px && y >= -margin_px && y <= screen_height_ + margin_px;
}

SDL_FPoint LensFlareRenderer::screen_center() const {
    return SDL_FPoint{ static_cast<float>(screen_width_) * 0.5f, static_cast<float>(screen_height_) * 0.5f };
}

void LensFlareRenderer::spawn_or_update_ghosts(const std::vector<Seed>& seeds) {
    for (auto& g : ghosts_) {
        g.dying = true;
        g.target_alpha = 0.0f;
    }

    std::vector<SDL_FPoint> pts;
    int new_budget = settings_.max_new_per_frame;

    for (const auto& s : seeds) {
        if (!s.valid) continue;
        axis_cascade_points(s, pts);
        const float seed_a = clamp01(s.strength * settings_.ghost_intensity_gain);
        const float base_sz = lerp(settings_.ghost_size_min, settings_.ghost_size_max, clamp01(0.15f + 0.85f * s.strength));

        for (std::size_t i = 0; i < pts.size(); ++i) {
            const SDL_FPoint p = pts[i];
            int desired_kind;
            if (i == 4) desired_kind = 2;
            else if (i == 1 || i == 2 || i == 5) desired_kind = 0;
            else desired_kind = 1;

            int best = -1;
            float best_d2 = 52.0f * 52.0f;
            for (int gi = 0; gi < static_cast<int>(ghosts_.size()); ++gi) {
                auto& g = ghosts_[gi];
                if (g.kind != desired_kind) continue;
                float dx = g.x - p.x;
                float dy = g.y - p.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best = gi;
                }
            }

            float kind_alpha = seed_a * (desired_kind == 0 ? 1.0f : desired_kind == 1 ? 0.70f : 0.85f);
            float kind_size = base_sz * (desired_kind == 0 ? 1.0f : desired_kind == 1 ? 1.4f : 1.15f);

            if (best >= 0) {
                auto& g = ghosts_[best];
                g.dying = false;
                g.tx = p.x;
                g.ty = p.y;
                g.target_alpha = std::max(g.target_alpha, kind_alpha);
                g.size_px = lerp(g.size_px, kind_size, 0.25f);
                g.life = std::min(g.life + 1.0f, g.max_life);
            } else if (new_budget > 0) {
                SDL_FPoint c = screen_center();
                float ax = p.x - c.x;
                float ay = p.y - c.y;
                float L = std::sqrt(ax * ax + ay * ay) + 1e-6f;
                ax /= L;
                ay /= L;

                Ghost g;
                g.kind = desired_kind;
                float spawn_dist = std::max<float>(std::max(screen_width_, screen_height_), L) + settings_.offscreen_spawn_bias;
                g.x = c.x + ax * spawn_dist;
                g.y = c.y + ay * spawn_dist;
                g.tx = p.x;
                g.ty = p.y;
                g.vx = -ax * settings_.ghost_spawn_speed;
                g.vy = -ay * settings_.ghost_spawn_speed;
                g.alpha = 0.0f;
                g.target_alpha = kind_alpha;
                g.size_px = kind_size;
                g.hue = 28.0f + (desired_kind == 0 ? (i * 8.0f) : desired_kind == 2 ? 10.0f : 5.0f);
                g.life = 0.0f;
                g.dying = false;

                ghosts_.push_back(g);
                --new_budget;
                if (new_budget <= 0) break;
            }
        }
        if (new_budget <= 0) break;
    }
}

void LensFlareRenderer::step_and_render_ghosts() {
    ensure_flare_textures();
    if (!circle_tex_ && !streak_tex_ && !star_tex_) {
        return;
    }

    std::vector<Ghost> keep;
    keep.reserve(ghosts_.size());

    for (auto& g : ghosts_) {
        g.x = lerp(g.x, g.tx, settings_.ghost_follow_ema) + g.vx * settings_.ghost_drift;
        g.y = lerp(g.y, g.ty, settings_.ghost_follow_ema) + g.vy * settings_.ghost_drift;

        if (!g.dying) g.alpha = clamp01(g.alpha + settings_.ghost_alpha_rise);
        else g.alpha = clamp01(g.alpha - settings_.ghost_alpha_fall);

        if (!g.dying && g.life > g.max_life) {
            g.dying = true;
            g.target_alpha = 0.0f;
        }
        g.life += 1.0f;

        float target = std::min(g.target_alpha, settings_.ghost_alpha_cap);
        g.alpha = std::min(g.alpha, target);

        if (g.dying && g.alpha <= 0.001f && !on_screen(g.x, g.y, 24)) {
            continue;
        }

        SDL_Texture* tex = (g.kind == 0 ? circle_tex_ : g.kind == 1 ? streak_tex_ : star_tex_);
        float angle = (g.kind == 1 ? (g.x >= screen_width_ * 0.5f ? +settings_.streak_angle_lean : -settings_.streak_angle_lean) : 0.0f);
        SDL_Color tint = warm_tint(g.hue, 1.0f);

        render_sprite(tex, g.x, g.y, g.alpha, g.size_px, angle, tint);
        keep.push_back(g);
    }

    ghosts_.swap(keep);
}

