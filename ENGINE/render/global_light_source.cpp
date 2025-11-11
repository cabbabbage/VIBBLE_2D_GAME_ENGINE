#include "global_light_source.hpp"

#include <iostream>

using json = nlohmann::json;

namespace {

SDL_Color ranged_color_to_sdl(const utils::color::RangedColor& range) {
    return utils::color::resolve_ranged_color(range);
}

std::optional<utils::color::RangedColor> first_key_color(const json& data) {
    auto keys_it = data.find("keys");
    if (keys_it == data.end() || !keys_it->is_array() || keys_it->empty()) {
        return std::nullopt;
    }
    const auto& entry = (*keys_it)[0];
    if (!entry.is_array() || entry.size() < 2) {
        return std::nullopt;
    }
    return utils::color::ranged_color_from_json(entry[1]);
}

} // namespace

Global_Light_Source::Global_Light_Source(SDL_Renderer* renderer,
                                         SDL_Point screen_center,
                                         int /*screen_width*/,
                                         SDL_Color fallback_base_color)
: renderer_(renderer),
  screen_center_(screen_center) {
    set_defaults(fallback_base_color);
}

void Global_Light_Source::set_defaults(SDL_Color fallback_base_color) {
    base_color_range_ = utils::color::RangedColor{
        {fallback_base_color.r, fallback_base_color.r},
        {fallback_base_color.g, fallback_base_color.g},
        {fallback_base_color.b, fallback_base_color.b},
        {fallback_base_color.a, fallback_base_color.a}
    };
    base_color_      = clamp_color_alpha(fallback_base_color);
    current_color_   = base_color_;
    light_brightness = current_color_.a;
}

bool Global_Light_Source::load_from_map_manifest(const json& map_info, std::string_view map_id) {
    if (!map_info.is_object()) {
        std::cerr << "[MapLight] Map manifest for '" << map_id << "' is not an object. Using defaults.\n";
        return false;
    }

    auto it = map_info.find("map_light_data");
    if (it == map_info.end() || !it->is_object()) {
        if (!map_id.empty()) {
            std::cerr << "[MapLight] Manifest for '" << map_id << "' has no valid map_light_data object. Using defaults.\n";
        } else {
            std::cerr << "[MapLight] Manifest has no valid map_light_data object. Using defaults.\n";
        }
        return false;
    }

    apply_config(*it);
    return true;
}

bool Global_Light_Source::initialize_from_map_manifest(const json& map_info, std::string_view map_id) {
    if (!load_from_map_manifest(map_info, map_id)) {
        current_color_   = base_color_;
        light_brightness = current_color_.a;
        return false;
    }
    return true;
}

void Global_Light_Source::apply_config(const json& data) {
    if (!data.is_object()) {
        return;
    }

    SDL_Color resolved = resolve_color_from_config(data);
    base_color_        = clamp_color_alpha(resolved);
    current_color_     = base_color_;
    base_color_range_ = utils::color::RangedColor{
        {base_color_.r, base_color_.r},
        {base_color_.g, base_color_.g},
        {base_color_.b, base_color_.b},
        {base_color_.a, base_color_.a}
    };
    light_brightness = current_color_.a;
}

SDL_Color Global_Light_Source::resolve_color_from_config(const json& data) const {
    if (auto base = utils::color::ranged_color_from_json(data.value("base_color", json{}))) {
        return clamp_color_alpha(ranged_color_to_sdl(*base));
    }
    if (auto key_color = first_key_color(data)) {
        return clamp_color_alpha(ranged_color_to_sdl(*key_color));
    }
    return current_color_;
}

SDL_Color Global_Light_Source::get_current_color() const {
    if (alpha_override_.has_value()) {
        SDL_Color c = current_color_;
        c.a = clamp_alpha(*alpha_override_);
        return c;
    }
    return current_color_;
}

int Global_Light_Source::get_brightness() const {
    return light_brightness;
}

void Global_Light_Source::set_alpha_override(std::optional<Uint8> alpha) {
    if (alpha.has_value()) {
        alpha_override_ = clamp_alpha(*alpha);
    } else {
        alpha_override_.reset();
    }
}

Uint8 Global_Light_Source::clamp_alpha(Uint8 value) const {
    const Uint8 min_alpha = 0;
    const Uint8 max_alpha = 255;
    if (value < min_alpha) {
        return min_alpha;
    }
    if (value > max_alpha) {
        return max_alpha;
    }
    return value;
}

SDL_Color Global_Light_Source::clamp_color_alpha(SDL_Color color) const {
    color.a = clamp_alpha(color.a);
    return color;
}
