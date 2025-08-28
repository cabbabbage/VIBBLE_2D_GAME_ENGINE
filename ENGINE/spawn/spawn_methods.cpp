
#include "spawn_methods.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

SpawnMethods::SpawnMethods(std::mt19937& rng,
                           Check& checker,
                           SpawnLogger& logger,
                           std::vector<Area>& exclusion_zones,
                           std::unordered_map<std::string, std::shared_ptr<AssetInfo>>& asset_info_library,
                           std::vector<std::unique_ptr<Asset>>& all_assets,
                           AssetLibrary* asset_library)
    : rng_(rng),
      checker_(checker),
      logger_(logger),
      exclusion_zones_(exclusion_zones),
      asset_info_library_(asset_info_library),
      all_(all_assets),
      asset_library_(asset_library)
{}




Asset* SpawnMethods::spawn_(const std::string& name,
                            const std::shared_ptr<AssetInfo>& info,
                            const Area& area,
                            int x,
                            int y,
                            int depth,
                            Asset* parent)
{
    
    auto assetPtr = std::make_unique<Asset>(info, area, x, y, depth, parent);
    Asset* raw = assetPtr.get();
    all_.push_back(std::move(assetPtr));

    
    if (raw->info && !raw->info->children.empty()) {
        std::cout << "[SpawnMethods] Spawned parent asset: \""
                  << raw->info->name << "\" at ("
                  << raw->pos_X << ", " << raw->pos_Y << ")\n";
    }

    
    if (raw->info && !raw->info->children.empty()) {
        
        std::vector<ChildInfo*> shuffled_children;
        for (auto& c : raw->info->children)
            shuffled_children.push_back(&c);

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(shuffled_children.begin(), shuffled_children.end(), g);

        for (auto* childInfo : shuffled_children) {
            if (!childInfo->has_area) {
                std::cout << "[SpawnMethods]  Skipping child (no area loaded)\n";
                continue;
            }

            const auto& childJsonPath = childInfo->json_path;
            std::cout << "[SpawnMethods]  Loading child JSON: "
                      << childJsonPath << "\n";

            if (!fs::exists(childJsonPath)) {
                std::cerr << "[SpawnMethods]  Child JSON not found: "
                          << childJsonPath << "\n";
                continue;
            }

            
            nlohmann::json j;
            try {
                std::ifstream in(childJsonPath);
                in >> j;
            } catch (const std::exception& e) {
                std::cerr << "[SpawnMethods]  Failed to parse child JSON: "
                          << childJsonPath << " | " << e.what() << "\n";
                continue;
            }

            
            Area childArea = *childInfo->area_ptr;

            childArea.align(raw->pos_X, raw->pos_Y);
            if (raw->flipped){
                childArea.flip_horizontal(raw->pos_X);
            }


            auto [cx, cy] = childArea.get_center();
            std::cout << "[SpawnMethods]  Child area aligned to center ("
                    << cx << ", " << cy << "), area size="
                    << childArea.get_area() << "\n";


            
            AssetSpawnPlanner childPlanner(
                std::vector<nlohmann::json>{ j },
                childArea.get_area(),
                *asset_library_
            );
            AssetSpawner childSpawner(asset_library_, exclusion_zones_);
            childSpawner.spawn_children(childArea, &childPlanner);

            
            auto kids = childSpawner.extract_all_assets();
            std::cout << "[SpawnMethods]  Spawned " << kids.size()
                    << " children for \"" << raw->info->name << "\"\n";

            for (auto& uptr : kids) {
                if (!uptr || !uptr->info) continue;

                
                uptr->set_z_offset(childInfo->z_offset);
                uptr->parent = raw;
                uptr->set_hidden(true);
                

                std::cout << "[SpawnMethods]    Adopting child \""
                        << uptr->info->name << "\" at ("
                        << uptr->pos_X << ", " << uptr->pos_Y
                        << "), z_offset=" << childInfo->z_offset << "\n";

                
                raw->add_child(uptr.get());
                all_.push_back(std::move(uptr));
            }

        }
    }

    return raw;
}





SpawnMethods::Point SpawnMethods::get_area_center(const Area& area) const {
    return area.get_center();
}

SpawnMethods::Point SpawnMethods::get_point_within_area(const Area& area) {
    auto [minx, miny, maxx, maxy] = area.get_bounds();
    for (int i = 0; i < 100; ++i) {
        int x = std::uniform_int_distribution<int>(minx, maxx)(rng_);
        int y = std::uniform_int_distribution<int>(miny, maxy)(rng_);
        if (area.contains_point({x, y})) return {x, y};
    }
    return {0, 0};
}

void SpawnMethods::spawn_item_exact(const SpawnInfo& item, const Area* area) {
    if (!item.info || item.ep_x < 0 || item.ep_y < 0 || !area) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    int width = maxx - minx;
    int height = maxy - miny;

    Point center = get_area_center(*area);
    double norm_x = (item.ep_x - 50.0) / 100.0;
    double norm_y = (item.ep_y - 50.0) / 100.0;

    int final_x = center.first + static_cast<int>(norm_x * width);
    int final_y = center.second + static_cast<int>(norm_y * height);


        logger_.output_and_log(item.name, item.quantity, 0, 0, 0, "exact");
        return;
    

    spawn_(item.name, item.info, *area, final_x, final_y, 0, nullptr);
    logger_.progress(item.info, 1, item.quantity);
    logger_.output_and_log(item.name, item.quantity, 1, 1, 1, "exact");
}

void SpawnMethods::spawn_item_center(const SpawnInfo& item, const Area* area) {
    if (!item.info || !area) return;

    const int Y_SHIFT = 200; 

    Point center = get_area_center(*area);
    center.second -= Y_SHIFT;

    if (checker_.check(item.info, center.first, center.second, exclusion_zones_, all_,
                       item.check_overlap, item.check_min_spacing, false, 5)) {
        logger_.output_and_log(item.name, item.quantity, 0, 1, 1, "center");
        return;
    }

    Asset* result = spawn_(item.name, item.info, *area, center.first, center.second, 0, nullptr);
    int spawned = result ? 1 : 0;

    logger_.progress(item.info, spawned, item.quantity);
    logger_.output_and_log(item.name, item.quantity, spawned, 1, 1, "center");
}


void SpawnMethods::spawn_item_random(const SpawnInfo& item, const Area* area) {
    if (!item.info || item.quantity <= 0 || !area) return;

    int spawned = 0, attempts = 0;
    int max_attempts = item.quantity * 10;

    while (spawned < item.quantity && attempts < max_attempts) {
        Point pos = get_point_within_area(*area);
        ++attempts;

        if (!area->contains_point(pos)) continue;
        if (checker_.check(item.info, pos.first, pos.second, exclusion_zones_, all_,
                           true, true, true, 5)) continue;

        spawn_(item.name, item.info, *area, pos.first, pos.second, 0, nullptr);
        ++spawned;
        logger_.progress(item.info, spawned, item.quantity);
    }

    logger_.output_and_log(item.name, item.quantity, spawned, attempts, max_attempts, "random");
}

void SpawnMethods::spawn_item_distributed(const SpawnInfo& item, const Area* area) {
    if (!item.info || item.quantity <= 0 || !area) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    int w = maxx - minx;
    int h = maxy - miny;
    if (w <= 0 || h <= 0) return;

    int spacing = std::max(1, item.grid_spacing);
    int jitter  = std::max(0, item.jitter);

    int placed = 0, attempts = 0, max_attempts = item.quantity * 10;

    for (int x = minx; x <= maxx && placed < item.quantity && attempts < max_attempts; x += spacing) {
        for (int y = miny; y <= maxy && placed < item.quantity && attempts < max_attempts; y += spacing) {
            int cx = x + std::uniform_int_distribution<int>(-jitter, jitter)(rng_);
            int cy = y + std::uniform_int_distribution<int>(-jitter, jitter)(rng_);
            ++attempts;

            if (std::uniform_int_distribution<int>(0, 99)(rng_) < item.empty_grid_spaces) continue;
            if (!area->contains_point({cx, cy})) continue;
            if (checker_.check(item.info, cx, cy, exclusion_zones_, all_,
                               true, false, true, 5)) continue;

            spawn_(item.name, item.info, *area, cx, cy, 0, nullptr);
            ++placed;
            logger_.progress(item.info, placed, item.quantity);
        }
    }

    logger_.output_and_log(item.name, item.quantity, placed, attempts, max_attempts, "distributed");
}


void SpawnMethods::spawn_item_perimeter(const SpawnInfo& item, const Area* area) {
    if (!item.info || item.quantity <= 0 || !area) return;

    const int Y_SHIFT = 200; 

    const auto& boundary = area->get_points();
    if (boundary.size() < 2) return;

    double cx = 0.0, cy = 0.0;
    for (const auto& pt : boundary) {
        cx += pt.first;
        cy += pt.second;
    }
    cx /= boundary.size();
    cy /= boundary.size();

    double shift_ratio = 1.0 - (item.border_shift / 100.0);
    std::vector<Point> contracted;
    contracted.reserve(boundary.size());
    for (const auto& pt : boundary) {
        double dx = pt.first - cx;
        double dy = pt.second - cy;
        int new_x = static_cast<int>(std::round(cx + dx * shift_ratio));
        int new_y = static_cast<int>(std::round(cy + dy * shift_ratio)) - Y_SHIFT;
        contracted.emplace_back(new_x, new_y);
    }

    std::vector<double> segment_lengths;
    double total_length = 0.0;
    for (size_t i = 0; i < contracted.size(); ++i) {
        const Point& a = contracted[i];
        const Point& b = contracted[(i + 1) % contracted.size()];
        double len = std::hypot(b.first - a.first, b.second - a.second);
        segment_lengths.push_back(len);
        total_length += len;
    }
    if (total_length <= 0.0) return;

    double spacing = total_length / item.quantity;
    double dist_accum = 0.0;
    size_t seg_index = 0;

    int angle_center = item.sector_center;
    int angle_range  = item.sector_range;

    int placed = 0, attempts = 0;

    for (int i = 0; i < item.quantity; ++i) {
        double target = i * spacing;
        while (seg_index < segment_lengths.size() &&
               dist_accum + segment_lengths[seg_index] < target) {
            dist_accum += segment_lengths[seg_index++];
        }
        if (seg_index >= segment_lengths.size()) break;

        const Point& p1 = contracted[seg_index];
        const Point& p2 = contracted[(seg_index + 1) % contracted.size()];
        double t = (target - dist_accum) / segment_lengths[seg_index];
        int x = static_cast<int>(std::round(p1.first + t * (p2.first - p1.first)));
        int y = static_cast<int>(std::round(p1.second + t * (p2.second - p1.second)));

        double angle = std::atan2(y - (cy - Y_SHIFT), x - cx) * 180.0 / M_PI;
        if (angle < 0) angle += 360;

        int angle_start = angle_center - angle_range / 2;
        int angle_end   = angle_center + angle_range / 2;
        bool within_sector = false;
        if (angle_start < 0 || angle_end >= 360) {
            within_sector = (angle >= (angle_start + 360) % 360 || angle <= angle_end % 360);
        } else {
            within_sector = (angle >= angle_start && angle <= angle_end);
        }
        if (!within_sector) continue;

        x += item.perimeter_x_offset;
        y += item.perimeter_y_offset;

        ++attempts;
        if (checker_.check(item.info, x, y, exclusion_zones_, all_,
                           item.check_overlap, false, false, 5)) continue;

        spawn_(item.name, item.info, *area, x, y, 0, nullptr);
        ++placed;
        logger_.progress(item.info, placed, item.quantity);
    }

    logger_.output_and_log(item.name, item.quantity, placed, attempts, item.quantity, "perimeter");
}



void SpawnMethods::spawn_distributed_batch(const std::vector<BatchSpawnInfo>& items,
                                           const Area* area,
                                           int spacing,
                                           int jitter)
{
    if (!area || items.empty()) return;

    auto [minx, miny, maxx, maxy] = area->get_bounds();
    int w = maxx - minx;
    int h = maxy - miny;
    if (w <= 0 || h <= 0) return;

    std::unordered_map<std::string, int> placed_quantities;
    for (const auto& item : items) placed_quantities[item.name] = 0;

    std::uniform_int_distribution<int> jitter_dist(-jitter, jitter);

    for (int x = minx; x <= maxx; x += spacing) {
        for (int y = miny; y <= maxy; y += spacing) {
            int cx = x + jitter_dist(rng_);
            int cy = y + jitter_dist(rng_);

            if (!area->contains_point({cx, cy})) continue;

            std::vector<int> weights;
            for (const auto& item : items) weights.push_back(item.percent);

            std::discrete_distribution<int> picker(weights.begin(), weights.end());
            const auto& selected = items[picker(rng_)];
            if (selected.name == "null") continue;

            auto it = asset_info_library_.find(selected.name);
            if (it == asset_info_library_.end()) continue;

            auto& info = it->second;
            if (checker_.check(info, cx, cy, exclusion_zones_, all_, true, false, true, 5)) continue;

            spawn_(selected.name, info, *area, cx, cy, 0, nullptr);
            ++placed_quantities[selected.name];
        }
    }

    for (const auto& item : items) {
        if (item.name == "null") continue;
        int placed = placed_quantities[item.name];
        logger_.output_and_log(item.name, placed, placed, placed, placed, "distributed_batch");
    }
}
