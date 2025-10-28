#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

namespace render_pipeline {

namespace detail {
    inline float& quality_cap_storage() {
        static float cap = 1.0f;
        return cap;
    }
}

struct ScaleSelection {
    int   index           = 0;
    float requested_scale = 1.0f;
    float stored_scale    = 1.0f;
    float remainder_scale = 1.0f;
};

struct ScalingLogic {
    using ScaleSteps = std::vector<float>;

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
        float         min_scale = 1.0f;
        float         max_scale = 1.0f;
        bool has_custom_steps() const { return !steps.empty(); }
    };

    static constexpr std::size_t kMaxVariantCount     = 3;
    static constexpr std::size_t kDefaultVariantCount = kMaxVariantCount;
    static inline const ScaleSteps& DefaultScaleSteps() {
        static const ScaleSteps kDefaultSteps = {1.00f, 0.75f, 0.50f};
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

        const float quality_cap = QualityCap();
        const bool enforce_cap = std::isfinite(quality_cap) && quality_cap > 0.0f && quality_cap < 0.999f;
        bool has_allowed = false;
        if (enforce_cap) {
            for (float candidate : steps) {
                if (candidate <= quality_cap + 1e-4f) {
                    has_allowed = true;
                    break;
                }
            }
        }

        for (std::size_t i = 0; i < steps.size(); ++i) {
            const float candidate = steps[i];
            if (enforce_cap && has_allowed && candidate > quality_cap + 1e-4f) {
                continue;
            }
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

    static inline void LoadPrecomputedProfiles(const std::filesystem::path& path = std::filesystem::path()) {
        ProfilesState& state = profiles_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!path.empty() && path != state.file_path) {
            state.file_path = path;
            state.loaded    = false;
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
                return profile;
            }
        }

        const auto& defaults = DefaultScaleSteps();
        profile.steps.assign(defaults.begin(), defaults.end());
        profile.revision = 0;
        return profile;
    }

private:
    struct ProfileEntry {
        ScaleSteps    steps;
        std::uint64_t revision = 0;
        float         min_scale = 1.0f;
        float         max_scale = 1.0f;
    };

    struct ProfilesState {
        std::filesystem::path file_path;
        bool                   loaded = false;
        std::mutex             mutex;
        std::unordered_map<std::string, ProfileEntry> entries;
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

        if (state.file_path.empty()) {
            state.file_path = std::filesystem::path("loading") / "scaling_profiles.json";
        }

        std::ifstream in(state.file_path);
        if (!in.good()) {
            return;
        }

        nlohmann::json root;
        try {
            in >> root;
        } catch (...) {
            return;
        }

        if (!root.is_object()) {
            return;
        }

        auto assets_it = root.find("assets");
        if (assets_it == root.end() || !assets_it->is_object()) {
            return;
        }

        for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            ProfileEntry entry;
            entry.revision = it.value().value("revision", static_cast<std::uint64_t>(0));
            entry.min_scale = static_cast<float>(it.value().value("min_scale", 1.0));
            entry.max_scale = static_cast<float>(it.value().value("max_scale", 1.0));

            if (auto steps_it = it.value().find("recommended_steps"); steps_it != it.value().end() && steps_it->is_array()) {
                for (const auto& value : *steps_it) {
                    if (!value.is_number()) {
                        continue;
                    }
                    entry.steps.push_back(static_cast<float>(value.get<double>()));
                }
            } else if (auto perc_it = it.value().find("recommended_percentages"); perc_it != it.value().end() && perc_it->is_array()) {
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

} // namespace render_pipeline
