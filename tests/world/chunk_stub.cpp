#include "world/chunk.hpp"

#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/font_cache.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"

SDL_FPoint camera::map_to_screen(SDL_Point world, float, float) const {
    return SDL_FPoint{ static_cast<float>(world.x), static_cast<float>(world.y) };
}

SDL_FPoint camera::screen_to_map(SDL_Point screen, float, float) const {
    return SDL_FPoint{ static_cast<float>(screen.x), static_cast<float>(screen.y) };
}

Global_Light_Source* Assets::map_light_source() {
    return nullptr;
}

const Global_Light_Source* Assets::map_light_source() const {
    return nullptr;
}

SDL_Color Global_Light_Source::get_current_color() const {
    return SDL_Color{255, 255, 255, 255};
}

SDL_Point Global_Light_Source::get_direction_target() const {
    return SDL_Point{0, 0};
}

render_pipeline::shading::ReactiveShadowSettings* Assets::reactive_shadow_settings() {
    return nullptr;
}

const render_pipeline::shading::ReactiveShadowSettings* Assets::reactive_shadow_settings() const {
    return nullptr;
}

const DMLabelStyle& DMStyles::Label() {
    static DMLabelStyle style{dm::FONT_PATH, 12, dm::rgba(255, 255, 255, 255)};
    return style;
}

DMFontCache& DMFontCache::instance() {
    static DMFontCache cache;
    return cache;
}

DMFontCache::~DMFontCache() = default;

SDL_Point DMFontCache::measure_text(const DMLabelStyle&, const std::string&) const {
    return SDL_Point{0, 0};
}

bool DMFontCache::draw_text(SDL_Renderer*,
                            const DMLabelStyle&,
                            const std::string&,
                            int,
                            int,
                            SDL_Rect* out_rect) const {
    if (out_rect) {
        *out_rect = SDL_Rect{0, 0, 0, 0};
    }
    return true;
}
