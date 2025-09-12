#include "asset_loader.hpp"
#include <fstream>
#include <iostream>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <stdexcept>
#include <SDL.h>
#include "asset/Asset.hpp"
#include "asset/asset_library.hpp"
#include "room/room.hpp"
#include "utils/area.hpp"
#include "room/generate_rooms.hpp"
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

AssetLoader::AssetLoader(const std::string& map_dir, SDL_Renderer* renderer)
: map_path_(map_dir),
renderer_(renderer)
{
	load_map_json();
	asset_library_ = std::make_unique<AssetLibrary>();
    loadRooms();
    {
        // Load animations only for assets actually present in generated rooms
        std::unordered_set<std::string> used;
        for (Room* room : rooms_) {
            for (const auto& aup : room->assets) {
                if (const Asset* a = aup.get()) {
                    if (a->info) used.insert(a->info->name);
                }
            }
        }
        asset_library_->loadAnimationsFor(renderer_, used);
    }
	finalizeAssets();
	auto distant_boundary = collectDistantAssets(0,2000);
	for(auto a : distant_boundary){
		a->set_hidden(true);
	}
	std::vector<Asset*> link_candidates;
	for (Room* room : rooms_) {
		for (auto& asset_up : room->assets) {
            if (auto* asset = asset_up.get()) {
                if (asset->info && asset->info->type != "Player" && !asset->info->moving_asset) {
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
	std::cout << "[link_by_child] Linked " << total_linked << " assets as children.\n";
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
	std::cout << "[" << group_type << "] Created " << groups.size() << " tile groups, total assets: " << total_assets << ", avg group size: " << avg_group_size << ", largest group: " << largest_group << "\n";
	return groups;
}

std::vector<Asset*> AssetLoader::collectDistantAssets(int fade_start_distance, int fade_end_distance) {
	std::vector<Asset*> distant_assets;
	distant_assets.reserve(rooms_.size() * 4);
	auto allZones = getAllRoomAndTrailAreas();
	for (Room* room : rooms_) {
		for (auto& asset_up : room->assets) {
			Asset* asset = asset_up.get();
			if (!asset->info || asset->info->type != "boundary") {
					asset->alpha_percentage = 1.0;
					asset->has_base_shadow  = false;
					asset->gradient_shadow  = true;
					continue;
			}
			bool is_inside = false;
			for (const Area& zone : allZones) {
					if (zone.contains_point({asset->pos.x, asset->pos.y})) {
								is_inside = true;
								break;
					}
			}
			if (!is_inside) {
                                       double minDistSq = std::numeric_limits<double>::infinity();
					for (const Area& zone : allZones) {
								const auto& pts = zone.get_points();
								for (size_t i = 0; i + 1 < pts.size(); ++i) {
													auto [x1, y1] = pts[i];
													auto [x2, y2] = pts[(i + 1) % pts.size()];
													double vx = x2 - x1, vy = y2 - y1;
													double wx = asset->pos.x - x1, wy = asset->pos.y - y1;
													double len2 = vx * vx + vy * vy;
													double t = len2 > 0.0 ? (vx * wx + vy * wy) / len2 : 0.0;
													t = std::clamp(t, 0.0, 1.0);
													double projx = x1 + t * vx;
													double projy = y1 + t * vy;
                                                                                                       double dx = projx - asset->pos.x;
                                                                                                       double dy = projy - asset->pos.y;
                                                                                                       double distSq = dx * dx + dy * dy;
                                                                                                       minDistSq = std::min(minDistSq, distSq);
                                                               }
                                       }
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
	}
	return distant_assets;
}

void AssetLoader::loadRooms() {
	GenerateRooms generator(map_layers_, map_center_x_, map_center_y_, map_path_);
	auto room_ptrs = generator.build(asset_library_.get(), map_radius_, map_boundary_file_);
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
	auto assetsVec = extract_all_assets();
	std::cout << "[AssetLoader] Created vector with " << assetsVec.size() << " assets\n";
	return assetsVec;
}

std::vector<Area> AssetLoader::getAllRoomAndTrailAreas() const {
	std::vector<Area> areas;
	areas.reserve(rooms_.size());
	for (Room* r : rooms_) {
		areas.push_back(*r->room_area);
	}
	return areas;
}


void AssetLoader::load_map_json() {
	std::ifstream f(map_path_ + "/map_info.json");
	if (!f) throw std::runtime_error("Failed to open map_info.json");
	json j;
	f >> j;
	map_radius_        = j.value("map_radius", 0);
	map_boundary_file_ = j.value("map_boundary", "");
	map_center_x_ = map_center_y_ = map_radius_;
	for (const auto& L : j["map_layers"]) {
		LayerSpec spec;
		spec.level     = L.value("level", 0);
		spec.radius    = L.value("radius", 0);
		spec.min_rooms = L.value("min_rooms", 0);
		spec.max_rooms = L.value("max_rooms", 0);
		for (const auto& R : L["rooms"]) {
			RoomSpec rs;
			rs.name = R.value("name", "unnamed");
			rs.min_instances = R.value("min_instances", 1);
			rs.max_instances = R.value("max_instances", 1);
			if (R.contains("required_children") && R["required_children"].is_array()) {
					for (const auto& c : R["required_children"]) {
								if (c.is_string()) {
													rs.required_children.push_back(c.get<std::string>());
								} else {
													std::cerr << "[AssetLoader] Room '" << rs.name
													<< "' has non-string entry in 'required_children'; skipping.\n";
								}
					}
			}
			spec.rooms.push_back(std::move(rs));
		}
		map_layers_.push_back(std::move(spec));
	}
}
