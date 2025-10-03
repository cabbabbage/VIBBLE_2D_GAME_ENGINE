#include "generate_trails.hpp"
#include "trail_geometry.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <cstdint>
#include <limits>
using json = nlohmann::json;

namespace {

constexpr int kNearestNeighborCount = 4;
constexpr double kLoopConnectionChance = 0.35;
constexpr double kLoopCapRatio = 0.25;

struct DisjointSet {
    explicit DisjointSet(size_t count) : parent(count), rank(count, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    size_t find(size_t x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);
        }
        return parent[x];
    }

    bool unite(size_t a, size_t b) {
        size_t root_a = find(a);
        size_t root_b = find(b);
        if (root_a == root_b) {
            return false;
        }
        if (rank[root_a] < rank[root_b]) {
            std::swap(root_a, root_b);
        }
        parent[root_b] = root_a;
        if (rank[root_a] == rank[root_b]) {
            ++rank[root_a];
        }
        return true;
    }

    std::vector<size_t> parent;
    std::vector<int> rank;
};

struct PointerPairHash {
    size_t operator()(const std::pair<Room*, Room*>& value) const noexcept {
        auto a = reinterpret_cast<std::uintptr_t>(value.first);
        auto b = reinterpret_cast<std::uintptr_t>(value.second);
        return std::hash<std::uintptr_t>{}(a) ^ (std::hash<std::uintptr_t>{}(b) << 1);
    }
};

struct PointerPairEqual {
    bool operator()(const std::pair<Room*, Room*>& lhs, const std::pair<Room*, Room*>& rhs) const noexcept {
        return lhs.first == rhs.first && lhs.second == rhs.second;
    }
};

std::pair<Room*, Room*> canonical_pair(Room* a, Room* b) {
    if (!a || !b) {
        return {nullptr, nullptr};
    }
    if (a > b) {
        std::swap(a, b);
    }
    return {a, b};
}

std::pair<double, double> room_center(Room* room) {
    if (!room) {
        return {0.0, 0.0};
    }
    if (room->room_area) {
        SDL_Point c = room->room_area->get_center();
        return {static_cast<double>(c.x), static_cast<double>(c.y)};
    }
    return {static_cast<double>(room->map_origin.first), static_cast<double>(room->map_origin.second)};
}

} // namespace
GenerateTrails::GenerateTrails(nlohmann::json& trail_data)
: rng_(std::random_device{}()),
trails_data_(&trail_data)
{
        if (trail_data.is_object()) {
                for (auto it = trail_data.begin(); it != trail_data.end(); ++it) {
                        available_assets_.push_back({ it.key(), &(*it) });
                }
        }
        if (testing) {
                std::cout << "[GenerateTrails] Loaded " << available_assets_.size() << " trail templates\n";
        }
        if (available_assets_.empty()) {
                throw std::runtime_error("[GenerateTrails] No trail templates found in map_info.json");
        }
}

void GenerateTrails::set_all_rooms_reference(const std::vector<Room*>& rooms) {
	all_rooms_reference = rooms;
}

std::vector<std::unique_ptr<Room>> GenerateTrails::generate_trails(
                                                                       const std::vector<std::pair<Room*, Room*>>& room_pairs,
                                                                       const std::vector<Area>& existing_areas,
                                                                       const std::string& map_dir,
                                                                       const std::string& map_info_path,
                                                                       AssetLibrary* asset_lib,
                                                                       const nlohmann::json* map_assets_data,
                                                                       double map_radius)
{
        trail_areas_.clear();
        std::vector<std::unique_ptr<Room>> trail_rooms;
        std::vector<Area> all_areas = existing_areas;
        auto connection_plan = plan_maze_connections(all_rooms_reference, room_pairs);
        if (testing) {
                std::cout << "[GenerateTrails] Planned " << connection_plan.size()
                          << " trail connections (" << room_pairs.size()
                          << " forced).\n";
        }
        for (const auto& [a, b] : connection_plan) {
                if (!a || !b) continue;
                if (testing) {
                        std::cout << "[GenerateTrails] Connecting: " << a->room_name
                        << " <--> " << b->room_name << "\n";
                }
                bool success = false;
                for (int attempts = 0; attempts < 1000 && !success; ++attempts) {
                        if (const auto* asset_ref = pick_random_asset()) {
                                success = TrailGeometry::attempt_trail_connection( a, b, all_areas, map_dir, map_info_path, asset_lib, trail_rooms, 1, asset_ref->data, asset_ref->name, map_assets_data, map_radius, testing, rng_);
                        }
                }
                if (!success && testing) {
                        std::cout << "[TrailGen] Failed to place trail between "
                        << a->room_name << " and " << b->room_name << "\n";
                }
        }
        find_and_connect_isolated(map_dir, map_info_path, asset_lib, all_areas, trail_rooms, map_assets_data, map_radius);
        if (testing) {
                std::cout << "[TrailGen] Total trail rooms created: " << trail_rooms.size() << "\n";
        }
        return trail_rooms;
}

const GenerateTrails::TrailTemplateRef* GenerateTrails::pick_random_asset() {
        if (available_assets_.empty()) return nullptr;
        std::uniform_int_distribution<size_t> dist(0, available_assets_.size() - 1);
        return &available_assets_[dist(rng_)];
}

std::vector<std::pair<Room*, Room*>> GenerateTrails::plan_maze_connections(
        const std::vector<Room*>& rooms,
        const std::vector<std::pair<Room*, Room*>>& forced_connections) {

        std::vector<std::pair<Room*, Room*>> planned;
        if (rooms.empty()) {
                return planned;
        }

        std::vector<Room*> unique_rooms;
        unique_rooms.reserve(rooms.size());
        std::unordered_set<Room*> seen;
        seen.reserve(rooms.size());
        for (Room* room : rooms) {
                if (!room) continue;
                if (seen.insert(room).second) {
                        unique_rooms.push_back(room);
                }
        }

        if (unique_rooms.size() < 2) {
                return planned;
        }

        std::unordered_map<Room*, size_t> index;
        index.reserve(unique_rooms.size());
        for (size_t i = 0; i < unique_rooms.size(); ++i) {
                index[unique_rooms[i]] = i;
        }

        DisjointSet dsu(unique_rooms.size());
        std::unordered_set<std::pair<Room*, Room*>, PointerPairHash, PointerPairEqual> blocked_pairs;
        blocked_pairs.reserve(unique_rooms.size() * kNearestNeighborCount + forced_connections.size());

        for (const auto& edge : forced_connections) {
                Room* a = edge.first;
                Room* b = edge.second;
                if (!a || !b) continue;
                auto ia = index.find(a);
                auto ib = index.find(b);
                if (ia == index.end() || ib == index.end()) continue;
                dsu.unite(ia->second, ib->second);
                auto key = canonical_pair(a, b);
                if (!key.first || !key.second) continue;
                if (blocked_pairs.insert(key).second) {
                        planned.emplace_back(a, b);
                }
        }

        struct CandidateEdge {
                Room* a = nullptr;
                Room* b = nullptr;
                double distance = 0.0;
                double jitter = 0.0;
        };

        std::vector<CandidateEdge> candidates;
        candidates.reserve(unique_rooms.size() * kNearestNeighborCount);

        for (size_t i = 0; i < unique_rooms.size(); ++i) {
                Room* a = unique_rooms[i];
                if (!a) continue;
                auto [ax, ay] = room_center(a);
                std::vector<std::pair<double, size_t>> neighbors;
                neighbors.reserve(unique_rooms.size());
                for (size_t j = 0; j < unique_rooms.size(); ++j) {
                        if (i == j) continue;
                        Room* b = unique_rooms[j];
                        if (!b) continue;
                        auto [bx, by] = room_center(b);
                        double dx = ax - bx;
                        double dy = ay - by;
                        double dist = std::hypot(dx, dy);
                        neighbors.emplace_back(dist, j);
                }
                std::sort(neighbors.begin(), neighbors.end(),
                          [](const auto& lhs, const auto& rhs) {
                                  return lhs.first < rhs.first;
                          });
                const int limit = std::min<int>(kNearestNeighborCount, static_cast<int>(neighbors.size()));
                for (int n = 0; n < limit; ++n) {
                        Room* b = unique_rooms[neighbors[n].second];
                        auto key = canonical_pair(a, b);
                        if (!key.first || !key.second) continue;
                        if (!blocked_pairs.insert(key).second) continue;
                        candidates.push_back(CandidateEdge{ a, b, neighbors[n].first, 0.0 });
                }
        }

        std::uniform_real_distribution<double> jitter_dist(0.0, 1.0);
        for (auto& candidate : candidates) {
                candidate.jitter = jitter_dist(rng_);
        }
        std::sort(candidates.begin(), candidates.end(), [](const CandidateEdge& lhs, const CandidateEdge& rhs) {
                double lw = lhs.distance + lhs.jitter * 25.0;
                double rw = rhs.distance + rhs.jitter * 25.0;
                if (lw == rw) {
                        if (lhs.a == rhs.a) return lhs.b < rhs.b;
                        return lhs.a < rhs.a;
                }
                return lw < rw;
        });

        std::uniform_real_distribution<double> loop_dist(0.0, 1.0);
        size_t loop_cap = static_cast<size_t>(std::ceil(unique_rooms.size() * kLoopCapRatio));
        if (loop_cap == 0 && unique_rooms.size() > 2) {
                loop_cap = 1;
        }
        size_t loops_added = 0;

        for (const auto& candidate : candidates) {
                Room* a = candidate.a;
                Room* b = candidate.b;
                if (!a || !b) continue;
                auto ia = index.find(a);
                auto ib = index.find(b);
                if (ia == index.end() || ib == index.end()) continue;
                if (dsu.unite(ia->second, ib->second)) {
                        planned.emplace_back(a, b);
                } else if (loops_added < loop_cap && loop_dist(rng_) < kLoopConnectionChance) {
                        planned.emplace_back(a, b);
                        ++loops_added;
                }
        }

        auto rebuild_components = [&]() {
                std::unordered_map<size_t, std::vector<size_t>> components;
                components.reserve(unique_rooms.size());
                for (size_t i = 0; i < unique_rooms.size(); ++i) {
                        components[dsu.find(i)].push_back(i);
                }
                return components;
        };

        auto components = rebuild_components();
        while (components.size() > 1) {
                std::vector<std::vector<size_t>> groups;
                groups.reserve(components.size());
                for (auto& entry : components) {
                        groups.push_back(std::move(entry.second));
                }
                const auto& base_group = groups.front();
                if (base_group.empty()) {
                        break;
                }
                double best_dist = std::numeric_limits<double>::max();
                size_t best_a = base_group.front();
                size_t best_b = base_group.front();
                for (size_t idx_a : base_group) {
                        auto [ax, ay] = room_center(unique_rooms[idx_a]);
                        for (size_t g = 1; g < groups.size(); ++g) {
                                for (size_t idx_b : groups[g]) {
                                        auto [bx, by] = room_center(unique_rooms[idx_b]);
                                        double dist = std::hypot(ax - bx, ay - by);
                                        if (dist < best_dist) {
                                                best_dist = dist;
                                                best_a = idx_a;
                                                best_b = idx_b;
                                        }
                                }
                        }
                }
                if (best_dist == std::numeric_limits<double>::max()) {
                        break;
                }
                Room* a = unique_rooms[best_a];
                Room* b = unique_rooms[best_b];
                if (!a || !b) {
                        break;
                }
                if (blocked_pairs.insert(canonical_pair(a, b)).second) {
                        planned.emplace_back(a, b);
                }
                dsu.unite(best_a, best_b);
                components = rebuild_components();
        }

        return planned;
}

void GenerateTrails::find_and_connect_isolated(
                                                   const std::string& map_dir,
                                                   const std::string& map_info_path,
                                                   AssetLibrary* asset_lib,
                                                   std::vector<Area>& existing_areas,
                                                   std::vector<std::unique_ptr<Room>>& trail_rooms,
                                                   const nlohmann::json* map_assets_data,
                                                   double map_radius)
{
	const int max_passes = 1000000;
	int allowed_intersections = 0;
	for (int pass = 0; pass < max_passes; ++pass) {
		std::unordered_set<Room*> visited;
		std::unordered_set<Room*> connected_to_spawn;
		std::vector<std::vector<Room*>> isolated_groups;
		auto mark_connected = [&](Room* room, auto&& self) -> void {
			if (!room || connected_to_spawn.count(room)) return;
			connected_to_spawn.insert(room);
			for (Room* neighbor : room->connected_rooms) {
					self(neighbor, self);
			}
};
		auto collect_group = [&](Room* room, std::vector<Room*>& group, auto&& self) -> void {
			if (!room || visited.count(room) || connected_to_spawn.count(room)) return;
			visited.insert(room);
			group.push_back(room);
			for (Room* neighbor : room->connected_rooms) {
					self(neighbor, group, self);
			}
};
		for (Room* room : all_rooms_reference) {
			if (room && room->layer == 0) {
					mark_connected(room, mark_connected);
					break;
			}
		}
		for (Room* room : all_rooms_reference) {
			if (!visited.count(room) && !connected_to_spawn.count(room)) {
					std::vector<Room*> group;
					collect_group(room, group, collect_group);
					if (!group.empty()) {
								isolated_groups.push_back(std::move(group));
					}
			}
		}
		if (isolated_groups.empty()) {
			if (testing) {
					std::cout << "[ConnectIsolated] All rooms connected after " << pass << " passes.\n";
			}
			break;
		}
		if (testing) {
			std::cout << "[ConnectIsolated] Pass " << pass + 1 << " - " << isolated_groups.size() << " disconnected groups found | allowed intersections: " << allowed_intersections << "\n";
		}
		bool any_connection_made = false;
		for (const auto& group : isolated_groups) {
			if (group.empty()) continue;
			std::vector<Room*> sorted_group = group;
			std::sort(sorted_group.begin(), sorted_group.end(), [](Room* a, Room* b) {
					return a->connected_rooms.size() < b->connected_rooms.size();
             });
			for (Room* roomA : sorted_group) {
					std::vector<Room*> candidates;
                                        for (Room* candidate : all_rooms_reference) {
								if (candidate == roomA || connected_to_spawn.count(candidate)) continue;
								bool illegal = std::any_of(illegal_connections.begin(), illegal_connections.end(),
								[&](const std::pair<Room*, Room*>& p) {
													return (p.first == roomA && p.second == candidate) ||
													(p.first == candidate && p.second == roomA);
                                   });
								if (illegal) continue;
								std::unordered_set<Room*> check_visited;
								std::function<bool(Room*)> dfs = [&](Room* current) -> bool {
													if (!current || check_visited.count(current)) return false;
													if (current->layer == 0) return true;
													check_visited.insert(current);
													for (Room* neighbor : current->connected_rooms) {
																					if (dfs(neighbor)) return true;
													}
													return false;
};
								if (dfs(candidate)) {
													candidates.push_back(candidate);
								}
					}
					if (candidates.empty()) continue;
					std::sort(candidates.begin(), candidates.end(), [](Room* a, Room* b) {
								return a->connected_rooms.size() < b->connected_rooms.size();
               });
					if (candidates.size() > 5) candidates.resize(5);
                                        for (Room* roomB : candidates) {
                                                                for (int attempt = 0; attempt < 100; ++attempt) {
                                                                                                        if (const auto* asset_ref = pick_random_asset()) {
                                                                                                                if (TrailGeometry::attempt_trail_connection(
                 roomA, roomB, existing_areas, map_dir,
                 map_info_path,
                 asset_lib, trail_rooms,
                 allowed_intersections,
                 asset_ref->data,
                 asset_ref->name,
                 map_assets_data,
                 map_radius,
                 testing, rng_)) {
                                                                                                                               any_connection_made = true;
                                                                                                                               goto next_group;
                                                                                                        }
                                                                                                        }
                                                                }
                                        }
                        }
			next_group:;
		}
		if (!any_connection_made && testing) {
			std::cout << "[ConnectIsolated] No connections made on pass " << pass + 1 << "\n";
		}
		if ((pass + 1) % 5 == 0) {
			++allowed_intersections;
			if (testing) {
					std::cout << "[ConnectIsolated] Increasing allowed intersections to " << allowed_intersections << "\n";
			}
		}
	}
}

void GenerateTrails::remove_connection(Room* a,
                                       Room* b,
                                       std::vector<std::unique_ptr<Room>>& trail_rooms,
                                       std::vector<Area>& existing_areas)
{
	if (!a || !b) return;
	std::cout << "[Debug][remove_connection] Removing connection between '"
	<< a->room_name << "' and '" << b->room_name << "'\n";
	a->remove_connecting_room(b);
	b->remove_connecting_room(a);
	std::cout << "[Debug][remove_connection] After removal, "
	<< a->room_name << " has " << a->connected_rooms.size() << " connections; " << b->room_name << " has " << b->connected_rooms.size() << " connections.\n";
	size_t before = trail_rooms.size();
	trail_rooms.erase(
	std::remove_if(trail_rooms.begin(), trail_rooms.end(),
	[&](const std::unique_ptr<Room>& trail) {
		if (!trail) return false;
                       bool connects_a = false, connects_b = false;
		for (Room* r : trail->connected_rooms) {
			if (r == a) connects_a = true;
			if (r == b) connects_b = true;
                       }
		if (connects_a && connects_b) {
			existing_areas.erase(
			std::remove_if(existing_areas.begin(), existing_areas.end(),
			[&](const Area& area) {
					return area.get_name() == trail->room_area->get_name();
                       }),
			existing_areas.end()
 );
                       return true;
                       }
                       return false;
                       }),
	trail_rooms.end()
 );
	std::cout << "[Debug][remove_connection] Removed "
	<< (before - trail_rooms.size()) << " trail room(s) connecting them.\n";
}

void GenerateTrails::remove_random_connection(std::vector<std::unique_ptr<Room>>& trail_rooms) {
	if (trail_rooms.empty()) {
		std::cout << "[Debug][remove_random_connection] No trail rooms to remove.\n";
		return;
	}
	std::uniform_int_distribution<size_t> dist(0, trail_rooms.size() - 1);
	size_t index = dist(rng_);
	Room* trail = trail_rooms[index].get();
	std::cout << "[Debug][remove_random_connection] Chosen trail index: "
	<< index << " (room: " << (trail ? trail->room_name : "<null>") << ")\n";
	if (!trail || trail->connected_rooms.size() < 2) {
		std::cout << "[Debug][remove_random_connection] Trail has fewer than 2 connections, skipping.\n";
		return;
	}
	Room* a = trail->connected_rooms[0];
	Room* b = trail->connected_rooms[1];
	std::cout << "[Debug][remove_random_connection] Disconnecting '"
	<< a->room_name << "' and '" << b->room_name << "'\n";
	if (a && b) {
		a->remove_connecting_room(b);
		b->remove_connecting_room(a);
		std::cout << "[Debug][remove_random_connection] After disconnect, "
		<< a->room_name << " has " << a->connected_rooms.size() << " connections; " << b->room_name << " has " << b->connected_rooms.size() << " connections.\n";
	}
	trail_rooms.erase(trail_rooms.begin() + index);
	std::cout << "[Debug][remove_random_connection] Erased trail room at index "
	<< index << ", remaining trail_rooms: " << trail_rooms.size() << "\n";
}

void GenerateTrails::remove_and_connect(std::vector<std::unique_ptr<Room>>& trail_rooms,
                                        std::vector<std::pair<Room*, Room*>>& illegal_connections,
                                        const std::string& map_dir,
                                        const std::string& map_info_path,
                                        AssetLibrary* asset_lib,
                                        std::vector<Area>& existing_areas,
                                        const nlohmann::json* map_assets_data,
                                        double map_radius)
{
	Room* target = nullptr;
	for (Room* room : all_rooms_reference) {
		if (room && room->layer > 2 && room->connected_rooms.size() > 3) {
			if (!target || room->connected_rooms.size() > target->connected_rooms.size()) {
					target = room;
			}
		}
	}
	if (!target) {
		std::cout << "[Debug][remove_and_connect] No target room with layer > 2 and >3 connections found.\n";
		return;
	}
	std::cout << "[Debug][remove_and_connect] Selected target room '" << target->room_name
	<< "' with " << target->connected_rooms.size() << " connections.\n";
	Room* most_connected = nullptr;
	for (Room* neighbor : target->connected_rooms) {
		if (neighbor->connected_rooms.size() <= 3) continue;
		if (!most_connected || neighbor->connected_rooms.size() > most_connected->connected_rooms.size()) {
			most_connected = neighbor;
		}
	}
	if (!most_connected) {
		std::cout << "[Debug][remove_and_connect] No neighbor with >3 connections found for target.\n";
		return;
	}
	std::cout << "[Debug][remove_and_connect] Selected neighbor '" << most_connected->room_name
	<< "' with " << most_connected->connected_rooms.size() << " connections.\n";
	remove_connection(target, most_connected, trail_rooms, existing_areas);
	illegal_connections.emplace_back(target, most_connected);
	std::cout << "[Debug][remove_and_connect] Marked connection illegal: ('"
	<< target->room_name << "', '" << most_connected->room_name << "')\n";
        find_and_connect_isolated(map_dir, map_info_path, asset_lib, existing_areas, trail_rooms, map_assets_data, map_radius);
	std::cout << "[Debug][remove_and_connect] Completed reconnect attempt for isolated groups.\n";
}

void GenerateTrails::circular_connection(std::vector<std::unique_ptr<Room>>& trail_rooms,
                                         const std::string& map_dir,
                                         const std::string& map_info_path,
                                         AssetLibrary* asset_lib,
                                         std::vector<Area>& existing_areas,
                                         const nlohmann::json* map_assets_data,
                                         double map_radius)
{
	if (all_rooms_reference.empty()) {
		std::cout << "[Debug][circular_connection] No rooms available.\n";
		return;
	}
	Room* outermost = nullptr;
	int max_layer = -1;
	for (Room* room : all_rooms_reference) {
		if (room && room->layer > max_layer) {
			max_layer = room->layer;
			outermost = room;
		}
	}
	if (!outermost) {
		std::cout << "[Debug][circular_connection] No outermost room found.\n";
		return;
	}
	std::cout << "[Debug][circular_connection] Outermost room: '" << outermost->room_name
	<< "', layer " << outermost->layer << "\n";
	std::unordered_set<Room*> lineage_set;
	for (Room* r = outermost; r; r = r->parent) {
		lineage_set.insert(r);
		if (r->layer == 0) break;
	}
        Room* current = outermost;
        int fail_counter = 0;
        bool first_iteration = true;
        while ((first_iteration || !lineage_set.count(current)) && fail_counter < 10) {
                first_iteration = false;
		std::vector<Room*> candidates;
		auto add_candidate = [&](Room* r) {
			if (!r || r->layer <= 1) return;
			if (std::find(current->connected_rooms.begin(), current->connected_rooms.end(), r) != current->connected_rooms.end())
			return;
			candidates.push_back(r);
};
		add_candidate(current->right_sibling);
		if (current->right_sibling) {
			add_candidate(current->right_sibling->parent);
			for (Room* child : current->right_sibling->children)
			add_candidate(child);
		}
		add_candidate(current->left_sibling);
		if (current->left_sibling) {
			add_candidate(current->left_sibling->parent);
			for (Room* child : current->left_sibling->children)
			add_candidate(child);
		}
		std::shuffle(candidates.begin(), candidates.end(), rng_);
		std::cout << "[Debug][circular_connection] Candidate count for '" << current->room_name
		<< "': " << candidates.size() << "\n";
		if (candidates.empty()) {
			std::cout << "[Debug][circular_connection] No candidates, breaking loop.\n";
			break;
		}
		Room* next = candidates.front();
		std::cout << "[Debug][circular_connection] Attempting to connect '"
		<< current->room_name << "' -> '" << next->room_name << "'\n";
		bool connected = false;
                for (int attempt = 0; attempt < 1000; ++attempt) {
                        if (const auto* asset_ref = pick_random_asset()) {
                                if (TrailGeometry::attempt_trail_connection(current,
                                                                             next,
                                                                             existing_areas,
                                                                             map_dir,
                                                                             map_info_path,
                                                                             asset_lib,
                                                                             trail_rooms,
                                                                             1,
                                                                             asset_ref->data,
                                                                             asset_ref->name,
                                                                             map_assets_data,
                                                                             map_radius,
                                                                             testing,
                                                                             rng_)) {
                                        std::cout << "[Debug][circular_connection] Connected on attempt "
                                        << attempt + 1 << " using asset: " << asset_ref->name << "\n";
                                        current = next;
                                        connected = true;
                                        break;
                                }
                        }
                }
		if (!connected) {
			std::cout << "[Debug][circular_connection] Failed to connect '"
			<< current->room_name << "' -> '" << next->room_name << "' after 1000 attempts.\n";
			++fail_counter;
		} else {
			fail_counter = 0;
		}
	}
	std::cout << "[Debug][circular_connection] Circular connection complete.\n";
}
