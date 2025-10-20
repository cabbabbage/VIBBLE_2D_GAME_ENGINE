// Dev-mode Shadow panel removed: provide a no-op stub to satisfy references.
#pragma once

#include <functional>
#include <SDL.h>

#include "DockableCollapsible.hpp"

namespace nlohmann { class json; }
class Assets; class Input; class SDL_Renderer;

class MapShadowPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;
    MapShadowPanel(Assets*, int = 0, int = 0) : DockableCollapsible("Shadows", true, 0, 0) {}
    ~MapShadowPanel() override = default;

    void set_map_info(nlohmann::json*, SaveCallback = nullptr) {}
    void set_reactive_settings(void*) {}

    void open() {}
    void close() {}
    void toggle() {}
    bool is_visible() const { return false; }

    void update(const Input&, int = 0, int = 0) {}
    bool handle_event(const SDL_Event&) { return false; }
    void render(SDL_Renderer*) const {}
    bool is_point_inside(int, int) const { return false; }

protected:
    void render_content(SDL_Renderer*) const override {}
    void layout_custom_content(int, int) const override {}
};
