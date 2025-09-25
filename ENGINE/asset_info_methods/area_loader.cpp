#include "area_loader.hpp"
#include "asset/asset_info.hpp"
#include "utils/area.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
using nlohmann::json;

void AreaLoader::load(AssetInfo& info,
                      const json& data,
                      float scale,
                      int offset_x,
                      int offset_y) {
	info.areas.clear();
	if (!data.contains("areas") || !data["areas"].is_array()) return;
	for (const auto& entry : data["areas"]) {
		if (!entry.is_object()) continue;
		std::string name = entry.value("name", std::string{});
		if (name.empty()) continue;
                std::vector<Area::Point> pts;
                int json_offset_x = entry.value("offset_x", 0);
                int json_offset_y = entry.value("offset_y", 0);
                const int base_offset_x = offset_x + json_offset_x;
                const int base_offset_y = offset_y - json_offset_y;
                if (entry.contains("points") && entry["points"].is_array()) {
                        for (const auto& p : entry["points"]) {
                                        if (p.is_array() && p.size() >= 2) {
                                                                int x = static_cast<int>(std::round(p[0].get<double>() * scale));
                                                                int y = static_cast<int>(std::round(p[1].get<double>() * scale));
                                                                pts.push_back({x + base_offset_x, y + base_offset_y});
                                        }
                        }
                }
		if (pts.empty()) continue;
		auto area = std::make_unique<Area>(name, pts);
		info.areas.push_back({name, std::move(area)});
	}
}
