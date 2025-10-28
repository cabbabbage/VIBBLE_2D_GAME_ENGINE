#ifndef ASSET_HPP
#define ASSET_HPP

#include <string>
#include <array>
#include <vector>
#include <memory>
#include <SDL.h>
#include <limits>
#include <cstdint>

#include "utils/area.hpp"
#include "asset_info.hpp"

#include "asset_controller.hpp"
#include "animation_update/animation_update.hpp"
#include "render_pipeline/ScalingLogic.hpp"

class camera;
class Assets;
class Input;
class AnimationFrame;
class AssetInfoUI;
class RenderAsset;
class AssetList;

class Asset {

        public:
    struct RenderTextureCache {
        SDL_Texture* texture = nullptr;
        int          width   = 0;
        int          height  = 0;
};

    Area get_area(const std::string& name) const;
    Asset(std::shared_ptr<AssetInfo> info,
          const Area& spawn_area,
          SDL_Point start_pos,
          int depth,
          Asset* parent = nullptr,
          const std::string& spawn_id = std::string{},
          const std::string& spawn_method = std::string{},
          int grid_resolution = 0);
    Asset(const Asset& other);
    Asset& operator=(const Asset& other);
    Asset(Asset&&) noexcept = default;
    Asset& operator=(Asset&&) noexcept = default;
    ~Asset();
    void finalize_setup();
    void on_scale_factor_changed();

    void update();
    SDL_Texture* get_current_frame() const;
    SDL_Texture* get_current_mask_texture(std::size_t variant_index = 0) const;
    std::string get_current_animation() const;
    bool is_current_animation_locked_in_progress() const;
    bool is_current_animation_last_frame() const;
    bool is_current_animation_looping() const;
    void add_child(Asset* asset_child);

    struct ScaleUsageStats {
        float requested_scale = 1.0f;
        float texture_scale   = 1.0f;
        float remainder_scale = 1.0f;
        int   variant_index   = 0;

        float requested_percent() const { return requested_scale * 100.0f; }
        float texture_percent() const { return texture_scale * 100.0f; }
        float remainder_percent() const { return remainder_scale * 100.0f; }
};

    const ScaleUsageStats& last_scale_usage() const { return last_scale_usage_; }

    void set_z_offset(int z);
    void set_shading_group(int x);
    bool is_shading_group_set() const;
    int  get_shading_group() const;
    class AnimationFrame* current_frame = nullptr;
    SDL_Texture* get_final_texture() const;
    void set_final_texture(SDL_Texture* tex);
    void set_camera(camera* v) { window = v; }
    void set_assets(Assets* a);
    Assets* get_assets() const { return assets_; }
    const std::string& owning_room_name() const { return owning_room_name_; }
    void set_owning_room_name(std::string name);
    AssetList* get_neighbors_list();
    const AssetList* get_neighbors_list() const;
    AssetList* get_impassable_naighbors();
    const AssetList* get_impassable_naighbors() const;
    void deactivate();
    int NeighborSearchRadius;
    void set_hidden(bool state);
    bool is_hidden() const;
    void Delete();
    void set_highlighted(bool state);
    bool is_highlighted();
    void set_selected(bool state);
    bool is_selected();
    void set_merged_from_neighbors(bool state);
    bool merged_from_neighbors() const;
    void cache_grid_residency(SDL_Point point);
    void clear_grid_residency_cache();
    bool has_grid_residency_cache() const;
    SDL_Point grid_residency_cache() const;
    RenderTextureCache& shadow_mask_cache();
    RenderTextureCache& shadow_mask_cache() const;
    RenderTextureCache& motion_blur_cache();
    RenderTextureCache& motion_blur_cache() const;
    Asset* parent = nullptr;
    std::shared_ptr<AssetInfo> info;
    std::string current_animation;
    SDL_Point pos{0, 0};
    int grid_resolution = 0;
    int z_index = 0;
    int z_offset = 0;
    bool active = false;
    bool flipped = false;
    float distance_from_camera = 0.0f;
    float angle_from_camera = 0.0f;

    std::vector<Asset*> asset_children;
    int depth = 0;
    bool is_shaded = false;
    bool dead = false;
    bool static_frame = true;
    int cached_w = 0;
    int cached_h = 0;
    std::uint64_t last_render_frame_id = 0;
    std::string spawn_id;
    std::string spawn_method;
    std::string owning_room_name_;
    std::unique_ptr<AnimationUpdate> anim_;
    std::unique_ptr<class AnimationRuntime> anim_runtime_;
        private:
    friend class AnimationUpdate;
    friend class AnimationRuntime;
    friend class Move;
    friend class AssetInfoUI;
    friend class RenderAsset;
    friend class Assets;
    camera* window = nullptr;
    bool highlighted = false;
    bool hidden = false;
    bool selected = false;
    bool merged_from_neighbors_ = false;
    void set_flip();
    void set_z_index();

    float frame_progress = 0.0f;
    int  shading_group = 0;
    bool shading_group_set = false;
    SDL_Texture* final_texture = nullptr;
    Assets* assets_ = nullptr;
    std::unique_ptr<AssetController>   controller_;
    std::unique_ptr<AssetList> neighbors;
    AssetList* impassable_naighbors = nullptr;
    SDL_Point last_neighbor_origin_{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
    bool neighbor_lists_initialized_ = false;
    void update_neighbor_lists(bool force_update);

    SDL_Point cached_grid_residency_{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
    bool      has_cached_grid_residency_ = false;

    struct DownscaleCacheEntry {
        float          scale    = 1.0f;
        int            width    = 0;
        int            height   = 0;
        SDL_Texture*   texture  = nullptr;
        std::uint64_t  revision = 0;
};

    void clear_downscale_cache();
    void invalidate_downscale_cache();
    void refresh_cached_dimensions();

    std::vector<DownscaleCacheEntry> downscale_cache_{};

    SDL_Texture* last_scaled_texture_      = nullptr;
    SDL_Texture* last_scaled_source_       = nullptr;
    int          last_scaled_w_            = 0;
    int          last_scaled_h_            = 0;
    float        last_scaled_camera_scale_ = -1.0f;

    ScaleUsageStats last_scale_usage_{};

    void update_scale_usage(float requested, float texture_scale, float remainder, int variant_index);
    void clear_render_caches();
    static void destroy_render_cache(RenderTextureCache& cache);

    mutable RenderTextureCache shadow_mask_cache_{};
    mutable RenderTextureCache motion_blur_cache_{};

    std::uint64_t final_texture_revision_ = 0;
};

#endif
