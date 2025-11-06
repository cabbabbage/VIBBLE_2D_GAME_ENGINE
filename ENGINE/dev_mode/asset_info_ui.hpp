#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "dev_mode/SlidingWindowContainer.hpp"
#include "dev_mode/asset_info_sections.hpp"

class AssetInfo;
class Input;
class Area;
class Assets;
class Section_BasicInfo;
class SearchAssets;
class Section_Shading;
class Section_SpawnGroups;
namespace animation_editor {
class AnimationEditorWindow;
}

namespace devmode::core {
class ManifestStore;
}

class AssetInfoUI {

	public:
    AssetInfoUI();
    ~AssetInfoUI();
    void set_info(const std::shared_ptr<AssetInfo>& info);
    void clear_info();
    void open();
    void close();
    void toggle();
    bool is_visible() const { return visible_; }
    bool is_locked() const;
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;
    void render_world_overlay(SDL_Renderer* r, const class camera& cam) const;
    void pulse_header();
    void set_assets(Assets* a);
    Assets* assets() const { return assets_; }
    void set_manifest_store(devmode::core::ManifestStore* store);
    devmode::core::ManifestStore* manifest_store() const { return manifest_store_; }
    void set_target_asset(class Asset* a);
    class Asset* get_target_asset() const { return target_asset_; }
    // Open Area Info UI for a room-scoped Area
    void open_for_room_area(class Room* room, const std::string& area_name);
    void clear_area_context();
    bool is_point_inside(int x, int y) const;
    SDL_Renderer* get_last_renderer() const { return last_renderer_; }
    void refresh_target_asset_scale();
    void sync_target_z_threshold();
    void request_apply_section(AssetInfoSectionId section_id);
    void set_header_visibility_callback(std::function<void(bool)> cb);
    void notify_light_sources_modified(bool purge_light_cache);
    void notify_spawn_group_entry_changed(const nlohmann::json& entry);
    void notify_spawn_group_removed(const std::string& spawn_id);
    void regenerate_shadow_masks();

  private:
    void rebuild_default_sections();
    void layout_widgets(int screen_w, int screen_h) const;
    void apply_camera_override(bool enable);
    float compute_player_screen_height(const class camera& cam) const;
    void save_now() const;
    void open_area_editor(const std::string& name);
    void open_room_area_editor(const std::string& name);
    bool apply_section_to_assets(AssetInfoSectionId section_id, const std::vector<std::string>& asset_names);
    static const char* section_display_name(AssetInfoSectionId section_id);
    void sync_map_light_panel_visibility(bool want_visible);
    bool validate_target_asset() const;
    bool apply_to_assets_with_info(const std::function<void(Asset*)>& fn);
    void on_animation_document_saved();
    void refresh_loaded_asset_instances();

  private:
    bool visible_ = false;
    std::shared_ptr<AssetInfo> info_{};
    mutable SDL_Renderer* last_renderer_ = nullptr;
    Assets* assets_ = nullptr;

    std::vector<std::unique_ptr<class DockableCollapsible>> sections_;
    class Section_BasicInfo* basic_info_section_ = nullptr;

    class Section_Lighting* lighting_section_ = nullptr;
    class Section_Shading* shading_section_ = nullptr;
    mutable class Asset* target_asset_ = nullptr;
    mutable SDL_Rect animation_editor_rect_{0,0,0,0};

    SlidingWindowContainer container_;

    mutable std::unique_ptr<class DMButton> configure_btn_;
    mutable std::unique_ptr<class ButtonWidget> configure_btn_widget_;
    bool camera_override_active_ = false;
    bool prev_camera_realism_enabled_ = false;
    bool prev_camera_parallax_enabled_ = false;
    std::unique_ptr<SearchAssets> asset_selector_;
    std::unique_ptr<animation_editor::AnimationEditorWindow> animation_editor_window_;
    bool map_light_panel_auto_opened_ = false;
    bool forcing_high_quality_rendering_ = false;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    Section_SpawnGroups* spawn_groups_section_ = nullptr;

    // Area context (when showing Area Info instead of Asset Info)
    bool area_mode_ = false;
    class Room* area_room_ = nullptr;
    std::string area_name_;
    class DockableCollapsible* area_settings_section_ = nullptr;
    class DockableCollapsible* area_spawns_section_ = nullptr;

    // Additional controls under Configure Animations
    std::unique_ptr<class DMButton> duplicate_btn_;
    std::unique_ptr<class ButtonWidget> duplicate_btn_widget_;
    std::unique_ptr<class DMButton> delete_btn_;
    std::unique_ptr<class ButtonWidget> delete_btn_widget_;

    // Duplicate asset popup state
    bool showing_duplicate_popup_ = false;
    std::string duplicate_asset_name_;

    // Delete asset confirmation modal state (mirrors AssetLibraryUI behavior)
    bool showing_delete_popup_ = false;
    struct PendingDeleteInfo { std::string name; std::string asset_dir; };
    std::optional<PendingDeleteInfo> pending_delete_;
    SDL_Rect delete_modal_rect_{0,0,0,0};
    SDL_Rect delete_yes_rect_{0,0,0,0};
    SDL_Rect delete_no_rect_{0,0,0,0};
    bool delete_yes_hovered_ = false;
    bool delete_no_hovered_ = false;
    bool delete_yes_pressed_ = false;
    bool delete_no_pressed_ = false;

    // Helpers for duplicate/delete flows
    bool duplicate_current_asset(const std::string& new_name);
    void request_delete_current_asset();
    void cancel_delete_request();
    void confirm_delete_request();
    void clear_delete_state();
    bool handle_delete_modal_event(const SDL_Event& e);
    void update_delete_modal_geometry(int screen_w, int screen_h);

    // Light crosshair dragging state
    bool light_drag_active_ = false;
    int  light_drag_index_ = -1;
};
