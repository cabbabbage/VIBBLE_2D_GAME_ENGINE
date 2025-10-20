#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <nlohmann/json_fwd.hpp>

class MapLightPanel;
class Assets;
class Input;
class DMSlider;
class DMButton;
class Widget;

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
    void build_ui();
    void rebuild_rows();
    void sync_ui_from_json();
    void sync_json_from_ui();
    void apply_settings_to_shared();

    // Light-map controls moved from preview panel
    void apply_virtual_light_map_quadrant_size(int size_px, bool apply_to_assets, bool mark_pending = true);
    void request_light_map_regeneration();

    MapLightPanel* light_panel_ = nullptr;
    Assets*        assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    SaveCallback    on_save_{};
    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;

    std::unique_ptr<DMSlider> opacity_strength_{};
    std::unique_ptr<DMSlider> parallax_strength_{};
    std::unique_ptr<DMSlider> scale_strength_{};
    std::unique_ptr<DMSlider> horizontal_falloff_{};
    std::unique_ptr<DMSlider> vertical_falloff_{};
    // Deprecated: max offset sliders removed from the shading panel UI
    std::unique_ptr<DMSlider> search_radius_{};
    std::unique_ptr<DMSlider> map_light_factor_{};
    std::unique_ptr<DMSlider> quadrant_size_px_{};
    std::unique_ptr<DMButton> regenerate_button_{};
    std::vector<std::unique_ptr<Widget>> widget_wrappers_{};
    render_pipeline::shading::ReactiveShadowSettings last_settings_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    bool needs_sync_to_json_ = false;
    int last_quadrant_size_px_ = 0;
    bool pending_light_map_regeneration_ = false;

protected:
    std::string_view lock_settings_namespace() const override { return "lighting"; }
    std::string_view lock_settings_id() const override { return "shadow_panel"; }
};
