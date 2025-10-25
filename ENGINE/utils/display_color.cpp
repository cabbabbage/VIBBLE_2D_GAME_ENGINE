#include "display_color.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>

#include "utils/ranged_color.hpp"

namespace utils::display_color {

namespace {

constexpr double kGoldenRatioConjugate = 0.6180339887498948482;

double channel_to_unit(Uint8 value) {
    return static_cast<double>(value) / 255.0;
}

SDL_Color clamp_color(SDL_Color color) {
    color.r = static_cast<Uint8>(std::clamp<int>(color.r, 0, 255));
    color.g = static_cast<Uint8>(std::clamp<int>(color.g, 0, 255));
    color.b = static_cast<Uint8>(std::clamp<int>(color.b, 0, 255));
    color.a = static_cast<Uint8>(std::clamp<int>(color.a, 0, 255));
    return color;
}

}

std::optional<SDL_Color> read(const nlohmann::json& entry) {
    if (!entry.is_object()) {
        return std::nullopt;
    }
    auto it = entry.find("display_color");
    if (it == entry.end()) {
        return std::nullopt;
    }
    if (auto parsed = utils::color::color_from_json(*it)) {
        return clamp_color(*parsed);
    }
    return std::nullopt;
}

void write(nlohmann::json& entry, SDL_Color color) {
    entry["display_color"] = utils::color::color_to_json(color);
}

SDL_Color hsv_to_rgb(double hue_degrees, double saturation, double value) {
    while (hue_degrees < 0.0) {
        hue_degrees += 360.0;
    }
    while (hue_degrees >= 360.0) {
        hue_degrees -= 360.0;
    }
    saturation = std::clamp(saturation, 0.0, 1.0);
    value = std::clamp(value, 0.0, 1.0);

    const double chroma = value * saturation;
    const double h_prime = hue_degrees / 60.0;
    const double x = chroma * (1.0 - std::fabs(std::fmod(h_prime, 2.0) - 1.0));

    double r = 0.0;
    double g = 0.0;
    double b = 0.0;

    if (0.0 <= h_prime && h_prime < 1.0) {
        r = chroma;
        g = x;
    } else if (1.0 <= h_prime && h_prime < 2.0) {
        r = x;
        g = chroma;
    } else if (2.0 <= h_prime && h_prime < 3.0) {
        g = chroma;
        b = x;
    } else if (3.0 <= h_prime && h_prime < 4.0) {
        g = x;
        b = chroma;
    } else if (4.0 <= h_prime && h_prime < 5.0) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }

    const double m = value - chroma;
    auto convert = [m](double component) -> Uint8 {
        component = std::clamp(component + m, 0.0, 1.0);
        return static_cast<Uint8>(std::lround(component * 255.0));
};

    SDL_Color out{convert(r), convert(g), convert(b), 255};
    return clamp_color(out);
}

double color_distance(SDL_Color a, SDL_Color b) {
    const double dr = channel_to_unit(a.r) - channel_to_unit(b.r);
    const double dg = channel_to_unit(a.g) - channel_to_unit(b.g);
    const double db = channel_to_unit(a.b) - channel_to_unit(b.b);
    return std::sqrt(dr * dr + dg * dg + db * db);
}

SDL_Color generate_distinct_color(const std::vector<SDL_Color>& used_colors) {
    if (used_colors.empty()) {
        return hsv_to_rgb(210.0, 0.60, 0.88);
    }

    const std::array<double, 3> saturations{0.65, 0.75, 0.85};
    const std::array<double, 3> values{0.88, 0.8, 0.95};

    SDL_Color best = hsv_to_rgb(45.0, 0.7, 0.9);
    double best_score = -1.0;

    const int candidate_count = 360;
    for (int i = 0; i < candidate_count; ++i) {
        const double hue = std::fmod((static_cast<double>(i) * kGoldenRatioConjugate) * 360.0, 360.0);
        for (double s : saturations) {
            for (double v : values) {
                SDL_Color candidate = hsv_to_rgb(hue, s, v);
                double min_distance = std::numeric_limits<double>::infinity();
                for (const SDL_Color& existing : used_colors) {
                    min_distance = std::min(min_distance, color_distance(candidate, existing));
                }
                if (min_distance > best_score) {
                    best_score = min_distance;
                    best = candidate;
                }
            }
        }
    }

    return clamp_color(best);
}

SDL_Color ensure(nlohmann::json& entry, std::vector<SDL_Color>& used_colors, bool* mutated) {
    if (mutated) {
        *mutated = false;
    }
    if (!entry.is_object()) {
        entry = nlohmann::json::object();
    }

    auto colors_match = [](const SDL_Color& lhs, const SDL_Color& rhs) {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
};

    if (auto existing = read(entry)) {
        SDL_Color color = clamp_color(*existing);
        auto already_present = std::find_if(used_colors.begin(), used_colors.end(), [&](const SDL_Color& other) {
            return colors_match(other, color);
        });
        if (already_present == used_colors.end()) {
            used_colors.push_back(color);
            return color;
        }
    }

    SDL_Color generated = generate_distinct_color(used_colors);
    write(entry, generated);
    used_colors.push_back(generated);
    if (mutated) {
        *mutated = true;
    }
    return generated;
}

std::vector<SDL_Color> collect(const nlohmann::json& entries) {
    std::vector<SDL_Color> result;
    if (!entries.is_object()) {
        return result;
    }
    result.reserve(entries.size());
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        if (auto color = read(*it)) {
            result.push_back(*color);
        }
    }
    return result;
}

}

