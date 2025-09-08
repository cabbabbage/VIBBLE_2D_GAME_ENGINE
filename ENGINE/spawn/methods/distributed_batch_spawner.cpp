#include "distributed_batch_spawner.hpp"
#include <random>
#include <unordered_map>
#include "utils/spawn_context.hpp"
#include "check.hpp"
#include "asset_spawn_planner.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include "spawn_logger.hpp"
void DistributedBatchSpawner::spawn(const std::vector<BatchSpawnInfo>& items,
                                    const Area* area,
                                    int spacing,
                                    int jitter,
                                    SpawnContext& ctx) {
	if (!area || items.empty()) return;
	auto gen_id = []() {
		static std::mt19937 rng(std::random_device{}());
		static const char* hex = "0123456789abcdef";
		std::uniform_int_distribution<int> d(0, 15);
		std::string s = "spn-";
		for (int i = 0; i < 12; ++i) s.push_back(hex[d(rng)]);
		return s;
	};
	const std::string spawn_id = gen_id();
	auto [minx, miny, maxx, maxy] = area->get_bounds();
	int w = maxx - minx;
	int h = maxy - miny;
	if (w <= 0 || h <= 0) return;
	std::unordered_map<std::string, int> placed_quantities;
	for (const auto& item : items) placed_quantities[item.name] = 0;
	std::uniform_int_distribution<int> jitter_dist(-jitter, jitter);
	for (int x = minx; x <= maxx; x += spacing) {
		for (int y = miny; y <= maxy; y += spacing) {
			int cx = x + jitter_dist(ctx.rng());
			int cy = y + jitter_dist(ctx.rng());
			if (!area->contains_point({cx, cy})) continue;
			std::vector<int> weights;
			for (const auto& item : items) weights.push_back(item.percent);
			std::discrete_distribution<int> picker(weights.begin(), weights.end());
			const auto& selected = items[picker(ctx.rng())];
			if (selected.name == "null") continue;
			auto it = ctx.info_library().find(selected.name);
			if (it == ctx.info_library().end()) continue;
			auto& info = it->second;
			if (ctx.checker().check(info, cx, cy, ctx.exclusion_zones(), ctx.all_assets(), true, false, true, 5)) continue;
			ctx.spawnAsset(selected.name, info, *area, cx, cy, 0, nullptr,
			spawn_id, std::string("DistributedBatch"));
			++placed_quantities[selected.name];
		}
	}
	for (const auto& item : items) {
		if (item.name == "null") continue;
		int placed = placed_quantities[item.name];
		ctx.logger().output_and_log(item.name, placed, placed, placed, placed, "distributed_batch");
	}
}
