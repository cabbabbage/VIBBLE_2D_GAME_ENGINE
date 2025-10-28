#pragma once

#include <memory>
#include "DockableCollapsible.hpp"
#include "render/camera.hpp"

class Assets;
class DMCheckbox;
class DMButton;
class CheckboxWidget;
class ButtonWidget;
class DMDropdown;
class DropdownWidget;
class Input;
class FloatSliderWidget;
class SectionLabelWidget;
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
    void reset_to_defaults();
    void reload_from_json();
    void apply_settings_if_needed();
    void apply_settings_to_camera(const camera::RealismSettings& settings, bool effects_enabled);
    camera::RealismSettings read_settings_from_ui() const;

private:
    Assets* assets_ = nullptr;
    camera::RealismSettings last_settings_{};
    bool last_realism_enabled_ = true;

    bool suppress_apply_once_ = false;

    std::unique_ptr<DMCheckbox> effects_checkbox_;
    std::unique_ptr<CheckboxWidget> effects_widget_;

    std::unique_ptr<DMButton> load_button_;
    std::unique_ptr<DMButton> reset_button_;
    std::unique_ptr<ButtonWidget> load_widget_;
    std::unique_ptr<ButtonWidget> reset_widget_;

    std::unique_ptr<SectionLabelWidget> render_section_label_;
    std::unique_ptr<SectionLabelWidget> perspective_section_label_;
    std::unique_ptr<SectionLabelWidget> smoothing_section_label_;

    std::unique_ptr<FloatSliderWidget> render_distance_slider_;
    std::unique_ptr<FloatSliderWidget> tripod_distance_slider_;
    std::unique_ptr<FloatSliderWidget> height_zoom1_slider_;
    std::unique_ptr<FloatSliderWidget> parallax_strength_slider_;
    std::unique_ptr<FloatSliderWidget> foreshorten_strength_slider_;
    std::unique_ptr<FloatSliderWidget> distance_strength_slider_;
    std::unique_ptr<FloatSliderWidget> min_render_size_slider_;
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

protected:
    std::string_view lock_settings_namespace() const override { return "camera"; }
    std::string_view lock_settings_id() const override { return "controls"; }
};
