#pragma once

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "utils/area.hpp"
#include "asset/asset_info.hpp"

struct SpawnCandidate {
        std::string name;
        std::string display_name;
        int weight = 0;
        std::shared_ptr<AssetInfo> info;
        bool is_null = false;
};

struct SpawnInfo {
        std::string name;
        std::string position;
        std::string spawn_id;
        SDL_Point exact_offset{0, 0};
        int exact_origin_w = 0;
        int exact_origin_h = 0;
        int quantity = 0;
        int grid_spacing = 0;
        int jitter = 0;
        int empty_grid_spaces = 0;
        SDL_Point exact_point{ -1, -1 };
        int border_shift = 0;
        int sector_center = 0;
        int sector_range = 0;
        SDL_Point perimeter_offset{0, 0};
        int percent_x_min = 0;
        int percent_x_max = 0;
        int percent_y_min = 0;
        int percent_y_max = 0;
        bool check_overlap = false;
        bool check_min_spacing = false;
        std::vector<SpawnCandidate> candidates;

        bool has_candidates() const { return !candidates.empty(); }

        const SpawnCandidate* select_candidate(std::mt19937& rng) const {
                if (candidates.empty()) return nullptr;
                std::vector<int> weights;
                weights.reserve(candidates.size());
                bool has_positive = false;
                for (const auto& cand : candidates) {
                        int w = cand.weight;
                        if (w < 0) w = 0;
                        if (w > 0) has_positive = true;
                        weights.push_back(w);
                }
                if (!has_positive) {
                        std::fill(weights.begin(), weights.end(), 1);
                }
                std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
                return &candidates[dist(rng)];
        }
};