
#include "generate_trails.hpp"
#include "trail_geometry.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

GenerateTrails::GenerateTrails(const std::string& trail_dir)
    : rng_(std::random_device{}())
{
    for (const auto& entry : fs::directory_iterator(trail_dir)) {
        if (entry.path().extension() == ".json") {
            available_assets_.push_back(entry.path().string());
        }
    }

    if (testing) {
        std::cout << "[GenerateTrails] Loaded " << available_assets_.size() << " trail assets\n";
    }

    if (available_assets_.empty()) {
        throw std::runtime_error("[GenerateTrails] No JSON trail assets found");
    }
}

void GenerateTrails::set_all_rooms_reference(const std::vector<Room*>& rooms) {
    all_rooms_reference = rooms;
}

std::vector<std::unique_ptr<Room>> GenerateTrails::generate_trails(
    const std::vector<std::pair<Room*, Room*>>& room_pairs,
    const std::vector<Area>& existing_areas,
    const std::string& map_dir,
    AssetLibrary* asset_lib)
{
    trail_areas_.clear();
    std::vector<std::unique_ptr<Room>> trail_rooms;
    std::vector<Area> all_areas = existing_areas;

    for (const auto& [a, b] : room_pairs) {
        if (testing) {
            std::cout << "[GenerateTrails] Connecting: " << a->room_name
                      << " <--> " << b->room_name << "\n";
        }

        bool success = false;
        for (int attempts = 0; attempts < 1000 && !success; ++attempts) {
            std::string path = pick_random_asset();
            success = TrailGeometry::attempt_trail_connection(
                a, b, all_areas, map_dir, asset_lib, trail_rooms,
                /*allowed_intersections=*/1,
                path, testing, rng_
            );
        }

        if (!success && testing) {
            std::cout << "[TrailGen] Failed to place trail between "
                      << a->room_name << " and " << b->room_name << "\n";
        }
    }
    circular_connection(trail_rooms, map_dir, asset_lib, all_areas);
    find_and_connect_isolated(map_dir, asset_lib, all_areas, trail_rooms);





    if (testing) {
        std::cout << "[TrailGen] Total trail rooms created: " << trail_rooms.size() << "\n";
    }

    return trail_rooms;
}

std::string GenerateTrails::pick_random_asset() {
    std::uniform_int_distribution<size_t> dist(0, available_assets_.size() - 1);
    return available_assets_[dist(rng_)];
}

void GenerateTrails::find_and_connect_isolated(
    const std::string& map_dir,
    AssetLibrary* asset_lib,
    std::vector<Area>& existing_areas,
    std::vector<std::unique_ptr<Room>>& trail_rooms)
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
            std::cout << "[ConnectIsolated] Pass " << pass + 1 << " - " << isolated_groups.size()
                      << " disconnected groups found | allowed intersections: "
                      << allowed_intersections << "\n";
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
                        std::string path = pick_random_asset();
                        if (TrailGeometry::attempt_trail_connection(
                                roomA, roomB, existing_areas, map_dir,
                                asset_lib, trail_rooms,
                                allowed_intersections,
                                path, testing, rng_)) {
                            any_connection_made = true;
                            goto next_group;
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
              << a->room_name << " has " << a->connected_rooms.size()
              << " connections; " << b->room_name << " has "
              << b->connected_rooms.size() << " connections.\n";

    
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
              << (before - trail_rooms.size())
              << " trail room(s) connecting them.\n";
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
                  << a->room_name << " has " << a->connected_rooms.size()
                  << " connections; " << b->room_name << " has "
                  << b->connected_rooms.size() << " connections.\n";
    }

    trail_rooms.erase(trail_rooms.begin() + index);
    std::cout << "[Debug][remove_random_connection] Erased trail room at index "
              << index << ", remaining trail_rooms: " << trail_rooms.size() << "\n";
}

void GenerateTrails::remove_and_connect(std::vector<std::unique_ptr<Room>>& trail_rooms,
                                        std::vector<std::pair<Room*, Room*>>& illegal_connections,
                                        const std::string& map_dir,
                                        AssetLibrary* asset_lib,
                                        std::vector<Area>& existing_areas) 
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

    
    find_and_connect_isolated(map_dir, asset_lib, existing_areas, trail_rooms);
    std::cout << "[Debug][remove_and_connect] Completed reconnect attempt for isolated groups.\n";
}


void GenerateTrails::circular_connection(std::vector<std::unique_ptr<Room>>& trail_rooms,
                                         const std::string& map_dir,
                                         AssetLibrary* asset_lib,
                                         std::vector<Area>& existing_areas)
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

    while (!lineage_set.count(current) && fail_counter < 10) {
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
            std::string path = pick_random_asset();
            if (TrailGeometry::attempt_trail_connection(current, next, existing_areas, map_dir,
                                                        asset_lib, trail_rooms,
                                                        /*allowed_intersections=*/1,
                                                        path, testing, rng_)) {
                std::cout << "[Debug][circular_connection] Connected on attempt "
                          << attempt+1 << " using asset: " << path << "\n";
                current = next;
                connected = true;
                break;
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
