#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <nlohmann/json.hpp>

class MapLightPanel;
class Assets;
struct VirtualLightMap;
class Input;

class MapShadowPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    MapShadowPanel(MapLightPanel* light_panel, Assets* assets, int x = 72, int y = 40);
    ~MapShadowPanel() override;

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

protected:
    void render_content(SDL_Renderer* r) const override;
    void layout_custom_content(int screen_w, int screen_h) const override;

private:
    void update_save_status(bool success) const;
    void build_ui();
    void rebuild_rows();
    void update_section_header_labels();
    void sync_ui_from_json();
    void sync_json_from_ui();
    void apply_immediate_settings();
    render_pipeline::shading::ReactiveShadowSettings current_settings_from_ui() const;
    void set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings);
    void set_reactive_checkboxes(const render_pipeline::shading::ReactiveShadowSettings& settings);
    render_pipeline::shading::ReactiveShadowSettings load_reactive_settings_from_dev_settings() const;
    void persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const;
    void write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings);
    nlohmann::json& ensure_reactive_settings_json();

    void toggle_opacity_section();
    void toggle_placement_section();
    void toggle_scale_section();

    static int clamp_int(int v, int lo, int hi);

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;
    MapLightPanel* light_panel_ = nullptr;
    Assets* assets_ = nullptr;

    std::unique_ptr<DMSlider> quadrant_count_;
    std::unique_ptr<DMSlider> quadrant_distance_falloff_;
    std::unique_ptr<DMSlider> quadrant_directional_strength_;

    std::unique_ptr<DMCheckbox> reactive_offsets_enabled_;
    std::unique_ptr<DMCheckbox> reactive_opacity_enabled_;
    std::unique_ptr<DMCheckbox> reactive_temporal_enabled_;

    std::unique_ptr<DMSlider> reactive_kernel_radius_;
    std::unique_ptr<DMSlider> reactive_outer_ring_weight_;
    std::unique_ptr<DMSlider> reactive_diagonal_weight_;
    std::unique_ptr<DMSlider> reactive_gradient_sensitivity_;
    std::unique_ptr<DMSlider> reactive_offset_strength_;
    std::unique_ptr<DMSlider> reactive_max_offset_ratio_;
    std::unique_ptr<DMSlider> reactive_front_weight_;
    std::unique_ptr<DMSlider> reactive_side_weight_;
    std::unique_ptr<DMSlider> reactive_back_weight_;
    std::unique_ptr<DMSlider> reactive_scale_factor_;
    std::unique_ptr<DMSlider> reactive_map_line_weight_;
    std::unique_ptr<DMSlider> reactive_parallax_strength_;
    std::unique_ptr<DMSlider> reactive_opacity_strength_;
    std::unique_ptr<DMSlider> reactive_min_opacity_;
    std::unique_ptr<DMSlider> reactive_max_opacity_;
    std::unique_ptr<DMSlider> reactive_temporal_smoothing_;
    std::unique_ptr<DMSlider> reactive_front_opacity_boost_;
    std::unique_ptr<DMSlider> reactive_similarity_threshold_;

    std::unique_ptr<DMButton> opacity_section_btn_;
    std::unique_ptr<DMButton> placement_section_btn_;
    std::unique_ptr<DMButton> scale_section_btn_;
    bool opacity_section_collapsed_ = false;
    bool placement_section_collapsed_ = false;
    bool scale_section_collapsed_ = false;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    class WarningLabel;
    WarningLabel* warning_label_ = nullptr;

    bool needs_sync_to_json_ = false;

    mutable std::string persistence_warning_text_;

    render_pipeline::shading::ReactiveShadowSettings last_applied_settings_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;
    bool reactive_settings_initialized_ = false;

    static constexpr int kPreviewWidth = 220;
    static constexpr int kPreviewPadding = 8;
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};
    mutable SDL_Rect preview_grid_rect_{0, 0, 0, 0};
    mutable int screen_width_px_ = 0;
    mutable int screen_height_px_ = 0;

protected:
    std::string_view lock_settings_namespace() const override { return "lighting"; }
    std::string_view lock_settings_id() const override { return "shadow_panel"; }
};
