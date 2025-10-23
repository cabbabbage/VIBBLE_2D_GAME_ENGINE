#include "ranged_color.hpp"

#include <algorithm>
#include <cmath>

namespace utils {
namespace color {

namespace {

int clamp_channel_value(int v) {
    return std::max(0, std::min(255, v));
}

ChannelRange make_range(int min_v, int max_v) {
    ChannelRange out;
    out.min = clamp_channel_value(std::min(min_v, max_v));
    out.max = clamp_channel_value(std::max(min_v, max_v));
    return out;
}

} // namespace

ChannelRange clamp_channel_range(const ChannelRange& range) {
    ChannelRange out;
    out.min = clamp_channel_value(range.min);
    out.max = clamp_channel_value(range.max);
    if (out.min > out.max) {
        std::swap(out.min, out.max);
    }
    return out;
}

RangedColor clamp_ranged_color(const RangedColor& color) {
    RangedColor out;
    out.r = clamp_channel_range(color.r);
    out.g = clamp_channel_range(color.g);
    out.b = clamp_channel_range(color.b);
    out.a = clamp_channel_range(color.a);
    return out;
}

std::optional<RangedColor> ranged_color_from_json(const nlohmann::json& value) {
    RangedColor out;
    bool parsed = false;

    if (value.is_object()) {
        auto read_channel = [&](const char* key) -> std::optional<ChannelRange> {
            auto it = value.find(key);
            if (it == value.end()) {
                return std::nullopt;
            }
            if (it->is_object()) {
                int min_v = 0;
                int max_v = 0;
                try {
                    if (auto min_it = it->find("min"); min_it != it->end()) {
                        min_v = min_it->get<int>();
                    }
                    if (auto max_it = it->find("max"); max_it != it->end()) {
                        max_v = max_it->get<int>();
                    }
                    return make_range(min_v, max_v);
                } catch (...) {
                    return std::nullopt;
                }
            }
            if (it->is_array() && it->size() >= 2) {
                try {
                    int min_v = (*it)[0].get<int>();
                    int max_v = (*it)[1].get<int>();
                    return make_range(min_v, max_v);
                } catch (...) {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        };

        if (auto range = read_channel("r")) { out.r = *range; parsed = true; }
        if (auto range = read_channel("g")) { out.g = *range; parsed = true; }
        if (auto range = read_channel("b")) { out.b = *range; parsed = true; }
        if (auto range = read_channel("a")) { out.a = *range; parsed = true; }
    }

    if (!parsed && value.is_array()) {
        try {
            if (value.size() >= 3) {
                int r = value[0].get<int>();
                int g = value[1].get<int>();
                int b = value[2].get<int>();
                int a = 255;
                if (value.size() >= 4) {
                    a = value[3].get<int>();
                }
                out.r = make_range(r, r);
                out.g = make_range(g, g);
                out.b = make_range(b, b);
                out.a = make_range(a, a);
                parsed = true;
            }
        } catch (...) {
            parsed = false;
        }
    }

    if (!parsed) {
        return std::nullopt;
    }

    return clamp_ranged_color(out);
}

nlohmann::json ranged_color_to_json(const RangedColor& color) {
    const RangedColor clamped = clamp_ranged_color(color);
    auto pack = [](const ChannelRange& range) {
        return nlohmann::json{{"min", range.min}, {"max", range.max}};
    };
    return nlohmann::json{
        {"r", pack(clamped.r)},
        {"g", pack(clamped.g)},
        {"b", pack(clamped.b)},
        {"a", pack(clamped.a)}
    };
}

SDL_Color resolve_ranged_color(const RangedColor& color) {
    const RangedColor clamped = clamp_ranged_color(color);
    auto midpoint = [](const ChannelRange& range) {
        return static_cast<Uint8>((range.min + range.max) / 2);
    };
    return SDL_Color{
        midpoint(clamped.r),
        midpoint(clamped.g),
        midpoint(clamped.b),
        midpoint(clamped.a)
    };
}

SDL_Color resolve_ranged_color(const nlohmann::json& value, SDL_Color fallback) {
    if (auto parsed = ranged_color_from_json(value)) {
        return resolve_ranged_color(*parsed);
    }
    return fallback;
}

} // namespace color
} // namespace utils

