#pragma once

#include <random>
#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

class Area;

struct MapGridSettings {
    int resolution = 0;
    int jitter = 0;
    int r_chunk = 0;

    static MapGridSettings defaults();
    static MapGridSettings from_json(const nlohmann::json* obj);

    void clamp();
    void apply_to_json(nlohmann::json& obj) const;

    int spacing() const;
    int chunk_size() const;
};

void ensure_map_grid_settings(nlohmann::json& map_info);
SDL_Point apply_map_grid_jitter(const MapGridSettings& settings, SDL_Point base, std::mt19937& rng, const Area& area);
