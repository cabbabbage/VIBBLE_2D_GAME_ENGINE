#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>
#include <nlohmann/json.hpp>

namespace render_pipeline {

struct ScaleSelection {
    int   index           = 0;
    float requested_scale = 1.0f;
    float stored_scale    = 1.0f;
    float remainder_scale = 1.0f;
};

struct ScalingLogic {
    using ScaleSteps = std::vector<float>;

    struct ScaleProfile {
        ScaleSteps          steps;
        std::uint64_t       revision = 0;
        bool has_custom_steps() const { return !steps.empty(); }
    };

    static constexpr std::size_t kDefaultVariantCount = 10;
    static inline const ScaleSteps& DefaultScaleSteps() {
        static const ScaleSteps kDefaultSteps = {
            1.00f, 0.90f, 0.80f, 0.70f, 0.60f,
            0.50f, 0.40f, 0.30f, 0.20f, 0.10f
        };
        return kDefaultSteps;
    }

    static inline float ComputeScale(int base_w, int base_h, int target_w, int target_h) {
        if (base_w <= 0 || base_h <= 0 || target_w <= 0 || target_h <= 0) {
            return 1.0f;
        }
        const float scale_w = static_cast<float>(target_w) / static_cast<float>(base_w);
        const float scale_h = static_cast<float>(target_h) / static_cast<float>(base_h);
        return (scale_w < scale_h) ? scale_w : scale_h;
    }

    static inline ScaleSelection Choose(float desired_scale) {
        return Choose(desired_scale, DefaultScaleSteps());
    }

    static inline ScaleSelection Choose(float desired_scale, const ScaleSteps& steps) {
        ScaleSelection result{};
        if (steps.empty()) {
            result.requested_scale = std::isfinite(desired_scale) && desired_scale > 0.0f ? desired_scale : 1.0f;
            result.stored_scale    = 1.0f;
            result.index           = 0;
            result.remainder_scale = result.requested_scale;
            return result;
        }
        if (!std::isfinite(desired_scale)) {
            desired_scale = 1.0f;
        }
        if (desired_scale <= 0.0f) {
            desired_scale = steps.back();
        }

        result.requested_scale = desired_scale;

        float best_diff = std::numeric_limits<float>::max();
        float chosen_scale = steps.front();
        int   chosen_index = 0;

        for (std::size_t i = 0; i < steps.size(); ++i) {
            const float candidate = steps[i];
            const float diff = std::fabs(candidate - desired_scale);
            if (diff < best_diff - 1e-4f) {
                best_diff    = diff;
                chosen_scale = candidate;
                chosen_index = static_cast<int>(i);
            } else if (std::fabs(diff - best_diff) <= 1e-4f && candidate > chosen_scale) {
                chosen_scale = candidate;
                chosen_index = static_cast<int>(i);
            }
        }

        result.index        = chosen_index;
        result.stored_scale = chosen_scale;
        result.remainder_scale = (chosen_scale > 0.0f) ? (desired_scale / chosen_scale) : 1.0f;
        return result;
    }

    static inline int ScalePercent(std::size_t index) {
        return ScalePercent(DefaultScaleSteps(), index);
    }

    static inline int ScalePercent(const ScaleSteps& steps, std::size_t index) {
        if (index >= steps.size()) {
            return 0;
        }
        return static_cast<int>(std::lround(steps[index] * 100.0f));
    }

    static inline std::string VariantFolder(const std::string& base, std::size_t index) {
        return VariantFolder(base, DefaultScaleSteps(), index);
    }

    static inline std::string VariantFolder(const std::string& base, const ScaleSteps& steps, std::size_t index) {
        return std::filesystem::path(base)
            .append("scale_" + std::to_string(ScalePercent(steps, index)))
            .string();
    }

    static inline std::array<int, kDefaultVariantCount> PercentSteps() {
        std::array<int, kDefaultVariantCount> percents{};
        const auto& defaults = DefaultScaleSteps();
        const std::size_t limit = std::min<std::size_t>(percents.size(), defaults.size());
        for (std::size_t i = 0; i < limit; ++i) {
            percents[i] = ScalePercent(defaults, i);
        }
        return percents;
    }

    static inline std::vector<int> PercentSteps(const ScaleSteps& steps) {
        std::vector<int> percents;
        percents.reserve(steps.size());
        for (std::size_t i = 0; i < steps.size(); ++i) {
            percents.push_back(ScalePercent(steps, i));
        }
        return percents;
    }

    static inline void ConfigureUsageStorage(const std::filesystem::path& path) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!path.empty() && path != state.file_path) {
            state.file_path = path;
            state.loaded    = false;
        }
        ensure_loaded(state);
    }

    static inline void SetUsageTrackingEnabled(bool enabled) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        state.enabled = enabled;
        if (enabled && !state.loaded) {
            ensure_loaded(state);
        }
    }

    static inline bool UsageTrackingEnabled() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        return state.enabled;
    }

    static inline void RecordUsage(const std::string& asset_key, float requested_scale, float stored_scale) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.enabled || asset_key.empty()) {
            return;
        }
        ensure_loaded(state);
        if (!state.data.contains("assets") || !state.data["assets"].is_object()) {
            state.data["assets"] = nlohmann::json::object();
        }
        auto& assets = state.data["assets"];
        nlohmann::json& entry = assets[asset_key];
        if (!entry.is_object()) {
            entry = default_asset_entry();
        }

        std::vector<std::uint64_t> histogram = parse_histogram(entry);
        const int bucket = histogram_bucket(requested_scale);
        if (bucket >= 0 && static_cast<std::size_t>(bucket) < histogram.size()) {
            histogram[static_cast<std::size_t>(bucket)] += 1;
        }

        const std::vector<int> recommended = compute_recommendations(histogram);
        const bool changed = update_entry(entry, histogram, recommended, requested_scale, stored_scale);
        if (changed) {
            state.dirty = true;
        }
        if (state.dirty) {
            save_to_disk(state);
        }
    }

    static inline bool FlushUsageData() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        return save_to_disk(state);
    }

    static inline ScaleProfile ProfileForAsset(const std::string& asset_key) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);

        ScaleProfile profile;
        const auto defaults = DefaultScaleSteps();

        if (!asset_key.empty() && state.data.contains("assets") && state.data["assets"].is_object()) {
            const auto& assets = state.data["assets"];
            auto it = assets.find(asset_key);
            if (it != assets.end() && it->is_object()) {
                const nlohmann::json& entry = *it;
                if (entry.contains("recommended_percentages") && entry["recommended_percentages"].is_array()) {
                    for (const auto& value : entry["recommended_percentages"]) {
                        if (!value.is_number()) {
                            continue;
                        }
                        const float percent = static_cast<float>(value.get<double>());
                        const float scale   = std::clamp(percent / 100.0f, 0.05f, 2.0f);
                        profile.steps.push_back(scale);
                    }
                }
                if (entry.contains("revision") && entry["revision"].is_number_unsigned()) {
                    profile.revision = entry["revision"].get<std::uint64_t>();
                }
            }
        }

        if (profile.steps.empty()) {
            profile.steps.assign(defaults.begin(), defaults.end());
        } else {
            std::sort(profile.steps.begin(), profile.steps.end(), std::greater<float>());
            profile.steps.erase(std::unique(profile.steps.begin(), profile.steps.end(), [](float a, float b) {
                return std::fabs(a - b) <= 1e-4f;
            }), profile.steps.end());
        }

        return profile;
    }

private:
    struct UsageState {
        std::mutex            mutex;
        bool                  enabled  = false;
        bool                  loaded   = false;
        bool                  dirty    = false;
        std::filesystem::path file_path = std::filesystem::path("loading") / "scaling_profiles.json";
        nlohmann::json        data      = default_storage();
    };

    static inline UsageState& usage_state() {
        static UsageState state;
        return state;
    }

    static inline nlohmann::json default_storage() {
        nlohmann::json data;
        data["version"] = 1;
        data["assets"] = nlohmann::json::object();
        return data;
    }

    static inline nlohmann::json default_asset_entry() {
        nlohmann::json entry;
        entry["histogram"] = nlohmann::json::array();
        for (int i = 0; i < kHistogramBucketCount; ++i) {
            entry["histogram"].push_back(0);
        }
        entry["recommended_percentages"] = nlohmann::json::array();
        entry["revision"]                = 0;
        entry["last_updated"]            = std::string{};
        return entry;
    }

    static inline void ensure_loaded(UsageState& state) {
        if (state.loaded) {
            return;
        }
        state.loaded = true;
        state.dirty  = false;
        if (state.file_path.empty()) {
            state.file_path = std::filesystem::path("loading") / "scaling_profiles.json";
        }
        std::ifstream in(state.file_path);
        if (!in.good()) {
            state.data  = default_storage();
            state.dirty = true;
            return;
        }
        try {
            nlohmann::json loaded = nlohmann::json::parse(in, nullptr, true, true);
            if (!loaded.is_object()) {
                state.data  = default_storage();
                state.dirty = true;
            } else {
                if (!loaded.contains("assets") || !loaded["assets"].is_object()) {
                    loaded["assets"] = nlohmann::json::object();
                }
                state.data = std::move(loaded);
            }
        } catch (...) {
            state.data  = default_storage();
            state.dirty = true;
        }
    }

    static inline std::vector<std::uint64_t> parse_histogram(const nlohmann::json& entry) {
        std::vector<std::uint64_t> histogram(kHistogramBucketCount, 0);
        if (entry.contains("histogram") && entry["histogram"].is_array()) {
            const auto& arr = entry["histogram"];
            for (std::size_t idx = 0; idx < histogram.size() && idx < arr.size(); ++idx) {
                if (arr[idx].is_number_unsigned()) {
                    histogram[idx] = arr[idx].get<std::uint64_t>();
                }
            }
        }
        return histogram;
    }

    static inline std::string timestamp_now() {
        const auto now        = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &time_t_now);
#else
        gmtime_r(&time_t_now, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    static inline int histogram_bucket(float requested_scale) {
        if (!std::isfinite(requested_scale) || requested_scale <= 0.0f) {
            return kHistogramBucketCount - 1;
        }
        int percent = static_cast<int>(std::lround(requested_scale * 100.0f));
        percent     = std::clamp(percent, 10, 200);
        const int bucket = std::clamp((100 - percent) / 10, 0, kHistogramBucketCount - 1);
        return bucket;
    }

    static inline std::vector<int> compute_recommendations(const std::vector<std::uint64_t>& histogram) {
        std::vector<int> result;
        if (histogram.empty()) {
            return result;
        }
        bool has_usage = false;
        for (std::uint64_t value : histogram) {
            if (value > 0) {
                has_usage = true;
                break;
            }
        }
        if (!has_usage) {
            const auto& defaults = DefaultScaleSteps();
            result.reserve(defaults.size());
            for (float step : defaults) {
                result.push_back(static_cast<int>(std::lround(step * 100.0f)));
            }
            return result;
        }

        result.push_back(100);
        struct BucketInfo {
            int              index;
            std::uint64_t    count;
        };
        std::vector<BucketInfo> buckets;
        buckets.reserve(histogram.size());
        for (std::size_t idx = 1; idx < histogram.size(); ++idx) {
            buckets.push_back(BucketInfo{ static_cast<int>(idx), histogram[idx] });
        }
        std::sort(buckets.begin(), buckets.end(), [](const BucketInfo& a, const BucketInfo& b) {
            if (a.count == b.count) {
                return a.index < b.index;
            }
            return a.count > b.count;
        });

        for (const BucketInfo& bucket : buckets) {
            if (bucket.count == 0) {
                break;
            }
            const int percent = std::clamp(100 - bucket.index * 10, 10, 200);
            if (std::find(result.begin(), result.end(), percent) == result.end()) {
                result.push_back(percent);
            }
            if (result.size() >= kMaxRecommendedVariants) {
                break;
            }
        }

        if (result.size() == 1) {
            const auto& defaults = DefaultScaleSteps();
            for (std::size_t idx = 1; idx < defaults.size() && result.size() < kMaxRecommendedVariants; ++idx) {
                const int percent = static_cast<int>(std::lround(defaults[idx] * 100.0f));
                if (std::find(result.begin(), result.end(), percent) == result.end()) {
                    result.push_back(percent);
                }
            }
        }

        std::sort(result.begin(), result.end(), std::greater<int>());
        return result;
    }

    static inline bool update_entry(nlohmann::json& entry,
                                    const std::vector<std::uint64_t>& histogram,
                                    const std::vector<int>& recommended,
                                    float requested_scale,
                                    float stored_scale) {
        bool changed = false;

        nlohmann::json hist_json = nlohmann::json::array();
        for (std::uint64_t value : histogram) {
            hist_json.push_back(value);
        }
        if (!entry.contains("histogram") || entry["histogram"] != hist_json) {
            entry["histogram"] = std::move(hist_json);
            changed             = true;
        }

        nlohmann::json recommended_json = nlohmann::json::array();
        for (int value : recommended) {
            recommended_json.push_back(value);
        }
        if (!entry.contains("recommended_percentages") || entry["recommended_percentages"] != recommended_json) {
            entry["recommended_percentages"] = std::move(recommended_json);
            std::uint64_t revision = entry.value("revision", static_cast<std::uint64_t>(0));
            entry["revision"]     = revision + 1;
            entry["last_updated"] = timestamp_now();
            changed                = true;
        }

        const int requested_percent = static_cast<int>(std::lround(requested_scale * 100.0f));
        entry["last_requested_percent"] = requested_percent;
        entry["last_requested_scale"]   = requested_scale;
        entry["last_used_texture_scale"] = stored_scale;
        return changed;
    }

    static inline bool save_to_disk(UsageState& state) {
        if (!state.dirty) {
            return true;
        }
        try {
            if (!state.file_path.empty()) {
                std::filesystem::create_directories(state.file_path.parent_path());
            }
            std::ofstream out(state.file_path);
            if (!out.good()) {
                return false;
            }
            out << state.data.dump(4);
            state.dirty = false;
            return true;
        } catch (...) {
            return false;
        }
    }

    static constexpr int kHistogramBucketCount    = 10;
    static constexpr std::size_t kMaxRecommendedVariants = 8;
};

inline SDL_Texture* CreateScaledTexture(SDL_Renderer* renderer,
                                        SDL_Texture* source,
                                        int src_w,
                                        int src_h,
                                        float scale) {
    if (!renderer || !source || scale <= 0.0f) {
        return nullptr;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src_h) * scale)));

    if (dst_w == src_w && dst_h == src_h) {
        return nullptr;
    }

    Uint32 format = SDL_PIXELFORMAT_RGBA8888;
    if (SDL_QueryTexture(source, &format, nullptr, nullptr, nullptr) != 0) {
        format = SDL_PIXELFORMAT_RGBA8888;
    }

    SDL_Texture* scaled = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_TARGET, dst_w, dst_h);
    if (!scaled) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(scaled, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(scaled, SDL_ScaleModeBest);
#endif

    SDL_Texture* previous_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, scaled);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_Rect dst{0, 0, dst_w, dst_h};
    SDL_RenderCopy(renderer, source, nullptr, &dst);

    SDL_SetRenderTarget(renderer, previous_target);
    return scaled;
}

inline SDL_Surface* CreateScaledSurface(SDL_Surface* src, float scale) {
    if (!src || scale <= 0.0f) {
        return nullptr;
    }

    if (std::fabs(scale - 1.0f) <= 1e-4f) {
        SDL_Surface* copy = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!copy) {
            return nullptr;
        }
        SDL_Rect rect{0, 0, src->w, src->h};
        if (SDL_BlitSurface(src, &rect, copy, &rect) != 0) {
            SDL_FreeSurface(copy);
            return nullptr;
        }
        return copy;
    }

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(src->w) * scale)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(src->h) * scale)));

    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, dst_w, dst_h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!dst) {
        return nullptr;
    }

    SDL_Rect src_rect{0, 0, src->w, src->h};
    SDL_Rect dst_rect{0, 0, dst_w, dst_h};
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    if (SDL_BlitScaled(src, &src_rect, dst, &dst_rect) != 0) {
        SDL_FreeSurface(dst);
        return nullptr;
    }

    return dst;
}

}  // namespace render_pipeline
