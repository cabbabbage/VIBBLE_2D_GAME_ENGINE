#pragma once

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <SDL.h>
#include "utils/area.hpp"
#include "asset/asset_info.hpp"

struct SpawnCandidate {
    std::string name;
    std::string display_name;
    double weight = 0.0;
    std::shared_ptr<AssetInfo> info;
    bool is_null = false;
};

struct SpawnInfo {

    std::string name;
    std::string position;
    std::string spawn_id;
    int priority = 0;
    int quantity = 0;
    bool check_min_spacing = false;
    int grid_resolution = 0;

    std::string link_area_name;

    SDL_Point exact_offset{0, 0};
    int exact_origin_w = 0;
    int exact_origin_h = 0;
    SDL_Point exact_point{-1, -1};

    int perimeter_radius = 0;

    int edge_inset_percent = 100;

    bool adjust_geometry_to_room = false;

    std::vector<SpawnCandidate> candidates;

    bool has_candidates() const { return !candidates.empty(); }

    const SpawnCandidate* select_candidate(std::mt19937& rng) const {
        if (candidates.empty()) return nullptr;
        std::vector<double> weights;
        weights.reserve(candidates.size());
        double total_positive = 0.0;
        for (const auto& cand : candidates) {
            double w = cand.weight;
            if (w < 0.0) w = 0.0;
            if (w > 0.0) total_positive += w;
            weights.push_back(w);
        }
        if (total_positive <= 0.0) {
            std::fill(weights.begin(), weights.end(), 1.0);
        }
        std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
        return &candidates[dist(rng)];
    }
};
