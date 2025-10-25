#include "ranged_color.hpp"

#include <algorithm>
#include <cmath>
#include <random>

namespace utils {
namespace color {

namespace {

int clamp_channel_value(int v) {
    return std::max(0, std::min(255, v));
}

std::optional<int> parse_channel_component(const nlohmann::json& value) {
    try {
        if (value.is_number_integer()) {
            return value.get<int>();
        }
        if (value.is_number_float()) {
            return static_cast<int>(std::lround(value.get<double>()));
        }
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            if (!text.empty() && text[0] == '#') {

                return std::nullopt;
            }
            size_t idx = 0;
            int parsed = std::stoi(text, &idx);
            if (idx == text.size()) {
                return parsed;
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<SDL_Color> parse_hex_color_string(const std::string& text) {
    if (text.empty() || text[0] != '#') {
        return std::nullopt;
    }
    auto parse_pair = [&](size_t offset) -> std::optional<int> {
        if (offset + 2 > text.size()) {
            return std::nullopt;
        }
        int value = 0;
        for (size_t i = offset; i < offset + 2; ++i) {
            char c = text[i];
            int digit = 0;
            if (c >= '0' && c <= '9') {
                digit = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                digit = 10 + (c - 'a');
            } else if (c >= 'A' && c <= 'F') {
                digit = 10 + (c - 'A');
            } else {
                return std::nullopt;
            }
            value = (value << 4) | digit;
        }
        return value;
};

    if (text.size() != 7 && text.size() != 9) {
        return std::nullopt;
    }
    auto r = parse_pair(1);
    auto g = parse_pair(3);
    auto b = parse_pair(5);
    auto a = text.size() == 9 ? parse_pair(7) : std::optional<int>{255};
    if (!r || !g || !b || !a) {
        return std::nullopt;
    }
    SDL_Color color{static_cast<Uint8>(clamp_channel_value(*r)),
                    static_cast<Uint8>(clamp_channel_value(*g)), static_cast<Uint8>(clamp_channel_value(*b)), static_cast<Uint8>(clamp_channel_value(*a))};
    return color;
}

ChannelRange make_range(int min_v, int max_v) {
    ChannelRange out;
    out.min = clamp_channel_value(std::min(min_v, max_v));
    out.max = clamp_channel_value(std::max(min_v, max_v));
    return out;
}

Uint8 random_channel_value(const ChannelRange& range) {
    const ChannelRange clamped = clamp_channel_range(range);
    static thread_local std::mt19937 rng([] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd()};
        return std::mt19937(seq);
    }());
    std::uniform_int_distribution<int> dist(clamped.min, clamped.max);
    return static_cast<Uint8>(dist(rng));
}

}

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
    return SDL_Color{
        random_channel_value(clamped.r), random_channel_value(clamped.g), random_channel_value(clamped.b), random_channel_value(clamped.a) };
}

SDL_Color resolve_ranged_color(const nlohmann::json& value, SDL_Color fallback) {
    if (auto parsed = ranged_color_from_json(value)) {
        return resolve_ranged_color(*parsed);
    }
    return fallback;
}

SDL_Color clamp_color(SDL_Color color) {
    color.r = static_cast<Uint8>(clamp_channel_value(color.r));
    color.g = static_cast<Uint8>(clamp_channel_value(color.g));
    color.b = static_cast<Uint8>(clamp_channel_value(color.b));
    color.a = static_cast<Uint8>(clamp_channel_value(color.a));
    return color;
}

std::optional<SDL_Color> color_from_json(const nlohmann::json& value) {
    if (value.is_string()) {
        if (auto parsed = parse_hex_color_string(value.get<std::string>())) {
            return clamp_color(*parsed);
        }
    }

    if (value.is_array()) {
        std::optional<int> components[4];
        const size_t count = std::min<size_t>(value.size(), 4);
        for (size_t i = 0; i < count; ++i) {
            components[i] = parse_channel_component(value[i]);
        }
        if (components[0] && components[1] && components[2]) {
            const int alpha = components[3].value_or(255);
            SDL_Color color{static_cast<Uint8>(clamp_channel_value(*components[0])),
                            static_cast<Uint8>(clamp_channel_value(*components[1])), static_cast<Uint8>(clamp_channel_value(*components[2])), static_cast<Uint8>(clamp_channel_value(alpha))};
            return clamp_color(color);
        }
    }

    if (value.is_object()) {
        auto read_component = [&](const char* key) -> std::optional<int> {
            auto it = value.find(key);
            if (it == value.end()) {
                return std::nullopt;
            }
            return parse_channel_component(*it);
};
        auto r = read_component("r");
        auto g = read_component("g");
        auto b = read_component("b");
        if (r && g && b) {
            const int alpha = read_component("a").value_or(255);
            SDL_Color color{static_cast<Uint8>(clamp_channel_value(*r)),
                            static_cast<Uint8>(clamp_channel_value(*g)), static_cast<Uint8>(clamp_channel_value(*b)), static_cast<Uint8>(clamp_channel_value(alpha))};
            return clamp_color(color);
        }
    }

    return std::nullopt;
}

nlohmann::json color_to_json(SDL_Color color) {
    SDL_Color clamped = clamp_color(color);
    return nlohmann::json::array({
        static_cast<int>(clamped.r),
        static_cast<int>(clamped.g),
        static_cast<int>(clamped.b),
        static_cast<int>(clamped.a)
    });
}

}
}

