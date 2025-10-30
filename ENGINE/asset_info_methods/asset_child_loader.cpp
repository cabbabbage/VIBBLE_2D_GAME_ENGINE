#include "asset_child_loader.hpp"
#include "asset/asset_info.hpp"

using nlohmann::json;

void ChildLoader::load_children(AssetInfo& info,
                                const json& data,
                                const std::string&)
{
    info.asset_children.clear();
    if (!data.contains("spawn_groups") || !data["spawn_groups"].is_array()) {
        // continue; still allow attachment-driven groups
    }

    if (data.contains("spawn_groups") && data["spawn_groups"].is_array()) {
        for (const auto& entry : data["spawn_groups"]) {
            if (!entry.is_object()) {
                continue;
            }

            bool link_to_area = false;
            std::string area_name;
            try {
                if (entry.contains("link_to_area")) {
                    if (entry["link_to_area"].is_boolean()) {
                        link_to_area = entry["link_to_area"].get<bool>();
                    } else if (entry["link_to_area"].is_number_integer()) {
                        link_to_area = entry["link_to_area"].get<int>() != 0;
                    }
                }
                if (entry.contains("linked_area") && entry["linked_area"].is_string()) {
                    area_name = entry["linked_area"].get<std::string>();
                }
            } catch (...) {
                link_to_area = false;
                area_name.clear();
            }

            if (!link_to_area || area_name.empty()) {
                continue;
            }

            ChildInfo ci;
            ci.area_name = area_name;
            ci.placed_on_top_parent = entry.value("placed_on_top_parent", false);
            try {
                if (entry.contains("z_offset") && entry["z_offset"].is_number_integer()) {
                    ci.z_offset = entry["z_offset"].get<int>();
                }
            } catch (...) {
                ci.z_offset = 0;
            }

            ci.spawn_group = entry;
            info.asset_children.emplace_back(std::move(ci));
        }
    }

    // Generate child groups from attachment areas (subtype = asset_child_attachment)
    try {
        for (const auto& na : info.areas) {
            if (!na.area) continue;
            if (na.attachment_subtype != "asset_child_attachment") continue;
            if (na.name.empty()) continue;

            ChildInfo ci;
            ci.area_name = na.name;
            ci.placed_on_top_parent = na.attachment_is_on_top;
            ci.z_offset = 0;
            json j = json::object();
            j["display_name"] = std::string("Attachment:") + na.name;
            j["position"] = "Random"; // default spawn within polygon
            j["link_to_area"] = true;
            j["linked_area"] = na.name;
            j["placed_on_top_parent"] = na.attachment_is_on_top;
            if (na.attachment_child_candidates.is_array()) {
                j["candidates"] = na.attachment_child_candidates;
            } else {
                j["candidates"] = json::array();
            }
            ci.spawn_group = std::move(j);
            info.asset_children.emplace_back(std::move(ci));
        }
    } catch (...) {
        // ignore errors in attachment-driven generation
    }
}
