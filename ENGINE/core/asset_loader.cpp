#include "asset_loader.hpp"
#include "asset_loader_internal.hpp"
#include <fstream>
#include <iostream>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <SDL.h>
#include "asset/Asset.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_types.hpp"
#include "audio/audio_engine.hpp"
#include "map_generation/room.hpp"
#include "utils/area.hpp"
#include "map_generation/generate_rooms.hpp"
#include "map_generation/map_layers_geometry.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace {
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

AssetLoader::AssetLoader(const std::string& map_dir, SDL_Renderer* renderer)
: map_path_(map_dir),
renderer_(renderer)
{
        const auto overall_begin = std::chrono::steady_clock::now();

        const auto map_begin = std::chrono::steady_clock::now();
        load_map_json();
        const auto map_end = std::chrono::steady_clock::now();

        AudioEngine::instance().init(map_path_);

        const auto library_begin = std::chrono::steady_clock::now();
        asset_library_ = std::make_unique<AssetLibrary>();
        const auto library_end = std::chrono::steady_clock::now();

        const auto rooms_begin = std::chrono::steady_clock::now();
        loadRooms();
        const auto rooms_end = std::chrono::steady_clock::now();
    {
        const auto preload_begin = std::chrono::steady_clock::now();

        std::unordered_set<std::string> used;
        for (Room* room : rooms_) {
            for (const auto& aup : room->assets) {
                if (const Asset* a = aup.get()) {
                    if (a->info) used.insert(a->info->name);
                }
            }
        }
        const std::size_t preload_count = used.size();
        asset_library_->loadAnimationsFor(renderer_, used);

        const auto preload_end = std::chrono::steady_clock::now();
        const double preload_ms = std::chrono::duration_cast<std::chrono::milliseconds>(preload_end - preload_begin).count();
        std::cout << "[AssetLoader] Preloaded animations for " << preload_count
                  << " referenced assets in " << preload_ms << "ms\n";
    }
        finalizeAssets();

        const auto overall_end = std::chrono::steady_clock::now();
        const double map_ms = std::chrono::duration_cast<std::chrono::milliseconds>(map_end - map_begin).count();
        const double library_ms = std::chrono::duration_cast<std::chrono::milliseconds>(library_end - library_begin).count();
        const double rooms_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rooms_end - rooms_begin).count();
        const double total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_begin).count();
        std::cout << "[AssetLoader] Map metadata loaded in " << map_ms << "ms\n";
        std::cout << "[AssetLoader] Asset library ready in " << library_ms << "ms\n";
        std::cout << "[AssetLoader] Rooms built in " << rooms_ms << "ms\n";
        std::cout << "[AssetLoader] Asset loader initialization completed in " << total_ms << "ms\n";
	auto distant_boundary = collectDistantAssets(0,2000);
	for(auto a : distant_boundary){
		a->set_hidden(true);
	}
	std::vector<Asset*> link_candidates;
	for (Room* room : rooms_) {
		for (auto& asset_up : room->assets) {
            if (auto* asset = asset_up.get()) {
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
	for (Asset* a : to_remove) {
		if (a == skip) continue;
		a->set_hidden(true);
	}
}

std::vector<std::vector<Asset*>> AssetLoader::group_neighboring_assets(
                                                                           const std::vector<Asset*>& assets,
                                                                           int tile_width,
                                                                           int tile_height,
                                                                           const std::string& group_type)
{
	std::unordered_map<long long, std::vector<Asset*>> tile_map;
	auto make_tile_key = [&](int tx, int ty) -> long long {
		return (static_cast<long long>(tx) << 32) ^ static_cast<unsigned long long>(ty);
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

std::vector<Asset*> AssetLoader::collectDistantAssets(int fade_start_distance, int fade_end_distance) {
	std::vector<Asset*> distant_assets;
	distant_assets.reserve(rooms_.size() * 4);
        auto allZones = getAllRoomAndTrailAreas();
        auto zoneCache = asset_loader_internal::build_zone_cache(allZones);
        for (Room* room : rooms_) {
                for (auto& asset_up : room->assets) {
                        Asset* asset = asset_up.get();
            if (!asset->info || asset->info->type != asset_types::boundary) {
                    asset->alpha_percentage = 1.0;
                    continue;
            }
                        SDL_Point asset_point{asset->pos.x, asset->pos.y};
                        if (asset_loader_internal::point_inside_any_zone(asset_point, zoneCache)) {
                                continue;
                        }
                        double minDistSq = asset_loader_internal::min_distance_sq_to_zones(asset_point, zoneCache, fade_end_distance);
                        double minDist = std::sqrt(minDistSq);
                        double alpha = 0.0;
                        if (minDist <= fade_start_distance) alpha = 1.0;
                        else if (minDist >= fade_end_distance) alpha = 0.0;
                        else {
                                double t = (minDist - fade_start_distance) / (fade_end_distance - fade_start_distance);
                                double diff = 1.0 - t;
                                alpha = diff * diff;
                        }
                        asset->alpha_percentage = alpha * 1.2;
                        bool distant = !(alpha > 0.3);
                        asset->static_frame = distant;
                        if (distant) distant_assets.push_back(asset);
                }
        }
        return distant_assets;
}

void AssetLoader::loadRooms() {
        GenerateRooms generator(map_layers_, map_center_x_, map_center_y_, map_path_, map_info_path_);
        nlohmann::json empty_boundary = nlohmann::json::object();
        nlohmann::json empty_rooms    = nlohmann::json::object();
        nlohmann::json empty_trails   = nlohmann::json::object();
        nlohmann::json empty_assets   = nlohmann::json::object();
        auto room_ptrs = generator.build( asset_library_.get(), map_radius_, map_boundary_data_ ? *map_boundary_data_ : empty_boundary, rooms_data_        ? *rooms_data_        : empty_rooms, trails_data_       ? *trails_data_       : empty_trails, map_assets_data_   ? *map_assets_data_   : empty_assets);
        for (auto& up : room_ptrs) {
                rooms_.push_back(up.get());
                all_rooms_.push_back(std::move(up));
	}
}

void AssetLoader::finalizeAssets() {
	for (Room* room : rooms_) {
		for (auto& asset_up : room->assets) {
			asset_up->finalize_setup();
		}
	}
}

std::vector<Asset> AssetLoader::extract_all_assets() {
	std::vector<Asset> out;
	out.reserve(rooms_.size() * 4);
	for (Room* room : rooms_) {
		for (auto& aup : room->assets) {
			Asset* asset = aup.get();
			if (!asset) continue;
			if (asset->is_hidden()) {
					continue;
			}
			out.push_back(std::move(*aup));
		}
	}
	return out;
}

std::vector<Asset> AssetLoader::createAssets() {
	return extract_all_assets();
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

void AssetLoader::load_map_json() {
        map_info_path_ = map_path_ + "/map_info.json";
        std::ifstream f(map_info_path_);
        if (!f) throw std::runtime_error("Failed to open map_info.json");

        json j;
        f >> j;
        map_info_json_ = std::move(j);

        if (!map_info_json_.is_object()) {
                map_info_json_ = nlohmann::json::object();
        }

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
        map_layers_.clear();

        if (layers_it != map_info_json_.end() && layers_it->is_array()) {
                const auto& radii = radii_result.layer_radii;
                map_layers_.reserve(layers_it->size());
                size_t index = 0;
                for (const auto& layer_entry : *layers_it) {
                        LayerSpec spec;
                        spec.level = static_cast<int>(index);
                        spec.radius = index < radii.size() ? radii[index] : 0.0;
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
                                                                        std::cerr << "[AssetLoader] Room '" << rs.name
                                                                                  << "' has non-string entry in 'required_children'; skipping.\n";
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
