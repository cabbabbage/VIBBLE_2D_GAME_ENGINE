#pragma once

#include <SDL.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

class Assets;
class LightMap;

namespace render_pipeline::shading {
struct ReactiveShadowSettings;
}

class LightMapManager {
public:
    struct QuadrantSnapshot {
        int      index               = -1;
        SDL_Rect world_rect{0, 0, 0, 0};
        bool     active              = false;
        bool     dirty               = false;
        float    base_brightness     = 0.0f;
        float    combined_brightness = 0.0f;
        float    static_average      = 0.0f;
        float    dynamic_average     = 0.0f;
        float    dynamic_min         = 0.0f;
        float    dynamic_max         = 0.0f;
        float    shadow_opacity_min  = 0.0f;
        float    shadow_opacity_max  = 0.0f;
    };

    explicit LightMapManager(Assets* assets);

    const LightMap* light_map() const;
    std::vector<QuadrantSnapshot> all_snapshots() const;
    std::vector<std::string>      assets_sampling_quadrant(int index) const;
    std::optional<QuadrantSnapshot> snapshot_for_quadrant(int index) const;

private:
    Assets* assets_ = nullptr;
};

