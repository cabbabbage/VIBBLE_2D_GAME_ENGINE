#include "asset_loader.hpp"
#include "asset_loader_internal.hpp"
#include <algorithm>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <chrono>
#include <limits>
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstdlib>
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
#include "world/world_grid.hpp"
#include "utils/grid.hpp"
#include "core/tile_builder.hpp"
#include <nlohmann/json.hpp>
#include "utils/loading_status_notifier.hpp"
#include "utils/log.hpp"
using json = nlohmann::json;

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
        vibble::log::info(std::string("[AssetLoader] Start for map '") + map_id_ + "' at root '" + map_path_ + "'.");
        using_shared_asset_library_ = (shared_asset_library != nullptr);
        if (using_shared_asset_library_) {
                asset_library_ = shared_asset_library;
        } else {
                owned_asset_library_ = std::make_unique<AssetLibrary>();
                asset_library_ = owned_asset_library_.get();
        }
        vibble::log::info(std::string("[AssetLoader] Asset library mode: ") + (using_shared_asset_library_ ? "shared" : "owned"));

        const auto overall_begin = std::chrono::steady_clock::now();

        const auto map_begin = std::chrono::steady_clock::now();
        loading_status::notify("Loading map data");
        load_from_manifest(map_manifest);
        const auto map_end = std::chrono::steady_clock::now();
        vibble::log::info(std::string("[AssetLoader] Map JSON parsed in ") + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(map_end - map_begin).count()) + "ms");

        const nlohmann::json& audio_manifest = map_manifest_json_.contains("audio") ? map_manifest_json_.at("audio") : nlohmann::json::object();
        try {
                const auto audio_begin = std::chrono::steady_clock::now();
                AudioEngine::instance().init(map_id_, audio_manifest, map_path_);
                const auto audio_end = std::chrono::steady_clock::now();
                vibble::log::info(std::string("[AssetLoader] Audio initialized in ") + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(audio_end - audio_begin).count()) + "ms");
        } catch (const std::exception& ex) {
                vibble::log::error(std::string("[AssetLoader] Audio init failed: ") + ex.what());
        } catch (...) {
                vibble::log::error("[AssetLoader] Audio init failed with unknown error.");
        }

        const auto library_begin = std::chrono::steady_clock::now();
        loading_status::notify("Loading assets");
        const auto library_end = std::chrono::steady_clock::now();
        if (asset_library_) {
                vibble::log::info(std::string("[AssetLoader] Asset library ready with ") + std::to_string(asset_library_->all().size()) + " known assets");
                vibble::log::debug(std::string("[AssetLoader] Asset library phase took ") + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(library_end - library_begin).count()) + "ms");
        }

        const auto rooms_begin = std::chrono::steady_clock::now();
        loading_status::notify("Creating map");
        try {
                loadRooms();
        } catch (const std::exception& ex) {
                vibble::log::error(std::string("[AssetLoader] loadRooms failed: ") + ex.what());
        } catch (...) {
                vibble::log::error("[AssetLoader] loadRooms failed with unknown error.");
        }
        const auto rooms_end = std::chrono::steady_clock::now();
        vibble::log::info(std::string("[AssetLoader] Rooms created: ") + std::to_string(rooms_.size()) + " in " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(rooms_end - rooms_begin).count()) + "ms");
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
                vibble::log::info(std::string("[AssetLoader] Preloading animations for used assets (") + std::to_string(preload_count) + ")...");
                asset_library_->loadAnimationsFor(renderer_, used);

                const auto preload_end = std::chrono::steady_clock::now();
                const double preload_ms = std::chrono::duration_cast<std::chrono::milliseconds>(preload_end - preload_begin).count();
                vibble::log::info(std::string("[AssetLoader] Preloaded animations for ") + std::to_string(preload_count) + " referenced assets in " + std::to_string(preload_ms) + "ms");
        } else {
                vibble::log::info("[AssetLoader] Using shared asset library cache; skipping per-map preload.");
        }
    }

    if (asset_library_) {
        if (renderer_) {
                try {
                        asset_library_->ensureAllAnimationsLoaded(renderer_);
                        vibble::log::info("[AssetLoader] Asset library warmup complete; animations cached in renderer.");
                } catch (const std::exception& ex) {
                        vibble::log::error(std::string("[AssetLoader] Asset library warmup failed: ") + ex.what());
                } catch (...) {
                        vibble::log::error("[AssetLoader] Asset library warmup failed with unknown error.");
                }
        } else {
                vibble::log::warn("[AssetLoader] Renderer unavailable; skipping asset library cache warmup.");
        }
    }
        loading_status::notify("Loading assets");
        vibble::log::info("[AssetLoader] Finalizing assets across rooms...");
        try {
                finalizeAssets();
        } catch (const std::exception& ex) {
                vibble::log::error(std::string("[AssetLoader] finalizeAssets threw: ") + ex.what());
        } catch (...) {
                vibble::log::error("[AssetLoader] finalizeAssets threw unknown error.");
        }
        vibble::log::info("[AssetLoader] Asset finalization completed; all assets are ready.");

        const auto overall_end = std::chrono::steady_clock::now();
        const double map_ms = std::chrono::duration_cast<std::chrono::milliseconds>(map_end - map_begin).count();
        const double library_ms = std::chrono::duration_cast<std::chrono::milliseconds>(library_end - library_begin).count();
        const double rooms_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rooms_end - rooms_begin).count();
        const double total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(overall_end - overall_begin).count();
        vibble::log::info(std::string("[AssetLoader] Map metadata loaded in ") + std::to_string(map_ms) + "ms");
        vibble::log::info(std::string("[AssetLoader] Asset library ready in ") + std::to_string(library_ms) + "ms");
        vibble::log::info(std::string("[AssetLoader] Rooms built in ") + std::to_string(rooms_ms) + "ms");
        vibble::log::info(std::string("[AssetLoader] Initialization completed in ") + std::to_string(total_ms) + "ms");
        auto distant_boundary = collectDistantAssets(150, 800);
        for (auto* asset : distant_boundary) {
                asset->set_hidden(true);
        }
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
        int considered = 0, skipped_type = 0, kept_in_room = 0, kept_in_zone = 0, removed = 0, locked = 0;

        for (Room* room : rooms_) {
                for (auto& asset_up : room->assets) {
                        Asset* asset = asset_up.get();
            if (!asset->info || asset->info->type != asset_types::boundary) {
                    ++skipped_type;
                    continue;
            }
                        ++considered;
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
                                ++kept_in_room;
                                continue;
                        }

                        if (asset_loader_internal::point_inside_any_zone(asset_point, zoneCache)) {
                                ++kept_in_zone;
                                continue;
                        }
                        double minDistSq = asset_loader_internal::min_distance_sq_to_zones(asset_point, zoneCache, remove_threshold);
                        double minDist = std::sqrt(minDistSq);

                        const bool should_lock = minDist > lock_distance;
                        const bool should_remove = minDist >= remove_distance;

                        if (asset && asset->info && asset->info->type == asset_types::player) {
                                asset->static_frame = false;
                        } else {
                                asset->static_frame = should_lock;
                        }
                        if (should_lock) ++locked;
                        if (should_remove) {
                                distant_assets.push_back(asset);
                                ++removed;
                                continue;
                        }
                }
        }

        vibble::log::debug(std::string("[AssetLoader] collectDistantAssets: considered=") + std::to_string(considered) + " removed=" + std::to_string(removed) + " locked=" + std::to_string(locked) + " kept_in_room=" + std::to_string(kept_in_room) + " kept_in_zone=" + std::to_string(kept_in_zone) + " skipped_non_boundary=" + std::to_string(skipped_type));

        return distant_assets;
}

void AssetLoader::loadRooms() {
        vibble::log::info("[AssetLoader] Starting room generation for map '" + map_id_ + "'");
        const double min_edge_distance = map_layers::min_edge_distance_from_map_manifest(map_manifest_json_);
        GenerateRooms generator(map_layers_, map_center_x_, map_center_y_, map_id_, map_manifest_json_, min_edge_distance, manifest_store_);
        nlohmann::json empty_boundary = nlohmann::json::object();
        nlohmann::json empty_rooms    = nlohmann::json::object();
        nlohmann::json empty_trails   = nlohmann::json::object();
        nlohmann::json empty_assets   = nlohmann::json::object();
        map_grid_settings_ = MapGridSettings::from_json(map_manifest_json_.contains("map_grid_settings") ? &map_manifest_json_["map_grid_settings"] : nullptr);
        MapGridSettings grid_settings = map_grid_settings_;
        nlohmann::json& map_assets_json = map_assets_data_ ? *map_assets_data_ : empty_assets;
        auto room_ptrs = generator.build( asset_library_, map_radius_, layer_radii_, map_boundary_data_ ? *map_boundary_data_ : empty_boundary, rooms_data_        ? *rooms_data_        : empty_rooms, trails_data_       ? *trails_data_       : empty_trails, map_assets_json, grid_settings);
        for (auto& up : room_ptrs) {
                rooms_.push_back(up.get());
                all_rooms_.push_back(std::move(up));
	}
        if (rooms_.empty()) {
                vibble::log::warn("[AssetLoader] Room generation returned no rooms; synthesizing a default spawn room.");
                try {
                        constexpr int kSpawnRadius = 1500;
                        const int diameter = kSpawnRadius * 2;
                        const int map_radius_int = map_radius_ > 0.0 ? static_cast<int>(std::lround(map_radius_)) : diameter;
                        const int mr = std::max(diameter, map_radius_int * 2);
                        SDL_Point center{mr / 2, mr / 2};

                        if (rooms_data_) {
                                nlohmann::json& spawn = (*rooms_data_)["spawn"];
                                if (!spawn.is_object()) spawn = nlohmann::json::object();
                                spawn["name"] = "spawn";
                                spawn["geometry"] = "Circle";
                                spawn["radius"] = kSpawnRadius;
                                spawn["min_radius"] = kSpawnRadius;
                                spawn["max_radius"] = kSpawnRadius;
                                spawn["min_width"] = diameter;
                                spawn["max_width"] = diameter;
                                spawn["min_height"] = diameter;
                                spawn["max_height"] = diameter;
                                spawn["edge_smoothness"] = 2;
                                spawn["is_spawn"] = true;
                                spawn["is_boss"] = false;
                                spawn["inherits_map_assets"] = false;
                                spawn["spawn_groups"] = nlohmann::json::array();
                        }
                        auto area = std::make_unique<Area>("spawn", center, diameter, diameter, std::string{"Circle"}, 2, mr, mr, 3);
                        nlohmann::json* rd_ptr = rooms_data_ ? &(*rooms_data_)["spawn"] : nullptr;
                        auto room = std::make_unique<Room>(Room::Point{center.x, center.y},
                                                           "room",
                                                           "spawn",
                                                           nullptr,
                                                           map_id_,
                                                           asset_library_,
                                                           area.get(),
                                                           rd_ptr,
                                                           &map_assets_json,
                                                           grid_settings,
                                                           static_cast<double>(mr / 2),
                                                           "rooms_data",
                                                           &map_manifest_json_,
                                                           manifest_store_,
                                                           map_id_);
                        room->layer = 0;
                        room->room_area = std::move(area);
                        rooms_.push_back(room.get());
                        all_rooms_.push_back(std::move(room));
                        vibble::log::info("[AssetLoader] Default spawn room synthesized.");
                } catch (const std::exception& ex) {
                        vibble::log::error(std::string("[AssetLoader] Fallback spawn synthesis failed: ") + ex.what());
                } catch (...) {
                        vibble::log::error("[AssetLoader] Fallback spawn synthesis failed with unknown error.");
                }
        } else {
                vibble::log::info("[AssetLoader] Room generation completed successfully: " + std::to_string(rooms_.size()) + " rooms created");
        }
        vibble::log::debug(std::string("[AssetLoader] loadRooms: rooms_=") + std::to_string(rooms_.size()));
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
                                ++finalized_assets;
                                ++room_finalized;
                        } catch (const std::exception& ex) {
                                vibble::log::error(std::string("[AssetLoader] finalizeAssets: exception during finalize_setup for '") + name + "': " + ex.what() + ". Skipping asset.");
                                asset_up.reset();
                                ++skipped_assets;
                                ++room_skipped;
                                continue;
                        } catch (...) {
                                vibble::log::error(std::string("[AssetLoader] finalizeAssets: unknown exception during finalize_setup for '") + name + "'. Skipping asset.");
                                asset_up.reset();
                                ++skipped_assets;
                                ++room_skipped;
                                continue;
                        }
                }

                if (room_total > 0) {
                        std::string msg = std::string("[AssetLoader] finalizeAssets: room=") + std::to_string(room_index) + " finalized " + std::to_string(room_finalized) + "/" + std::to_string(room_total);
                        if (room_skipped > 0) {
                                msg += std::string(" (skipped ") + std::to_string(room_skipped) + ")";
                        }
                        vibble::log::debug(msg);
                }

                ++room_index;
        }

        {
                std::string msg = std::string("[AssetLoader] finalizeAssets complete: ") + std::to_string(finalized_assets) + "/" + std::to_string(total_assets) + " assets ready";
                if (skipped_assets > 0) {
                        msg += std::string(" (") + std::to_string(skipped_assets) + " skipped)";
                }
                vibble::log::info(msg);
        }
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

void AssetLoader::createAssets(world::WorldGrid& grid) {
        const auto t0 = std::chrono::steady_clock::now();

        grid.set_chunk_resolution(std::max(0, map_grid_settings_.r_chunk));
        vibble::log::debug(std::string("[AssetLoader] createAssets: requested r_chunk=") + std::to_string(map_grid_settings_.r_chunk));

        auto extracted_assets = extract_all_assets();
        std::vector<Asset*> registered_assets;
        registered_assets.reserve(extracted_assets.size());
        vibble::log::info(std::string("[AssetLoader] Extracted ") + std::to_string(extracted_assets.size()) + " visible assets from rooms");

        for (auto& asset_up : extracted_assets) {
                if (!asset_up) continue;
                Asset* asset = grid.create_asset_at_point(std::move(asset_up));
                if (asset) {
                        registered_assets.push_back(asset);
                }
        }
        vibble::log::debug(std::string("[AssetLoader] Registered assets: total=") + std::to_string(registered_assets.size()));

        const auto t1 = std::chrono::steady_clock::now();

        {
            loader_tiles::build_grid_tiles(renderer_, grid, map_grid_settings_, registered_assets);
        }

        vibble::log::debug(std::string("[AssetLoader] createAssets total ") + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()) + "ms");
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

void AssetLoader::load_from_manifest(const nlohmann::json& map_manifest) {
        map_manifest_json_ = map_manifest;
        if (!map_manifest_json_.is_object()) {
                map_manifest_json_ = nlohmann::json::object();
        }

        ensure_map_grid_settings(map_manifest_json_);

        map_assets_data_   = &map_manifest_json_["map_assets_data"];
        if (!map_assets_data_->is_object()) *map_assets_data_ = nlohmann::json::object();
        map_boundary_data_ = &map_manifest_json_["map_boundary_data"];
        if (!map_boundary_data_->is_object()) *map_boundary_data_ = nlohmann::json::object();
        rooms_data_        = &map_manifest_json_["rooms_data"];
        if (!rooms_data_->is_object()) *rooms_data_ = nlohmann::json::object();
        trails_data_       = &map_manifest_json_["trails_data"];
        if (!trails_data_->is_object()) *trails_data_ = nlohmann::json::object();

        try {
                auto ml_it = map_manifest_json_.find("map_layers");
                const bool missing_or_empty = (ml_it == map_manifest_json_.end()) || !ml_it->is_array() || ml_it->empty();
                if (missing_or_empty) {

                        std::string spawn_name;
                        if (rooms_data_ && rooms_data_->is_object()) {
                                for (auto it = rooms_data_->begin(); it != rooms_data_->end(); ++it) {
                                        if (it.value().is_object() && it.value().value("is_spawn", false)) {
                                                spawn_name = it.key();
                                                break;
                                        }
                                }
                                if (spawn_name.empty() && rooms_data_->contains("spawn")) {
                                        spawn_name = "spawn";
                                }
                        }
                        if (spawn_name.empty()) {
                                spawn_name = "spawn";

                                nlohmann::json& rd = *rooms_data_;
                                nlohmann::json& spawn_entry = rd[spawn_name];
                                if (!spawn_entry.is_object() || spawn_entry.empty()) {

                                        constexpr int kSpawnRadius = 1500;
                                        const int diameter = kSpawnRadius * 2;
                                        spawn_entry = nlohmann::json::object();
                                        spawn_entry["name"]                 = spawn_name;
                                        spawn_entry["geometry"]             = "Circle";
                                        spawn_entry["radius"]               = kSpawnRadius;
                                        spawn_entry["min_radius"]           = kSpawnRadius;
                                        spawn_entry["max_radius"]           = kSpawnRadius;
                                        spawn_entry["min_width"]            = diameter;
                                        spawn_entry["max_width"]            = diameter;
                                        spawn_entry["min_height"]           = diameter;
                                        spawn_entry["max_height"]           = diameter;
                                        spawn_entry["edge_smoothness"]      = 2;
                                        spawn_entry["is_spawn"]             = true;
                                        spawn_entry["is_boss"]              = false;
                                        spawn_entry["inherits_map_assets"]  = false;
                                        spawn_entry["spawn_groups"]         = nlohmann::json::array();
                                }
                        }

                        nlohmann::json rooms_arr = nlohmann::json::array();
                        nlohmann::json spawn_spec;
                        spawn_spec["name"]               = spawn_name;
                        spawn_spec["max_instances"]      = 1;
                        spawn_spec["required_children"]  = nlohmann::json::array();
                        rooms_arr.push_back(std::move(spawn_spec));

                        nlohmann::json inferred_layer;
                        inferred_layer["level"]     = 0;
                        inferred_layer["max_rooms"] = 1;
                        inferred_layer["rooms"]     = std::move(rooms_arr);

                        map_manifest_json_["map_layers"] = nlohmann::json::array({ inferred_layer });
                        vibble::log::info(std::string("[AssetLoader] Inferred default map_layers for blank map '") + map_id_ + "'.");
                }
        } catch (...) {

        }

        auto layers_it = map_manifest_json_.find("map_layers");
        map_layers::LayerRadiiResult radii_result;
        const nlohmann::json* rooms_data_ptr = rooms_data_;
        if (layers_it != map_manifest_json_.end()) {
                const double min_edge = map_layers::min_edge_distance_from_map_manifest(map_manifest_json_);
                radii_result = map_layers::compute_layer_radii(*layers_it, rooms_data_ptr, min_edge);
        }

        map_radius_   = radii_result.map_radius;
        map_center_x_ = map_center_y_ = map_radius_;
        layer_radii_  = radii_result.layer_radii;
        if (layers_it != map_manifest_json_.end() && layers_it->is_array()) {
                for (std::size_t idx = 0; idx < layers_it->size(); ++idx) {
                        auto& layer_entry = (*layers_it)[idx];
                        if (!layer_entry.is_object()) {
                                continue;
                        }
                        const double ring_radius = idx < radii_result.layer_radii.size() ? radii_result.layer_radii[idx] : 0.0;
                        const double extent_value = idx < radii_result.layer_extents.size() ? radii_result.layer_extents[idx] : 0.0;
                        layer_entry["ring_radius"] = ring_radius;
                        layer_entry["bounding_extent"] = extent_value;
                }
        }
        map_manifest_json_["map_layers_settings"]["min_edge_distance"] = radii_result.min_edge_distance;
        map_layers_.clear();

        if (layers_it != map_manifest_json_.end() && layers_it->is_array()) {
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
                                                                        vibble::log::warn(std::string("[AssetLoader] Room '") + rs.name + "' has non-string entry in 'required_children'; skipping.");
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

        vibble::log::debug(std::string("[AssetLoader] load_from_manifest: map_radius_=") + std::to_string(map_radius_) + " layers=" + std::to_string(map_layers_.size()));
}
