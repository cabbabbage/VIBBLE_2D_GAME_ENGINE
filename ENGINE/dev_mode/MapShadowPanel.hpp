#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "render/light_map.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <nlohmann/json.hpp>

class MapLightPanel;
class Assets;
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
    void sync_ui_from_json();
    void sync_json_from_ui();
    void apply_immediate_settings();
    render_pipeline::shading::ReactiveShadowSettings current_settings_from_ui() const;
    void set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings);
    render_pipeline::shading::ReactiveShadowSettings load_reactive_settings_from_dev_settings() const;
    void persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const;
    void write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings);
    nlohmann::json& ensure_reactive_settings_json();

    void apply_virtual_light_map_quadrants(int quadrants, bool force_refresh);
    void force_shading_refresh_if_needed(bool force_refresh);

    static int clamp_int(int v, int lo, int hi);

    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_;
    MapLightPanel* light_panel_ = nullptr;
    Assets* assets_ = nullptr;

    std::unique_ptr<DMSlider> map_light_factor_;
    std::unique_ptr<DMSlider> horizontal_falloff_;
    std::unique_ptr<DMSlider> vertical_falloff_;
    std::unique_ptr<DMSlider> max_offset_x_;
    std::unique_ptr<DMSlider> max_offset_y_;
    std::unique_ptr<DMSlider> shadow_scale_;
    std::unique_ptr<DMSlider> size_scale_factor_;
    std::unique_ptr<DMSlider> quadrant_count_;

    std::vector<std::unique_ptr<Widget>> widget_wrappers_;

    bool needs_sync_to_json_ = false;

    render_pipeline::shading::ReactiveShadowSettings last_applied_settings_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;
    bool reactive_settings_initialized_ = false;
    render_pipeline::shading::ReactiveShadowSettings forced_settings_snapshot_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    int last_quadrant_count_ = VirtualLightMap::kDefaultGridSize;
    int forced_quadrant_snapshot_ = VirtualLightMap::kDefaultGridSize;

protected:
    std::string_view lock_settings_namespace() const override { return "lighting"; }
    std::string_view lock_settings_id() const override { return "shadow_panel"; }
};
