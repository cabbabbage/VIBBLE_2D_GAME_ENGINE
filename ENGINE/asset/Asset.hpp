#ifndef ASSET_HPP
#define ASSET_HPP

#include <string>
#include <array>
#include <vector>
#include <memory>
#include <optional>
#include <SDL.h>
#include <limits>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <cmath>

#include "utils/area.hpp"
#include "asset_info.hpp"

#include "utils/transform_smoothing.hpp"

#include "asset_controller.hpp"
#include "animation_update/animation_update.hpp"
#include "render/render.hpp"

class WarpedScreenGrid;
class Assets;
class Input;
class AnimationFrame;
class Animation;
class AssetInfoUI;
class RenderAsset;
class AssetList;

struct RenderObject {
    SDL_Texture* texture = nullptr;
    SDL_Rect screen_rect{};
    SDL_Color color_mod{255, 255, 255, 255};
    SDL_BlendMode blend_mode = SDL_BLENDMODE_BLEND;
    double angle = 0.0;
    SDL_Point center{0, 0};
    bool use_custom_center = false;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
};

using RenderCompositePackage = std::vector<RenderObject>;

struct DepthCueRenderData {
    SDL_Texture* base_texture = nullptr;
    SDL_Texture* foreground_texture = nullptr;
    SDL_Texture* background_texture = nullptr;
    bool has_depth_cue = false;
};

class Asset {

        public:
    RenderCompositePackage render_package;
    RenderCompositePackage scene_mask_lights;
    struct RenderTextureCache {
        SDL_Texture* texture = nullptr;
        int          width   = 0;
        int          height  = 0;
};

    struct MaskRenderMetadata {
        struct TextureDefaults {
            SDL_Texture* texture = nullptr;
            Uint8        r = 255;
            Uint8        g = 255;
            Uint8        b = 255;
            Uint8        a = 255;
            SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
            bool         initialized = false;
            void reset();
};

        SDL_Texture* last_mask_texture = nullptr;
        int          mask_w            = 0;
        int          mask_h            = 0;
        bool         has_dimensions    = false;
        TextureDefaults mask_defaults;
        TextureDefaults base_defaults;

        void reset();
};

    struct TilingInfo {
        bool      enabled      = false;
        SDL_Point grid_origin{0, 0};
        SDL_Point tile_size{0, 0};
        SDL_Rect  coverage{0, 0, 0, 0};
        SDL_Point anchor{0, 0};

        bool is_valid() const {
            return enabled && tile_size.x > 0 && tile_size.y > 0 && coverage.w > 0 && coverage.h > 0;
        }
};

    struct AnimationChildAttachment {
        int child_index = -1;
        std::string asset_name;
        std::shared_ptr<AssetInfo> info;
        const Animation* animation = nullptr;
        const AnimationFrame* current_frame = nullptr;
        float frame_progress = 0.0f;
        SDL_Point world_pos{0, 0};
        float rotation_degrees = 0.0f;
        bool visible = false;
        bool render_in_front = true;
        int cached_w = 0;
        int cached_h = 0;
        bool was_visible = false;
        int last_parent_frame_index = -1;
        Asset* spawned_asset = nullptr;
        const AnimationChildData* timeline = nullptr;
        AnimationChildMode timeline_mode = AnimationChildMode::Static;
        bool timeline_active = false;
        int timeline_frame_cursor = 0;
        float timeline_frame_progress = 0.0f;
};

    struct BoundsSquare {
        float center_x = 0.0f;
        float center_y = 0.0f;
        float half_size = 0.0f;

        bool valid() const { return std::isfinite(half_size) && half_size > 0.0f; }
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
    void rebuild_animation_runtime();

    void initialize_animation_children_recursive();
    bool is_finalized() const { return finalized_; }
    void on_scale_factor_changed();

    void update();
    SDL_Texture* get_current_frame() const;
    std::string get_current_animation() const;
    bool is_current_animation_locked_in_progress() const;
    bool is_current_animation_last_frame() const;
    bool is_current_animation_looping() const;
    const AnimationFrame* current_animation_frame() const { return current_frame; }
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
    struct ScaleVariantState {
        int   last_variant_index = 0;
        float hysteresis_min     = 0.0f;
        float hysteresis_max     = std::numeric_limits<float>::max();
};

    const ScaleVariantState& scale_variant_state() const { return scale_variant_state_; }

    void set_z_offset(int z);
    void set_shading_group(int x);
    bool is_shading_group_set() const;
    int  get_shading_group() const;
    void set_frame_progress(float p) { frame_progress = p; }
    class AnimationFrame* current_frame = nullptr;
    const AnimationFrame* last_rendered_frame() const { return last_rendered_frame_; }
    void set_last_rendered_frame(const AnimationFrame* frame) { last_rendered_frame_ = frame; }
    void reset_last_rendered_frame() { last_rendered_frame_ = nullptr; }
    SDL_Texture* get_texture();
    void set_camera(WarpedScreenGrid* v) { window = v; }
    void set_assets(Assets* a);
    Assets* get_assets() const { return assets_; }
    void set_tiling_info(std::optional<TilingInfo> info);
    const std::optional<TilingInfo>& tiling_info() const { return tiling_info_; }
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
    void sync_transform_to_position();
    void set_grid_id(std::uint64_t id);
    std::uint64_t grid_id() const { return grid_id_; }
    void clear_grid_id();

    SDL_Texture* composite_texture() const { return composite_texture_; }
    void set_composite_texture(SDL_Texture* tex);
    bool is_composite_dirty() const { return composite_dirty_; }
    void mark_composite_dirty() { composite_dirty_ = true; }
    void clear_composite_dirty() { composite_dirty_ = false; }
    const SDL_Rect& composite_rect() const { return composite_rect_; }
    void set_composite_rect(const SDL_Rect& r) { composite_rect_ = r; }
    float        composite_scale() const { return composite_scale_; }

    RenderTextureCache& shadow_mask_cache();
    RenderTextureCache& shadow_mask_cache() const;
    RenderTextureCache& cast_shadow_cache();
    RenderTextureCache& cast_shadow_cache() const;
    MaskRenderMetadata& mask_render_metadata();
    MaskRenderMetadata& mask_render_metadata() const;

    float smoothed_translation_x() const;
    float smoothed_translation_y() const;
    float smoothed_scale() const;
    float smoothed_alpha() const;
    const BoundsSquare& base_bounds_local() const { return base_bounds_local_; }
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
    const std::vector<AnimationChildAttachment>& animation_children() const { return animation_children_; }
    int depth = 0;
    bool is_shaded = false;
    bool dead = false;
    bool static_frame = true;
    bool needs_target = false;
    bool target_reached = false;
    int cached_w = 0;
    int cached_h = 0;
    std::uint64_t last_render_frame_id = 0;
    std::uint64_t visibility_stamp = 0;
    std::string spawn_id;
    std::string spawn_method;
    std::string owning_room_name_;
    std::unique_ptr<AnimationUpdate> anim_;
    std::unique_ptr<class AnimationRuntime> anim_runtime_;
    float current_scale = 1.00f;
    float current_nearest_variant_scale = 1.00f;
    float current_remaining_scale_adjustment = 1.00f;
    int   current_variant_index = 0;

    void update_scale_values();
    SDL_Texture* get_current_variant_texture() const;
    void set_current_animation(const std::string& name);

public:
    static void SetFlipOverrideForSpawnId(const std::string& spawn_id, bool enabled, bool flipped);
    static void ClearFlipOverrideForSpawnId(const std::string& spawn_id);
private:
    static std::unordered_map<std::string, std::pair<bool,bool>> s_flip_overrides_;
    static std::mutex s_flip_overrides_mutex_;
    friend class AnimationUpdate;
    friend class AnimationRuntime;
    friend class Move;
    friend class AssetInfoUI;
    friend class RenderAsset;
    friend class FrameEditorSession;
    friend class Assets;
    friend class CompositeAssetRenderer;
    WarpedScreenGrid* window = nullptr;
    bool highlighted = false;
    bool hidden = false;
    bool selected = false;
    bool merged_from_neighbors_ = false;
    void set_flip();
    void set_z_index();

    float frame_progress = 0.0f;
    int  shading_group = 0;
    bool shading_group_set = false;
    Assets* assets_ = nullptr;
    std::unique_ptr<AssetController>   controller_;
    std::unique_ptr<AssetList> neighbors;
    AssetList* impassable_naighbors = nullptr;
    std::vector<AnimationChildAttachment> animation_children_;
    bool animation_children_initialized_ = false;
    bool initializing_animation_children_ = false;
    std::optional<TilingInfo> tiling_info_{};
    SDL_Point last_neighbor_origin_{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
    bool neighbor_lists_initialized_ = false;
    void update_neighbor_lists(bool force_update);
    void ensure_animation_runtime(bool force_recreate);

    void clear_downscale_cache();
    void invalidate_downscale_cache();
    void refresh_cached_dimensions();
    void recompute_local_bounds_square();

    std::uint64_t downscale_cache_ready_revision_ = 0;

    BoundsSquare base_bounds_local_{};
    SDL_Rect     composite_bounds_local_{0, 0, 0, 0};

    SDL_Texture* last_scaled_texture_      = nullptr;
    SDL_Texture* last_scaled_source_       = nullptr;
    int          last_scaled_w_            = 0;
    int          last_scaled_h_            = 0;
    float        last_scaled_camera_scale_ = -1.0f;

    ScaleUsageStats last_scale_usage_{};
    ScaleVariantState scale_variant_state_{};

    void clear_render_caches();
    void reset_mask_render_metadata();
    static void destroy_render_cache(RenderTextureCache& cache);

    mutable RenderTextureCache shadow_mask_cache_{};
    mutable RenderTextureCache cast_shadow_cache_{};
    mutable MaskRenderMetadata mask_render_metadata_{};

    TransformSmoothingState translation_smoothing_x_{};
    TransformSmoothingState translation_smoothing_y_{};
    TransformSmoothingState scale_smoothing_{};
    TransformSmoothingState alpha_smoothing_{};

    const AnimationFrame* last_rendered_frame_ = nullptr;

    bool finalized_ = false;
    std::uint64_t grid_id_ = 0;
    bool has_cached_grid_residency_ = false;
    SDL_Point cached_grid_residency_{0, 0};

    SDL_Texture* composite_texture_ = nullptr;
    bool         composite_dirty_   = true;
    SDL_Rect     composite_rect_    = {0, 0, 0, 0};
    float        composite_scale_   = 1.0f;
};

#endif
