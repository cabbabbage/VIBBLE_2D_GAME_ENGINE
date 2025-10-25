#pragma once

#include <optional>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

namespace utils::display_color {

std::optional<SDL_Color> read(const nlohmann::json& entry);
void write(nlohmann::json& entry, SDL_Color color);
SDL_Color ensure(nlohmann::json& entry, std::vector<SDL_Color>& used_colors, bool* mutated = nullptr);
std::vector<SDL_Color> collect(const nlohmann::json& entries);
SDL_Color generate_distinct_color(const std::vector<SDL_Color>& used_colors);

double color_distance(SDL_Color a, SDL_Color b);
SDL_Color hsv_to_rgb(double hue_degrees, double saturation, double value);

}

