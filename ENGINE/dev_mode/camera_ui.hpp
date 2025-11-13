#pragma once

#include <memory>
#include "DockableCollapsible.hpp"
#include "render/camera.hpp"

class Assets;
class DMCheckbox;
class DMButton;
class CheckboxWidget;
class DMDropdown;
class DropdownWidget;
class Input;
class FloatSliderWidget;
class SectionToggleWidget;
class DiscreteSliderWidget;

class CameraUIPanel : public DockableCollapsible {
public:
    explicit CameraUIPanel(Assets* assets, int x = 80, int y = 80);
    ~CameraUIPanel() override;

    void set_assets(Assets* assets);

    void open();
    void close();
    void toggle();
    bool is_point_inside(int x, int y) const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    void sync_from_camera();

private:
    void build_ui();
    void rebuild_rows();
    void apply_settings_if_needed();
    void apply_settings_to_camera(const camera::RealismSettings& settings, bool effects_enabled);
    camera::RealismSettings read_settings_from_ui() const;
    void on_control_value_changed();
    void enforce_depth_effects_choice();

private:
    Assets* assets_ = nullptr;
    camera::RealismSettings last_settings_{};
    bool last_realism_enabled_ = true;

    bool suppress_apply_once_ = false;
    bool was_visible_ = false;

    std::unique_ptr<DMCheckbox> effects_checkbox_;
    std::unique_ptr<CheckboxWidget> effects_widget_;

    std::unique_ptr<Widget> header_spacer_;
    std::unique_ptr<Widget> hero_banner_widget_;
    std::unique_ptr<Widget> controls_spacer_;
    std::unique_ptr<SectionToggleWidget> visibility_section_header_;
    std::unique_ptr<SectionToggleWidget> depth_section_header_;
    std::unique_ptr<SectionToggleWidget> colors_section_header_;
    std::unique_ptr<SectionToggleWidget> blur_section_header_;
    std::unique_ptr<SectionToggleWidget> zoom_section_header_;
    std::unique_ptr<SectionToggleWidget> smoothing_section_header_;

    std::unique_ptr<FloatSliderWidget> render_distance_slider_;
    std::unique_ptr<FloatSliderWidget> render_radius_y_offset_slider_;
    std::unique_ptr<FloatSliderWidget> tripod_distance_slider_;
    std::unique_ptr<FloatSliderWidget> height_zoom1_slider_;
    std::unique_ptr<FloatSliderWidget> parallax_strength_slider_;
    std::unique_ptr<FloatSliderWidget> foreshorten_strength_slider_;
    std::unique_ptr<FloatSliderWidget> distance_strength_slider_;
    std::unique_ptr<FloatSliderWidget> min_render_size_slider_;
    // Perspective Colors sliders
    std::unique_ptr<FloatSliderWidget> distance_saturation_factor_min_slider_;
    std::unique_ptr<FloatSliderWidget> distance_saturation_factor_max_slider_;
    std::unique_ptr<FloatSliderWidget> primary_color_boost_min_slider_;
    std::unique_ptr<FloatSliderWidget> primary_color_boost_max_slider_;
    std::unique_ptr<FloatSliderWidget> ground_brightness_factor_slider_;
    std::unique_ptr<FloatSliderWidget> background_brightness_slider_;
    // Perspective Blur sliders
    std::unique_ptr<FloatSliderWidget> max_foreground_blur_slider_;
    std::unique_ptr<FloatSliderWidget> max_background_blur_slider_;
    // Interpolation dropdowns (UI-only for now)
    std::unique_ptr<DMDropdown> blur_falloff_dropdown_;
    std::unique_ptr<DropdownWidget> blur_falloff_widget_;
    std::unique_ptr<DMDropdown> color_primary_interp_dropdown_;
    std::unique_ptr<DropdownWidget> color_primary_interp_widget_;
    std::unique_ptr<DMDropdown> color_brightness_interp_dropdown_;
    std::unique_ptr<DropdownWidget> color_brightness_interp_widget_;
    std::unique_ptr<DiscreteSliderWidget> render_quality_slider_;
    std::unique_ptr<DMCheckbox> smoothing_checkbox_;
    std::unique_ptr<CheckboxWidget> smoothing_widget_;
    std::unique_ptr<DMDropdown> smoothing_method_dropdown_;
    std::unique_ptr<DropdownWidget> smoothing_method_widget_;
    std::unique_ptr<FloatSliderWidget> motion_tau_slider_;
    std::unique_ptr<FloatSliderWidget> motion_stiffness_slider_;
    std::unique_ptr<FloatSliderWidget> motion_max_step_slider_;
    std::unique_ptr<FloatSliderWidget> motion_snap_slider_;
    std::unique_ptr<FloatSliderWidget> parallax_smoothing_slider_;
    std::unique_ptr<FloatSliderWidget> hysteresis_margin_slider_;
    std::unique_ptr<FloatSliderWidget> min_zoom_multiplier_slider_;
    std::unique_ptr<FloatSliderWidget> max_zoom_multiplier_slider_;

    bool visibility_section_expanded_ = true;
    bool depth_section_expanded_ = true;
    bool colors_section_expanded_ = false;
    bool blur_section_expanded_ = false;
    bool zoom_section_expanded_ = false;
    bool smoothing_section_expanded_ = false;

protected:
    std::string_view lock_settings_namespace() const override { return "camera"; }
    std::string_view lock_settings_id() const override { return "controls"; }
    void layout_custom_content(int screen_w, int screen_h) const override;
};
