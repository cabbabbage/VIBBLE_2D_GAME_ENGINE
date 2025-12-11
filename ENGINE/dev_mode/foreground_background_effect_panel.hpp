#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "dev_mode/float_slider_widget.hpp"
#include "render/image_effect_settings.hpp"
#include <nlohmann/json.hpp>

class Assets;
class AssetInfo;
class DMDropdown;
class DropdownWidget;
class DMButton;
class ButtonWidget;
class Widget;
class Input;

class ForegroundBackgroundEffectPanel : public DockableCollapsible {
public:
    enum class EffectMode {
        Foreground,
        Background
};
    explicit ForegroundBackgroundEffectPanel(Assets* assets, int x = 160, int y = 160);
    ~ForegroundBackgroundEffectPanel() override;

    void set_assets(Assets* assets);
    void refresh_from_camera();

    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;
    void render_content(SDL_Renderer* renderer) const override;

    void open();

    using CloseCallback = std::function<void()>;
    void set_close_callback(CloseCallback callback) { close_callback_ = std::move(callback); }
    void close();
    bool is_point_inside(int x, int y) const;

private:
    void layout_custom_content(int screen_w, int screen_h) const override;

    void build_ui();
    void rebuild_rows();
    void rebuild_asset_options();
    void recreate_asset_dropdown();
    void handle_asset_selection(int index);

    void update_controls_from_settings(const camera_effects::ImageEffectSettings& settings);
    camera_effects::ImageEffectSettings read_current_settings() const;
    void on_slider_changed();

    void set_mode(EffectMode mode);
    void save_current_mode_settings();
    void load_current_mode_settings();

    void rebuild_previews();
    bool ensure_preview_source();
    void destroy_preview_textures();

    void apply_and_regenerate();
    void restore_defaults();
    void purge_mismatched_caches(std::uint64_t fg_hash, std::uint64_t bg_hash, bool force_purge = false);
    void request_preview_rebuild();
    bool can_render_preview() const;

    void save_depth_cue_settings_to_manifest();
    bool load_depth_cue_settings_from_manifest();
    void update_preview_and_manifest();
    void generate_preview_with_python(const std::string& image_path, const camera_effects::ImageEffectSettings& settings);
    void load_preview_texture(const std::string& image_path);
    bool settings_equal(const camera_effects::ImageEffectSettings& a, const camera_effects::ImageEffectSettings& b, float epsilon = 1e-5f) const;
    bool should_skip_preview(const std::string& source_path, EffectMode mode, const camera_effects::ImageEffectSettings& settings) const;

private:
    Assets* assets_ = nullptr;
    EffectMode current_mode_ = EffectMode::Foreground;

    std::vector<std::string> asset_names_;
    std::string selected_asset_;
    std::string preview_animation_id_;
    std::shared_ptr<AssetInfo> preview_info_;

    std::unique_ptr<Widget> header_spacer_;

    std::unique_ptr<DMButton> fg_mode_button_;
    std::unique_ptr<DMButton> bg_mode_button_;
    std::unique_ptr<ButtonWidget> fg_mode_button_widget_;
    std::unique_ptr<ButtonWidget> bg_mode_button_widget_;

    std::unique_ptr<DMDropdown> asset_dropdown_;
    std::unique_ptr<DropdownWidget> asset_dropdown_widget_;

    std::unique_ptr<Widget> preview_;
    std::unique_ptr<FloatSliderWidget> contrast_;
    std::unique_ptr<FloatSliderWidget> brightness_;
    std::unique_ptr<FloatSliderWidget> blur_;
    std::unique_ptr<FloatSliderWidget> saturation_r_;
    std::unique_ptr<FloatSliderWidget> saturation_g_;
    std::unique_ptr<FloatSliderWidget> saturation_b_;
    std::unique_ptr<FloatSliderWidget> hue_;

    std::unique_ptr<DMButton> apply_button_;
    std::unique_ptr<ButtonWidget> apply_button_widget_;
    std::unique_ptr<DMButton> restore_defaults_button_;
    std::unique_ptr<ButtonWidget> restore_defaults_button_widget_;

    SDL_Texture* base_preview_texture_ = nullptr;
    int base_preview_w_ = 0;
    int base_preview_h_ = 0;
    SDL_Texture* current_preview_texture_ = nullptr;
    int current_preview_w_ = 0;
    int current_preview_h_ = 0;

    camera_effects::ImageEffectSettings fg_settings_{};
    camera_effects::ImageEffectSettings bg_settings_{};
    camera_effects::ImageEffectSettings saved_fg_{};
    camera_effects::ImageEffectSettings saved_bg_{};
    camera_effects::ImageEffectSettings current_settings_{};
    camera_effects::ImageEffectSettings last_preview_settings_{};
    EffectMode last_preview_mode_ = EffectMode::Foreground;
    std::string last_preview_asset_;
    std::string last_preview_source_path_;

    bool preview_dirty_ = true;
    bool has_unsaved_changes_ = false;

    static constexpr int kPreviewPanelWidth = 320;
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};

    CloseCallback close_callback_;
};
