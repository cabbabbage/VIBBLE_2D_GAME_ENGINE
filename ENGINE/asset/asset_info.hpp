#pragma once

#include "animation.hpp"
#include "utils/area.hpp"
#include "utils/shadow_mask_settings.hpp"
#include "utils/light_source.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_set>

namespace devmode::core {
class ManifestStore;
}

struct ChildInfo {
    std::string area_name;
    int z_offset = 0;
    bool placed_on_top_parent = false;
    nlohmann::json spawn_group;
};

struct AsyncChildDefinition {
    std::string name;
    std::string asset;
    std::string animation;
    std::vector<AnimationChildFrameData> frames;

    bool valid() const { return !name.empty() && !asset.empty() && !frames.empty(); }
};

struct MappingOption {
	std::string animation;
	float percent;
};

struct MappingEntry {
	std::string condition;
	std::vector<MappingOption> options;
};

using Mapping = std::vector<MappingEntry>;

class AssetInfo {

        public:
    SDL_Texture* preview_texture = nullptr;
    using ChildInfo = ::ChildInfo;
    AssetInfo(const std::string &asset_folder_name);
    AssetInfo(const std::string &asset_folder_name, const nlohmann::json& metadata);
    static std::shared_ptr<AssetInfo> from_manifest_entry(const std::string& asset_folder_name, const nlohmann::json& metadata);
    using ManifestStoreProvider = std::function<devmode::core::ManifestStore*()>;
    static void set_manifest_store_provider(ManifestStoreProvider provider);
    ~AssetInfo();

    bool has_tag(const std::string &tag) const;
    std::vector<LightSource> light_sources;
    std::string name;
    std::string type;
    std::string start_animation;
    int z_threshold;
    bool passable;
    bool is_shaded = false;
    ShadowMaskSettings shadow_mask_settings{};
    float shading_parallax_amount = 0.0f;
    float shading_screen_brightness_multiplier = 1.0f;
    float shading_opacity_multiplier = 1.0f;
    int min_same_type_distance;
    int min_distance_all;
    float scale_factor;
    bool smooth_scaling = true;
    int original_canvas_width = 0;
    int original_canvas_height = 0;
    bool flipable;
    bool apply_distance_scaling = true;
    bool apply_vertical_scaling = true;
    bool tillable = false;
    std::vector<std::string> tags;
    std::vector<std::string> anti_tags;

    std::vector<std::string> animation_children;

    std::vector<AsyncChildDefinition> async_children;
    bool is_light_source = false;
    bool moving_asset = false;
    std::vector<float>  scale_variants;
    struct NamedArea {
        struct RenderFrame {
            int width = 0;
            int height = 0;
            int pivot_x = 0;
            int pivot_y = 0;
            float pixel_scale = 1.0f;

            bool is_valid() const {
                return width > 0 && height > 0 && std::isfinite(pixel_scale) && pixel_scale > 0.0f;
            }
};
        std::string name;
        std::string type;
        std::string kind;
        std::unique_ptr<Area> area;
        std::optional<RenderFrame> render_frame;

        std::string attachment_subtype;
        bool        attachment_is_on_top = false;
        nlohmann::json attachment_child_candidates = nlohmann::json::array();
};
    std::vector<NamedArea> areas;
    std::map<std::string, Animation> animations;
    std::map<std::string, Mapping> mappings;
    std::vector<ChildInfo> asset_children;
    std::string custom_controller_key;

	public:
    void loadAnimations(SDL_Renderer* renderer);
    bool commit_manifest();
    void set_asset_type(const std::string &t);
    void set_z_threshold(int z);
    void set_min_same_type_distance(int d);
    void set_min_distance_all(int d);
    void set_neighbor_search_radius(int radius);
    void set_flipable(bool v);
    void set_scale_factor(float factor);
    void set_scale_percentage(float percent);
    void set_scale_filter(bool smooth);
    void set_apply_distance_scaling(bool v);
    void set_apply_vertical_scaling(bool v);
    void set_tags(const std::vector<std::string> &t);
    void add_tag(const std::string &tag);
    void remove_tag(const std::string &tag);
    void set_anti_tags(const std::vector<std::string> &t);
    void add_anti_tag(const std::string &tag);
    void remove_anti_tag(const std::string &tag);
    void set_animation_children(const std::vector<std::string>& children);
    void append_animation_child(const std::string& child);
    void remove_animation_child_at(std::size_t index);
    void set_async_children(const std::vector<AsyncChildDefinition>& children);
    void set_passable(bool v);
    void set_tillable(bool v);
    Area* find_area(const std::string& name);
    void upsert_area_from_editor(const class Area& area, std::optional<NamedArea::RenderFrame> frame = std::nullopt);
    std::string pick_next_animation(const std::string& mapping_id) const;
    int NeighborSearchRadius = 500;

    void set_children(const std::vector<ChildInfo>& asset_children);

    void set_lighting(const std::vector<LightSource>& lights);
    void set_shadow_mask_settings(const ShadowMaskSettings& settings);
    void set_shading_enabled(bool enabled);
    void set_shading_parallax_amount(float amount);
    void set_shading_screen_brightness_multiplier(float multiplier);
    void set_shading_opacity_multiplier(float multiplier);

    void set_spawn_groups_payload(const nlohmann::json& groups);
    nlohmann::json spawn_groups_payload() const;

    std::string info_json_path() const { return info_json_path_; }
    std::string asset_dir_path() const { return dir_path_; }

    const std::unordered_set<std::string>& tag_lookup() const { return tag_lookup_; }
    const std::unordered_set<std::string>& anti_tag_lookup() const { return anti_tag_lookup_; }

    bool remove_area(const std::string& name);
    bool rename_area(const std::string& old_name, const std::string& new_name);

    std::vector<std::string> animation_names() const;

    nlohmann::json animation_payload(const std::string& name) const;

    bool upsert_animation(const std::string& name, const nlohmann::json& payload);

    bool remove_animation(const std::string& name);

    bool rename_animation(const std::string& old_name, const std::string& new_name);

    void set_start_animation_name(const std::string& name);

    bool reload_animations_from_disk();

    bool update_animation_properties(const std::string& animation_name, const nlohmann::json& properties);

    struct AreaCodec {
        static SDL_Point scaled_anchor(const AssetInfo& info, std::optional<float> scale_override = std::nullopt);

        static nlohmann::json encode_entry( const AssetInfo& info, const Area& area, const std::string& final_type, const std::string& final_kind, std::optional<NamedArea::RenderFrame> frame = std::nullopt);

        static std::optional<NamedArea> decode_entry(const AssetInfo& info, const nlohmann::json& entry);
};

    void set_spawn_groups(const nlohmann::json& groups);

    bool rebuild_light_texture(SDL_Renderer* renderer, std::size_t light_index);
    bool ensure_light_textures(SDL_Renderer* renderer);

        private:
    void load_base_properties(const nlohmann::json &data);
    void generate_lights(SDL_Renderer* renderer);
    void clear_light_textures();
    void load_animations(const nlohmann::json& data);
    void load_areas(const nlohmann::json &data);
    void load_children(const nlohmann::json &data);
    nlohmann::json anims_json_;
    std::string dir_path_;
    nlohmann::json info_json_;
    std::string info_json_path_;
    void initialize_from_json(const nlohmann::json& data);
    void rebuild_tag_cache();
    void rebuild_anti_tag_cache();
    std::unordered_set<std::string> tag_lookup_;
    std::unordered_set<std::string> anti_tag_lookup_;
    friend class AnimationLoader;
    friend class LightingLoader;
    friend class ChildLoader;
#if defined(ASSET_INFO_ENABLE_TEST_ACCESS)
    friend struct AssetInfoTestAccess;
#endif
};

#if defined(ASSET_INFO_ENABLE_TEST_ACCESS)
struct AssetInfoTestAccess {
    static void initialize_info_json(AssetInfo& info, nlohmann::json data);
    static void rebuild_tag_cache(AssetInfo& info);
    static void rebuild_anti_tag_cache(AssetInfo& info);
};
#endif
