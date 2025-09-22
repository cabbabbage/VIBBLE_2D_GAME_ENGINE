#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

#include "dev_mode/asset_info_sections.hpp"

class AssetInfo;
class Input;
class Area;
class Assets;
class AnimationsEditorPanel;
class Section_BasicInfo;

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
    void update(const Input& input, int screen_w, int screen_h);
    void handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;
    void render_world_overlay(SDL_Renderer* r, const class camera& cam) const;
    void pulse_header();
    void set_assets(Assets* a);
    Assets* assets() const { return assets_; }
    void set_target_asset(class Asset* a) { target_asset_ = a; }
    class Asset* get_target_asset() const { return target_asset_; }
    bool is_point_inside(int x, int y) const;
    SDL_Renderer* get_last_renderer() const { return last_renderer_; }
    void refresh_target_asset_scale();
    void sync_target_z_threshold();
    void request_apply_section(AssetInfoSectionId section_id);

  private:
    void layout_widgets(int screen_w, int screen_h) const;
    void apply_camera_override(bool enable);
    float compute_player_screen_height(const class camera& cam) const;
    void save_now() const;
    void open_area_editor(const std::string& name);
    bool apply_section_to_assets(AssetInfoSectionId section_id, const std::vector<std::string>& asset_names);
    static const char* section_display_name(AssetInfoSectionId section_id);

  private:
    bool visible_ = false;
    std::shared_ptr<AssetInfo> info_{};
    mutable SDL_Renderer* last_renderer_ = nullptr;
    Assets* assets_ = nullptr; // non-owning
    // Section-based UI
    std::vector<std::unique_ptr<class DockableCollapsible>> sections_;
    class Section_BasicInfo* basic_info_section_ = nullptr; // non-owning ptr
    class Section_Areas* areas_section_ = nullptr;     // non-owning ptr into sections_
    class Section_Lighting* lighting_section_ = nullptr; // non-owning ptr
    class Asset* target_asset_ = nullptr;               // asset being edited (non-owning)
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;
    mutable SDL_Rect panel_ {0,0,0,0};
    mutable SDL_Rect scroll_region_{0,0,0,0};
    mutable SDL_Rect name_label_rect_{0,0,0,0};
    // Footer button: Configure Animations
    mutable std::unique_ptr<class DMButton> configure_btn_;
    std::unique_ptr<AnimationsEditorPanel> animations_panel_;
    int pulse_frames_ = 0;
    bool camera_override_active_ = false;
    bool prev_camera_realism_enabled_ = false;
    bool prev_camera_parallax_enabled_ = false;
    std::unique_ptr<class ApplySettingsModal> apply_modal_;
};
