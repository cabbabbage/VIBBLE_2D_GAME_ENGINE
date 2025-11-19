#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL.h>

#include "core/manifest/manifest_loader.hpp"

namespace render_pipeline {

namespace detail {
    inline float& quality_cap_storage() {
        static float cap = 1.0f;
        return cap;
    }
    inline std::once_flag& scale_hint_once() {
        static std::once_flag flag;
        return flag;
    }
}

inline void EnsureBestScaleHint() {
#if SDL_VERSION_ATLEAST(2,0,12)
    std::call_once(detail::scale_hint_once(), []() {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    });
#endif
}

struct ScaleSelection {
    int   index           = 0;
    float requested_scale = 1.0f;
    float stored_scale    = 1.0f;
    float remainder_scale = 1.0f;
    float hysteresis_min  = 0.0f;
    float hysteresis_max  = std::numeric_limits<float>::max();
    int   preload_index   = -1;
};

struct ScalingLogic {
    using ScaleSteps = std::vector<float>;

    struct HysteresisState {
        int   last_index = 0;
        float min_scale  = 0.0f;
        float max_scale  = std::numeric_limits<float>::max();
    };

    struct HysteresisOptions {
        float margin         = 0.05f;
        float preload_margin = 0.02f;
    };

    static constexpr float kDefaultHysteresisMargin = 0.05f;
    static constexpr float kDefaultPreloadMargin    = 0.02f;

    static void SetQualityCap(float cap) {
        if (!std::isfinite(cap) || cap <= 0.0f) {
            cap = 0.1f;
        }
        cap = std::clamp(cap, 0.1f, 1.0f);
        detail::quality_cap_storage() = cap;
    }

    static float QualityCap() {
        return detail::quality_cap_storage();
    }

    struct ScaleProfile {
        ScaleSteps    steps;
        std::uint64_t revision  = 0;
        bool          had_entry = false;
        bool          created_entry = false;
        bool          revision_changed = false;
        float         min_scale = 1.0f;
        float         max_scale = 1.0f;
        bool has_custom_steps() const { return !steps.empty(); }
    };

    static constexpr std::size_t kMaxVariantCount     = 4;
    static constexpr std::size_t kDefaultVariantCount = kMaxVariantCount;
    static inline const ScaleSteps& DefaultScaleSteps() {
        static const ScaleSteps kDefaultSteps = {1.00f, 0.75f, 0.50f, 0.25f};
        return kDefaultSteps;
    }

    static inline void NormalizeVariantSteps(ScaleSteps& steps) {
        ScaleSteps cleaned;
        cleaned.reserve(steps.size());
        for (float value : steps) {
            if (!std::isfinite(value) || value <= 0.0f) {
                continue;
            }
            cleaned.push_back(value);
        }

        if (cleaned.empty()) {
            const auto& defaults = DefaultScaleSteps();
            cleaned.assign(defaults.begin(), defaults.end());
        }

        std::sort(cleaned.begin(), cleaned.end(), std::greater<float>());
        cleaned.erase(std::unique(cleaned.begin(), cleaned.end(), [](float a, float b) {
            return std::fabs(a - b) <= 1e-4f;
        }), cleaned.end());

        if (cleaned.empty()) {
            cleaned.push_back(1.0f);
        }

        if (std::fabs(cleaned.front() - 1.0f) > 1e-4f) {
            cleaned.insert(cleaned.begin(), 1.0f);
        }

        ScaleSteps prioritized;
        prioritized.reserve(kMaxVariantCount);
        constexpr float kMinSpacing = 0.05f;
        for (float value : cleaned) {
            bool too_close = false;
            for (float existing : prioritized) {
                if (std::fabs(existing - value) < kMinSpacing) {
                    too_close = true;
                    break;
                }
            }
            if (!too_close) {
                prioritized.push_back(value);
            }
            if (prioritized.size() >= kMaxVariantCount) {
                break;
            }
        }

        if (prioritized.size() < kMaxVariantCount) {
            const auto& defaults = DefaultScaleSteps();
            for (float value : defaults) {
                if (prioritized.size() >= kMaxVariantCount) {
                    break;
                }
                bool exists = false;
                for (float existing : prioritized) {
                    if (std::fabs(existing - value) < kMinSpacing) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    prioritized.push_back(value);
                }
            }
        }

        if (prioritized.empty()) {
            prioritized.push_back(1.0f);
        }

        std::sort(prioritized.begin(), prioritized.end(), std::greater<float>());
        if (prioritized.size() > kMaxVariantCount) {
            prioritized.resize(kMaxVariantCount);
        }

        steps.swap(prioritized);
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
        return Choose(desired_scale,
                      DefaultScaleSteps(),
                      HysteresisState{},
                      desired_scale,
                      HysteresisOptions{});
    }

    static inline ScaleSelection Choose(float desired_scale, const ScaleSteps& steps) {
        return Choose(desired_scale,
                      steps,
                      HysteresisState{},
                      desired_scale,
                      HysteresisOptions{});
    }

    static inline ScaleSelection Choose(float desired_scale,
                                        const ScaleSteps& steps,
                                        const HysteresisState& state,
                                        float smoothed_scale,
                                        HysteresisOptions options = HysteresisOptions{}) {
        // Base selection prefers downscaling: choose smallest stored scale >= requested.
        // If none is available (e.g., zoomed-in beyond largest allowed), choose largest allowed.
        ScaleSelection base = choose_closest(desired_scale, steps);
        if (steps.empty()) {
            return base;
        }

        if (!std::isfinite(options.margin) || options.margin < 0.0f) {
            options.margin = kDefaultHysteresisMargin;
        }
        if (!std::isfinite(options.preload_margin) || options.preload_margin < 0.0f) {
            options.preload_margin = kDefaultPreloadMargin;
        }

        const float safe_smoothed = (std::isfinite(smoothed_scale) && smoothed_scale > 0.0f)
                                        ? smoothed_scale
                                        : base.requested_scale;

        HysteresisState current = state;
        const int max_index = static_cast<int>(steps.size() - 1);
        current.last_index = std::clamp(current.last_index, 0, max_index);
        if (!std::isfinite(current.min_scale) || current.min_scale < 0.0f) {
            current.min_scale = 0.0f;
        }
        if (!std::isfinite(current.max_scale) || current.max_scale < current.min_scale) {
            current.max_scale = std::numeric_limits<float>::max();
        }

        int candidate = current.last_index;
        auto bounds = variant_bounds(steps, candidate, options.margin);
        float min_bound = bounds.first;
        float max_bound = bounds.second;

        if (safe_smoothed >= current.min_scale && safe_smoothed <= current.max_scale) {
            // Stay with previous variant while within hysteresis window.
            candidate = current.last_index;
            bounds = variant_bounds(steps, candidate, options.margin);
            min_bound = bounds.first;
            max_bound = bounds.second;
        } else if (safe_smoothed < current.min_scale && candidate < max_index) {
            do {
                candidate = std::min(candidate + 1, max_index);
                bounds = variant_bounds(steps, candidate, options.margin);
                min_bound = bounds.first;
                max_bound = bounds.second;
            } while (safe_smoothed < min_bound && candidate < max_index);
        } else if (safe_smoothed > current.max_scale && candidate > 0) {
            do {
                candidate = std::max(candidate - 1, 0);
                bounds = variant_bounds(steps, candidate, options.margin);
                min_bound = bounds.first;
                max_bound = bounds.second;
            } while (safe_smoothed > max_bound && candidate > 0);
        } else {
            candidate = base.index;
            bounds = variant_bounds(steps, candidate, options.margin);
            min_bound = bounds.first;
            max_bound = bounds.second;
        }

        // Do not clamp candidate to the "closest" index; allow selecting a larger
        // variant (smaller index) when it avoids upscaling a smaller variant.

        ScaleSelection result = base;
        result.index = candidate;
        result.stored_scale = steps[candidate];
        if (result.stored_scale <= 0.0f) {
            result.stored_scale = 1.0f;
        }
        result.remainder_scale = (result.stored_scale > 0.0f)
                                     ? (result.requested_scale / result.stored_scale)
                                     : 1.0f;
        bounds = variant_bounds(steps, candidate, options.margin);
        result.hysteresis_min = bounds.first;
        result.hysteresis_max = bounds.second;

        result.preload_index = -1;
        float best_distance = std::numeric_limits<float>::max();

        if (candidate < max_index) {
            const float boundary = 0.5f * (steps[candidate] + steps[candidate + 1]);
            const float diff = std::fabs(safe_smoothed - boundary);
            if (diff <= options.preload_margin) {
                result.preload_index = candidate + 1;
                best_distance       = diff;
            }
        }
        if (candidate > 0) {
            const float boundary = 0.5f * (steps[candidate] + steps[candidate - 1]);
            const float diff = std::fabs(safe_smoothed - boundary);
            if (diff <= options.preload_margin && diff < best_distance && (candidate - 1) >= base.index) {
                result.preload_index = candidate - 1;
                best_distance       = diff;
            }
        }

        if (result.preload_index < 0 || result.preload_index > max_index) {
            result.preload_index = -1;
        }

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
        return std::filesystem::path(base).append("scale_" + std::to_string(ScalePercent(steps, index))).string();
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

    static inline void LoadPrecomputedProfiles(bool force_reload = false) {
        ProfilesState& state = profiles_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (force_reload) {
            state.loaded = false;
        }
        ensure_loaded(state);
    }

    static inline ScaleProfile ProfileForAsset(const std::string& asset_key) {
        ProfilesState& state = profiles_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);

        ScaleProfile profile;
        profile.had_entry = false;
        profile.created_entry = false;
        profile.min_scale = 1.0f;
        profile.max_scale = 1.0f;

        if (!asset_key.empty()) {
            auto it = state.entries.find(asset_key);
            if (it != state.entries.end()) {
                profile.had_entry = true;
                profile.steps     = it->second.steps;
                profile.revision  = it->second.revision;
                profile.min_scale = it->second.min_scale;
                profile.max_scale = it->second.max_scale;
                record_profile_history(state, asset_key, profile);
                return profile;
            }
        }

        const auto& defaults = DefaultScaleSteps();
        profile.steps.assign(defaults.begin(), defaults.end());
        profile.revision = 0;
        record_profile_history(state, asset_key, profile);
        return profile;
    }

private:
    static inline ScaleSelection choose_closest(float desired_scale, const ScaleSteps& steps) {
        ScaleSelection result{};
        if (steps.empty()) {
            result.requested_scale = std::isfinite(desired_scale) && desired_scale > 0.0f ? desired_scale : 1.0f;
            result.stored_scale    = 1.0f;
            result.index           = 0;
            result.remainder_scale = result.requested_scale;
            return result;
        }
        float sanitized = desired_scale;
        if (!std::isfinite(sanitized)) {
            sanitized = 1.0f;
        }
        if (sanitized <= 0.0f) {
            sanitized = steps.back();
        }

        result.requested_scale = sanitized;

        const float quality_cap = QualityCap();
        const bool  enforce_cap = std::isfinite(quality_cap) && quality_cap > 0.0f && quality_cap < 0.999f;
        bool has_allowed = false;
        if (enforce_cap) {
            for (float candidate : steps) {
                if (candidate <= quality_cap + 1e-4f) {
                    has_allowed = true;
                    break;
                }
            }
        }

        // Steps are sorted descending. Pick the first allowed step >= requested.
        int   chosen_index = -1;
        float chosen_scale = steps.front();
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const float candidate = steps[i];
            if (enforce_cap && has_allowed && candidate > quality_cap + 1e-4f) {
                continue;
            }
            if (candidate + 1e-4f >= sanitized) {
                chosen_index = static_cast<int>(i);
                chosen_scale = candidate;
                break;
            }
        }

        // If none is >= requested (zoomed-in), choose the largest allowed.
        if (chosen_index < 0) {
            for (std::size_t i = 0; i < steps.size(); ++i) {
                const float candidate = steps[i];
                if (enforce_cap && has_allowed && candidate > quality_cap + 1e-4f) {
                    continue;
                }
                chosen_index = static_cast<int>(i);
                chosen_scale = candidate;
                break;
            }
            if (chosen_index < 0) {
                // Fallback to the largest step if filtering excluded all.
                chosen_index = 0;
                chosen_scale = steps.front();
            }
        }

        result.index           = chosen_index;
        result.stored_scale    = chosen_scale;
        result.remainder_scale = (chosen_scale > 0.0f) ? (sanitized / chosen_scale) : 1.0f;
        return result;
    }

    static inline std::pair<float, float> variant_bounds(const ScaleSteps& steps,
                                                         int index,
                                                         float margin) {
        if (steps.empty()) {
            return {0.0f, std::numeric_limits<float>::max()};
        }
        const float safe_margin = (std::isfinite(margin) && margin > 0.0f) ? margin : 0.0f;
        const float current     = steps[std::clamp(index, 0, static_cast<int>(steps.size() - 1))];
        float min_bound = 0.0f;
        float max_bound = std::numeric_limits<float>::max();

        if (index + 1 < static_cast<int>(steps.size())) {
            const float boundary = 0.5f * (current + steps[index + 1]);
            min_bound = std::max(0.0f, boundary - safe_margin);
        }
        if (index > 0) {
            const float boundary = 0.5f * (current + steps[index - 1]);
            max_bound = boundary + safe_margin;
        }

        if (min_bound > max_bound) {
            const float midpoint = 0.5f * (min_bound + max_bound);
            min_bound            = std::min(min_bound, midpoint);
            max_bound            = std::max(max_bound, midpoint);
        }

        return {min_bound, max_bound};
    }

    struct ProfileEntry {
        ScaleSteps    steps;
        std::uint64_t revision = 0;
        float         min_scale = 1.0f;
        float         max_scale = 1.0f;
    };

    struct ProfileObservation {
        bool          had_entry = false;
        std::uint64_t revision  = 0;
    };

    struct ProfilesState {
        bool                   loaded = false;
        std::mutex             mutex;
        std::unordered_map<std::string, ProfileEntry> entries;
        std::unordered_map<std::string, ProfileObservation> history;
    };

    static inline ProfilesState& profiles_state() {
        static ProfilesState state;
        return state;
    }

    static inline void ensure_loaded(ProfilesState& state) {
        if (state.loaded) {
            return;
        }
        state.loaded = true;
        state.entries.clear();

        manifest::ManifestData manifest_data;
        try {
            manifest_data = manifest::load_manifest();
        } catch (...) {
            return;
        }

        if (!manifest_data.assets.is_object()) {
            return;
        }

        for (auto it = manifest_data.assets.begin(); it != manifest_data.assets.end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }

            const auto profile_it = it.value().find("scaling_profile");
            if (profile_it == it.value().end() || !profile_it->is_object()) {
                continue;
            }

            ProfileEntry entry;
            entry.revision  = profile_it->value("revision", static_cast<std::uint64_t>(0));
            entry.min_scale = static_cast<float>(profile_it->value("min_scale", 1.0));
            entry.max_scale = static_cast<float>(profile_it->value("max_scale", 1.0));

            if (auto steps_it = profile_it->find("recommended_steps"); steps_it != profile_it->end() && steps_it->is_array()) {
                for (const auto& value : *steps_it) {
                    if (!value.is_number()) {
                        continue;
                    }
                    entry.steps.push_back(static_cast<float>(value.get<double>()));
                }
            } else if (auto perc_it = profile_it->find("recommended_percentages");
                       perc_it != profile_it->end() && perc_it->is_array()) {
                for (const auto& value : *perc_it) {
                    if (!value.is_number()) {
                        continue;
                    }
                    entry.steps.push_back(static_cast<float>(value.get<double>()) / 100.0f);
                }
            }

            NormalizeVariantSteps(entry.steps);
            state.entries.emplace(it.key(), std::move(entry));
        }
    }

    static inline void record_profile_history(ProfilesState& state,
                                              const std::string& asset_key,
                                              ScaleProfile& profile) {
        if (asset_key.empty()) {
            return;
        }

        auto [it, inserted] = state.history.emplace(asset_key, ProfileObservation{
            profile.had_entry,
            profile.revision
        });

        if (inserted) {
            // First observation becomes the baseline; do not flag any changes.
            return;
        }

        ProfileObservation& previous = it->second;
        if (!previous.had_entry && profile.had_entry) {
            profile.created_entry = true;
        }
        if (previous.had_entry && profile.had_entry && previous.revision != profile.revision) {
            profile.revision_changed = true;
        }

        previous.had_entry = profile.had_entry;
        previous.revision  = profile.revision;
    }
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

    EnsureBestScaleHint();

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

} // namespace render_pipeline
