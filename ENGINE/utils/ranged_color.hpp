#pragma once

#include <SDL.h>
#include <nlohmann/json.hpp>
#include <optional>

namespace utils {
namespace color {

struct ChannelRange {
    int min = 0;
    int max = 255;
};

struct RangedColor {
    ChannelRange r;
    ChannelRange g;
    ChannelRange b;
    ChannelRange a;
};

ChannelRange clamp_channel_range(const ChannelRange& range);
RangedColor clamp_ranged_color(const RangedColor& color);

std::optional<RangedColor> ranged_color_from_json(const nlohmann::json& value);

nlohmann::json ranged_color_to_json(const RangedColor& color);

SDL_Color resolve_ranged_color(const RangedColor& color);
SDL_Color resolve_ranged_color(const nlohmann::json& value,
                               SDL_Color fallback = SDL_Color{255, 255, 255, 255});

SDL_Color clamp_color(SDL_Color color);
std::optional<SDL_Color> color_from_json(const nlohmann::json& value);
nlohmann::json color_to_json(SDL_Color color);

}
}

