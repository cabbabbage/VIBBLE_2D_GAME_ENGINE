#include "generate_rooms.hpp"
#include "generate_trails.hpp"
#include "spawn/asset_spawner.hpp"
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

GenerateRooms::GenerateRooms(const std::vector<LayerSpec>& layers,
                             int map_cx,
                             int map_cy,
                             const std::string& map_dir,
                             const std::string& map_info_path)
: map_layers_(layers),
map_center_x_(map_cx),
map_center_y_(map_cy),
map_path_(map_dir),
map_info_path_(map_info_path),
rng_(std::random_device{}())
{}

SDL_Point GenerateRooms::polar_to_cartesian(int cx, int cy, int radius, float angle_rad) {
	float x = cx + std::cos(angle_rad) * radius;
	float y = cy + std::sin(angle_rad) * radius;
	return SDL_Point{ static_cast<int>(std::round(x)), static_cast<int>(std::round(y)) };
}

std::vector<RoomSpec> GenerateRooms::get_children_from_layer(const LayerSpec& layer) {
        std::vector<RoomSpec> result;
        const int target = std::max(0, layer.max_rooms);
        if (testing) {
                std::cout << "[GenerateRooms] Building layer " << layer.level
                          << " targeting " << target << " rooms\n";
        }

        if (target == 0) return result;

        std::vector<RoomSpec> candidates;
        for (const auto& r : layer.rooms) {
                const int max_instances = std::max(0, r.max_instances);
                if (testing) {
                        std::cout << "[GenerateRooms] Room type: " << r.name
                                  << " count: " << max_instances << "\n";
                }
                for (int i = 0; i < max_instances; ++i) {
                        candidates.push_back(r);
                }
        }

        if (candidates.empty()) return result;

        std::shuffle(candidates.begin(), candidates.end(), rng_);
        if (static_cast<int>(candidates.size()) <= target) {
                return candidates;
        }

        result.insert(result.end(), candidates.begin(), candidates.begin() + target);
        return result;
}

std::vector<std::unique_ptr<Room>> GenerateRooms::build(AssetLibrary* asset_lib,
                                                        double map_radius,
                                                        const nlohmann::json& boundary_data,
                                                        nlohmann::json& rooms_data,
                                                        nlohmann::json& trails_data,
                                                        const nlohmann::json& map_assets_data) {
        std::vector<std::unique_ptr<Room>> all_rooms;
        if (map_layers_.empty()) return all_rooms;
        const auto& root_spec = map_layers_[0].rooms[0];
        if (testing) {
                std::cout << "[GenerateRooms] Creating root room: " << root_spec.name << "\n";
        }
        auto get_room_data = [&](const std::string& name) -> nlohmann::json* {
                if (!rooms_data.is_object()) return nullptr;
                return &rooms_data[name];
        };
        auto* map_assets_ptr = &map_assets_data;
        auto root = std::make_unique<Room>(
                                        Room::Point{ map_center_x_, map_center_y_ },
                                        "room",
                                        root_spec.name,
                                        nullptr,
                                        map_path_,
                                        map_info_path_,
                                        asset_lib,
                                        nullptr,
                                        get_room_data(root_spec.name),
                                        map_assets_ptr,
                                        map_radius,
                                        "rooms_data"
 );
        root->layer = 0;
        all_rooms.push_back(std::move(root));
	std::vector<Room*> current_parents = { all_rooms[0].get() };
	std::vector<Sector> current_sectors = { { current_parents[0], 0.0f, 2 * M_PI } };
	for (size_t li = 1; li < map_layers_.size(); ++li) {
		const LayerSpec& layer = map_layers_[li];
		auto children_specs = get_children_from_layer(layer);
		int radius = layer.radius;
		if (testing) {
			std::cout << "[GenerateRooms] Layer " << layer.level
			<< " radius: " << radius
			<< ", children count: " << children_specs.size() << "\n";
		}
		std::vector<Sector> next_sectors;
		std::vector<Room*> next_parents;
		if (li == 1) {
			std::shuffle(children_specs.begin(), children_specs.end(), rng_);
			float slice = 2.0f * M_PI / children_specs.size();
			float buf = slice * 0.05f;
			for (size_t i = 0; i < children_specs.size(); ++i) {
					float angle = i * slice + buf;
					SDL_Point pos = polar_to_cartesian(map_center_x_, map_center_y_, radius, angle);
					if (testing) {
								std::cout << "[GenerateRooms] Placing layer-1 child " << children_specs[i].name
								<< " at angle " << angle << " → (" << pos.x << ", " << pos.y << ")\n";
					}
                                        auto child = std::make_unique<Room>(
                                                Room::Point{ pos.x, pos.y },
                                                "room",
                                                children_specs[i].name,
                                                current_parents[0],
                                                map_path_,
                                                map_info_path_,
                                                asset_lib,
                                                nullptr,
                                                get_room_data(children_specs[i].name),
                                                map_assets_ptr,
                                                map_radius,
                                                "rooms_data"
                                        );
					child->layer = layer.level;
					if (!next_parents.empty()) {
								next_parents.back()->set_sibling_right(child.get());
								child->set_sibling_left(next_parents.back());
					}
					current_parents[0]->children.push_back(child.get());
					next_sectors.push_back({ child.get(), angle - (slice - 2 * buf) / 2, slice - 2 * buf });
					next_parents.push_back(child.get());
					all_rooms.push_back(std::move(child));
			}
		} else {
			std::unordered_map<Room*, std::vector<RoomSpec>> assignments;
			for (const auto& sec : current_sectors) {
					for (const auto& rs : map_layers_[li-1].rooms) {
								if (sec.room->room_name == rs.name) {
													for (const auto& cname : rs.required_children) {
																					if (testing) {
																																		std::cout << "[GenerateRooms] Adding required child " << cname
																																		<< " for parent " << rs.name << "\n";
																					}
                                                                                                                               assignments[sec.room].push_back({cname, 1, {}});
													}
								}
					}
			}
			std::vector<Room*> parent_order;
			for (auto& sec : current_sectors) parent_order.push_back(sec.room);
			std::vector<int> counts(parent_order.size(), 0);
			for (auto& rs : children_specs) {
					auto it = std::min_element(counts.begin(), counts.end());
					int idx = int(std::distance(counts.begin(), it));
					assignments[parent_order[idx]].push_back(rs);
					counts[idx]++;
			}
			for (auto& sec : current_sectors) {
					Room* parent = sec.room;
					auto& kids = assignments[parent];
					if (kids.empty()) continue;
					std::shuffle(kids.begin(), kids.end(), rng_);
					float slice = sec.span_angle / kids.size();
					float buf = slice * 0.05f;
					for (size_t i = 0; i < kids.size(); ++i) {
								float angle = sec.start_angle + i * slice + buf;
								float spread = slice - 2 * buf;
								SDL_Point pos = polar_to_cartesian(map_center_x_, map_center_y_, radius, angle);
								if (testing) {
													std::cout << "[GenerateRooms] Placing child " << kids[i].name
													<< " under parent " << parent->room_name
													<< " at angle " << angle << " → (" << pos.x << ", " << pos.y << ")\n";
								}
                                                            auto child = std::make_unique<Room>(
                                                                    Room::Point{ pos.x, pos.y },
                                                                    "room",
                                                                    kids[i].name,
                                                                    parent,
                                                                    map_path_,
                                                                    map_info_path_,
                                                                    asset_lib,
                                                                    nullptr,
                                                                    get_room_data(kids[i].name),
                                                                    map_assets_ptr,
                                                                    map_radius,
                                                                    "rooms_data"
                                                            );
								child->layer = layer.level;
								if (!next_parents.empty()) {
													next_parents.back()->set_sibling_right(child.get());
													child->set_sibling_left(next_parents.back());
								}
								parent->children.push_back(child.get());
								next_sectors.push_back({ child.get(), angle - spread/2, spread });
								next_parents.push_back(child.get());
								all_rooms.push_back(std::move(child));
					}
			}
		}
		current_parents = next_parents;
		current_sectors = next_sectors;
	}
	std::vector<std::pair<Room*,Room*>> connections;
	for (auto& rp : all_rooms) {
		for (Room* c : rp->children) {
			connections.emplace_back(rp.get(), c);
		}
	}
	std::vector<Area> existing_areas;
	for (const auto& r : all_rooms) {
		existing_areas.push_back(*r->room_area);
	}
	if (testing) {
		std::cout << "[GenerateRooms] Total rooms created (pre-trail): " << all_rooms.size() << "\n";
		std::cout << "[GenerateRooms] Beginning trail generation...\n";
	}
        if (all_rooms.size() > 1) {
                GenerateTrails trailgen(trails_data);
                std::vector<Room*> room_refs;
                room_refs.reserve(all_rooms.size());
                for (auto& room_ptr : all_rooms) {
                        room_refs.push_back(room_ptr.get());
                }
                trailgen.set_all_rooms_reference(room_refs);
                auto trail_objects = trailgen.generate_trails(
                        connections,
                        existing_areas,
                        map_path_,
                        map_info_path_,
                        asset_lib,
                        map_assets_ptr,
                        map_radius);
                for (auto& t : trail_objects) {
                        all_rooms.push_back(std::move(t));
                }
        }
	if (testing) {
		std::cout << "[GenerateRooms] Trail generation complete. Total rooms now: " << all_rooms.size() << "\n";
	}
        if (!boundary_data.is_null() && !boundary_data.empty()) {
                std::cout << "[Boundary] Starting boundary asset spawning...\n";
                std::vector<Area> exclusion_zones;
                for (const auto& r : all_rooms) {
                        exclusion_zones.push_back(*r->room_area);
                }
		std::cout << "[Boundary] Collected " << exclusion_zones.size() << " exclusion zones from existing rooms.\n";
		int cx = map_radius;
		int cy = map_radius;
		int diameter = map_radius * 2;
		SDL_Point center{cx, cy};
		Area area("Map", center, diameter, diameter, "Circle", 1, diameter, diameter);
		std::cout << "[Boundary] Created circular boundary area with diameter " << diameter << "\n";
		AssetSpawner spawner(asset_lib, exclusion_zones);
                std::vector<std::unique_ptr<Asset>> boundary_assets = spawner.spawn_boundary_from_json(
                        boundary_data,
                        area,
                        map_info_path_ + "::map_boundary_data");
		std::cout << "[Boundary] Extracted " << boundary_assets.size() << " spawned boundary assets\n";
		int assigned_count = 0;
		for (auto& asset_ptr : boundary_assets) {
			Asset* asset = asset_ptr.get();
			if (!asset) continue;
			Room* closest_room = nullptr;
			double closest_dist_sq = std::numeric_limits<double>::max();
			for (const auto& room_ptr : all_rooms) {
					auto [minx, miny, maxx, maxy] = room_ptr->room_area->get_bounds();
					int center_x = (minx + maxx) / 2;
					int center_y = (miny + maxy) / 2;
					double dx = static_cast<double>(asset->pos.x - center_x);
					double dy = static_cast<double>(asset->pos.y - center_y);
					double dist_sq = dx * dx + dy * dy;
					if (dist_sq < closest_dist_sq) {
								closest_dist_sq = dist_sq;
								closest_room = room_ptr.get();
					}
			}
			if (closest_room) {
					std::vector<std::unique_ptr<Asset>> wrapper;
					wrapper.push_back(std::move(asset_ptr));
					closest_room->add_room_assets(std::move(wrapper));
					assigned_count++;
			}
		}
		std::cout << "[Boundary] Assigned " << assigned_count << " assets to closest rooms\n";
	}
	return all_rooms;
}
