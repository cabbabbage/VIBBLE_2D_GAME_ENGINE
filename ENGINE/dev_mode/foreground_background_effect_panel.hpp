#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "render/image_effect_settings.hpp"

class Assets;
class AssetInfo;
class DMDropdown;
class DropdownWidget;
class FloatSliderWidget;
class DMButton;
class ButtonWidget;
class Widget;
class Input;

class ForegroundBackgroundEffectPanel : public DockableCollapsible {
public:
    explicit ForegroundBackgroundEffectPanel(Assets* assets, int x = 160, int y = 160);
    ~ForegroundBackgroundEffectPanel() override;

    void set_assets(Assets* assets);
    void refresh_from_camera();

    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    void open();
    void close();
    bool is_point_inside(int x, int y) const;

private:
    struct SectionWidgets {
        std::unique_ptr<Widget> label;
        std::unique_ptr<Widget> preview;
        std::unique_ptr<FloatSliderWidget> rgb_boost;
        std::unique_ptr<FloatSliderWidget> contrast;
        std::unique_ptr<FloatSliderWidget> brightness;
        std::unique_ptr<FloatSliderWidget> blur;
        std::unique_ptr<FloatSliderWidget> saturation_r;
        std::unique_ptr<FloatSliderWidget> saturation_g;
        std::unique_ptr<FloatSliderWidget> saturation_b;
        std::unique_ptr<FloatSliderWidget> hue;
    };

    void build_ui();
    void rebuild_rows();
    void rebuild_asset_options();
    void recreate_asset_dropdown();
    void handle_asset_selection(int index);

    void update_section_from_settings(const camera_effects::ImageEffectSettings& settings, SectionWidgets& widgets);
    camera_effects::ImageEffectSettings read_section_settings(const SectionWidgets& widgets) const;
    void on_slider_changed();

    void rebuild_previews();
    bool ensure_preview_source();
    void destroy_preview_textures();

    void apply_and_regenerate();
    void purge_mismatched_caches(std::uint64_t fg_hash, std::uint64_t bg_hash);

private:
    Assets* assets_ = nullptr;
    std::vector<std::string> asset_names_;
    std::string selected_asset_;
    std::string preview_animation_id_;
    std::shared_ptr<AssetInfo> preview_info_;

    std::unique_ptr<Widget> header_spacer_;
    std::unique_ptr<DMDropdown> asset_dropdown_;
    std::unique_ptr<DropdownWidget> asset_dropdown_widget_;

    SectionWidgets fg_widgets_;
    SectionWidgets bg_widgets_;

    std::unique_ptr<DMButton> apply_button_;
    std::unique_ptr<ButtonWidget> apply_button_widget_;

    SDL_Texture* base_preview_texture_ = nullptr;
    int base_preview_w_ = 0;
    int base_preview_h_ = 0;
    SDL_Texture* fg_preview_texture_ = nullptr;
    SDL_Texture* bg_preview_texture_ = nullptr;
    int fg_preview_w_ = 0;
    int fg_preview_h_ = 0;
    int bg_preview_w_ = 0;
    int bg_preview_h_ = 0;

    camera_effects::ImageEffectSettings fg_settings_{};
    camera_effects::ImageEffectSettings bg_settings_{};
    camera_effects::ImageEffectSettings saved_fg_{};
    camera_effects::ImageEffectSettings saved_bg_{};

    bool preview_dirty_ = true;
    bool has_unsaved_changes_ = false;
};
