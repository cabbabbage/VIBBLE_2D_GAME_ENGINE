#include "generate_rooms.hpp"
#include "generate_trails.hpp"
#include "spawn/asset_spawner.hpp"
#include "spawn/map_wide_asset_spawner.hpp"
#include "utils/display_color.hpp"
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace {
constexpr double kTau = 6.28318530717958647692;

inline int div_floor(int value, int divisor) {
        if (divisor == 0) {
                return 0;
        }
        if (value >= 0) {
                return value / divisor;
        }
        return -static_cast<int>((static_cast<long long>(-value) + divisor - 1) / divisor);
}

inline std::int64_t bucket_key(int x, int y) {
        return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(y));
}

class RoomSpatialIndex {
public:
        explicit RoomSpatialIndex(const std::vector<std::unique_ptr<Room>>& rooms, int bucket_size = 2048, int max_radius = 8)
        : bucket_size_(std::max(1, bucket_size)), max_radius_(std::max(1, max_radius)) {
                entries_.reserve(rooms.size());
                for (const auto& room_ptr : rooms) {
                        Room* room = room_ptr.get();
                        if (!room || !room->room_area) {
                                continue;
                        }
                        SDL_Point center = room->room_area->get_center();
                        entries_.push_back(RoomEntry{room, center});
                        const int bx = div_floor(center.x, bucket_size_);
                        const int by = div_floor(center.y, bucket_size_);
                        buckets_[bucket_key(bx, by)].push_back(&entries_.back());
                }
        }

        Room* find_owner(SDL_Point pt) const {
                const RoomEntry* best = nullptr;
                double best_dist_sq = std::numeric_limits<double>::max();
                const int base_bx = div_floor(pt.x, bucket_size_);
                const int base_by = div_floor(pt.y, bucket_size_);

                auto consider_bucket = [&](int bx, int by) -> bool {
                        auto it = buckets_.find(bucket_key(bx, by));
                        if (it == buckets_.end()) {
                                return false;
                        }
                        for (const RoomEntry* entry : it->second) {
                                if (!entry || !entry->room || !entry->room->room_area) {
                                        continue;
                                }
                                if (entry->room->room_area->contains_point(pt)) {
                                        best = entry;
                                        best_dist_sq = 0.0;
                                        return true;
                                }
                                const double dx = static_cast<double>(pt.x - entry->center.x);
                                const double dy = static_cast<double>(pt.y - entry->center.y);
                                const double dist_sq = dx * dx + dy * dy;
                                if (dist_sq < best_dist_sq) {
                                        best_dist_sq = dist_sq;
                                        best = entry;
                                }
                        }
                        return false;
};

                for (int radius = 0; radius <= max_radius_; ++radius) {
                        for (int by = base_by - radius; by <= base_by + radius; ++by) {
                                for (int bx = base_bx - radius; bx <= base_bx + radius; ++bx) {
                                        if (consider_bucket(bx, by) && best_dist_sq == 0.0) {
                                                return best ? best->room : nullptr;
                                        }
                                }
                        }
                        if (best) {
                                return best->room;
                        }
                }

                if (!best) {
                        for (const RoomEntry& entry : entries_) {
                                if (!entry.room || !entry.room->room_area) {
                                        continue;
                                }
                                if (entry.room->room_area->contains_point(pt)) {
                                        return entry.room;
                                }
                                const double dx = static_cast<double>(pt.x - entry.center.x);
                                const double dy = static_cast<double>(pt.y - entry.center.y);
                                const double dist_sq = dx * dx + dy * dy;
                                if (!best || dist_sq < best_dist_sq) {
                                        best = &entry;
                                        best_dist_sq = dist_sq;
                                }
                        }
                }

                return best ? best->room : nullptr;
        }

private:
        struct RoomEntry {
                Room* room = nullptr;
                SDL_Point center{0, 0};
};

        int bucket_size_ = 2048;
        int max_radius_ = 8;
        std::vector<RoomEntry> entries_;
        std::unordered_map<std::int64_t, std::vector<const RoomEntry*>> buckets_;
};
}

GenerateRooms::GenerateRooms(const std::vector<LayerSpec>& layers,
                             int map_cx,
                             int map_cy,
                             const std::string& map_id,
                             nlohmann::json& map_manifest,
                             double min_edge_distance,
                             devmode::core::ManifestStore* manifest_store,
                             Room::ManifestWriter manifest_writer)
: map_layers_(layers),
map_center_x_(map_cx),
map_center_y_(map_cy),
map_id_(map_id),
map_manifest_(&map_manifest),
manifest_store_(manifest_store),
manifest_writer_(std::move(manifest_writer)),
rng_(std::random_device{}()),
min_edge_distance_(std::max(0.0, min_edge_distance))
{}

SDL_Point GenerateRooms::polar_to_cartesian(int cx, int cy, double radius, float angle_rad) {
        const double x = static_cast<double>(cx) + std::cos(angle_rad) * radius;
        const double y = static_cast<double>(cy) + std::sin(angle_rad) * radius;
        return SDL_Point{ static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)) };
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
                                                        const std::vector<double>& layer_radii,
                                                        const nlohmann::json& boundary_data,
                                                        nlohmann::json& rooms_data,
                                                        nlohmann::json& trails_data,
                                                        nlohmann::json& map_assets_data,
                                                        const MapGridSettings& grid_settings) {
        std::cout << "[GenerateRooms] Starting build for " << map_layers_.size() << " layers\n";
        std::vector<std::unique_ptr<Room>> all_rooms;
        if (map_layers_.empty()) {
                std::cout << "[GenerateRooms] No layers to process, returning empty\n";
                return all_rooms;
        }

        if (map_layers_[0].rooms.empty()) {
                std::string fallback_name = "spawn";
                if (rooms_data.is_object()) {
                        for (auto it = rooms_data.begin(); it != rooms_data.end(); ++it) {
                                if (it.value().is_object() && it.value().value("is_spawn", false)) {
                                        fallback_name = it.key();
                                        break;
                                }
                        }
                }
                RoomSpec rs;
                rs.name = fallback_name;
                rs.max_instances = 1;
                map_layers_[0].rooms.push_back(rs);
        }
        const auto& root_spec = map_layers_[0].rooms[0];
        std::cout << "[GenerateRooms] Creating root room: " << root_spec.name << "\n";
        if (testing) {
                std::cout << "[GenerateRooms] Creating root room: " << root_spec.name << "\n";
        }
        if (!rooms_data.is_object()) {
                rooms_data = nlohmann::json::object();
        }

        if (!rooms_data.contains(root_spec.name) || !rooms_data[root_spec.name].is_object()) {
                constexpr int kSpawnRadius = 1500;
                const int diameter = kSpawnRadius * 2;
                nlohmann::json entry = nlohmann::json::object();
                entry["name"]                = root_spec.name;
                entry["geometry"]            = "Circle";
                entry["radius"]              = kSpawnRadius;
                entry["min_radius"]          = kSpawnRadius;
                entry["max_radius"]          = kSpawnRadius;
                entry["min_width"]           = diameter;
                entry["max_width"]           = diameter;
                entry["min_height"]          = diameter;
                entry["max_height"]          = diameter;
                entry["edge_smoothness"]     = 2;
                entry["is_spawn"]            = true;
                entry["is_boss"]             = false;
                entry["inherits_map_assets"] = false;
                entry["spawn_groups"]        = nlohmann::json::array();
                rooms_data[root_spec.name] = std::move(entry);
        }

        std::vector<SDL_Color> room_colors = utils::display_color::collect(rooms_data);

        auto get_room_data = [&](const std::string& name) -> nlohmann::json* {
                if (!rooms_data.is_object()) return nullptr;
                nlohmann::json& entry = rooms_data[name];
                bool mutated = false;
                utils::display_color::ensure(entry, room_colors, &mutated);
                (void)mutated;
                return &entry;
};
        const nlohmann::json* rooms_data_lookup = rooms_data.is_object() ? &rooms_data : nullptr;
        auto room_extent_lookup = [&](const std::string& room_name) {
                double extent = map_layers::room_extent_from_rooms_data(rooms_data_lookup, room_name);
                return (extent > 0.0) ? extent : 1.0;
};
        if (!map_assets_data.is_object()) {
                map_assets_data = nlohmann::json::object();
        }
        const nlohmann::json* map_assets_ptr = &map_assets_data;
        auto root = std::make_unique<Room>(
                                        Room::Point{ map_center_x_, map_center_y_ },
                                        "room",
                                        root_spec.name,
                                        nullptr,
                                        map_id_,
                                        asset_lib,
                                        nullptr,
                                        get_room_data(root_spec.name),
                                        map_assets_ptr,
                                        grid_settings,
                                        map_radius,
                                        "rooms_data",
                                        map_manifest_,
                                        manifest_store_,
                                        map_id_,
                                        manifest_writer_
 );
        root->layer = 0;
        all_rooms.push_back(std::move(root));
        std::cout << "[GenerateRooms] Root room created successfully\n";
        std::vector<Room*> current_parents = { all_rooms[0].get() };
        std::vector<Sector> current_sectors = { { current_parents[0], 0.0f, 2 * M_PI } };
        auto append_sectors_from_angles = [](const std::vector<Room*>& rooms,
                                             const std::vector<double>& angles,
                                             std::vector<Sector>& out) {
                if (rooms.empty() || rooms.size() != angles.size()) {
                        return;
                }
                if (rooms.size() == 1) {
                        out.push_back({ rooms[0], 0.0f, static_cast<float>(kTau) });
                        return;
                }
                for (std::size_t idx = 0; idx < rooms.size(); ++idx) {
                        const double current = angles[idx];
                        const double prev = (idx == 0) ? angles.back() - kTau : angles[idx - 1];
                        const double next = (idx + 1 == angles.size()) ? angles.front() + kTau : angles[idx + 1];
                        const double prev_gap = current - prev;
                        const double next_gap = next - current;
                        const double start = current - prev_gap * 0.5;
                        const double span = (prev_gap + next_gap) * 0.5;
                        out.push_back({ rooms[idx], static_cast<float>(start), static_cast<float>(span) });
                }
};
        for (size_t li = 1; li < map_layers_.size(); ++li) {
                std::cout << "[GenerateRooms] Processing layer " << li << "\n";
                const LayerSpec& layer = map_layers_[li];
                const double radius = (li < layer_radii.size()) ? layer_radii[li] : 0.0;
                auto children_specs = get_children_from_layer(layer);
                std::cout << "[GenerateRooms] Layer " << layer.level << " radius: " << radius << ", children count: " << children_specs.size() << "\n";
                if (testing) {
                        std::cout << "[GenerateRooms] Layer " << layer.level
                        << " radius: " << radius
			<< ", children count: " << children_specs.size() << "\n";
		}
		std::vector<Sector> next_sectors;
		std::vector<Room*> next_parents;
                if (li == 1) {
                        if (!children_specs.empty()) {
                                std::shuffle(children_specs.begin(), children_specs.end(), rng_);
                                std::vector<double> extents;
                                extents.reserve(children_specs.size());
                                for (const auto& spec : children_specs) {
                                        extents.push_back(room_extent_lookup(spec.name));
                                }
                                std::uniform_real_distribution<double> start_angle_dist(0.0, kTau);
                                map_layers::RadialLayout layout = map_layers::compute_radial_layout( radius, extents, min_edge_distance_, start_angle_dist(rng_));
                                std::vector<double> angles = layout.angles;
                                if (angles.size() != children_specs.size()) {
                                        angles.resize(children_specs.size());
                                        const double step = kTau / static_cast<double>(children_specs.size());
                                        for (std::size_t idx = 0; idx < angles.size(); ++idx) {
                                                angles[idx] = step * static_cast<double>(idx);
                                        }
                                }
                                const double used_radius = layout.radius;
                                std::vector<double> placed_angles;
                                placed_angles.reserve(children_specs.size());
                                for (std::size_t i = 0; i < children_specs.size(); ++i) {
                                        const double angle = angles[i];
                                        SDL_Point pos = polar_to_cartesian(map_center_x_, map_center_y_, used_radius, static_cast<float>(angle));
                                        if (testing) {
                                                std::cout << "[GenerateRooms] Placing layer-1 child " << children_specs[i].name
                                                          << " at angle " << angle << " → (" << pos.x << ", " << pos.y << ")\n";
                                        }
                                        auto child = std::make_unique<Room>(
                                                Room::Point{ pos.x, pos.y },
                                                "room",
                                                children_specs[i].name,
                                                current_parents[0],
                                                map_id_,
                                                asset_lib,
                                                nullptr,
                                                get_room_data(children_specs[i].name),
                                                map_assets_ptr,
                                                grid_settings,
                                                map_radius,
                                                "rooms_data",
                                                map_manifest_,
                                                manifest_store_,
                                                map_id_,
                                                manifest_writer_
                                        );
                                        child->layer = layer.level;
                                        if (!next_parents.empty()) {
                                                next_parents.back()->set_sibling_right(child.get());
                                                child->set_sibling_left(next_parents.back());
                                        }
                                        current_parents[0]->children.push_back(child.get());
                                        next_parents.push_back(child.get());
                                        placed_angles.push_back(angle);
                                        all_rooms.push_back(std::move(child));
                                }
                                append_sectors_from_angles(next_parents, placed_angles, next_sectors);
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
                        std::vector<RoomSpec> ordered_specs;
                        std::vector<Room*> ordered_parents;
                        ordered_specs.reserve(children_specs.size());
                        ordered_parents.reserve(children_specs.size());
                        for (auto& sec : current_sectors) {
                                        Room* parent = sec.room;
                                        auto& kids = assignments[parent];
                                        if (kids.empty()) continue;
                                        std::shuffle(kids.begin(), kids.end(), rng_);
                                        for (auto& spec : kids) {
                                                ordered_specs.push_back(spec);
                                                ordered_parents.push_back(parent);
                                        }
                        }
                        if (!ordered_specs.empty()) {
                                        std::vector<double> extents;
                                        extents.reserve(ordered_specs.size());
                                        for (const auto& spec : ordered_specs) {
                                                extents.push_back(room_extent_lookup(spec.name));
                                        }
                                        std::uniform_real_distribution<double> start_angle_dist(0.0, kTau);
                                        map_layers::RadialLayout layout = map_layers::compute_radial_layout( radius, extents, min_edge_distance_, start_angle_dist(rng_));
                                        std::vector<double> angles = layout.angles;
                                        if (angles.size() != ordered_specs.size()) {
                                                angles.resize(ordered_specs.size());
                                                const double step = kTau / static_cast<double>(ordered_specs.size());
                                                for (std::size_t idx = 0; idx < angles.size(); ++idx) {
                                                        angles[idx] = step * static_cast<double>(idx);
                                                }
                                        }
                                        const double used_radius = layout.radius;
                                        std::vector<double> placed_angles;
                                        placed_angles.reserve(ordered_specs.size());
                                        for (std::size_t idx = 0; idx < ordered_specs.size(); ++idx) {
                                                Room* parent = ordered_parents[idx];
                                                const double angle = angles[idx];
                                                SDL_Point pos = polar_to_cartesian(map_center_x_, map_center_y_, used_radius, static_cast<float>(angle));
                                                if (testing) {
                                                        std::cout << "[GenerateRooms] Placing child " << ordered_specs[idx].name
                                                                  << " under parent " << parent->room_name
                                                                  << " at angle " << angle << " → (" << pos.x << ", " << pos.y << ")\n";
                                                }
                                                auto child = std::make_unique<Room>(
                                                        Room::Point{ pos.x, pos.y },
                                                        "room",
                                                        ordered_specs[idx].name,
                                                        parent,
                                                        map_id_,
                                                        asset_lib,
                                                        nullptr,
                                                        get_room_data(ordered_specs[idx].name),
                                                        map_assets_ptr,
                                                        grid_settings,
                                                        map_radius,
                                                        "rooms_data",
                                                        map_manifest_,
                                                        manifest_store_,
                                                        map_id_,
                                                        manifest_writer_
                                                );
                                                child->layer = layer.level;
                                                if (!next_parents.empty()) {
                                                        next_parents.back()->set_sibling_right(child.get());
                                                        child->set_sibling_left(next_parents.back());
                                                }
                                                parent->children.push_back(child.get());
                                                next_parents.push_back(child.get());
                                                placed_angles.push_back(angle);
                                                all_rooms.push_back(std::move(child));
                                        }
                                        append_sectors_from_angles(next_parents, placed_angles, next_sectors);
                        }
                }
                current_parents = next_parents;
                current_sectors = next_sectors;
                std::cout << "[GenerateRooms] Layer " << li << " completed, total rooms: " << all_rooms.size() << "\n";
        }
	std::vector<std::pair<Room*,Room*>> connections;
	for (auto& rp : all_rooms) {
		for (Room* c : rp->children) {
			connections.emplace_back(rp.get(), c);
		}
	}
	std::cout << "[GenerateRooms] Parent-child connections established: " << connections.size() << " connections\n";
	std::vector<Area> existing_areas;
	for (const auto& r : all_rooms) {
		existing_areas.push_back(*r->room_area);
	}
	std::cout << "[GenerateRooms] Existing areas collected: " << existing_areas.size() << "\n";
	std::cout << "[GenerateRooms] Total rooms created (pre-trail): " << all_rooms.size() << "\n";
	std::cout << "[GenerateRooms] Beginning trail generation...\n";
        if (all_rooms.size() > 1) {
                GenerateTrails trailgen(trails_data, room_colors);
                std::vector<Room*> room_refs;
                room_refs.reserve(all_rooms.size());
                for (auto& room_ptr : all_rooms) {
                        room_refs.push_back(room_ptr.get());
                }
                trailgen.set_all_rooms_reference(room_refs);
                auto trail_objects = trailgen.generate_trails( connections, existing_areas, map_id_, asset_lib, map_assets_ptr, map_radius, map_manifest_, manifest_store_, manifest_writer_);
                for (auto& t : trail_objects) {
                        all_rooms.push_back(std::move(t));
                }
        }
        std::cout << "[GenerateRooms] Trail generation complete. Total rooms now: " << all_rooms.size() << "\n";
        std::cout << "[GenerateRooms] Spawning map-wide assets...\n";
        {
                MapWideAssetSpawner map_wide(asset_lib, grid_settings, map_id_, map_assets_data);
                map_wide.spawn(all_rooms);
        }
        std::cout << "[GenerateRooms] Map-wide assets spawned\n";
        if (!boundary_data.is_null() && !boundary_data.empty()) {
                std::cout << "[GenerateRooms] Processing boundary assets...\n";
                std::vector<Area> exclusion_zones;
                for (const auto& r : all_rooms) {
                        exclusion_zones.push_back(*r->room_area);
                }
                const int map_radius_int = map_radius > 0.0 ? static_cast<int>(std::lround(map_radius)) : 0;
                const int diameter = map_radius_int * 2;
                SDL_Point center{map_radius_int, map_radius_int};
                Area area("Map", center, diameter, diameter, "Circle", 1, diameter, diameter, 3);
                AssetSpawner spawner(asset_lib, exclusion_zones);
                std::vector<std::unique_ptr<Asset>> boundary_assets = spawner.spawn_boundary_from_json( boundary_data, area, map_id_ + "::map_boundary_data");
                int assigned_count = 0;
                RoomSpatialIndex room_index(all_rooms);
                for (auto& asset_ptr : boundary_assets) {
                        Asset* asset = asset_ptr.get();
                        if (!asset) continue;
                        Room* owner = room_index.find_owner(asset->pos);
                        if (owner) {
                                asset->set_owning_room_name(owner->room_name);
                                std::vector<std::unique_ptr<Asset>> wrapper;
                                        wrapper.push_back(std::move(asset_ptr));
                                        owner->add_room_assets(std::move(wrapper));
                                        assigned_count++;
                        }
                }
                std::cout << "[GenerateRooms] Boundary assets processed, " << assigned_count << " assigned\n";
        }
	std::cout << "[GenerateRooms] Build completed successfully\n";
	return all_rooms;
}
