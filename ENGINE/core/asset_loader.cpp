#include "asset_loader.hpp"
#include "asset_loader_internal.hpp"
#include <algorithm>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <limits>
#include <cstdint>
#include <string>
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
#include "util/grid.hpp"
#include <nlohmann/json.hpp>
#include "utils/loading_status_notifier.hpp"
#include "utils/log.hpp"
using json = nlohmann::json;

namespace {

        // Allow very large full-map static light textures. 512 MiB default ceiling.
        // 3072x4096 RGBA ~ 48 MiB; even 8k maps stay within this in most cases.
        constexpr std::int64_t kMaxStaticLightTextureBytes = 512LL * 1024LL * 1024LL; // 512 MiB ceiling
        constexpr std::int64_t kBytesPerPixel              = static_cast<std::int64_t>(sizeof(std::uint32_t));
        constexpr std::int64_t kMaxStaticLightPixels       =
            kMaxStaticLightTextureBytes / (kBytesPerPixel > 0 ? kBytesPerPixel : 1);
        static_assert(kMaxStaticLightPixels > 0, "Max static light pixels must be positive.");

        std::int64_t floor_div64(std::int64_t value, std::int64_t step) {
                if (step == 0) {
                        return 0;
                }
                const std::int64_t quotient  = value / step;
                const std::int64_t remainder = value % step;
                if (remainder == 0) {
                        return quotient;
                }
                if ((remainder < 0) != (step < 0)) {
                        return quotient - 1;
                }
                return quotient;
        }

        int clamp_to_int(std::int64_t value) {
                if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min())) {
                        return std::numeric_limits<int>::min();
                }
                if (value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
                        return std::numeric_limits<int>::max();
                }
                return static_cast<int>(value);
        }

        // Small helper to stringify a rect in logs
        inline std::string rect_str(const SDL_Rect& r) {
                return std::string("{x=") + std::to_string(r.x) + ", y=" + std::to_string(r.y) +
                       ", w=" + std::to_string(r.w) + ", h=" + std::to_string(r.h) + "}";
        }

        // Rate-limit noisy per-item logs (e.g., first N, then every Kth)
        struct LogLimiter {
                int first_n = 10;
                int every_k = 100;
                bool operator()(int i) const {
                        if (i < first_n) return true;
                        return (i % every_k) == 0;
                }
        };

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
        vibble::log::info(std::string("[AssetLoader] Start for map '") + map_id_ + "'.");
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
        vibble::log::debug(std::string("[AssetLoader] Map JSON parsed for '") + map_id_ + "'.");

        const nlohmann::json& audio_manifest = map_info_json_.contains("audio") ? map_info_json_.at("audio") : nlohmann::json::object();
        AudioEngine::instance().init(map_id_, audio_manifest, map_path_);

        const auto library_begin = std::chrono::steady_clock::now();
        loading_status::notify("Loading assets");
        const auto library_end = std::chrono::steady_clock::now();
        if (asset_library_) {
                vibble::log::info(std::string("[AssetLoader] Asset library ready with ") + std::to_string(asset_library_->all().size()) + " known assets");
        }

        const auto rooms_begin = std::chrono::steady_clock::now();
        loading_status::notify("Creating map");
        loadRooms();
        const auto rooms_end = std::chrono::steady_clock::now();
        vibble::log::info(std::string("[AssetLoader] Rooms created: ") + std::to_string(rooms_.size()));
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
                vibble::log::info(std::string("[AssetLoader] Preloaded animations for ") + std::to_string(preload_count) +
                          " referenced assets in " + std::to_string(preload_ms) + "ms");
        } else {
                vibble::log::info("[AssetLoader] Using shared asset library cache; skipping per-map preload.");
        }
    }

    if (asset_library_) {
        if (renderer_) {
                asset_library_->ensureAllAnimationsLoaded(renderer_);
                vibble::log::info("[AssetLoader] Asset library warmup complete; animations cached in renderer.");
        } else {
                vibble::log::warn("[AssetLoader] Renderer unavailable; skipping asset library cache warmup.");
        }
    }
        loading_status::notify("Loading assets");
        vibble::log::info("[AssetLoader] Finalizing assets across rooms...");
        finalizeAssets();
        assets_finalized_ = true;
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
        auto distant_boundary = collectDistantAssets(150, 400);
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

                        asset->static_frame = should_lock;
                        if (should_lock) ++locked;
                        if (should_remove) {
                                distant_assets.push_back(asset);
                                ++removed;
                                continue;
                        }
                }
        }

        vibble::log::debug(std::string("[AssetLoader] collectDistantAssets: considered=") + std::to_string(considered) +
                           " removed=" + std::to_string(removed) +
                           " locked=" + std::to_string(locked) +
                           " kept_in_room=" + std::to_string(kept_in_room) +
                           " kept_in_zone=" + std::to_string(kept_in_zone) +
                           " skipped_non_boundary=" + std::to_string(skipped_type));

        return distant_assets;
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
                        } catch (const std::exception& ex) {
                                vibble::log::error(std::string("[AssetLoader] finalizeAssets: exception during finalize_setup for '") + name + "': " + ex.what());
                                throw;
                        } catch (...) {
                                vibble::log::error(std::string("[AssetLoader] finalizeAssets: unknown exception during finalize_setup for '") + name + "'");
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
                        vibble::log::debug(msg);
                }

                ++room_index;
        }

        {
                std::string msg = std::string("[AssetLoader] finalizeAssets complete: ") + std::to_string(finalized_assets) +
                                  "/" + std::to_string(total_assets) + " assets ready";
                if (skipped_assets > 0) {
                        msg += std::string(" (") + std::to_string(skipped_assets) + " skipped)";
                }
                vibble::log::info(msg);
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
        const auto t0 = std::chrono::steady_clock::now();

        grid.set_chunk_resolution(std::max(0, map_grid_settings_.r_chunk));
        vibble::log::debug(std::string("[AssetLoader] createAssets: requested r_chunk=") + std::to_string(map_grid_settings_.r_chunk));

        spawned_assets_ = extract_all_assets();
        vibble::log::info(std::string("[AssetLoader] Extracted ") + std::to_string(spawned_assets_.size()) + " visible assets from rooms");

        int lit_count = 0;
        int static_lights = 0;
        for (const auto& asset_up : spawned_assets_) {
                Asset* asset = asset_up.get();
                if (!asset) continue;
                grid.register_asset(asset);
                if (asset->info && !asset->info->light_sources.empty()) {
                        ++lit_count;
                        if (!asset->info->moving_asset) ++static_lights;
                }
        }
        vibble::log::debug(std::string("[AssetLoader] Registered assets: total=") + std::to_string(spawned_assets_.size()) +
                           " with_lights=" + std::to_string(lit_count) +
                           " static_light_assets=" + std::to_string(static_lights));

        const auto t_plan_begin = std::chrono::steady_clock::now();
        plan_map_chunks(grid);
        const auto t_plan_end = std::chrono::steady_clock::now();
        vibble::log::info(std::string("[AssetLoader] plan_map_chunks finished; planned=") + std::to_string(planned_chunks_.size()) +
                          " in " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t_plan_end - t_plan_begin).count()) + "ms");

        // Defer static light precomputation until assets are spawned and handed off
        vibble::log::info("[AssetLoader] Deferring full-map light map texture creation until assets are spawned.");

        const auto t1 = std::chrono::steady_clock::now();
        vibble::log::debug(std::string("[AssetLoader] createAssets total ") +
                           std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()) + "ms");
}

std::vector<std::unique_ptr<Asset>> AssetLoader::take_spawned_assets() {
        assets_extracted_ = true;
        return std::move(spawned_assets_);
}

void AssetLoader::bake_light_map(world::Grid& grid) {
        if (!assets_finalized_) {
                vibble::log::warn("[AssetLoader] bake_light_map called before finalizeAssets; proceeding cautiously.");
        }
        if (!assets_extracted_) {
                vibble::log::warn("[AssetLoader] bake_light_map called before assets were handed off; proceeding anyway.");
        }
        if (planned_chunks_.empty()) {
                plan_map_chunks(grid);
        }
        vibble::log::info("[AssetLoader] Beginning full-map light map texture creation...");
        const auto t_bake_begin = std::chrono::steady_clock::now();
        precompute_light_map(grid);
        const auto t_bake_end = std::chrono::steady_clock::now();
        vibble::log::info(std::string("[AssetLoader] precompute_light_map finished in ") +
                          std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t_bake_end - t_bake_begin).count()) + "ms");
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

        vibble::log::debug(std::string("[AssetLoader] load_map_json: map_radius_=") + std::to_string(map_radius_) +
                           " layers=" + std::to_string(map_layers_.size()));
}

void AssetLoader::precompute_light_map(world::Grid& grid) {
        precomputed_light_map_.reset();

        if (planned_chunks_.empty()) {
                vibble::log::info("[AssetLoader] precompute_light_map: no planned chunks; skipping static lighting precomputation.");
                map_chunks_.clear();
                return;
        }

        const auto t0 = std::chrono::steady_clock::now();

        bool have_bounds = false;
        int  min_x      = 0;
        int  min_y      = 0;
        int  max_x      = 0;
        int  max_y      = 0;
        for (const PlannedChunk& planned : planned_chunks_) {
                const SDL_Rect& bounds = planned.world_bounds;
                if (!have_bounds) {
                        min_x      = bounds.x;
                        min_y      = bounds.y;
                        max_x      = bounds.x + bounds.w;
                        max_y      = bounds.y + bounds.h;
                        have_bounds = true;
                } else {
                        min_x = std::min(min_x, bounds.x);
                        min_y = std::min(min_y, bounds.y);
                        max_x = std::max(max_x, bounds.x + bounds.w);
                        max_y = std::max(max_y, bounds.y + bounds.h);
                }
        }

        if (!have_bounds) {
                vibble::log::warn("[AssetLoader] precompute_light_map: unable to derive full-map bounds from planned chunks.");
                map_chunks_.clear();
                return;
        }

        std::int64_t width64  = static_cast<std::int64_t>(max_x) - static_cast<std::int64_t>(min_x);
        std::int64_t height64 = static_cast<std::int64_t>(max_y) - static_cast<std::int64_t>(min_y);

        if (width64 <= 0 || height64 <= 0) {
                vibble::log::warn(std::string("[AssetLoader] precompute_light_map: invalid full-map dimensions (") +
                                  std::to_string(width64) + "x" + std::to_string(height64) + "); skipping static lighting.");
                map_chunks_.clear();
                return;
        }

        bool size_overflow = false;
        if (width64 > 0 && height64 > 0) {
                size_overflow = width64 > std::numeric_limits<std::int64_t>::max() / height64;
        } else {
                size_overflow = true;
        }

        if (size_overflow) {
                vibble::log::warn(std::string("[AssetLoader] precompute_light_map: size overflow for full texture (") +
                                  std::to_string(width64) + "x" + std::to_string(height64) + ").");
        }

        const std::int64_t pixel_count64 = size_overflow ? 0 : width64 * height64;

        bool can_create_full_texture = renderer_ && !size_overflow;
        if (!renderer_) {
                vibble::log::warn("[AssetLoader] Renderer unavailable; skipping full-map static lighting texture creation.");
        }

        if (width64 > std::numeric_limits<int>::max() || height64 > std::numeric_limits<int>::max()) {
                vibble::log::warn(std::string("[AssetLoader] precompute_light_map: full texture dimensions exceed SDL limits (") +
                                  std::to_string(width64) + "x" + std::to_string(height64) + ").");
                can_create_full_texture = false;
        }

        if (!size_overflow && pixel_count64 > kMaxStaticLightPixels) {
                vibble::log::warn(std::string("[AssetLoader] precompute_light_map: full texture exceeds size cap (") +
                                  std::to_string(width64) + "x" + std::to_string(height64) + ").");
                can_create_full_texture = false;
        }

        const std::int64_t clamped_width64  = std::clamp<std::int64_t>(width64, 1, static_cast<std::int64_t>(std::numeric_limits<int>::max()));
        const std::int64_t clamped_height64 = std::clamp<std::int64_t>(height64, 1, static_cast<std::int64_t>(std::numeric_limits<int>::max()));
        const int          full_width       = static_cast<int>(clamped_width64);
        const int          full_height      = static_cast<int>(clamped_height64);
        SDL_Rect  full_world_bounds{min_x, min_y, full_width, full_height};

        SDL_Texture* full_texture = nullptr;
        int          lights_considered = 0;
        int          lights_drawn      = 0;

        if (can_create_full_texture) {
                SDL_Texture* created = SDL_CreateTexture(renderer_,
                                                         SDL_PIXELFORMAT_RGBA8888,
                                                         SDL_TEXTUREACCESS_TARGET,
                                                         full_width,
                                                         full_height);
                if (!created) {
                        vibble::log::warn(std::string("[AssetLoader] precompute_light_map: SDL_CreateTexture failed: ") + SDL_GetError());
                        can_create_full_texture = false;
                } else {
                        full_texture = created;
                        SDL_SetTextureBlendMode(full_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
                        SDL_SetTextureScaleMode(full_texture, SDL_ScaleModeBest);
#endif

                        SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
                        if (SDL_SetRenderTarget(renderer_, full_texture) != 0) {
                                vibble::log::warn(std::string("[AssetLoader] precompute_light_map: SDL_SetRenderTarget failed: ") + SDL_GetError());
                                SDL_DestroyTexture(full_texture);
                                full_texture          = nullptr;
                                can_create_full_texture = false;
                                SDL_SetRenderTarget(renderer_, previous_target);
                        } else {
                                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
                                SDL_RenderClear(renderer_);

#if SDL_VERSION_ATLEAST(2, 0, 6)
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

                                LogLimiter limiter{10, 100};

                                // Iterate grid-registered assets chunk-by-chunk to avoid dependence
                                // on internal transient vectors and ensure correctness after spawn.
                                for (const PlannedChunk& planned_src : planned_chunks_) {
                                        world::Chunk& src_chunk = grid.get_or_create_chunk_ij(planned_src.i, planned_src.j);
                                        for (Asset* asset : src_chunk.assets) {
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

                                                const int draw_w = std::max(1, src_w);
                                                const int draw_h = std::max(1, src_h);
                                                SDL_Rect world_dst{
                                                    asset->pos.x + light.offset_x - draw_w / 2,
                                                    asset->pos.y + light.offset_y - draw_h / 2,
                                                    draw_w,
                                                    draw_h};

                                                ++lights_considered;

                                                SDL_Rect intersection{};
                                                if (!SDL_IntersectRect(&world_dst, &full_world_bounds, &intersection)) {
                                                        if (limiter(lights_considered - 1)) {
                                                                vibble::log::debug(std::string("[AssetLoader] Full-map light skipped (out of bounds) dst=") + rect_str(world_dst));
                                                        }
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
                                                local_dst.x -= full_world_bounds.x;
                                                local_dst.y -= full_world_bounds.y;

                                                if (limiter(lights_drawn)) {
                                                        vibble::log::debug(std::string("[AssetLoader] Stamping static light onto full map dst=") + rect_str(local_dst));
                                                }

                                                SDL_RenderCopy(renderer_, tex, nullptr, &local_dst);
                                                ++lights_drawn;

                                                SDL_SetTextureBlendMode(tex, save_bm);
                                                SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                                                SDL_SetTextureAlphaMod(tex, save_a);
                                                }
                                        }
                                }

                                SDL_SetRenderTarget(renderer_, previous_target);

                                auto precomputed = std::make_unique<PrecomputedLightMap>();
                                precomputed->map_width    = full_width;
                                precomputed->map_height   = full_height;
                                precomputed->full_texture = full_texture;
                                precomputed_light_map_    = std::move(precomputed);

                                vibble::log::info(std::string("[AssetLoader] Full-map static light texture built: size=") +
                                                  std::to_string(full_width) + "x" + std::to_string(full_height) +
                                                  " lights_considered=" + std::to_string(lights_considered) +
                                                  " drawn=" + std::to_string(lights_drawn));
                        }
                }
        }

        map_chunks_.clear();
        map_chunks_.reserve(planned_chunks_.size());

        int baked_chunks   = 0;
        int skipped_chunks = 0;

        SDL_BlendMode full_save_blend = SDL_BLENDMODE_BLEND;
        if (renderer_ && full_texture) {
                SDL_GetTextureBlendMode(full_texture, &full_save_blend);
                SDL_SetTextureBlendMode(full_texture, SDL_BLENDMODE_NONE);
        }

        const int full_src_width  = full_world_bounds.w;
        const int full_src_height = full_world_bounds.h;

        // Heuristic: avoid GPU readbacks from render targets when the full-map mask is very large.
        // Some drivers are unstable when calling SDL_RenderReadPixels repeatedly with large RTs.
        const bool avoid_readback = (full_src_width >= 3072 || full_src_height >= 3072);

        auto estimate_chunk_brightness = [&](const SDL_Rect& bounds) -> float {
                if (bounds.w <= 0 || bounds.h <= 0) return 0.0f;
                const double chunk_area = static_cast<double>(bounds.w) * static_cast<double>(bounds.h);
                if (chunk_area <= 0.0) return 0.0f;
                double lit_area = 0.0;
                for (const auto& asset_up : spawned_assets_) {
                        const Asset* asset = asset_up.get();
                        if (!asset || !asset->info) continue;
                        if (asset->info->light_sources.empty() || asset->info->moving_asset) continue;
                        for (const auto& light : asset->info->light_sources) {
                                SDL_Texture* tex = light.texture;
                                if (!tex) continue;
                                int src_w = light.cached_w > 0 ? light.cached_w : 0;
                                int src_h = light.cached_h > 0 ? light.cached_h : 0;
                                if (src_w <= 0 || src_h <= 0) SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
                                if (src_w <= 0 || src_h <= 0) continue;
                                const int draw_w = std::max(1, src_w);
                                const int draw_h = std::max(1, src_h);
                                SDL_Rect world_dst{
                                    asset->pos.x + light.offset_x - draw_w / 2,
                                    asset->pos.y + light.offset_y - draw_h / 2,
                                    draw_w,
                                    draw_h};
                                SDL_Rect intersect{};
                                if (SDL_IntersectRect(&world_dst, &bounds, &intersect)) {
                                        lit_area += static_cast<double>(intersect.w) * static_cast<double>(intersect.h);
                                }
                        }
                }
                const double coverage = std::clamp(lit_area / chunk_area, 0.0, 1.0);
                // Map coverage to brightness conservatively (lights carve darkness):
                return static_cast<float>(coverage);
        };

        // Helper: stamp all static lights that intersect a destination rect, using the same
        // erase-alpha custom blend used for the full-map path.
        auto stamp_static_lights_into = [&](const world::Chunk& src_chunk,
                                            SDL_Texture* dst_target,
                                            const SDL_Rect& dst_world_bounds) -> std::pair<int,int> {
                if (!renderer_ || !dst_target) return {0,0};
                SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
                if (SDL_SetRenderTarget(renderer_, dst_target) != 0) {
                        vibble::log::warn(std::string("[AssetLoader] stamp_static_lights_into: SDL_SetRenderTarget failed: ") + SDL_GetError());
                        SDL_SetRenderTarget(renderer_, previous_target);
                        return {0,0};
                }

                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
                SDL_RenderClear(renderer_);

#if SDL_VERSION_ATLEAST(2, 0, 6)
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

                int considered = 0;
                int drawn      = 0;
                LogLimiter limiter{10, 100};
                for (Asset* a_ptr : src_chunk.assets) {
                        const Asset* asset = a_ptr;
                        if (!asset || !asset->info) continue;
                        if (asset->info->light_sources.empty() || asset->info->moving_asset) continue;
                        for (const auto& light : asset->info->light_sources) {
                                SDL_Texture* tex = light.texture;
                                if (!tex) continue;
                                int src_w = light.cached_w > 0 ? light.cached_w : 0;
                                int src_h = light.cached_h > 0 ? light.cached_h : 0;
                                if (src_w <= 0 || src_h <= 0) {
                                        SDL_QueryTexture(tex, nullptr, nullptr, &src_w, &src_h);
                                }
                                if (src_w <= 0 || src_h <= 0) continue;

                                const int draw_w = std::max(1, src_w);
                                const int draw_h = std::max(1, src_h);
                                SDL_Rect world_dst{
                                    asset->pos.x + light.offset_x - draw_w / 2,
                                    asset->pos.y + light.offset_y - draw_h / 2,
                                    draw_w,
                                    draw_h};

                                ++considered;

                                SDL_Rect intersection{};
                                if (!SDL_IntersectRect(&world_dst, &dst_world_bounds, &intersection)) {
                                        if (limiter(considered - 1)) {
                                                vibble::log::debug(std::string("[AssetLoader] Chunk-light skipped (out of bounds) dst=") + rect_str(world_dst));
                                        }
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

                                // Convert to local (chunk) coords and clip to target to avoid negative dst rects
                                SDL_Rect local_dst = world_dst;
                                local_dst.x -= dst_world_bounds.x;
                                local_dst.y -= dst_world_bounds.y;

                                SDL_Rect dst_clip{0, 0, dst_world_bounds.w, dst_world_bounds.h};
                                SDL_Rect clipped_dst{};
                                if (!SDL_IntersectRect(&local_dst, &dst_clip, &clipped_dst)) {
                                        continue;
                                }

                                // No scaling: src_w/h == draw_w/h. Compute matching src sub-rect.
                                // Offset from unclipped dst to clipped dst becomes src offset
                                const int off_x = clipped_dst.x - local_dst.x;
                                const int off_y = clipped_dst.y - local_dst.y;
                                int tex_w = 0, tex_h = 0;
                                SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h);
                                // Prefer actual query for safety; fall back to src_w/h if query fails.
                                if (tex_w <= 0) tex_w = src_w;
                                if (tex_h <= 0) tex_h = src_h;
                                SDL_Rect src_rect{ std::clamp(off_x, 0, tex_w),
                                                   std::clamp(off_y, 0, tex_h),
                                                   std::clamp(clipped_dst.w, 0, std::max(0, tex_w - std::clamp(off_x, 0, tex_w))),
                                                   std::clamp(clipped_dst.h, 0, std::max(0, tex_h - std::clamp(off_y, 0, tex_h))) };
                                if (src_rect.w <= 0 || src_rect.h <= 0) {
                                        continue;
                                }

                                if (limiter(drawn)) {
                                        vibble::log::debug(std::string("[AssetLoader] Stamping static light into chunk dst=") + rect_str(clipped_dst));
                                }

                                SDL_RenderCopy(renderer_, tex, &src_rect, &clipped_dst);
                                ++drawn;

                                SDL_SetTextureBlendMode(tex, save_bm);
                                SDL_SetTextureColorMod(tex, save_r, save_g, save_b);
                                SDL_SetTextureAlphaMod(tex, save_a);
                        }
                }

                SDL_SetRenderTarget(renderer_, previous_target);
                return {considered, drawn};
        };

        for (const PlannedChunk& planned : planned_chunks_) {
                world::Chunk& chunk = grid.get_or_create_chunk_ij(planned.i, planned.j);
                chunk.base_brightness     = 0.0f;
                chunk.brightness_strength = 1.0f;
                chunk.opacity_strength    = 1.0f;
                chunk.scale_strength      = 1.0f;
                chunk.offset_x            = 0;
                chunk.offset_y            = 0;
                chunk.shadow              = {};
                chunk.has_dynamic_overlay = false;
                chunk.lighting_dirty      = true;
                chunk.light.min_static_avg_strength = 0.0f;
                chunk.light.max_static_avg_strength = 1.0f;
                chunk.light.needs_update            = true;
                map_chunks_.push_back(&chunk);

                if (!renderer_) {
                        ++skipped_chunks;
                        continue;
                }

                if (chunk.static_light_map) {
                        SDL_DestroyTexture(chunk.static_light_map);
                        chunk.static_light_map = nullptr;
                }

                const SDL_Rect& bounds = planned.world_bounds;

                SDL_Texture* chunk_texture = SDL_CreateTexture(renderer_,
                                                                 SDL_PIXELFORMAT_RGBA8888,
                                                                 SDL_TEXTUREACCESS_TARGET,
                                                                 bounds.w,
                                                                 bounds.h);
                if (!chunk_texture) {
                        vibble::log::warn(std::string("[AssetLoader] Failed to create chunk texture for (") +
                                          std::to_string(planned.i) + ", " + std::to_string(planned.j) + "): " + SDL_GetError());
                        ++skipped_chunks;
                        continue;
                }
                SDL_SetTextureBlendMode(chunk_texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
                SDL_SetTextureScaleMode(chunk_texture, SDL_ScaleModeBest);
#endif

                if (full_texture) {
                        SDL_Rect src{bounds.x - full_world_bounds.x,
                                      bounds.y - full_world_bounds.y,
                                      bounds.w,
                                      bounds.h};

                        if (src.x < 0 || src.y < 0 || src.x + src.w > full_src_width || src.y + src.h > full_src_height) {
                                vibble::log::warn(std::string("[AssetLoader] Chunk (") + std::to_string(planned.i) + ", " + std::to_string(planned.j) +
                                                  ") requested out-of-range copy from full map; will bake locally.");
                                // Fall back to local bake for this chunk only.
                                auto [c,d] = stamp_static_lights_into(chunk, chunk_texture, bounds);
                                (void)c; (void)d;
                        } else {
                                SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
                                if (SDL_SetRenderTarget(renderer_, chunk_texture) != 0) {
                                        vibble::log::warn(std::string("[AssetLoader] Failed to set chunk render target (") +
                                                          std::to_string(planned.i) + ", " + std::to_string(planned.j) + "): " + SDL_GetError());
                                        SDL_DestroyTexture(chunk_texture);
                                        SDL_SetRenderTarget(renderer_, previous_target);
                                        ++skipped_chunks;
                                        continue;
                                }

                                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                                SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
                                SDL_RenderClear(renderer_);

                                if (SDL_RenderCopy(renderer_, full_texture, &src, nullptr) != 0) {
                                        vibble::log::warn(std::string("[AssetLoader] Failed to copy full-map lighting into chunk (") +
                                                          std::to_string(planned.i) + ", " + std::to_string(planned.j) + "): " + SDL_GetError());
                                        SDL_SetRenderTarget(renderer_, previous_target);
                                        SDL_DestroyTexture(chunk_texture);
                                        ++skipped_chunks;
                                        continue;
                                }

                                SDL_SetRenderTarget(renderer_, previous_target);
                        }
                } else {
                        // Full-map texture unavailable: bake static lights directly into this chunk.
                        auto [considered, drawn] = stamp_static_lights_into(chunk, chunk_texture, bounds);
                        if (drawn == 0) {
                                vibble::log::debug(std::string("[AssetLoader] Local-baked chunk had no intersecting static lights (") +
                                                   std::to_string(planned.i) + ", " + std::to_string(planned.j) + ")");
                        }
                }

                chunk.static_light_map = chunk_texture;
                chunk.lighting_dirty   = false;

                if (avoid_readback) {
                        chunk.base_brightness = estimate_chunk_brightness(bounds);
                } else {
                        chunk.base_brightness = compute_chunk_average_brightness(chunk.static_light_map);
                        // Fallback to estimate if readback returned 0 and we likely had lights
                        if (chunk.base_brightness <= 0.0f) {
                                float est = estimate_chunk_brightness(bounds);
                                if (est > 0.0f) chunk.base_brightness = est;
                        }
                }
                chunk.base_brightness = std::clamp(chunk.base_brightness, 0.0f, 1.0f);
                chunk.light.min_static_avg_strength = std::min(1.0f, chunk.base_brightness);
                chunk.light.max_static_avg_strength = std::max(1.0f, chunk.base_brightness);
                chunk.light.needs_update            = true;

                ++baked_chunks;
        }

        if (renderer_ && full_texture) {
                // Restore blend mode, then immediately free the large full-map mask to reduce GPU memory pressure
                SDL_SetTextureBlendMode(full_texture, full_save_blend);
                SDL_DestroyTexture(full_texture);
                full_texture = nullptr;
                if (precomputed_light_map_) {
                        // We do not need to carry the full texture forward; chunk baking is complete.
                        precomputed_light_map_->full_texture = nullptr;
                }
        }

        const auto t1 = std::chrono::steady_clock::now();
        vibble::log::info(std::string("[AssetLoader] Completed light map precomputation for all chunks. baked=") +
                          std::to_string(baked_chunks) +
                          " skipped=" + std::to_string(skipped_chunks) +
                          " total_ms=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()));
}

void AssetLoader::plan_map_chunks(const world::Grid& grid) {
        vibble::log::debug("[AssetLoader] plan_map_chunks: begin");

        planned_chunks_.clear();
        map_chunks_.clear();

        const int requested_r_chunk = grid.chunk_resolution();
        const int r_chunk = std::clamp(requested_r_chunk, 0, vibble::grid::kMaxResolution);
        if (requested_r_chunk != r_chunk) {
                vibble::log::warn(std::string("[AssetLoader] Chunk resolution request ") +
                                  std::to_string(requested_r_chunk) + " exceeded limits; using " +
                                  std::to_string(r_chunk) + ".");
        }

        const std::int64_t step64 = std::int64_t{1} << r_chunk;
        if (step64 <= 0 || step64 > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
                vibble::log::warn("[AssetLoader] Skipping chunk planning due to unsupported chunk size.");
                return;
        }
        const int step = static_cast<int>(step64);

        const int chunk_size_px = vibble::grid::delta(r_chunk);
        vibble::log::debug(std::string("[AssetLoader] plan_map_chunks: map='") + map_id_ +
                           "' requested_r_chunk=" + std::to_string(requested_r_chunk) +
                           " clamped_r_chunk=" + std::to_string(r_chunk) +
                           " chunk_size_px=" + std::to_string(chunk_size_px) +
                           " (assets=" + std::to_string(spawned_assets_.size()) + ")");

        if (chunk_size_px < 16 || chunk_size_px > 2048) {
                vibble::log::warn(std::string("[AssetLoader] Chunk size outside expected range: ") +
                                  std::to_string(chunk_size_px) + "px; manifest may need review.");
        }

        SDL_Point origin = grid.origin();
        const std::int64_t origin_x64 = static_cast<std::int64_t>(origin.x);
        const std::int64_t origin_y64 = static_cast<std::int64_t>(origin.y);
        vibble::log::debug(std::string("[AssetLoader] Grid origin: {x=") + std::to_string(origin.x) + ", y=" + std::to_string(origin.y) +
                           "} step=" + std::to_string(step) + " (2^" + std::to_string(r_chunk) + ")");

        const auto chunk_key = [](int i, int j) {
                const auto hi = static_cast<std::uint32_t>(i);
                const auto lo = static_cast<std::uint32_t>(j);
                return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
        };

        std::unordered_set<std::uint64_t> visited;
        visited.reserve(spawned_assets_.size() * 4);

        int debug_chunk_count = 0;
        LogLimiter limiter{10, 100};

        auto plan_chunk = [&](int i, int j) {
                const std::uint64_t key = chunk_key(i, j);
                if (!visited.insert(key).second) {
                        return;
                }

                SDL_Rect bounds{
                        origin.x + i * step,
                        origin.y + j * step,
                        step,
                        step};

                if (limiter(debug_chunk_count)) {
                        vibble::log::debug(std::string("[AssetLoader] Planned chunk (") + std::to_string(i) + ", " +
                                           std::to_string(j) + ") bounds=" + rect_str(bounds));
                }
                ++debug_chunk_count;

                planned_chunks_.push_back(PlannedChunk{i, j, bounds});
        };

        struct Bounds {
                std::int64_t min_x;
                std::int64_t min_y;
                std::int64_t max_x;
                std::int64_t max_y;
        };

        std::optional<Bounds> world_bounds;
        auto merge_bounds = [&](std::int64_t min_x, std::int64_t min_y, std::int64_t max_x, std::int64_t max_y) {
                if (min_x > max_x || min_y > max_y) {
                        vibble::log::warn("[AssetLoader] merge_bounds: invalid input skipped");
                        return;
                }
                if (!world_bounds) {
                        world_bounds = Bounds{min_x, min_y, max_x, max_y};
                        return;
                }
                world_bounds->min_x = std::min(world_bounds->min_x, min_x);
                world_bounds->min_y = std::min(world_bounds->min_y, min_y);
                world_bounds->max_x = std::max(world_bounds->max_x, max_x);
                world_bounds->max_y = std::max(world_bounds->max_y, max_y);
        };

        const auto extent_it = map_info_json_.find("map_extent");
        if (extent_it != map_info_json_.end() && extent_it->is_object()) {
                const nlohmann::json& extent = *extent_it;
                const bool has_min_x = extent.contains("min_x") && extent["min_x"].is_number();
                const bool has_min_y = extent.contains("min_y") && extent["min_y"].is_number();
                const bool has_max_x = extent.contains("max_x") && extent["max_x"].is_number();
                const bool has_max_y = extent.contains("max_y") && extent["max_y"].is_number();
                if (has_min_x && has_min_y && has_max_x && has_max_y) {
                        merge_bounds(static_cast<std::int64_t>(std::llround(extent["min_x"].get<double>())),
                                     static_cast<std::int64_t>(std::llround(extent["min_y"].get<double>())),
                                     static_cast<std::int64_t>(std::llround(extent["max_x"].get<double>())),
                                     static_cast<std::int64_t>(std::llround(extent["max_y"].get<double>())));
                        vibble::log::debug("[AssetLoader] Using map_extent for initial world bounds.");
                }
        }

        for (const Room* room : rooms_) {
                if (!room || !room->room_area) {
                        continue;
                }
                const Area* area = room->room_area.get();
                if (!area) {
                        continue;
                }
                const auto& points = area->get_points();
                if (points.empty()) {
                        continue;
                }
                try {
                        const auto [min_x, min_y, max_x, max_y] = area->get_bounds();
                        merge_bounds(static_cast<std::int64_t>(min_x),
                                     static_cast<std::int64_t>(min_y),
                                     static_cast<std::int64_t>(max_x),
                                     static_cast<std::int64_t>(max_y));
                } catch (const std::exception& ex) {
                        vibble::log::warn(std::string("[AssetLoader] Failed to query room bounds: ") + ex.what());
                }
        }

        if (!world_bounds) {
                const std::int64_t radius = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::llround(map_radius_)));
                const std::int64_t center_x = static_cast<std::int64_t>(std::llround(map_center_x_));
                const std::int64_t center_y = static_cast<std::int64_t>(std::llround(map_center_y_));
                if (radius > 0) {
                        merge_bounds(center_x - radius, center_y - radius, center_x + radius, center_y + radius);
                        vibble::log::debug("[AssetLoader] Fallback world bounds from map_radius.");
                }
        }

        if (world_bounds) {
                const std::int64_t min_x = world_bounds->min_x - origin_x64;
                const std::int64_t min_y = world_bounds->min_y - origin_y64;
                const std::int64_t max_x = world_bounds->max_x - origin_x64;
                const std::int64_t max_y = world_bounds->max_y - origin_y64;

                const int i_min = clamp_to_int(floor_div64(min_x, step64));
                const int j_min = clamp_to_int(floor_div64(min_y, step64));
                const int i_max = clamp_to_int(floor_div64(max_x, step64));
                const int j_max = clamp_to_int(floor_div64(max_y, step64));

                vibble::log::debug(std::string("[AssetLoader] Bounds-driven chunk ij: i[") +
                                   std::to_string(i_min) + "," + std::to_string(i_max) + "] j[" +
                                   std::to_string(j_min) + "," + std::to_string(j_max) + "]");

                for (int j = j_min; j <= j_max; ++j) {
                        for (int i = i_min; i <= i_max; ++i) {
                                plan_chunk(i, j);
                        }
                }

                const std::int64_t width_chunks = static_cast<std::int64_t>(i_max) - static_cast<std::int64_t>(i_min) + 1;
                const std::int64_t height_chunks = static_cast<std::int64_t>(j_max) - static_cast<std::int64_t>(j_min) + 1;
                const std::int64_t total_slots = (width_chunks > 0 && height_chunks > 0)
                                                        ? width_chunks * height_chunks
                                                        : 0;
                vibble::log::debug(std::string("[AssetLoader] Bounds-driven chunk coverage: min(") +
                                   std::to_string(world_bounds->min_x) + ", " + std::to_string(world_bounds->min_y) +
                                   ") max(" + std::to_string(world_bounds->max_x) + ", " +
                                   std::to_string(world_bounds->max_y) + ") => " +
                                   std::to_string(total_slots) + " chunk slots");
        } else {
                vibble::log::debug("[AssetLoader] No room or extent bounds available; relying on static-light coverage.");
        }

        const std::size_t chunk_count_after_bounds = planned_chunks_.size();
        bool discovered_static_light = false;

        int light_assets_seen = 0;
        LogLimiter asset_limit{10, 100};

        for (const auto& asset_up : spawned_assets_) {
                const Asset* asset = asset_up.get();
                if (!asset || !asset->info) {
                        continue;
                }
                if (asset->info->light_sources.empty() || asset->info->moving_asset) {
                        continue;
                }

                ++light_assets_seen;

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

                        if (asset_limit(light_assets_seen - 1)) {
                                vibble::log::debug(std::string("[AssetLoader] Static-light influence from asset @(") +
                                                   std::to_string(asset->pos.x) + "," + std::to_string(asset->pos.y) +
                                                   ") light_dst=" + rect_str(world_dst));
                        }

                        const std::int64_t min_x = static_cast<std::int64_t>(world_dst.x) - step64 - origin_x64;
                        const std::int64_t min_y = static_cast<std::int64_t>(world_dst.y) - step64 - origin_y64;
                        const std::int64_t max_x = static_cast<std::int64_t>(world_dst.x) +
                                                   static_cast<std::int64_t>(world_dst.w) - 1 + step64 - origin_x64;
                        const std::int64_t max_y = static_cast<std::int64_t>(world_dst.y) +
                                                   static_cast<std::int64_t>(world_dst.h) - 1 + step64 - origin_y64;

                        const int i_min = clamp_to_int(floor_div64(min_x, step64));
                        const int j_min = clamp_to_int(floor_div64(min_y, step64));
                        const int i_max = clamp_to_int(floor_div64(max_x, step64));
                        const int j_max = clamp_to_int(floor_div64(max_y, step64));

                        for (int j = j_min; j <= j_max; ++j) {
                                for (int i = i_min; i <= i_max; ++i) {
                                        plan_chunk(i, j);
                                }
                        }
                }
        }

        if (!discovered_static_light) {
                const std::int64_t center_x = static_cast<std::int64_t>(std::llround(map_center_x_));
                const std::int64_t center_y = static_cast<std::int64_t>(std::llround(map_center_y_));
                const int i = clamp_to_int(floor_div64(center_x - origin_x64, step64));
                const int j = clamp_to_int(floor_div64(center_y - origin_y64, step64));
                vibble::log::debug("[AssetLoader] No static lights discovered; planning fallback center chunk.");
                plan_chunk(i, j);
        }

        const std::size_t total_chunk_count = planned_chunks_.size();
        const std::size_t static_light_chunk_count = total_chunk_count >= chunk_count_after_bounds
                                                             ? total_chunk_count - chunk_count_after_bounds
                                                             : 0;

        vibble::log::info(std::string("[AssetLoader] Planned ") + std::to_string(total_chunk_count) +
                          " chunk(s) for baking (bounds: " +
                          std::to_string(chunk_count_after_bounds) + ", static-light: " +
                          std::to_string(static_light_chunk_count) + ")");
}


float AssetLoader::compute_chunk_average_brightness(SDL_Texture* texture) const {
        if (!renderer_ || !texture) {
                vibble::log::debug("[AssetLoader] compute_chunk_average_brightness: early 0 (renderer/texture null)");
                return 0.0f;
        }

        int tex_w = 0;
        int tex_h = 0;
        if (SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h) != 0 || tex_w <= 0 || tex_h <= 0) {
                vibble::log::warn(std::string("[AssetLoader] compute_chunk_average_brightness: bad texture size (") +
                                  std::to_string(tex_w) + "x" + std::to_string(tex_h) + "), returning 0");
                return 0.0f;
        }

        const auto t0 = std::chrono::steady_clock::now();

        const std::int64_t tex_w64 = static_cast<std::int64_t>(tex_w);
        const std::int64_t tex_h64 = static_cast<std::int64_t>(tex_h);
        bool overflow = tex_w64 <= 0 || tex_h64 <= 0;

        if (!overflow) {
                overflow = tex_w64 > std::numeric_limits<std::int64_t>::max() / tex_h64;
        }

        if (overflow) {
                vibble::log::warn(std::string("[AssetLoader] Skipping chunk brightness computation due to size overflow(") +
                                  std::to_string(tex_w) + "x" + std::to_string(tex_h) + ").");
                return 0.0f;
        }

        const std::int64_t pixel_count64 = tex_w64 * tex_h64;
        if (pixel_count64 > kMaxStaticLightPixels) {
                vibble::log::warn(std::string("[AssetLoader] Skipping chunk brightness computation due to excessive texture size (") +
                                  std::to_string(tex_w) + "x" + std::to_string(tex_h) + ").");
                return 0.0f;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(pixel_count64);
        if (pixel_count == 0) {
                vibble::log::debug("[AssetLoader] compute_chunk_average_brightness: pixel_count==0, return 0");
                return 0.0f;
        }

        std::unique_ptr<SDL_PixelFormat, decltype(&SDL_FreeFormat)> format(
            SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888), &SDL_FreeFormat);
        if (!format) {
                vibble::log::warn("[AssetLoader] compute_chunk_average_brightness: SDL_AllocFormat failed");
                return 0.0f;
        }

        const std::size_t row_length = static_cast<std::size_t>(tex_w);
        std::vector<std::uint32_t> row(row_length);
        if (row.empty()) {
                vibble::log::debug("[AssetLoader] compute_chunk_average_brightness: row buffer empty");
                return 0.0f;
        }

        SDL_Texture* previous_target = SDL_GetRenderTarget(renderer_);
        if (SDL_SetRenderTarget(renderer_, texture) != 0) {
                vibble::log::warn(std::string("[AssetLoader] compute_chunk_average_brightness: failed SetRenderTarget: ") + SDL_GetError());
                SDL_SetRenderTarget(renderer_, previous_target);
                return 0.0f;
        }

        const int pitch = tex_w * static_cast<int>(sizeof(std::uint32_t));
        const double inv_255 = 1.0 / 255.0;
        double accum = 0.0;

        // Read scanlines; log only the first/last few to avoid spam.
        for (int y = 0; y < tex_h; ++y) {
                SDL_Rect row_rect{0, y, tex_w, 1};
                if (SDL_RenderReadPixels(renderer_, &row_rect, SDL_PIXELFORMAT_RGBA8888, row.data(), pitch) != 0) {
                        vibble::log::warn(std::string("[AssetLoader] compute_chunk_average_brightness: RenderReadPixels failed row=") +
                                          std::to_string(y) + " err=" + SDL_GetError());
                        SDL_SetRenderTarget(renderer_, previous_target);
                        return 0.0f;
                }

                for (std::size_t x = 0; x < row.size(); ++x) {
                        Uint8 a = 255;
                        SDL_GetRGBA(row[x], format.get(), nullptr, nullptr, nullptr, &a);
                        accum += 1.0 - static_cast<double>(a) * inv_255;
                }

                if (y < 2 || y == tex_h - 1) {
                        // Light touch log for first/last rows
                        vibble::log::debug(std::string("[AssetLoader] brightness scan row ") + std::to_string(y) + "/" + std::to_string(tex_h-1));
                }
        }

        SDL_SetRenderTarget(renderer_, previous_target);

        const double average = accum / static_cast<double>(pixel_count);
        const float result = static_cast<float>(std::clamp(average, 0.0, 1.0));

        const auto t1 = std::chrono::steady_clock::now();
        vibble::log::debug(std::string("[AssetLoader] compute_chunk_average_brightness: ") +
                           std::to_string(tex_w) + "x" + std::to_string(tex_h) +
                           " avg=" + std::to_string(result) +
                           " took=" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()) + "ms");
        return result;
}
