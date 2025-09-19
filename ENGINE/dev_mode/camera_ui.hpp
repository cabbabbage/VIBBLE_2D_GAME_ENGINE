#pragma once

#include <memory>
#include "DockableCollapsible.hpp"
#include "render/camera.hpp"

class Assets;
class DMCheckbox;
class DMButton;
class CheckboxWidget;
class ButtonWidget;
class Input;
class FloatSliderWidget;
class SectionLabelWidget;

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
    void save_to_json();
    void reload_from_json();
    void apply_settings_if_needed();
    void apply_settings_to_camera(const camera::RealismSettings& settings,
                                  bool effects_enabled);
    camera::RealismSettings read_settings_from_ui() const;

private:
    Assets* assets_ = nullptr;
    camera::RealismSettings last_settings_{};
    bool last_realism_enabled_ = true;
    bool suppress_apply_once_ = false;

    std::unique_ptr<DMCheckbox> effects_checkbox_;
    std::unique_ptr<CheckboxWidget> effects_widget_;

    std::unique_ptr<DMButton> load_button_;
    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<DMButton> reset_button_;
    std::unique_ptr<ButtonWidget> load_widget_;
    std::unique_ptr<ButtonWidget> save_widget_;
    std::unique_ptr<ButtonWidget> reset_widget_;

    std::unique_ptr<SectionLabelWidget> render_section_label_;
    std::unique_ptr<SectionLabelWidget> perspective_section_label_;
    std::unique_ptr<SectionLabelWidget> position_section_label_;

    std::unique_ptr<FloatSliderWidget> render_distance_slider_;
    std::unique_ptr<FloatSliderWidget> tripod_distance_slider_;
    std::unique_ptr<FloatSliderWidget> height_zoom1_slider_;
    std::unique_ptr<FloatSliderWidget> parallax_strength_slider_;
    std::unique_ptr<FloatSliderWidget> foreshorten_strength_slider_;
    std::unique_ptr<FloatSliderWidget> distance_strength_slider_;
    std::unique_ptr<FloatSliderWidget> vertical_offset_slider_;
};
