#pragma once

#include "render/warped_screen_grid.hpp"
#include "asset/asset_library.hpp"
#include <SDL.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "map_generation/room.hpp"
#include "world/world_grid.hpp"
#include "asset/Asset.hpp"
#include "world/grid_point.hpp"

class Asset;
class SceneRenderer;
class LightMap;
struct SDL_Renderer;
class CurrentRoomFinder;
class Room;
class Input;
class DevControls;
class AssetInfo;

class QuickTaskPopup;
namespace animation_editor {
class AnimationDocument;
class PreviewProvider;
class AnimationEditorWindow;
}
namespace devmode::core {
class ManifestStore;
}
class Assets {
public:
    Assets(AssetLibrary& library,
           Asset*,
           std::vector<Room*> rooms,
           int screen_width,
           int screen_height,
           int screen_center_x,
           int screen_center_y,
           int map_radius,
           SDL_Renderer* renderer,
           const std::string& map_id,
           const nlohmann::json& map_manifest,
           std::string content_root = {},
           world::WorldGrid&& world_grid = world::WorldGrid{});
    ~Assets();

    nlohmann::json save_current_room(std::string room_name);
    void update(const Input& input);
    void set_dev_mode(bool mode);
    void set_force_high_quality_rendering(bool enable);
    bool force_high_quality_rendering() const { return force_high_quality_rendering_; }
    void set_render_dark_mask_enabled(bool enabled);
    bool render_dark_mask_enabled() const { return render_dark_mask_enabled_; }
    void set_render_suppressed(bool suppressed);
    void set_input(Input* m);
    Input* get_input() const { return input; }
    Asset* find_asset_by_name(const std::string& name) const;
    bool contains_asset(const Asset* asset) const;

    const std::vector<Asset*>& get_selected_assets() const;
    const std::vector<Asset*>& get_highlighted_assets() const;
    Asset* get_hovered_asset() const;

    const std::vector<Asset*>& getActive() const;
    const std::vector<Asset*>& getFilteredActiveAssets() const;
    const std::vector<world::GridPoint*>& active_points() const { return active_points_; }
    const std::vector<Asset*>& getActiveRaw() const { return active_assets; }
    const std::vector<Asset*>& getActiveLightAssets() const { return active_light_assets_; }
    const std::vector<Asset*>& getActiveLitAssets() const { return active_light_assets_; }
    const std::vector<Asset*>& getActiveStaticLightAssets() const { return active_static_light_assets_; }
    const std::vector<Asset*>& getActiveMovingLightAssets() const { return active_moving_light_assets_; }
    std::vector<Asset*>& mutable_filtered_active_assets() { return filtered_active_assets; }
    WarpedScreenGrid& getView() { return camera_; }
    const WarpedScreenGrid& getView() const { return camera_; }

    float frame_delta_seconds() const { return last_frame_dt_seconds_; }

    void render_overlays(SDL_Renderer* renderer);
    SDL_Renderer* renderer() const;
    void toggle_asset_library();
    void open_asset_library();
    void close_asset_library();
    bool is_asset_library_open() const;
    void toggle_room_config();
    void close_room_config();
    bool is_room_config_open() const;

    std::shared_ptr<AssetInfo> consume_selected_asset_from_library();
    void open_asset_info_editor(const std::shared_ptr<AssetInfo>& info);
    void open_asset_info_editor_for_asset(Asset* a);
    void open_animation_editor_for_asset(const std::shared_ptr<AssetInfo>& info);
    void close_asset_info_editor();
    bool is_asset_info_editor_open() const;

    bool is_asset_info_lighting_section_expanded() const;
    void clear_editor_selection();
    void handle_sdl_event(const SDL_Event& e);
    void finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info);
    void on_camera_settings_changed();
    void reload_camera_settings();
    void apply_camera_runtime_settings();
    void set_depth_effects_enabled(bool enabled);
    bool depth_effects_enabled() const { return depth_effects_enabled_; }

    void focus_camera_on_asset(Asset* a, double zoom_factor = 0.8, int duration_steps = 25);

    void begin_frame_editor_session(Asset* asset, std::shared_ptr<animation_editor::AnimationDocument> document, std::shared_ptr<animation_editor::PreviewProvider> preview, const std::string& animation_id, animation_editor::AnimationEditorWindow* host_to_toggle);

    devmode::core::ManifestStore* manifest_store();
    const devmode::core::ManifestStore* manifest_store() const;
    void notify_spawn_group_config_changed(const nlohmann::json& entry);
    void notify_spawn_group_removed(const std::string& spawn_id);

    void show_dev_notice(const std::string& message, Uint32 duration_ms = 2000);

    void set_editor_current_room(Room* room);

    Room* current_room() { return current_room_; }
    const Room* current_room() const { return current_room_; }
    std::vector<const Room::NamedArea*> current_room_trigger_areas() const;

    nlohmann::json& map_info_json() { return map_info_json_; }
    const nlohmann::json& map_info_json() const { return map_info_json_; }
    const std::string& map_path() const { return map_path_; }
    const std::string& map_id() const { return map_id_; }
    world::WorldGrid& world_grid() { return world_grid_; }
    const world::WorldGrid& world_grid() const { return world_grid_; }

    void persist_map_info_json();

    AssetLibrary& library();
    const AssetLibrary& library() const;

    void set_rooms(std::vector<Room*> rooms);
    std::vector<Room*>& rooms();
    const std::vector<Room*>& rooms() const;
    void notify_rooms_changed();
    std::size_t rooms_generation() const { return rooms_generation_; }

    void refresh_active_asset_lists();
    void refresh_filtered_active_assets();
    void mark_active_assets_dirty();
    bool rebuild_active_assets_if_needed();
    void initialize_active_assets(SDL_Point center);
    std::uint64_t dev_active_state_version() const { return dev_active_state_version_; }

    const LightMap* light_map() const;
    LightMap*       light_map();
    void force_shaded_assets_rerender();
    bool apply_lighting_grid_subdivide(int subdivisions);
    void set_map_light_panel_visible(bool visible);
    bool is_map_light_panel_visible() const;
    void set_update_map_light_enabled(bool enabled);
    bool update_map_light_enabled() const;

    void apply_map_grid_settings(const MapGridSettings& settings, bool persist_json = true);
    int  map_grid_chunk_resolution() const;
    const MapGridSettings& map_grid_settings() const { return map_grid_settings_; }

    std::optional<Asset::TilingInfo> compute_tiling_for_asset(const Asset* asset) const;

    bool is_dev_mode() const { return dev_mode; }

    bool scene_light_map_only_mode() const;

    int shading_group_count() const { return num_groups_; }

    void notify_light_map_asset_moved(const Asset* asset);
    void notify_light_map_static_assets_changed();

    std::vector<Asset*> all;
    Asset* player = nullptr;

    Asset* spawn_asset(const std::string& name, SDL_Point world_pos);

    void rebuild_from_grid_state();

    void ensure_light_textures_loaded(Asset* asset);

    const std::vector<world::Chunk*>& active_chunks() const { return world_grid_.active_chunks(); }

private:
    void save_map_info_json();
    void apply_map_light_config();
    bool on_map_light_changed();
    void hydrate_map_info_sections();
    void load_camera_settings_from_json();
    void write_camera_settings_to_json();
    void schedule_removal(Asset* a);

    bool process_removals();
    void addAsset(const std::string& name, SDL_Point g);
    void update_filtered_active_assets();
    void ensure_dev_controls();
    void update_scene_render_quality();
    int  saved_render_quality_percent() const;
    int  effective_render_quality_percent() const;
    void sync_dev_controls_current_room(Room* room, bool force_refresh = false);
    void reset_dev_controls_current_room_cache();

    friend class SceneRenderer;
    friend class Asset;

    CurrentRoomFinder* finder_ = nullptr;
    Input* input = nullptr;
    DevControls* dev_controls_ = nullptr;
    Room* dev_controls_last_room_ = nullptr;
    std::unique_ptr<QuickTaskPopup> quick_task_popup_;
    WarpedScreenGrid camera_;
    SceneRenderer* scene = nullptr;
    int screen_width;
    int screen_height;
    int dx = 0;
    int dy = 0;
    std::vector<Asset*> active_assets;
    std::vector<Asset*> filtered_active_assets;
    std::vector<Asset*> active_light_assets_;
    std::vector<Asset*> active_static_light_assets_;
    std::vector<Asset*> active_moving_light_assets_;
    std::unordered_set<Asset*> active_moving_light_lookup_;
    std::unordered_set<Asset*> scratch_moving_light_lookup_;
    std::vector<Room*> rooms_;
    std::size_t rooms_generation_ = 0;
    Room* current_room_ = nullptr;
    int num_groups_ = 40;
    bool dev_mode = false;
    bool suppress_render_ = false;

    bool suppress_dev_renderer_ = false;
    bool force_high_quality_rendering_ = false;
    bool render_dark_mask_enabled_ = true;
    bool depth_effects_enabled_ = false;
    bool asset_boundary_box_display_enabled_ = false;
    world::WorldGrid world_grid_{};
    std::vector<world::GridPoint*> active_points_;
    std::vector<Asset*> removal_queue;
    std::mutex removal_queue_mutex_;
    std::vector<Asset*> non_player_update_buffer_;
    std::atomic<bool> non_player_update_buffer_dirty_{true};

    float      last_frame_dt_seconds_   = 1.0f / 60.0f;
    double     perf_counter_frequency_  = 0.0;
    std::uint64_t last_frame_counter_   = 0;

    AssetLibrary& library_;
    std::string map_id_;
    std::string map_path_;
    nlohmann::json map_info_json_;
    std::atomic<bool> active_assets_dirty_{true};
    MapGridSettings map_grid_settings_{};
    std::unique_ptr<devmode::core::ManifestStore> manifest_store_fallback_;
    std::optional<float> last_audio_effect_max_distance_{};
    float max_asset_height_world_ = 0.0f;
    float max_asset_width_world_  = 0.0f;
    float cached_zoom_level_      = 0.0f;
    bool  max_asset_dimensions_dirty_ = true;
    std::vector<Asset*> visible_candidate_buffer_;
    std::uint64_t active_candidate_generation_ = 0;

    bool pending_initial_rebuild_ = false;
    bool logged_initial_rebuild_warning_ = false;

    struct GridMovementCommand {
        Asset* asset = nullptr;
        SDL_Point previous{0, 0};
        SDL_Point current{0, 0};
};

    void track_asset_for_grid(Asset* asset);
    void untrack_asset_for_grid(Asset* asset);
    void register_pending_static_assets();
    void rebuild_all_assets_from_grid();
    void rebuild_active_from_screen_grid();

    std::vector<Asset*> moving_assets_for_grid_;
    std::vector<Asset*> pending_static_grid_registration_;
    std::vector<GridMovementCommand> movement_commands_buffer_;
    std::vector<Asset*> grid_registration_buffer_;

    void touch_dev_active_state_version();

    std::uint64_t dev_active_state_version_ = 1;
    std::uint64_t filtered_active_assets_hash_ = 0;

    struct DevNotice {
        using TexturePtr = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;

        DevNotice()
            : texture(nullptr, SDL_DestroyTexture) {}

        std::string message;
        Uint32 expiry_ms = 0;
        TexturePtr texture;
        int texture_width = 0;
        int texture_height = 0;
        bool dirty = true;
};

    std::optional<DevNotice> dev_notice_;

    void rebuild_non_player_update_buffer_if_needed();
    void update_active_assets(SDL_Point center);
    bool asset_bounds_in_screen_space(const Asset* asset, SDL_FRect& out_rect) const;
    void update_max_asset_dimensions();
    void invalidate_max_asset_dimensions();
    SDL_Rect screen_world_rect() const;
    int audio_effect_max_distance_world() const;

    void update_audio_camera_metrics();
    void mark_non_player_update_buffer_dirty() {
        non_player_update_buffer_dirty_.store(true, std::memory_order_release);
    }

private:
    SDL_Point last_known_player_pos_{0, 0};
    bool      last_player_pos_valid_ = false;

    std::vector<SDL_Rect> culled_debug_rects_;
};
#include "utils/map_grid_settings.hpp"
