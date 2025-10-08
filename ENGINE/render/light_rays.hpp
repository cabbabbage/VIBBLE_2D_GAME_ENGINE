#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <SDL.h>

#include <nlohmann/json.hpp>

enum class BrightnessMetric {
    Luma709 = 0,
    MaxRGB,
    AvgRGB,
    EnergyRGB
};

inline std::string brightness_metric_to_string(BrightnessMetric metric) {
    switch (metric) {
        case BrightnessMetric::Luma709:  return "Luma709";
        case BrightnessMetric::MaxRGB:   return "MaxRGB";
        case BrightnessMetric::AvgRGB:   return "AvgRGB";
        case BrightnessMetric::EnergyRGB:return "EnergyRGB";
    }
    return "MaxRGB";
}

inline BrightnessMetric brightness_metric_from_string(std::string_view name,
                                                      BrightnessMetric fallback = BrightnessMetric::MaxRGB) {
    std::string lowered;
    lowered.reserve(name.size());
    for (unsigned char ch : name) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lowered == "luma709" || lowered == "luma" || lowered == "rec709") {
        return BrightnessMetric::Luma709;
    }
    if (lowered == "maxrgb" || lowered == "max" || lowered == "peak") {
        return BrightnessMetric::MaxRGB;
    }
    if (lowered == "avgrgb" || lowered == "average" || lowered == "avg") {
        return BrightnessMetric::AvgRGB;
    }
    if (lowered == "energyrgb" || lowered == "energy") {
        return BrightnessMetric::EnergyRGB;
    }
    return fallback;
}

struct LightRaysParams {
    BrightnessMetric metric = BrightnessMetric::MaxRGB;
    bool  use_alpha_in_mask = false;
    float gamma_comp        = 0.9f;
    float min_luma_threshold = 0.35f;
    float bright_percentile  = 0.90f;
    int   samples            = 112;
    float density            = 1.4f;
    float decay              = 0.985f;
    float weight             = 1.35f;
    float exposure           = 2.2f;
    int   downsample_log2    = 1;

    static LightRaysParams defaults() { return LightRaysParams{}; }

    static LightRaysParams from_json(const nlohmann::json& data) {
        LightRaysParams params = LightRaysParams::defaults();
        if (!data.is_object()) {
            return params;
        }

        auto safe_bool = [&](const char* key, bool def) {
            try { return data.at(key).get<bool>(); } catch (...) { return def; }
        };
        auto safe_float = [&](const char* key, float def, float lo, float hi) {
            double value = static_cast<double>(def);
            try { value = data.at(key).get<double>(); } catch (...) {}
            if (!std::isfinite(value)) value = def;
            value = std::clamp(value, static_cast<double>(lo), static_cast<double>(hi));
            return static_cast<float>(value);
        };
        auto safe_int = [&](const char* key, int def, int lo, int hi) {
            int value = def;
            try { value = data.at(key).get<int>(); } catch (...) {}
            if (value < lo) value = lo;
            if (value > hi) value = hi;
            return value;
        };

        try {
            params.metric = brightness_metric_from_string(data.at("metric").get<std::string>(), params.metric);
        } catch (...) {}
        params.use_alpha_in_mask = safe_bool("use_alpha_in_mask", params.use_alpha_in_mask);
        params.gamma_comp        = safe_float("gamma_comp", params.gamma_comp, 0.1f, 4.0f);
        params.min_luma_threshold = safe_float("min_luma_threshold", params.min_luma_threshold, 0.0f, 1.0f);
        params.bright_percentile  = safe_float("bright_percentile", params.bright_percentile, 0.0f, 1.0f);
        params.samples            = safe_int("samples", params.samples, 1, 512);
        params.density            = safe_float("density", params.density, 0.01f, 4.0f);
        params.decay              = safe_float("decay", params.decay, 0.5f, 0.9999f);
        params.weight             = safe_float("weight", params.weight, 0.0f, 8.0f);
        params.exposure           = safe_float("exposure", params.exposure, 0.0f, 8.0f);
        params.downsample_log2    = safe_int("downsample_log2", params.downsample_log2, 0, 4);

        return params;
    }

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["metric"] = brightness_metric_to_string(metric);
        j["use_alpha_in_mask"] = use_alpha_in_mask;
        j["gamma_comp"] = gamma_comp;
        j["min_luma_threshold"] = min_luma_threshold;
        j["bright_percentile"] = bright_percentile;
        j["samples"] = samples;
        j["density"] = density;
        j["decay"] = decay;
        j["weight"] = weight;
        j["exposure"] = exposure;
        j["downsample_log2"] = downsample_log2;
        return j;
    }
};

struct LightRaysConfig {
    bool  final_blur_enabled = true;
    float final_blur_radius  = 2.5f;
    float final_blur_mix     = 0.85f;
    bool  per_light_enabled  = true;
    LightRaysParams per_light = LightRaysParams::defaults();

    static LightRaysConfig defaults() { return LightRaysConfig{}; }

    static LightRaysConfig from_json(const nlohmann::json& data) {
        LightRaysConfig config = LightRaysConfig::defaults();
        if (!data.is_object()) {
            return config;
        }

        auto safe_bool = [&](const char* key, bool def) {
            try { return data.at(key).get<bool>(); } catch (...) { return def; }
        };
        auto safe_float = [&](const char* key, float def, float lo, float hi) {
            double value = static_cast<double>(def);
            try { value = data.at(key).get<double>(); } catch (...) {}
            if (!std::isfinite(value)) value = def;
            value = std::clamp(value, static_cast<double>(lo), static_cast<double>(hi));
            return static_cast<float>(value);
        };

        config.final_blur_enabled = safe_bool("enabled", config.final_blur_enabled);
        config.final_blur_radius  = safe_float("final_blur_radius", config.final_blur_radius, 0.0f, 32.0f);
        config.final_blur_mix     = safe_float("final_blur_mix", config.final_blur_mix, 0.0f, 1.0f);
        config.per_light_enabled  = safe_bool("per_light_enabled", config.per_light_enabled);
        config.per_light          = LightRaysParams::from_json(data);
        return config;
    }

    nlohmann::json to_json() const {
        nlohmann::json j = per_light.to_json();
        j["enabled"] = final_blur_enabled;
        j["final_blur_radius"] = final_blur_radius;
        j["final_blur_mix"] = final_blur_mix;
        j["per_light_enabled"] = per_light_enabled;
        return j;
    }
};

class LightRaysPass {
public:
    LightRaysPass(SDL_Renderer* renderer = nullptr,
                  int screen_w = 0,
                  int screen_h = 0);
    ~LightRaysPass();

    void set_renderer(SDL_Renderer* renderer);
    void set_screen_size(int w, int h);
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }

    void set_params(const LightRaysParams& params);
    const LightRaysParams& params() const { return params_; }

    void set_light_screen_pos(SDL_Point pos) { light_screen_pos_ = pos; }

    SDL_Texture* compute(SDL_Texture* source_texture);

private:
    bool ensure_resources();
    void release_resources();
    void update_downsample_dimensions();

    float brightness_from_pixel(uint32_t pixel) const;
    float sample_brightness(float u, float v) const;

private:
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* capture_texture_ = nullptr;
    SDL_Texture* rays_texture_ = nullptr;
    SDL_PixelFormat* pixel_format_ = nullptr;

    int screen_w_ = 0;
    int screen_h_ = 0;
    int downsample_w_ = 0;
    int downsample_h_ = 0;

    SDL_Point light_screen_pos_{0, 0};

    bool enabled_ = false;
    LightRaysParams params_{};

    std::vector<uint32_t> capture_pixels_;
    std::vector<float> downsampled_mask_;
    std::vector<uint32_t> rays_pixels_;
};
