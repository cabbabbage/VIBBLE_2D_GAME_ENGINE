#include "asset_child_loader.hpp"
#include "asset/asset_info.hpp"

using nlohmann::json;

void ChildLoader::load_children(AssetInfo& info,
                                const json& data,
                                const std::string&)
{
    info.asset_children.clear();
    // Spawn group parsing removed; no child loading from manifest spawn_groups.

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
            // Attachment-driven child candidates no longer map to spawn groups; keep minimal area linkage only.
            info.asset_children.emplace_back(std::move(ci));
        }
    } catch (...) {
        // ignore errors in attachment-driven generation
    }
}
