#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "utils/grid.hpp"
#include "utils/area.hpp"

namespace vibble::grid {

class Occupancy {
public:
    struct Vertex {
        SDL_Point index{0, 0};
        SDL_Point world{0, 0};
        bool occupied{false};
};

    Occupancy() = default;
    Occupancy(const Area& area, int resolution, Grid& grid, bool allow_partial_overlap = false);

    void rebuild(const Area& area, int resolution, Grid& grid, bool allow_partial_overlap = false);

    Vertex* nearest_vertex(SDL_Point world);
    Vertex* random_vertex_in_area(const Area& area, std::mt19937& rng);
    std::vector<Vertex*> vertices_in_area(const Area& area);
    Vertex* vertex_at_world(SDL_Point world);
    Vertex* vertex_at_index(SDL_Point index);
    void set_occupied(Vertex* vertex, bool occupied = true);
    void set_occupied_at(SDL_Point world, bool occupied = true);
    bool cell_overlaps(const Area& area, SDL_Point world) const;
    int free_count() const { return free_count_; }
    int resolution() const { return resolution_; }

private:
    using Key = std::uint64_t;

    static Key make_key(SDL_Point index);

    void populate_vertices(const Area& area, int resolution, Grid& grid);

    bool allow_partial_overlap_ = false;
    std::vector<Vertex> vertices_;
    std::unordered_map<Key, std::size_t> lookup_;
    Grid* grid_ = nullptr;
    int resolution_ = 0;
    int free_count_ = 0;
    SDL_Point min_index_{0, 0};
    SDL_Point max_index_{0, 0};
};

}

