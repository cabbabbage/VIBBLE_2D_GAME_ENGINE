#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include <limits>
#include <SDL.h>

#include "utils/light_source.hpp"
#include "utils/shadow_mask_settings.hpp"

class Asset;
class camera_grid;
class Animation;
class AssetInfo;

namespace runtime {

class TemporaryMergedAssetInfo {
public:
    explicit TemporaryMergedAssetInfo(std::string name);

    void absorb(const Asset& asset, SDL_Point merged_origin);
    void set_geometry(int width, int height, SDL_Point pivot, const std::vector<SDL_Point>& local_polygon);
    void set_animation(const std::string& animation_id, Animation animation);
    std::shared_ptr<AssetInfo> finalize(const std::vector<float>& variant_steps);

private:
    std::shared_ptr<AssetInfo> info_;
    bool passable_ = true;
    bool flipable_ = true;
    bool smooth_scaling_ = true;
    bool is_shaded_ = false;
    bool moving_asset_ = false;
    bool is_light_source_ = false;
    int min_same_type_distance_ = std::numeric_limits<int>::max();
    int min_distance_all_ = std::numeric_limits<int>::max();
    int neighbor_radius_ = 0;
    int z_threshold_ = 0;
    float scale_factor_sum_ = 0.0f;
    int scale_factor_count_ = 0;
    std::unordered_set<std::string> tags_;
    std::unordered_set<std::string> anti_tags_;
    std::vector<LightSource> light_sources_;
    std::optional<ShadowMaskSettings> shadow_settings_;
    std::optional<std::string> type_;
    std::optional<std::string> custom_controller_;
};

class AssetMerger {
public:
    AssetMerger(SDL_Renderer* renderer, const camera_grid* active_camera = nullptr);

    std::unique_ptr<Asset> merge(std::vector<std::unique_ptr<Asset>> assets);

private:
    struct SampledAsset {
        Asset* asset = nullptr;
        SDL_Texture* frame_texture = nullptr;
        SDL_Texture* mask_texture = nullptr;
        int width = 0;
        int height = 0;
        double left = 0.0;
        double top = 0.0;
        double pivot_x = 0.0;
        double pivot_y = 0.0;
        float pivot_ratio_x = 0.5f;
        float pivot_ratio_y = 1.0f;
};

    SDL_Texture* create_scaled_texture(SDL_Texture* source, int source_w, int source_h, float scale, bool smooth) const;
    std::vector<float> build_variant_steps(float camera_scale_hint) const;
    std::vector<SampledAsset> sample_assets(const std::vector<std::unique_ptr<Asset>>& assets, double& min_x, double& min_y, double& max_x, double& max_y) const;

    SDL_Renderer* renderer_ = nullptr;
    const camera_grid* camera_ = nullptr;
};

}
