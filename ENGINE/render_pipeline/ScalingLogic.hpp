#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL.h>
#include <nlohmann/json.hpp>

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
        ScaleSteps          steps;
        std::uint64_t       revision = 0;
        bool                had_entry = false;
        bool                created_entry = false;
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
        return std::filesystem::path(base) .append("scale_" + std::to_string(ScalePercent(steps, index))) .string();
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
        refresh_usage_samples_flag(state);
    }

    static inline void SetUsageTrackingEnabled(bool enabled) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        const bool was_enabled = state.enabled;
        state.enabled          = enabled;
        if (enabled && !was_enabled) {
            if (update_new_values_flag(state, true)) {

                save_to_disk(state);
            }
        }
    }

    static inline bool ToggleUsageTracking() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        const bool new_state = !state.enabled;
        state.enabled        = new_state;
        if (new_state) {
            if (update_new_values_flag(state, true)) {
                save_to_disk(state);
            }
        }
        return new_state;
    }

    static inline bool MarkNewValuesPending() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        const bool previous = state.data.value("new_values", false);
        if (update_new_values_flag(state, true)) {
            save_to_disk(state);
        }
        return previous;
    }

    static inline bool ConsumeNewValues() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        const bool previous = state.data.value("new_values", false);
        if (update_new_values_flag(state, false)) {
            save_to_disk(state);
        }
        return previous;
    }

    static inline bool NewValuesPending() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        return state.data.value("new_values", false);
    }

    static inline bool HasPendingUsageData() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        return state.data.value("new_values", false);
    }

    static inline void ClearPendingUsageData() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        if (update_new_values_flag(state, false)) {
            save_to_disk(state);
        }
    }

    static inline void ResetAssetUsage(const std::string& asset_key) {
        if (asset_key.empty()) {
            return;
        }

        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);

        state.pending_samples.erase(asset_key);
        state.next_allowed_sample.erase(asset_key);
        state.queued_assets.erase(asset_key);
        auto& queue = state.sampling_queue;
        queue.erase(std::remove(queue.begin(), queue.end(), asset_key), queue.end());
        refresh_usage_samples_flag(state);

        bool changed = false;
        if (state.data.contains("assets") && state.data["assets"].is_object()) {
            auto& assets = state.data["assets"];
            auto it = assets.find(asset_key);
            if (it != assets.end()) {
                assets.erase(it);
                changed = true;
            }
        }

        if (changed) {
            state.dirty = true;
            save_to_disk(state);
        }
    }

    static inline bool UsageTrackingEnabled() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        return state.enabled;
    }

    static inline bool UsageSamplingPending() {
        return usage_samples_pending_flag().load(std::memory_order_acquire);
    }

    static inline void RecordUsage(const std::string& asset_key, float requested_scale, float stored_scale) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.enabled || asset_key.empty()) {
            return;
        }
        ensure_loaded(state);
        enqueue_usage_sample(state, asset_key, requested_scale, stored_scale);
    }

    static inline bool FlushUsageData() {
        wait_for_sampling_task();

        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);
        const Uint32 now = SDL_GetTicks();
        const bool merged = process_pending_samples(state, now, true);
        refresh_usage_samples_flag(state);
        if (merged || state.dirty) {
            return save_to_disk(state);
        }
        return true;
    }

    static inline void TickUsageSampling() {
        wait_for_sampling_task();
        TickUsageSamplingWork();
    }

    static inline void ScheduleUsageSamplingAsync() {
        if (!UsageSamplingPending()) {
            return;
        }

        UsageState& state = usage_state();
        {
            std::lock_guard<std::mutex> guard(state.mutex);
            if (!state.enabled) {
                refresh_usage_samples_flag(state);
                return;
            }
            if (state.pending_samples.empty() && state.queued_assets.empty()) {
                refresh_usage_samples_flag(state);
                return;
            }
        }

        auto& pending_flag = sampling_pending_flag();
        bool   expected    = false;
        if (!pending_flag.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        auto worker = []() {
            struct ResetFlag {
                ~ResetFlag() {
                    sampling_pending_flag().store(false, std::memory_order_release);
                }
            } reset_flag;

            try {
                TickUsageSamplingWork();
            } catch (...) {
            }
        };

        std::future<void> new_task;
        try {
            new_task = std::async(std::launch::async, std::move(worker));
        } catch (...) {
            pending_flag.store(false, std::memory_order_release);
            TickUsageSamplingWork();
            return;
        }

        std::future<void> previous;
        {
            std::lock_guard<std::mutex> guard(sampling_task_mutex());
            auto& stored = sampling_task_handle();
            if (stored.valid()) {
                previous = std::move(stored);
            }
            stored = std::move(new_task);
        }

        if (previous.valid()) {
            try {
                previous.wait();
            } catch (...) {
            }
        }
    }

    static inline void ShutdownUsageSampling() {
        wait_for_sampling_task();
        FlushUsageData();
        wait_for_sampling_task();
    }

    static inline ScaleProfile ProfileForAsset(const std::string& asset_key) {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        ensure_loaded(state);

        ScaleProfile profile;

        if (!asset_key.empty() && state.data.contains("assets") && state.data["assets"].is_object()) {
            const auto& assets = state.data["assets"];
            auto it = assets.find(asset_key);
            if (it != assets.end() && it->is_object()) {
                profile.had_entry = true;
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

        if (!profile.had_entry) {
            ensure_assets_container(state);
            nlohmann::json& entry = state.data["assets"][asset_key];
            if (!entry.is_object()) {
                entry = default_asset_entry();
            }

            const auto& defaults = DefaultScaleSteps();
            const std::size_t initial_variant_count = kMaxVariantCount;
            profile.steps.clear();
            for (std::size_t idx = 0; idx < initial_variant_count && idx < defaults.size(); ++idx) {
                profile.steps.push_back(defaults[idx]);
            }

            NormalizeVariantSteps(profile.steps);

            nlohmann::json recommended_json = nlohmann::json::array();
            for (float step : profile.steps) {
                recommended_json.push_back(static_cast<int>(std::lround(step * 100.0f)));
            }
            entry["recommended_percentages"] = std::move(recommended_json);
            std::uint64_t revision = entry.value("revision", static_cast<std::uint64_t>(0));
            revision += 1;
            entry["revision"]     = revision;
            entry["last_updated"] = timestamp_now();
            profile.revision       = revision;
            profile.created_entry  = true;
            state.dirty            = true;
            save_to_disk(state);
        }

        NormalizeVariantSteps(profile.steps);

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
        struct Sample {
            float requested_scale = 1.0f;
            float stored_scale    = 1.0f;
};
        std::unordered_map<std::string, std::vector<Sample>> pending_samples;
        std::unordered_map<std::string, Uint32>               next_allowed_sample;
        std::deque<std::string>                               sampling_queue;
        std::unordered_set<std::string>                       queued_assets;
};

    static inline UsageState& usage_state() {
        static UsageState state;
        return state;
    }

    static inline std::atomic<bool>& usage_samples_pending_flag() {
        static std::atomic<bool> pending{false};
        return pending;
    }

    static inline std::atomic<bool>& sampling_pending_flag() {
        static std::atomic<bool> pending{false};
        return pending;
    }

    static inline std::mutex& sampling_task_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    static inline std::future<void>& sampling_task_handle() {
        static std::future<void> task;
        return task;
    }

    static inline bool has_pending_usage_samples(const UsageState& state) {
        return !state.pending_samples.empty() || !state.queued_assets.empty();
    }

    static inline void refresh_usage_samples_flag(const UsageState& state) {
        usage_samples_pending_flag().store(has_pending_usage_samples(state), std::memory_order_release);
    }

    static inline void wait_for_sampling_task() {
        std::future<void> task;
        {
            std::lock_guard<std::mutex> guard(sampling_task_mutex());
            auto& stored = sampling_task_handle();
            if (stored.valid()) {
                task = std::move(stored);
            }
        }

        if (task.valid()) {
            try {
                task.wait();
            } catch (...) {
            }
        }
    }

    static inline void TickUsageSamplingWork() {
        UsageState& state = usage_state();
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.enabled) {
            return;
        }
        ensure_loaded(state);
        const Uint32 now    = SDL_GetTicks();
        const bool   merged = process_pending_samples(state, now, false);
        refresh_usage_samples_flag(state);
        if (merged && state.dirty) {
            save_to_disk(state);
        }
    }

    static inline nlohmann::json default_storage() {
        nlohmann::json data;
        data["version"]    = 1;
        data["assets"]     = nlohmann::json::object();
        data["new_values"] = false;
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

        if (!state.data.contains("new_values") || !state.data["new_values"].is_boolean()) {
            state.data["new_values"] = false;
            state.dirty               = true;
        }
    }

    static inline void ensure_assets_container(UsageState& state) {
        if (!state.data.contains("assets") || !state.data["assets"].is_object()) {
            state.data["assets"] = nlohmann::json::object();
        }
    }

    static inline void enqueue_usage_sample(UsageState& state,
                                            const std::string& asset_key,
                                            float requested_scale,
                                            float stored_scale) {
        auto& samples = state.pending_samples[asset_key];
        samples.push_back(UsageState::Sample{ requested_scale, stored_scale });
        if (state.queued_assets.insert(asset_key).second) {
            state.sampling_queue.push_back(asset_key);
        }
        usage_samples_pending_flag().store(true, std::memory_order_release);
    }

    static inline bool merge_samples_for_asset(UsageState& state,
                                               const std::string& asset_key,
                                               const std::vector<UsageState::Sample>& samples) {
        if (samples.empty()) {
            return false;
        }

        ensure_assets_container(state);
        auto& assets = state.data["assets"];
        nlohmann::json& entry = assets[asset_key];
        if (!entry.is_object()) {
            entry = default_asset_entry();
        }

        std::vector<std::uint64_t> histogram = parse_histogram(entry);
        for (const auto& sample : samples) {
            const int bucket = histogram_bucket(sample.requested_scale);
            if (bucket >= 0 && static_cast<std::size_t>(bucket) < histogram.size()) {
                histogram[static_cast<std::size_t>(bucket)] += 1;
            }
        }

        const UsageState::Sample& last_sample = samples.back();
        const std::vector<int> recommended = compute_recommendations(histogram);
        const bool changed = update_entry(entry, histogram, recommended, last_sample.requested_scale, last_sample.stored_scale);
        if (changed) {
            state.dirty = true;
        }
        return changed;
    }

    static inline bool process_pending_samples(UsageState& state, Uint32 now, bool process_all) {
        bool merged = false;
        std::size_t iterations = state.sampling_queue.size();
        std::size_t processed  = 0;

        while (!state.sampling_queue.empty() && iterations-- > 0) {
            std::string asset_key = std::move(state.sampling_queue.front());
            state.sampling_queue.pop_front();
            state.queued_assets.erase(asset_key);

            auto pending_it = state.pending_samples.find(asset_key);
            if (pending_it == state.pending_samples.end() || pending_it->second.empty()) {
                state.pending_samples.erase(asset_key);
                continue;
            }

            const Uint32 next_allowed = state.next_allowed_sample[asset_key];
            const bool eligible = process_all || SDL_TICKS_PASSED(now, next_allowed);
            if (!eligible) {
                state.sampling_queue.push_back(asset_key);
                state.queued_assets.insert(asset_key);
                continue;
            }

            merged = merge_samples_for_asset(state, asset_key, pending_it->second) || merged;
            state.next_allowed_sample[asset_key] = now + kSamplingIntervalMs;
            state.pending_samples.erase(pending_it);
            ++processed;

            if (!process_all && processed >= 1) {
                break;
            }
        }

        return merged;
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

        constexpr int kMinPercentSpacing = 5;
        auto add_percent = [&](int percent) {
            percent = std::clamp(percent, 10, 200);
            for (int existing : result) {
                if (std::abs(existing - percent) < kMinPercentSpacing) {
                    return false;
                }
            }
            result.push_back(percent);
            return true;
};

        bool has_usage = false;
        for (std::uint64_t value : histogram) {
            if (value > 0) {
                has_usage = true;
                break;
            }
        }

        if (!has_usage) {
            add_percent(100);
            const auto& defaults = DefaultScaleSteps();
            for (float step : defaults) {
                add_percent(static_cast<int>(std::lround(step * 100.0f)));
                if (result.size() >= kMaxVariantCount) {
                    break;
                }
            }
            std::sort(result.begin(), result.end(), std::greater<int>());
            if (result.size() > kMaxVariantCount) {
                result.resize(kMaxVariantCount);
            }
            return result;
        }

        add_percent(100);
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
            const int bucket_center = 100 - bucket.index * 10 - 5;
            add_percent(bucket_center);
            if (result.size() >= kMaxVariantCount) {
                break;
            }
        }

        if (result.size() < kMaxVariantCount) {
            const auto& defaults = DefaultScaleSteps();
            for (float step : defaults) {
                add_percent(static_cast<int>(std::lround(step * 100.0f)));
                if (result.size() >= kMaxVariantCount) {
                    break;
                }
            }
        }

        std::sort(result.begin(), result.end(), std::greater<int>());
        if (result.size() > kMaxVariantCount) {
            result.resize(kMaxVariantCount);
        }
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

    static inline bool update_new_values_flag(UsageState& state, bool value) {
        if (!state.data.contains("new_values") || !state.data["new_values"].is_boolean()) {
            state.data["new_values"] = false;
            state.dirty               = true;
        }
        const bool current = state.data["new_values"].get<bool>();
        if (current == value) {
            return false;
        }
        state.data["new_values"] = value;
        state.dirty               = true;
        return true;
    }

    static constexpr Uint32 kSamplingIntervalMs   = 15'000u;
    static constexpr int    kHistogramBucketCount = 10;
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

}
