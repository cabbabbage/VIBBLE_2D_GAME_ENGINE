
#include "asset_spawner.hpp"
#include "asset_spawn_planner.hpp"
#include "utils/spawn_context.hpp"
#include "methods/exact_spawner.hpp"
#include "methods/center_spawner.hpp"
#include "methods/random_spawner.hpp"
#include "methods/distributed_spawner.hpp"
#include "methods/perimeter_spawner.hpp"
#include "methods/distributed_batch_spawner.hpp"
#include "check.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

AssetSpawner::AssetSpawner(AssetLibrary* asset_library,
                           std::vector<Area> exclusion_zones)
    : asset_library_(asset_library),
      exclusion_zones(std::move(exclusion_zones)),
      rng_(std::random_device{}()),
      checker_(false),
      logger_("", "") {}

void AssetSpawner::spawn(Room& room) {
    if (!room.planner) {
        std::cerr << "[AssetSpawner] Room planner is null — skipping room: " << room.room_name << "\n";
        return;
    }

    const Area& spawn_area = *room.room_area;
    logger_ = SpawnLogger(room.map_path, room.room_directory);
    run_spawning(room.planner.get(), spawn_area);
    room.add_room_assets(std::move(all_));
}

std::vector<std::unique_ptr<Asset>> AssetSpawner::spawn_boundary_from_file(const std::string& json_path, const Area& spawn_area) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        std::cerr << "[BoundarySpawner] Failed to open file: " << json_path << "\n";
        return {};
    }

    nlohmann::json boundary_json;
    file >> boundary_json;
    std::vector<nlohmann::json> json_sources{ boundary_json };
    AssetSpawnPlanner planner(json_sources, spawn_area.get_area(), *asset_library_);

    logger_ = SpawnLogger("", "");
    run_spawning(&planner, spawn_area);
    return extract_all_assets();
}

void AssetSpawner::spawn_children(const Area& spawn_area, AssetSpawnPlanner* planner) {
    if (!planner) {
        std::cerr << "[AssetSpawner] Child planner is null — skipping.\n";
        return;
    }

    logger_ = SpawnLogger("", "");
    run_spawning(planner, spawn_area);
}

std::vector<std::unique_ptr<Asset>> AssetSpawner::extract_all_assets() {
    return std::move(all_);
}

void AssetSpawner::run_spawning(AssetSpawnPlanner* planner, const Area& area) {
    asset_info_library_ = asset_library_->all();
    spawn_queue_ = planner->get_spawn_queue();
    auto batch_assets = planner->get_batch_spawn_assets();
    int spacing = planner->get_batch_grid_spacing();
    int jitter  = planner->get_batch_jitter();

    SpawnContext ctx(rng_, checker_, logger_, exclusion_zones, asset_info_library_, all_, asset_library_);
    ExactSpawner exact;
    CenterSpawner center;
    RandomSpawner random;
    DistributedSpawner distributed;
    PerimeterSpawner perimeter;
    DistributedBatchSpawner batch;

    for (auto& queue_item : spawn_queue_) {
        logger_.start_timer();
        if (!queue_item.info) continue;

        const std::string& pos = queue_item.position;
        // Support both legacy and new naming for exact-percentage spawning
        if (pos == "spawn_exact_percentage()" || pos == "Exact Position") {
            exact.spawn(queue_item, &area, ctx);
        } else if (pos == "Center") {
            center.spawn(queue_item, &area, ctx);
        } else if (pos == "Perimeter") {
            perimeter.spawn(queue_item, &area, ctx);
        } else if (pos == "Distributed") {
            distributed.spawn(queue_item, &area, ctx);
        } else {
            random.spawn(queue_item, &area, ctx);
        }
    }

    if (!batch_assets.empty()) {
        batch.spawn(batch_assets, &area, spacing, jitter, ctx);
    }
}
