#pragma once

#include <functional>
#include <memory>
#include "DockableCollapsible.hpp"
#include "render/warped_screen_grid.hpp"

class Assets;
class DMCheckbox;
class DMButton;
class Widget;
class ButtonWidget;
class CheckboxWidget;
class DMDropdown;
class DropdownWidget;
class Input;
class FloatSliderWidget;
class SectionToggleWidget;
class DiscreteSliderWidget;
class PitchDialWidget;
class ZoomKeyPointWidget;

class CameraUIPanel : public DockableCollapsible {
public:
    explicit CameraUIPanel(Assets* assets, int x = 80, int y = 80);
    ~CameraUIPanel() override;

    void set_assets(Assets* assets);
    void set_image_effects_panel_callback(std::function<void()> cb);

    void open();
    void close();
    void toggle();
    bool is_point_inside(int x, int y) const;
    bool is_blur_section_visible() const { return is_visible() && depthcue_section_expanded_; }
    bool is_depth_section_visible() const { return is_visible() && depth_section_expanded_; }

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    void sync_from_camera();

    void set_on_realism_enabled_changed(std::function<void(bool)> cb) { on_realism_enabled_changed_ = std::move(cb); }
    void set_on_depth_effects_enabled_changed(std::function<void(bool)> cb) { on_depth_effects_enabled_changed_ = std::move(cb); }

private:
    std::function<void(bool)> on_realism_enabled_changed_;
    std::function<void(bool)> on_depth_effects_enabled_changed_;
    void build_ui();
    void rebuild_rows();
    void apply_settings_if_needed();
    void apply_settings_to_camera(const WarpedScreenGrid::RealismSettings& settings, bool effects_enabled, bool depthcue_enabled);
    WarpedScreenGrid::RealismSettings read_settings_from_ui() const;
    void on_control_value_changed();
    void snap_zoom_to_anchor(float target_zoom, bool anchor_is_min_section);

private:
    Assets* assets_ = nullptr;
    WarpedScreenGrid::RealismSettings last_settings_{};
    bool last_realism_enabled_ = true;

    bool suppress_apply_once_ = false;
    bool was_visible_ = false;

    std::unique_ptr<Widget> header_spacer_;
    std::unique_ptr<Widget> hero_banner_widget_;
    std::unique_ptr<DMCheckbox> realism_enabled_checkbox_;
    std::unique_ptr<CheckboxWidget> realism_widget_;
    std::unique_ptr<Widget> controls_spacer_;
    std::unique_ptr<DMCheckbox> depthcue_checkbox_;
    std::unique_ptr<CheckboxWidget> depthcue_widget_;
    std::unique_ptr<SectionToggleWidget> visibility_section_header_;
    std::unique_ptr<SectionToggleWidget> depth_section_header_;
    std::unique_ptr<SectionToggleWidget> depthcue_section_header_;

    std::unique_ptr<ZoomKeyPointWidget> zoom_in_keypoint_;
    std::unique_ptr<ZoomKeyPointWidget> zoom_out_keypoint_;
    std::unique_ptr<FloatSliderWidget> min_render_size_slider_;
    std::unique_ptr<FloatSliderWidget> cull_margin_slider_;
    std::unique_ptr<FloatSliderWidget> perspective_zero_distance_slider_;
    std::unique_ptr<FloatSliderWidget> perspective_hundred_distance_slider_;

    std::unique_ptr<FloatSliderWidget> foreground_texture_opacity_slider_;
    std::unique_ptr<FloatSliderWidget> background_texture_opacity_slider_;

    std::unique_ptr<DMDropdown> texture_opacity_interp_dropdown_;
    std::unique_ptr<DropdownWidget> texture_opacity_interp_widget_;
    std::unique_ptr<DMButton> image_effect_button_;
    std::unique_ptr<ButtonWidget> image_effect_widget_;

    std::unique_ptr<DiscreteSliderWidget> render_quality_slider_;
    bool visibility_section_expanded_ = true;
    bool depth_section_expanded_ = true;
    bool zoom_in_settings_expanded_ = true;
    bool zoom_out_settings_expanded_ = false;
    bool depthcue_section_expanded_ = false;
    bool applying_settings_ = false;

    bool last_depthcue_enabled_ = false;
    std::function<void()> open_image_effects_cb_;
    int last_screen_w_ = 0;
    int last_screen_h_ = 0;

protected:
    std::string_view lock_settings_namespace() const override { return "camera"; }
    std::string_view lock_settings_id() const override { return "controls"; }
    void layout_custom_content(int screen_w, int screen_h) const override;
};
