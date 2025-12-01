#include "utils/grid_occupancy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibble::grid {

namespace {
constexpr int kMaxSearchRadius = 4096;
}

Occupancy::Occupancy(const Area& area, int resolution, Grid& grid, bool allow_partial_overlap) {
    rebuild(area, resolution, grid, allow_partial_overlap);
}

void Occupancy::rebuild(const Area& area, int resolution, Grid& grid, bool allow_partial_overlap) {
    vertices_.clear();
    lookup_.clear();
    grid_ = &grid;
    resolution_ = clamp_resolution(resolution);
    free_count_ = 0;
    allow_partial_overlap_ = allow_partial_overlap;
    populate_vertices(area, resolution_, grid);
}

void Occupancy::populate_vertices(const Area& area, int resolution, Grid& grid) {
    if (!grid_) {
        grid_ = &grid;
    }
    if (area.get_points().empty()) {
        return;
    }
    auto [minx, miny, maxx, maxy] = area.get_bounds();
    SDL_Point min_world{minx, miny};
    SDL_Point max_world{maxx, maxy};

    SDL_Point min_index = grid.world_to_index(min_world, resolution);
    SDL_Point max_index = grid.world_to_index(SDL_Point{max_world.x, max_world.y}, resolution);
    if (min_index.x > max_index.x) std::swap(min_index.x, max_index.x);
    if (min_index.y > max_index.y) std::swap(min_index.y, max_index.y);

    min_index_ = min_index;
    max_index_ = max_index;

    for (int j = min_index.y; j <= max_index.y; ++j) {
        for (int i = min_index.x; i <= max_index.x; ++i) {
            SDL_Point world = grid.index_to_world(i, j, resolution);
            bool inside = area.contains_point(world);
            bool overlaps = false;
            if (!inside && allow_partial_overlap_) {
                const int cell_size = delta(resolution);
                const int cell_min_x = world.x;
                const int cell_min_y = world.y;
                const int cell_max_x = cell_min_x + cell_size;
                const int cell_max_y = cell_min_y + cell_size;
                overlaps = !(cell_max_x < minx || maxx < cell_min_x || cell_max_y < miny || maxy < cell_min_y);
            }
            if (!inside && !overlaps) {
                continue;
            }
            Vertex vertex;
            vertex.index = SDL_Point{i, j};
            vertex.world = world;
            vertex.occupied = false;
            const Key key = make_key(vertex.index);
            lookup_[key] = vertices_.size();
            vertices_.push_back(vertex);
        }
    }
    free_count_ = static_cast<int>(vertices_.size());
}

Occupancy::Vertex* Occupancy::nearest_vertex(SDL_Point world) {
    if (vertices_.empty() || !grid_) {
        return nullptr;
    }
    SDL_Point origin_index = grid_->world_to_index(world, resolution_);

    auto get_vertex = [&](SDL_Point index) -> Vertex* {
        const auto it = lookup_.find(make_key(index));
        if (it == lookup_.end()) {
            return nullptr;
        }
        return &vertices_[it->second];
};

    if (Vertex* v = get_vertex(origin_index); v && !v->occupied) {
        return v;
    }

    const int max_dx = std::max(std::abs(origin_index.x - min_index_.x), std::abs(origin_index.x - max_index_.x));
    const int max_dy = std::max(std::abs(origin_index.y - min_index_.y), std::abs(origin_index.y - max_index_.y));
    const int limit = std::min(kMaxSearchRadius, std::max(max_dx, max_dy));

    for (int radius = 1; radius <= limit; ++radius) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = origin_index.x + dx;
            const int y1 = origin_index.y - radius;
            const int y2 = origin_index.y + radius;
            if (Vertex* v1 = get_vertex(SDL_Point{x, y1}); v1 && !v1->occupied) {
                return v1;
            }
            if (Vertex* v2 = get_vertex(SDL_Point{x, y2}); v2 && !v2->occupied) {
                return v2;
            }
        }
        for (int dy = -radius + 1; dy <= radius - 1; ++dy) {
            const int y = origin_index.y + dy;
            const int x1 = origin_index.x - radius;
            const int x2 = origin_index.x + radius;
            if (Vertex* v1 = get_vertex(SDL_Point{x1, y}); v1 && !v1->occupied) {
                return v1;
            }
            if (Vertex* v2 = get_vertex(SDL_Point{x2, y}); v2 && !v2->occupied) {
                return v2;
            }
        }
    }
    return nullptr;
}

Occupancy::Vertex* Occupancy::random_vertex_in_area(const Area& area, std::mt19937& rng) {
    if (vertices_.empty()) {
        return nullptr;
    }
    std::vector<std::size_t> candidates;
    candidates.reserve(vertices_.size());
    for (std::size_t idx = 0; idx < vertices_.size(); ++idx) {
        const auto& vertex = vertices_[idx];
        if (vertex.occupied) {
            continue;
        }
        if (!area.contains_point(vertex.world)) {
            continue;
        }
        candidates.push_back(idx);
    }
    if (candidates.empty()) {
        return nullptr;
    }
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    return &vertices_[candidates[pick(rng)]];
}

std::vector<Occupancy::Vertex*> Occupancy::vertices_in_area(const Area& area) {
    std::vector<Vertex*> result;
    result.reserve(vertices_.size());
    for (auto& vertex : vertices_) {
        if (area.contains_point(vertex.world)) {
            result.push_back(&vertex);
        }
    }
    return result;
}

Occupancy::Vertex* Occupancy::vertex_at_world(SDL_Point world) {
    if (!grid_) {
        return nullptr;
    }
    SDL_Point index = grid_->world_to_index(world, resolution_);
    return vertex_at_index(index);
}

Occupancy::Vertex* Occupancy::vertex_at_index(SDL_Point index) {
    const auto it = lookup_.find(make_key(index));
    if (it == lookup_.end()) {
        return nullptr;
    }
    return &vertices_[it->second];
}

void Occupancy::set_occupied(Vertex* vertex, bool occupied) {
    if (!vertex) {
        return;
    }
    if (vertex->occupied == occupied) {
        return;
    }
    vertex->occupied = occupied;
    free_count_ += occupied ? -1 : 1;
    if (free_count_ < 0) {
        free_count_ = 0;
    }
}

void Occupancy::set_occupied_at(SDL_Point world, bool occupied) {
    if (Vertex* vertex = vertex_at_world(world)) {
        set_occupied(vertex, occupied);
    }
}

bool Occupancy::cell_overlaps(const Area& area, SDL_Point world) const {
    if (!grid_) {
        return area.contains_point(world);
    }
    if (!allow_partial_overlap_) {
        return area.contains_point(world);
    }
    if (area.get_points().empty()) {
        return false;
    }
    SDL_Point index = grid_->world_to_index(world, resolution_);
    SDL_Point cell_min = grid_->index_to_world(index, resolution_);
    const int cell_size = delta(resolution_);
    const int cell_min_x = cell_min.x;
    const int cell_min_y = cell_min.y;
    const int cell_max_x = cell_min_x + cell_size;
    const int cell_max_y = cell_min_y + cell_size;
    auto [minx, miny, maxx, maxy] = area.get_bounds();
    return !(cell_max_x < minx || maxx < cell_min_x || cell_max_y < miny || maxy < cell_min_y);
}

Occupancy::Key Occupancy::make_key(SDL_Point index) {
    return (static_cast<Key>(static_cast<std::uint32_t>(index.x)) << 32) |
           static_cast<std::uint32_t>(index.y);
}

}

