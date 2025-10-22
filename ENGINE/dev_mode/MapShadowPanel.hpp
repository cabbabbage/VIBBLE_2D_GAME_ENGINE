#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "utils/shadow_mask_settings.hpp"

class Assets;
class Input;
class DMSlider;

class MapShadowPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    MapShadowPanel(Assets* assets, int x = 0, int y = 0);
    ~MapShadowPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w = 0, int screen_h = 0) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;
    bool is_point_inside(int x, int y) const override;

protected:
    void render_content(SDL_Renderer* renderer) const override;

private:
    void build_ui();
    void rebuild_rows();
    void set_slider_defaults();
    void sync_ui_from_json();
    bool sync_json_from_ui();
    void apply_immediate_settings(bool force_refresh = false);
    render_pipeline::shading::ReactiveShadowSettings current_settings_from_ui() const;
    ShadowMaskSettings current_shadow_mask_from_ui() const;
    void set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings);
    void set_shadow_mask_sliders(const ShadowMaskSettings& settings);
    void write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings);
    void write_shadow_mask_settings_to_json(const ShadowMaskSettings& settings);

    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_{};

    std::unique_ptr<DMSlider> horizontal_falloff_;
    std::unique_ptr<DMSlider> vertical_falloff_;
    std::unique_ptr<DMSlider> max_offset_x_;
    std::unique_ptr<DMSlider> max_offset_y_;
    std::unique_ptr<DMSlider> shadow_scale_;
    std::unique_ptr<DMSlider> size_scale_factor_;
    std::unique_ptr<DMSlider> search_radius_;
    std::unique_ptr<DMSlider> opacity_strength_;
    std::unique_ptr<DMSlider> parallax_strength_;
    std::unique_ptr<DMSlider> scale_strength_;
    std::unique_ptr<DMSlider> static_weight_;
    std::unique_ptr<DMSlider> dynamic_weight_;

    std::unique_ptr<DMSlider> mask_expansion_ratio_;
    std::unique_ptr<DMSlider> mask_blur_scale_;
    std::unique_ptr<DMSlider> mask_falloff_start_;
    std::unique_ptr<DMSlider> mask_falloff_exponent_;
    std::unique_ptr<DMSlider> mask_alpha_multiplier_;

    std::vector<std::unique_ptr<class Widget>> widget_wrappers_{};

    render_pipeline::shading::ReactiveShadowSettings last_applied_settings_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    render_pipeline::shading::ReactiveShadowSettings forced_settings_snapshot_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    ShadowMaskSettings last_shadow_mask_settings_ = SanitizeShadowMaskSettings({});

    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;
};

