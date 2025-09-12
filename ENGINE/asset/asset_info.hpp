#pragma once

#include "animation.hpp"
#include "utils/area.hpp"
#include "utils/light_source.hpp"
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct ChildInfo {
    std::string json_path;
    std::string area_name;
    int z_offset;
    nlohmann::json inline_assets; // optional: array of child asset entries
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
    AssetInfo(const std::string &asset_folder_name);
    ~AssetInfo();
    void loadAnimations(SDL_Renderer *renderer);
    bool has_tag(const std::string &tag) const;
    std::vector<LightSource> light_sources;
    std::vector<LightSource> orbital_light_sources;
    std::string name;
    std::string type;
    std::string start_animation;
    int z_threshold;
    bool passable;
    bool has_shading;
    int min_same_type_distance;
    int min_distance_all;
    float scale_factor;
    int original_canvas_width;
    int original_canvas_height;
    bool flipable;
    std::vector<std::string> tags;
    bool has_light_source;
    bool moving_asset = false;
    struct NamedArea {
    std::string name;
    std::unique_ptr<Area> area;
	};
    std::vector<NamedArea> areas;
    std::map<std::string, Animation> animations;
    std::map<std::string, Mapping> mappings;
    std::vector<ChildInfo> children;
    std::string custom_controller_key;

	public:
    bool update_info_json() const;
    void set_asset_type(const std::string &t);
    void set_z_threshold(int z);
    void set_min_same_type_distance(int d);
    void set_min_distance_all(int d);
    void set_flipable(bool v);
    void set_scale_factor(float factor);
    void set_scale_percentage(float percent);
    void set_tags(const std::vector<std::string> &t);
    void add_tag(const std::string &tag);
    void remove_tag(const std::string &tag);
    void set_passable(bool v);
    Area* find_area(const std::string& name);
    void upsert_area_from_editor(const class Area& area);
    std::string pick_next_animation(const std::string& mapping_id) const;
    // Children editing (for dev-mode UI)
    void set_children(const std::vector<ChildInfo>& children);
    // Lighting editing
    void set_lighting(bool has_shading,
                      const LightSource& shading,
                      int shading_factor,
                      const std::vector<LightSource>& lights);
    // Accessors
    std::string info_json_path() const { return info_json_path_; }
    std::string asset_dir_path() const { return dir_path_; }
    // Remove a named area from memory and JSON, returns true if removed
    bool remove_area(const std::string& name);

    // --- Animations editing helpers for dev-mode UI ---
    // Names in animations map (JSON order not guaranteed)
    std::vector<std::string> animation_names() const;
    // Return a copy of the animation payload JSON (empty object if missing)
    nlohmann::json animation_payload(const std::string& name) const;
    // Create or update an animation payload (writes to info_json_ and anims_json_)
    bool upsert_animation(const std::string& name, const nlohmann::json& payload);
    // Remove an animation by name (updates JSON containers)
    bool remove_animation(const std::string& name);
    // Rename an animation key (preserves payload). Also updates start if needed
    bool rename_animation(const std::string& old_name, const std::string& new_name);
    // Update start animation string; empty allowed
    void set_start_animation_name(const std::string& name);

	private:
    void load_base_properties(const nlohmann::json &data);
    void generate_lights(SDL_Renderer *renderer);
    void load_areas(const nlohmann::json &data, float scale, int offset_x, int offset_y);
    void load_children(const nlohmann::json &data);
    nlohmann::json anims_json_;
    std::string dir_path_;
    nlohmann::json info_json_;
    std::string info_json_path_;
    friend class AnimationLoader;
    friend class LightingLoader;
    friend class AreaLoader;
    friend class ChildLoader;
};
