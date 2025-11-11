#pragma once

#include <SDL.h>
#include <string_view>
#include <optional>
#include <nlohmann/json.hpp>

#include "utils/ranged_color.hpp"

class Global_Light_Source {

public:
    Global_Light_Source(SDL_Renderer* renderer, SDL_Point screen_center, int screen_width, SDL_Color fallback_base_color);
    ~Global_Light_Source() = default;
    bool initialize_from_map_manifest(const nlohmann::json& map_info, std::string_view map_id);
    void apply_config(const nlohmann::json& data);
    SDL_Color get_current_color() const;
    int       get_brightness() const;
    // Temporarily override the alpha channel of the current map light color
    // without modifying underlying map configuration. Pass std::nullopt to clear.
    void      set_alpha_override(std::optional<Uint8> alpha);

private:
    void set_defaults(SDL_Color fallback_base_color);
    bool load_from_map_manifest(const nlohmann::json& map_info, std::string_view map_id);
    SDL_Color resolve_color_from_config(const nlohmann::json& data) const;
    Uint8 clamp_alpha(Uint8 value) const;
    SDL_Color clamp_color_alpha(SDL_Color color) const;

private:
    SDL_Renderer* renderer_;
    SDL_Point     screen_center_;
    utils::color::RangedColor base_color_range_{{255,255},{255,255},{255,255},{255,255}};
    SDL_Color base_color_;
    SDL_Color current_color_;
    int       light_brightness = 255;

    // Optional override for alpha channel applied in get_current_color()
    std::optional<Uint8> alpha_override_{};
};
