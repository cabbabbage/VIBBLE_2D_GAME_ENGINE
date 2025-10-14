#pragma once

#include <array>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "render/lens_flare_renderer.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <nlohmann/json.hpp>

namespace render_pipeline::shading {
struct ReactiveShadowSettings;
}

struct OrbitSettings;
struct ScreenLightSettings;

class MapLightPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    MapLightPanel(int x = 40, int y = 40);
    ~MapLightPanel() override;

    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings);

    void open();
    void close();
    void toggle();
    bool is_visible() const;

    void update(const Input& input, int screen_w = 0, int screen_h = 0);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    bool is_point_inside(int x, int y) const;

    const std::string& persistence_warning() const { return persistence_warning_text_; }

protected:

    void render_content(SDL_Renderer* r) const override;

private:

    void update_save_status(bool success) const;

    void build_ui();
    void apply_changes();
    void rebuild_rows();
    void update_section_header_labels();
    void sync_ui_from_json();
    void sync_json_from_ui();
    void load_update_map_light_setting();
    nlohmann::json& ensure_light();
    nlohmann::json& ensure_screen_light(nlohmann::json& light);
    nlohmann::json& ensure_reactive_settings(nlohmann::json& light);
    nlohmann::json& ensure_lens_flare_settings(nlohmann::json& light);

    struct OrbitSettings {
        int update_interval = 10;
        int orbit_x = 0;
        int orbit_y = 0;
        int min_opacity = 0;
        int max_opacity = 255;
        bool operator==(const OrbitSettings& other) const {
            return update_interval == other.update_interval &&
                   orbit_x == other.orbit_x &&
                   orbit_y == other.orbit_y &&
                   min_opacity == other.min_opacity &&
                   max_opacity == other.max_opacity;
        }
};

    struct ScreenLightSettings {
        int r = 255;
        int g = 255;
        int b = 255;
        int min_opacity = 0;
        int max_opacity = 255;
        bool operator==(const ScreenLightSettings& other) const {
            return r == other.r &&
                   g == other.g &&
                   b == other.b &&
                   min_opacity == other.min_opacity &&
                   max_opacity == other.max_opacity;
        }
};

    OrbitSettings sanitize_orbit_settings(const OrbitSettings& raw) const;
    ScreenLightSettings sanitize_screen_settings(const ScreenLightSettings& raw, const OrbitSettings& orbit) const;
    OrbitSettings current_orbit_settings_from_ui() const;
    ScreenLightSettings current_screen_settings_from_ui() const;
    void set_orbit_sliders(const OrbitSettings& orbit);
    void set_screen_sliders(const ScreenLightSettings& screen);
    LensFlareRenderer::Settings current_lens_settings_from_ui() const;
    void set_lens_sliders(const LensFlareRenderer::Settings& settings);
    void write_lens_settings_to_json(const LensFlareRenderer::Settings& settings);
    render_pipeline::shading::ReactiveShadowSettings current_reactive_settings_from_ui() const;
    void set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings);
    void set_reactive_checkboxes(const render_pipeline::shading::ReactiveShadowSettings& settings);
    render_pipeline::shading::ReactiveShadowSettings load_reactive_settings_from_dev_settings() const;
    void persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const;
    void write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings);
    void write_orbit_settings_to_json(const OrbitSettings& orbit);
    void write_screen_settings_to_json(const ScreenLightSettings& screen);
    void apply_immediate_settings();
    bool commit_light_changes();

    void ensure_keys_array();
    void clamp_key_index();
    void select_prev_key();
    void select_next_key();
    void add_key_pair_at_current_angle();
    void delete_current_key();

    static int clamp_int(int v, int lo, int hi);
    static float clamp_float(float v, float lo, float hi);
    static float wrap_angle(float a);

private:

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;
    nlohmann::json editing_light_{};

    int current_key_index_ = 0;

    std::unique_ptr<DMCheckbox> update_map_light_checkbox_;
    std::unique_ptr<DMButton> update_btn_;
    std::unique_ptr<DMButton> orbit_section_btn_;
    std::unique_ptr<DMButton> screen_section_btn_;
    std::unique_ptr<DMButton> texture_section_btn_;
    std::unique_ptr<DMButton> reactive_section_btn_;
    std::unique_ptr<DMButton> lens_section_btn_;
    bool orbit_section_collapsed_ = false;
    bool screen_section_collapsed_ = false;
    bool texture_section_collapsed_ = false;
    bool reactive_section_collapsed_ = false;
    bool lens_section_collapsed_ = false;
    std::unique_ptr<DMSlider> radius_;
    std::unique_ptr<DMSlider> intensity_;
    std::unique_ptr<DMSlider> orbit_x_;
    std::unique_ptr<DMSlider> orbit_y_;
    std::unique_ptr<DMSlider> update_interval_;
    std::unique_ptr<DMSlider> mult_x100_;
    std::unique_ptr<DMSlider> falloff_;
    std::unique_ptr<DMSlider> min_opacity_;
    std::unique_ptr<DMSlider> max_opacity_;

    std::unique_ptr<DMSlider> screen_r_;
    std::unique_ptr<DMSlider> screen_g_;
    std::unique_ptr<DMSlider> screen_b_;
    std::unique_ptr<DMSlider> screen_min_opacity_;
    std::unique_ptr<DMSlider> screen_max_opacity_;

    std::unique_ptr<DMSlider> base_r_;
    std::unique_ptr<DMSlider> base_g_;
    std::unique_ptr<DMSlider> base_b_;
    std::unique_ptr<DMSlider> base_a_;

    std::unique_ptr<DMCheckbox> lens_enabled_;
    std::unique_ptr<DMSlider> lens_seed_threshold_;
    std::unique_ptr<DMSlider> lens_seed_ema_;
    std::unique_ptr<DMSlider> lens_size_scalar_;
    std::unique_ptr<DMSlider> lens_fade_frames_;
    std::unique_ptr<DMSlider> lens_intensity_gain_;
    std::unique_ptr<DMSlider> lens_alpha_cap_;

    std::unique_ptr<DMButton> prev_key_btn_;
    std::unique_ptr<DMButton> next_key_btn_;
    std::unique_ptr<DMButton> add_pair_btn_;
    std::unique_ptr<DMButton> delete_btn_;

    std::unique_ptr<DMSlider> key_angle_;
    std::unique_ptr<DMSlider> key_r_;
    std::unique_ptr<DMSlider> key_g_;
    std::unique_ptr<DMSlider> key_b_;
    std::unique_ptr<DMSlider> key_a_;

    std::unique_ptr<DMCheckbox> reactive_offsets_enabled_;
    std::unique_ptr<DMCheckbox> reactive_opacity_enabled_;
    std::unique_ptr<DMCheckbox> reactive_temporal_enabled_;

    std::unique_ptr<DMSlider> reactive_kernel_radius_;
    std::unique_ptr<DMSlider> reactive_outer_ring_weight_;
    std::unique_ptr<DMSlider> reactive_diagonal_weight_;
    std::unique_ptr<DMSlider> reactive_gradient_sensitivity_;
    std::unique_ptr<DMSlider> reactive_offset_strength_;
    std::unique_ptr<DMSlider> reactive_max_offset_ratio_;
    std::unique_ptr<DMSlider> reactive_scale_factor_;
    std::unique_ptr<DMSlider> reactive_map_line_weight_;
    std::unique_ptr<DMSlider> reactive_parallax_strength_;
    std::unique_ptr<DMSlider> reactive_opacity_strength_;
    std::unique_ptr<DMSlider> reactive_min_opacity_;
    std::unique_ptr<DMSlider> reactive_max_opacity_;
    std::unique_ptr<DMSlider> reactive_temporal_smoothing_;

    mutable std::string current_key_label_;
    mutable std::string persistence_warning_text_;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    class WarningLabel;
    WarningLabel* warning_label_ = nullptr;

    void toggle_orbit_section();
    void toggle_screen_section();
    void toggle_texture_section();
    void toggle_reactive_section();
    void toggle_lens_section();

    bool needs_sync_to_json_ = false;

    bool update_map_light_enabled_ = false;

    OrbitSettings last_applied_orbit_{};
    ScreenLightSettings last_applied_screen_{};
    render_pipeline::shading::ReactiveShadowSettings last_applied_reactive_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;
    bool reactive_settings_initialized_ = false;
    LensFlareRenderer::Settings last_applied_lens_ = LensFlareRenderer::sanitize_settings(LensFlareRenderer::default_settings());

protected:
    std::string_view lock_settings_namespace() const override { return "lighting"; }
    std::string_view lock_settings_id() const override { return "map_panel"; }
};
