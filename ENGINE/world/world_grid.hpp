#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "utils/grid.hpp"
#include "world/chunk_manager.hpp"
#include "world/grid_point.hpp"

class Asset;
class WarpedScreenGrid;

namespace world {

class WorldGrid {
public:
    WorldGrid() : WorldGrid(SDL_Point{0, 0}, 0) {}
    WorldGrid(SDL_Point origin, int r_chunk);

    void set_chunk_resolution(int r);
    void set_grid_resolution(int r);
    int  grid_resolution() const;
    int  chunk_resolution() const { return r_chunk_; }
    SDL_Point origin() const { return origin_; }
    void set_origin(SDL_Point origin);

    Asset* create_asset_at_point(std::unique_ptr<Asset> a);
    Asset* create_asset_at_point(Asset* a);
    Asset* register_asset(std::unique_ptr<Asset> a);
    Asset* register_asset(Asset* a);
    Chunk* ensure_chunk_from_world(SDL_Point world_px);
    Chunk* chunk_from_world(SDL_Point world_px) const;
    Chunk* get_or_create_chunk_ij(int i, int j);
    std::vector<Chunk*> all_chunks() const {
        const auto& storage = chunks_.storage();
        std::vector<Chunk*> result;
        result.reserve(storage.size());
        for (const auto& chunk : storage) {
            if (chunk) {
                result.push_back(chunk.get());
            }
        }
        return result;
    }

    Asset* move_asset_to_point(Asset* a, SDL_Point old_pos, SDL_Point new_pos);
    Asset* move_asset(Asset* a, SDL_Point old_pos, SDL_Point new_pos);

    Asset* remove_asset(Asset* a);
    void unregister_asset(Asset* a);
    void rebuild_chunks();

    void update_active_chunks(const SDL_Rect& camera_world, int margin_px);

    const std::vector<Chunk*>& active_chunks() const;

    ChunkManager& chunks();
    const ChunkManager& chunks() const;
    std::vector<Asset*> all_assets() const;

    SDL_Point grid_index_from_world(SDL_Point world) const;
    GridId point_id_from_world(SDL_Point world) const;
    const std::unordered_map<GridId, GridPoint>& points() const { return points_; }
    std::unordered_map<GridId, GridPoint>& points() { return points_; }
    GridPoint* point_for_id(GridId id);
    const GridPoint* point_for_id(GridId id) const;
    GridPoint* point_for_asset(const Asset* asset);
    const GridPoint* point_for_asset(const Asset* asset) const;

private:
    void remove_from_chunk(Asset* a, Chunk* c);
    void invalidate_active_cache();
    GridId make_point_id(int i, int j) const;
    void remove_asset_from_point(Asset* a, GridPoint& point);
    GridPoint& ensure_point(SDL_Point grid_index);
    void bind_asset_to_point(Asset* a, GridPoint& point, SDL_Point world_pos, Chunk* owning_chunk, SDL_Point chunk_index);
    void prune_empty_points();
    std::unique_ptr<Asset> extract_from_point(Asset* a, GridPoint& point);

    SDL_Point origin_{0, 0};
    int       r_chunk_ = 0;
    int       grid_resolution_ = 0;

    ChunkManager chunks_;
    std::unordered_map<Asset*, Chunk*> residency_;

    bool     has_cached_camera_rect_ = false;
    SDL_Rect last_expanded_camera_{0, 0, 0, 0};
    int      last_margin_px_         = -1;
    int      last_chunk_resolution_  = -1;

    std::unordered_map<GridId, GridPoint> points_;
    std::unordered_map<Asset*, GridId> asset_to_point_;
};

}
