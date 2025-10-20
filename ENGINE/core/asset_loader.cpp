#include "asset_loader.hpp"
#include "asset_loader_internal.hpp"
#include <iostream>
#include <numeric>
#include <algorithm>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <limits>
#include <cstdint>
#include <SDL.h>
#include "asset/Asset.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_types.hpp"
#include "audio/audio_engine.hpp"
#include "map_generation/room.hpp"
#include "utils/area.hpp"
#include "utils/map_grid_settings.hpp"
#include "map_generation/generate_rooms.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include "world/chunk.hpp"
#include "world/grid.hpp"
#include <nlohmann/json.hpp>
#include "utils/loading_status_notifier.hpp"
#include "utils/log.hpp"
using json = nlohmann::json;

namespace {
        // Temporary guard to preserve the merging implementation without applying it.
        constexpr bool kEnableAssetMerging = false;
        Asset* findCenterAsset(const std::vector<Asset*>& group) {
                if (group.empty()) return nullptr;
                double avgX = std::accumulate(group.begin(), group.end(), 0.0,
                [](double sum, Asset* a) { return sum + a->pos.x; }) / group.size();
                double avgY = std::accumulate(group.begin(), group.end(), 0.0,
                [](double sum, Asset* a) { return sum + a->pos.y; }) / group.size();
                Asset* center = group.front();
                double bestDistSq = std::numeric_limits<double>::infinity();
                for (auto* a : group) {
                        double dx = a->pos.x - avgX;
                        double dy = a->pos.y - avgY;
                        double distSq = dx * dx + dy * dy;
                        if (distSq < bestDistSq) {
                                        bestDistSq = distSq;
                                        center = a;
                        }
        }
                return center;
        }
}

AssetLoader::~AssetLoader() = default;

AssetLoader::AssetLoader(const std::string& map_id,
                         const nlohmann::json& map_manifest,
                         SDL_Renderer* renderer,
                         std::string content_root,
                         devmode::core::ManifestStore* manifest_store,
                         AssetLibrary* shared_asset_library)
: map_id_(map_id),
map_path_(std::move(content_root)),
renderer_(renderer),
manifest_store_(manifest_store)
{
        log::info(std::string("[AssetLoader] Start for map '") + map_id_ + "'.");
        using_shared_asset_library_ = (shared_asset_library != nullptr);
        if (using_shared_asset_library_) {
                asset_library_ = shared_asset_library;
        } else {
                owned_asset_library_ = std::make_unique<AssetLibrary>();
                asset_library_ = owned_asset_library_.get();
        }

        const auto overall_begin = std::chrono::steady_clock::now();

        const auto map_begin = std::chrono::steady_clock::now();
        loading_status::notify("Loading map data");
        load_map_json(map_manifest);
        const auto map_end = std::chrono::steady_clock::now();
        log::debug(std::string("[AssetLoader] Map JSON parsed for '") + map_id_ + "'.");

        const nlohmann::json& audio_manifest = map_info_json_.contains("audio") ? map_info_json_.at("audio") : nlohmann::json::object();
        AudioEngine::instance().init(map_id_, audio_manifest, map_path_);

        const auto library_begin = std::chrono::steady_clock::now();
        loading_status::notify("Loading assets");
        const auto library_end = std::chrono::steady_clock::now();
        if (asset_library_) {
                log::info(std::string("[AssetLoader] Asset library ready with ") + std::to_string(asset_library_->all().size()) + " known assets");
        }

        const auto rooms_begin = std::chrono::steady_clock::now();
        loading_status::notify("Creating map");
        loadRooms();
        const auto rooms_end = std::chrono::steady_clock::now();
        log::info(std::string("[AssetLoader] Rooms created: ") + std::to_string(rooms_.size()));
        loading_status::notify("Loading assets");
    {
        const auto preload_begin = std::chrono::steady_clock::now();

        if (asset_library_ && !using_shared_asset_library_) {
                std::unordered_set<std::string> used;
                for (Room* room : rooms_) {
                        for (const auto& aup : room->assets) {
                                if (const Asset* a = aup.get()) {
                                        if (a->info) used.insert(a->info->name);
                                }
                        }
                }
                const std::size_t preload_count = used.size();
                log::info(std::string("[AssetLoader] Preloading animations for used assets (") + std::to_string(preload_count) + ")...");
                asset_library_->loadAnimationsFor(renderer_, used);

                const auto preload_end = std::chrono::steady_clock::now();
                const double preload_ms = std::chrono::duration_cast<std::chrono::milliseconds>(preload_end - preload_begin).count();
                log::info(std::string("[AssetLoader] Preloaded animations for ") + std::to_string(preload_count) +
                          " referenced assets in " + std::to_string(preload_ms) + "ms");
        } else {
                log::info("[AssetLoader] Using shared asset library cache; skipping per-map preload.");
        }
    }

    if (asset_library_) {
        if (renderer_) {
                asset_library_->ensureAllAnimationsLoaded(renderer_);
                log::info("[AssetLoader] Asset library warmup complete; animations cached in renderer.");
        } else {
                log::warn("[AssetLoader] Renderer unavailable; skipping asset library cache warmup.");
        }
    }
        loading_status::notify("Loading assets");
        log::info("[AssetLoader] Finalizing assets across rooms...");
        finalizeAssets();
        log::info("[AssetLoader] Asset finalization completed; all assets are ready.");

        const auto overall_end = std::chrono::steady_clock::now();
        const double map_ms = std::chrono::duration_cast<std::chrono::milliseconds>(map_end - map_begin).count();
        const double library_ms = std::chrono::duration_cast<std::chrono::milliseconds>(library_end - library_begin).count();
        const double rooms_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rooms_end - rooms_begin).count();
        const double total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_begin).count();
        log::info(std::string("[AssetLoader] Map metadata loaded in ") + std::to_string(map_ms) + "ms");
        log::info(std::string("[AssetLoader] Asset library ready in ") + std::to_string(library_ms) + "ms");
        log::info(std::string("[AssetLoader] Rooms built in ") + std::to_string(rooms_ms) + "ms");
        log::info(std::string("[AssetLoader] Initialization completed in ") + std::to_string(total_ms) + "ms");
        auto distant_boundary = collectDistantAssets(150, 400);
        for (auto* asset : distant_boundary) {
                asset->set_hidden(true);
        }
	std::vector<Asset*> link_candidates;
	for (Room* room : rooms_) {
		for (auto& asset_up : room->assets) {
            if (auto* asset = asset_up.get()) {
                if (asset->is_hidden()) {
                    continue;
                }
                if (asset->info && asset->info->type != asset_types::player && !asset->info->moving_asset) {
                    link_candidates.push_back(asset);
                }
            }
                }
        }
	auto neighbor_assets = group_neighboring_assets(link_candidates, 500, 500, "Child Linking");
	link_by_child(neighbor_assets);
}

void AssetLoader::link_by_child(const std::vector<std::vector<Asset*>>& groups) {
        if (!kEnableAssetMerging) {
                return;
        }
        size_t total_linked = 0;
	for (const auto& group : groups) {
		if (group.empty()) continue;
		Asset* center_asset = findCenterAsset(group);
		if (!center_asset) continue;
		for (auto* a : group) {
			if (a != center_asset) {
					center_asset->add_child(a);
					total_linked++;
			}
		}
		removeMergedAssets({group.begin(), group.end()}, center_asset);
	}
}

void AssetLoader::removeMergedAssets(const std::vector<Asset*>& to_remove, Asset* skip) {
        if (!kEnableAssetMerging) {
                return;
        }
        for (Asset* a : to_remove) {
                if (a == skip) continue;
                a->set_hidden(true);
                a->set_merged_from_neighbors(true);
        }
}

std::vector<std::vector<Asset*>> AssetLoader::group_neighboring_assets(
                                                                           const std::vector<Asset*>& assets,
                                                                           int tile_width,
                                                                           int tile_height,
                                                                           const std::string& group_type)
{
        std::unordered_map<std::uint64_t, std::vector<Asset*>> tile_map;
        auto make_tile_key = [&](int tx, int ty) -> std::uint64_t {
                // Pack tile coordinates into a single key using unsigned arithmetic to avoid UB.
                const auto ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(tx));
                const auto uy = static_cast<std::uint64_t>(static_cast<std::uint32_t>(ty));
                return (ux << 32) | uy;
        };
	for (Asset* a : assets) {
		if (!a) continue;
		int tx = a->pos.x / tile_width;
		int ty = a->pos.y / tile_height;
		if (a->pos.x < 0 && a->pos.x % tile_width != 0) tx -= 1;
		if (a->pos.y < 0 && a->pos.y % tile_height != 0) ty -= 1;
		tile_map[make_tile_key(tx, ty)].push_back(a);
	}
	std::vector<std::vector<Asset*>> groups;
	groups.reserve(tile_map.size());
	for (auto& [key, group] : tile_map) {
		groups.push_back(std::move(group));
	}
	size_t total_assets = 0;
	size_t largest_group = 0;
	for (const auto& g : groups) {
		total_assets += g.size();
		largest_group = std::max(largest_group, g.size());
	}
	double avg_group_size = groups.empty() ? 0.0 : (double)total_assets / groups.size();
	return groups;
}

std::vector<Asset*> AssetLoader::collectDistantAssets(int lock_threshold, int remove_threshold) {
        std::vector<Asset*> distant_assets;
        distant_assets.reserve(rooms_.size() * 4);
        auto allZones = getAllRoomAndTrailAreas();
        auto zoneCache = asset_loader_internal::build_zone_cache(allZones);

        std::unordered_map<std::string, Room*> room_lookup;
        room_lookup.reserve(rooms_.size());
        for (Room* room : rooms_) {
                if (room) {
                        room_lookup.emplace(room->room_name, room);
                }
        }

        const double remove_distance = static_cast<double>(remove_threshold);
        const double lock_distance = static_cast<double>(lock_threshold);
        std::vector<Asset*> locked_boundary_assets;
        locked_boundary_assets.reserve(rooms_.size());
        for (Room* room : rooms_) {
                for (auto& asset_up : room->assets) {
                        Asset* asset = asset_up.get();
            if (!asset->info || asset->info->type != asset_types::boundary) {
                    continue;
            }
                        SDL_Point asset_point{asset->pos.x, asset->pos.y};

                        Room* owning_room = room;
                        const std::string& owner_name = asset->owning_room_name();
                        if (!owner_name.empty()) {
                                auto it = room_lookup.find(owner_name);
                                if (it != room_lookup.end() && it->second) {
                                        owning_room = it->second;
                                }
                        }

                        if (owning_room && owning_room->room_area && owning_room->room_area->contains_point(asset_point)) {
                                continue;
                        }

                        if (asset_loader_internal::point_inside_any_zone(asset_point, zoneCache)) {
                                continue;
                        }
                        double minDistSq = asset_loader_internal::min_distance_sq_to_zones(asset_point, zoneCache, remove_threshold);
                        double minDist = std::sqrt(minDistSq);

                        const bool should_lock = minDist > lock_distance;
                        const bool should_remove = minDist >= remove_distance;

                        asset->static_frame = should_lock;
                        if (should_remove) {
                                distant_assets.push_back(asset);
                                continue;
                        }
                        if (should_lock) {
                                locked_boundary_assets.push_back(asset);
                        }
                }
        }
        if (kEnableAssetMerging && !locked_boundary_assets.empty()) {
                mergeLockedBoundaryAssets(locked_boundary_assets);
        }
        return distant_assets;
}

void AssetLoader::mergeLockedBoundaryAssets(const std::vector<Asset*>& locked_assets) {
        if (!kEnableAssetMerging) {
                return;
        }
        constexpr std::size_t group_size = 4;
        std::vector<Asset*> eligible;
        eligible.reserve(locked_assets.size());
        for (Asset* asset : locked_assets) {
                if (!asset) {
                        continue;
                }
                if (asset->is_hidden()) {
                        continue;
                }
                eligible.push_back(asset);
        }
        if (eligible.size() < group_size) {
                return;
        }

        struct Candidate {
                Asset* asset;
                double angle;
        };

        std::vector<Candidate> ordered;
        ordered.reserve(eligible.size());
        const double center_x = static_cast<double>(map_center_x_);
        const double center_y = static_cast<double>(map_center_y_);
        for (Asset* asset : eligible) {
                const double dx = static_cast<double>(asset->pos.x) - center_x;
                const double dy = static_cast<double>(asset->pos.y) - center_y;
                ordered.push_back({asset, std::atan2(dy, dx)});
        }

        std::sort(ordered.begin(), ordered.end(), [](const Candidate& lhs, const Candidate& rhs) {
                return lhs.angle < rhs.angle;
        });

        auto distance_between = [](const Asset* lhs, const Asset* rhs) {
                const double dx = static_cast<double>(lhs->pos.x) - static_cast<double>(rhs->pos.x);
                const double dy = static_cast<double>(lhs->pos.y) - static_cast<double>(rhs->pos.y);
                return std::sqrt(dx * dx + dy * dy);
        };

        std::vector<double> neighbor_distances;
        neighbor_distances.reserve(ordered.size() > 1 ? ordered.size() - 1 : 0);
        for (std::size_t i = 1; i < ordered.size(); ++i) {
                neighbor_distances.push_back(distance_between(ordered[i - 1].asset, ordered[i].asset));
        }

        double spacing_threshold = 0.0;
        if (!neighbor_distances.empty()) {
                double sum = std::accumulate(neighbor_distances.begin(), neighbor_distances.end(), 0.0);
                const double largest_gap = *std::max_element(neighbor_distances.begin(), neighbor_distances.end());
                if (neighbor_distances.size() > 1) {
                        sum -= largest_gap;
                        spacing_threshold = (sum / static_cast<double>(neighbor_distances.size() - 1)) * 1.5;
                } else {
                        spacing_threshold = neighbor_distances.front() * 1.5;
                }
        }

        if (spacing_threshold <= 0.0) {
                spacing_threshold = std::numeric_limits<double>::infinity();
        }

        std::vector<Asset*> current_group;
        current_group.reserve(group_size);
        Asset* previous_asset = nullptr;
        for (const Candidate& candidate : ordered) {
                if (previous_asset) {
                        const double gap = distance_between(previous_asset, candidate.asset);
                        if (gap > spacing_threshold) {
                                current_group.clear();
                        }
                }

                current_group.push_back(candidate.asset);
                if (current_group.size() == group_size) {
                        Asset* center_asset = findCenterAsset(current_group);
                        if (center_asset) {
                                for (Asset* asset : current_group) {
                                        if (asset == center_asset) {
                                                continue;
                                        }
                                        center_asset->add_child(asset);
                                }
                                removeMergedAssets(current_group, center_asset);
                        }
                        current_group.clear();
                }

                previous_asset = candidate.asset;
        }
}

void AssetLoader::loadRooms() {
        GenerateRooms generator(map_layers_,
                                map_center_x_,
                                map_center_y_,
                                map_id_,
                                map_info_json_,
                                manifest_store_);
        nlohmann::json empty_boundary = nlohmann::json::object();
        nlohmann::json empty_rooms    = nlohmann::json::object();
        nlohmann::json empty_trails   = nlohmann::json::object();
        nlohmann::json empty_assets   = nlohmann::json::object();
        map_grid_settings_ = MapGridSettings::from_json(map_info_json_.contains("map_grid_settings") ? &map_info_json_["map_grid_settings"] : nullptr);
        MapGridSettings grid_settings = map_grid_settings_;
        auto room_ptrs = generator.build( asset_library_,
                                          map_radius_,
                                          layer_radii_,
                                          map_boundary_data_ ? *map_boundary_data_ : empty_boundary,
                                          rooms_data_        ? *rooms_data_        : empty_rooms,
                                          trails_data_       ? *trails_data_       : empty_trails,
                                          map_assets_data_   ? *map_assets_data_   : empty_assets,
                                          grid_settings);
        for (auto& up : room_ptrs) {
                rooms_.push_back(up.get());
                all_rooms_.push_back(std::move(up));
	}
}

void AssetLoader::finalizeAssets() {
        std::size_t room_index         = 0;
        std::size_t total_assets       = 0;
        std::size_t finalized_assets   = 0;
        std::size_t skipped_assets     = 0;

        for (Room* room : rooms_) {
                if (!room) {
                        ++room_index;
                        continue;
                }

                const std::size_t room_total = room->assets.size();
                std::size_t       room_finalized = 0;
                std::size_t       room_skipped   = 0;

                for (auto& asset_up : room->assets) {
                        ++total_assets;
                        Asset* a = asset_up.get();
                        if (!a || !a->info) {
                                ++skipped_assets;
                                ++room_skipped;
                                continue;
                        }

                        const std::string name = a->info->name;
                        try {
                                asset_up->finalize_setup();
                        } catch (const std::exception& ex) {
                                log::error(std::string("[AssetLoader] finalizeAssets: exception during finalize_setup for '") + name + "': " + ex.what());
                                throw;
                        } catch (...) {
                                log::error(std::string("[AssetLoader] finalizeAssets: unknown exception during finalize_setup for '") + name + "'");
                                throw;
                        }

                        ++finalized_assets;
                        ++room_finalized;
                }

                if (room_total > 0) {
                        std::string msg = std::string("[AssetLoader] finalizeAssets: room=") + std::to_string(room_index) +
                                          " finalized " + std::to_string(room_finalized) + "/" + std::to_string(room_total);
                        if (room_skipped > 0) {
                                msg += std::string(" (skipped ") + std::to_string(room_skipped) + ")";
                        }
                        log::debug(msg);
                }

                ++room_index;
        }

        {
                std::string msg = std::string("[AssetLoader] finalizeAssets complete: ") + std::to_string(finalized_assets) +
                                  "/" + std::to_string(total_assets) + " assets ready";
                if (skipped_assets > 0) {
                        msg += std::string(" (") + std::to_string(skipped_assets) + " skipped)";
                }
                log::info(msg);
        }
}

std::unique_ptr<PrecomputedLightMap> AssetLoader::take_precomputed_light_map() {
        return std::move(precomputed_light_map_);
}

std::vector<std::unique_ptr<Asset>> AssetLoader::extract_all_assets() {
        std::vector<std::unique_ptr<Asset>> out;
        out.reserve(rooms_.size() * 4);
        for (Room* room : rooms_) {
                if (!room) continue;
                auto& assets = room->assets;
                for (auto it = assets.begin(); it != assets.end();) {
                        std::unique_ptr<Asset>& aup = *it;
                        Asset* asset = aup.get();
                        if (!asset) {
                                it = assets.erase(it);
                                continue;
                        }
                        if (asset->is_hidden()) {
                                ++it;
                                continue;
                        }
                        out.push_back(std::move(aup));
                        it = assets.erase(it);
                }
        }
        return out;
}

void AssetLoader::createAssets(world::Grid& grid) {
        grid.set_chunk_resolution(std::max(0, map_grid_settings_.r_chunk));
        spawned_assets_ = extract_all_assets();
        log::info(std::string("[AssetLoader] Extracted ") + std::to_string(spawned_assets_.size()) + " visible assets from rooms");
        for (const auto& asset_up : spawned_assets_) {
                Asset* asset = asset_up.get();
                if (!asset) {
                        continue;
                }
                grid.register_asset(asset);
        }
        instantiate_map_chunks(grid);
        log::info("[AssetLoader] Beginning full-map light map texture creation...");
        precompute_light_map(grid);
}

std::vector<std::unique_ptr<Asset>> AssetLoader::take_spawned_assets() {
        return std::move(spawned_assets_);
}

std::vector<const Area*> AssetLoader::getAllRoomAndTrailAreas() const {
        std::vector<const Area*> areas;
        areas.reserve(rooms_.size());
        for (const Room* r : rooms_) {
                if (r && r->room_area) {
                        areas.push_back(r->room_area.get());
                }
        }
        return areas;
}

void AssetLoader::load_map_json(const nlohmann::json& map_manifest) {
        map_info_json_ = map_manifest;
        if (!map_info_json_.is_object()) {
                map_info_json_ = nlohmann::json::object();
        }

        ensure_map_grid_settings(map_info_json_);

        map_assets_data_   = &map_info_json_["map_assets_data"];
        if (!map_assets_data_->is_object()) *map_assets_data_ = nlohmann::json::object();
        map_boundary_data_ = &map_info_json_["map_boundary_data"];
        if (!map_boundary_data_->is_object()) *map_boundary_data_ = nlohmann::json::object();
        rooms_data_        = &map_info_json_["rooms_data"];
        if (!rooms_data_->is_object()) *rooms_data_ = nlohmann::json::object();
        trails_data_       = &map_info_json_["trails_data"];
        if (!trails_data_->is_object()) *trails_data_ = nlohmann::json::object();

        const auto layers_it = map_info_json_.find("map_layers");
        map_layers::LayerRadiiResult radii_result;
        const nlohmann::json* rooms_data_ptr = rooms_data_;
        if (layers_it != map_info_json_.end()) {
                radii_result = map_layers::compute_layer_radii(*layers_it, rooms_data_ptr);
        }

        map_radius_   = radii_result.map_radius;
        map_center_x_ = map_center_y_ = map_radius_;
        layer_radii_  = radii_result.layer_radii;
        map_layers_.clear();

        if (layers_it != map_info_json_.end() && layers_it->is_array()) {
                map_layers_.reserve(layers_it->size());
                size_t index = 0;
                for (const auto& layer_entry : *layers_it) {
                        LayerSpec spec;
                        spec.level = static_cast<int>(index);
                        spec.max_rooms = 0;

                        if (layer_entry.is_object()) {
                                spec.level     = layer_entry.value("level", spec.level);
                                spec.max_rooms = layer_entry.value("max_rooms", 0);

                                auto rooms_array_it = layer_entry.find("rooms");
                                if (rooms_array_it != layer_entry.end() && rooms_array_it->is_array()) {
                                        for (const auto& room_entry : *rooms_array_it) {
                                                if (!room_entry.is_object()) {
                                                        continue;
                                                }
                                                RoomSpec rs;
                                                rs.name          = room_entry.value("name", "unnamed");
                                                rs.max_instances = room_entry.value("max_instances", 1);

                                                auto required_it = room_entry.find("required_children");
                                                if (required_it != room_entry.end() && required_it->is_array()) {
                                                        for (const auto& child : *required_it) {
                                                                if (child.is_string()) {
                                                                        rs.required_children.push_back(child.get<std::string>());
                                                                } else {
                                                                        log::warn(std::string("[AssetLoader] Room '") + rs.name + "' has non-string entry in 'required_children'; skipping.");
                                                                }
                                                        }
                                                }

                                                spec.rooms.push_back(std::move(rs));
                                        }
                                }
                        }

                        map_layers_.push_back(std::move(spec));
                        ++index;
                }
        }
}

void AssetLoader::precompute_light_map(world::Grid& grid) {
        precomputed_light_map_.reset();
        for (world::Chunk* chunk : map_chunks_) {
                if (!chunk) {
                        continue;
                }
                bake_chunk_lighting(grid, *chunk);
        }
}

namespace {
int floor_div(int value, int step) {
        if (step == 0) {
                return 0;
        }
        const int quotient = value / step;
        const int remainder = value % step;
        if (remainder == 0) {
                return quotient;
        }
        if ((remainder < 0) != (step < 0)) {
                return quotient - 1;
        }
        return quotient;
}
} // namespace

void AssetLoader::instantiate_map_chunks(world::Grid& grid) {
        map_chunks_.clear();
        const int r_chunk = std::max(0, grid.chunk_resolution());
        const int step    = 1 << r_chunk;
        if (step <= 0) {
                return;
        }

        SDL_Point origin = grid.origin();

        const auto chunk_key = [](int i, int j) {
                const auto hi = static_cast<std::uint32_t>(i);
                const auto lo = static_cast<std::uint32_t>(j);
                return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
        };

        std::unordered_set<std::uint64_t> visited;
        visited.reserve(spawned_assets_.size() * 4);

        auto register_chunk = [&](int i, int j) {
                const std::uint64_t key = chunk_key(i, j);
                if (!visited.insert(key).second) {
                        return;
                }
                world::Chunk& chunk = grid.get_or_create_chunk_ij(i, j);
                chunk.base_brightness     = 0.0f;
                chunk.brightness_strength = 1.0f;
                chunk.opacity_strength    = 1.0f;
                chunk.scale_strength      = 1.0f;
                chunk.offset_x            = 0;
                chunk.offset_y            = 0;
                chunk.shadow              = {};
                chunk.lighting_dirty      = true;
                chunk.has_dynamic_overlay = false;
                map_chunks_.push_back(&chunk);
        };

        auto expand_rect = [&](const SDL_Rect& rect) {
                SDL_Rect expanded = rect;
                expanded.x -= step;
                expanded.y -= step;
                expanded.w += step * 2;
                expanded.h += step * 2;
                return expanded;
        };

        bool discovered_static_light = false;

        for (const auto& asset_up : spawned_assets_) {
                const Asset* asset = asset_up.get();
                if (!asset || !asset->info) {
                        continue;
                }
                if (asset->info->light_sources.empty() || asset->info->moving_asset) {
                        continue;
                }

                for (const auto& light : asset->info->light_sources) {
                        SDL_Texture* tex = light.texture;
                        if (!tex) {
                                continue;
                        }

                        int src_w = light.cached_w > 0 ? light.cached_w : 0;
                        int src_h = light.cached_h > 0 ? light.cached_h : 0;
                        if (src_w <= 0 || src_h <= 0) {
                                SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
                        }
                        if (src_w <= 0 || src_h <= 0) {
                                continue;
                        }

                        discovered_static_light = true;

                        const int draw_w = std::max(1, src_w);
                        const int draw_h = std::max(1, src_h);
                        SDL_Rect world_dst{
                                asset->pos.x + light.offset_x - draw_w / 2,
                                asset->pos.y + light.offset_y - draw_h / 2,
                                draw_w,
                                draw_h};

                        SDL_Rect expanded = expand_rect(world_dst);

                        const int i_min = floor_div(expanded.x - origin.x, step);
                        const int j_min = floor_div(expanded.y - origin.y, step);
                        const int i_max = floor_div(expanded.x + expanded.w - 1 - origin.x, step);
                        const int j_max = floor_div(expanded.y + expanded.h - 1 - origin.y, step);

                        for (int j = j_min; j <= j_max; ++j) {
                                for (int i = i_min; i <= i_max; ++i) {
                                        register_chunk(i, j);
                                }
                        }
                }
        }

        if (!discovered_static_light) {
                // Ensure at least one chunk exists so downstream systems have a stable baseline.
                const int center_x = static_cast<int>(std::llround(map_center_x_));
                const int center_y = static_cast<int>(std::llround(map_center_y_));
                const int i = floor_div(center_x - origin.x, step);
                const int j = floor_div(center_y - origin.y, step);
                register_chunk(i, j);
        }

        log::info(std::string("[AssetLoader] Prepared ") + std::to_string(map_chunks_.size()) +
                  " static-light chunk(s) for baking");
}

void AssetLoader::bake_chunk_lighting(world::Grid&, world::Chunk& chunk) {
        chunk.base_brightness     = 0.0f;
        chunk.brightness_strength = 1.0f;
        chunk.opacity_strength    = 1.0f;
        chunk.scale_strength      = 1.0f;
        chunk.offset_x            = 0;
        chunk.offset_y            = 0;
        chunk.shadow              = {};
        chunk.has_dynamic_overlay = false;
        chunk.lighting_dirty      = true;

        if (chunk.static_light_map) {
                SDL_DestroyTexture(chunk.static_light_map);
                chunk.static_light_map = nullptr;
        }

        if (!renderer_) {
                return;
        }

        const int width  = std::max(1, chunk.world_bounds.w);
        const int height = std::max(1, chunk.world_bounds.h);
        SDL_Texture* texture = SDL_CreateTexture(renderer_,
                                                 SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET,
                                                 width,
                                                 height);
        if (!texture) {
                return;
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeBest);
#endif

        SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, texture);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);

#if SDL_VERSION_ATLEAST(2, 0, 6)
        // Match the runtime light-map blending so the cached brightness reflects actual static lighting.
        const SDL_BlendMode erase_alpha_blend = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE,
            SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD);
#else
        const SDL_BlendMode erase_alpha_blend = SDL_BLENDMODE_ADD;
#endif

        for (const auto& asset_up : spawned_assets_) {
                const Asset* asset = asset_up.get();
                if (!asset || !asset->info) {
                        continue;
                }
                if (asset->info->light_sources.empty() || asset->info->moving_asset) {
                        continue;
                }
                for (const auto& light : asset->info->light_sources) {
                        SDL_Texture* tex = light.texture;
                        if (!tex) {
                                continue;
                        }
                        int src_w = light.cached_w > 0 ? light.cached_w : 0;
                        int src_h = light.cached_h > 0 ? light.cached_h : 0;
                        if (src_w <= 0 || src_h <= 0) {
                                SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
                        }
                        if (src_w <= 0 || src_h <= 0) {
                                continue;
                        }

                        // Lights should appear on the baked map at the exact size authored for them.
                        const int draw_w = std::max(1, src_w);
                        const int draw_h = std::max(1, src_h);
                        SDL_Point world_center{asset->pos.x + light.offset_x, asset->pos.y + light.offset_y};
                        SDL_Rect world_dst{world_center.x - draw_w / 2,
                                           world_center.y - draw_h / 2,
                                           draw_w,
                                           draw_h};

                        SDL_Rect intersection{};
                        if (!SDL_IntersectRect(&world_dst, &chunk.world_bounds, &intersection)) {
                                continue;
                        }

                        Uint8 save_r = 255, save_g = 255, save_b = 255, save_a = 255;
                        SDL_BlendMode save_bm = SDL_BLENDMODE_BLEND;
                        SDL_GetTextureColorMod(tex, &save_r, &save_g, &save_b);
                        SDL_GetTextureAlphaMod(tex, &save_a);
                        SDL_GetTextureBlendMode(tex, &save_bm);

                        SDL_SetTextureBlendMode(tex, erase_alpha_blend);
                        SDL_SetTextureColorMod(tex, 255, 255, 255);
                        SDL_SetTextureAlphaMod(tex, 255);
                        SDL_Rect local_dst = world_dst;
                        local_dst.x -= chunk.world_bounds.x;
                        local_dst.y -= chunk.world_bounds.y;
                        SDL_RenderCopy(renderer_, tex, nullptr, &local_dst);

                        SDL_SetTextureBlendMode(tex, save_bm);
                        SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                        SDL_SetTextureAlphaMod(tex, save_a);
                }
        }

        SDL_SetRenderTarget(renderer_, previous_target);

        chunk.base_brightness = compute_chunk_average_brightness(texture);
        chunk.base_brightness = std::clamp(chunk.base_brightness, 0.0f, 1.0f);

        if (texture) {
                SDL_DestroyTexture(texture);
        }
}

float AssetLoader::compute_chunk_average_brightness(SDL_Texture* texture) const {
        if (!renderer_ || !texture) {
                return 0.0f;
        }

        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
                return 0.0f;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(tex_w) * static_cast<std::size_t>(tex_h);
        std::vector<std::uint32_t> pixels(pixel_count);
        if (pixels.empty()) {
                return 0.0f;
        }

        SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, texture);
        const int pitch = tex_w * static_cast<int>(sizeof(std::uint32_t));
        if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_RGBA8888, pixels.data(), pitch) != 0) {
                SDL_SetRenderTarget(renderer_, previous_target);
                return 0.0f;
        }
        SDL_SetRenderTarget(renderer_, previous_target);

        std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> format(
            SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888), &SDL_FreeFormat);
        if (!format) {
                return 0.0f;
        }

        double accum = 0.0;
        for (std::uint32_t pixel : pixels) {
                Uint8 a = 255;
                SDL_GetRGBA(pixel, format.get(), nullptr, nullptr, nullptr, &a);
                const double transparency = 1.0 - static_cast<double>(a) / 255.0;
                accum += transparency;
        }

        if (pixel_count == 0) {
                return 0.0f;
        }

        const double average = accum / static_cast<double>(pixel_count);
        return static_cast<float>(std::clamp(average, 0.0, 1.0));
}
