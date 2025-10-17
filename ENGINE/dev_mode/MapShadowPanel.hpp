#pragma once

#include <functional>
#include <memory>
#include <string>
#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

#include <nlohmann/json_fwd.hpp>

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
    MapLightPanel* light_panel_ = nullptr;
    Assets*        assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    SaveCallback    on_save_{};
    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;

protected:
    std::string_view lock_settings_namespace() const override { return "lighting"; }
    std::string_view lock_settings_id() const override { return "shadow_panel"; }
};
