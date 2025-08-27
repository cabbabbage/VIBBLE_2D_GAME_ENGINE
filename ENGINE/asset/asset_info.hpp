#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <SDL.h>
#include <nlohmann/json.hpp>
#include "utils/area.hpp"
#include "utils/generate_light.hpp"
#include "animation.hpp"
#include "utils/light_source.hpp"

struct ChildInfo {
    std::string               json_path;
    std::unique_ptr<Area>     area_ptr;
    bool                      has_area = false;
    int                       z_offset;
};



class AssetInfo {
public:
    nlohmann::json json_data_;

    AssetInfo(const std::string& asset_folder_name);
    ~AssetInfo();

    void loadAnimations(SDL_Renderer* renderer);
    bool has_tag(const std::string& tag) const;
    std::vector<LightSource> light_sources;
    std::vector<LightSource> orbital_light_sources;

    std::string                   name;
    std::string                   type;
    int                           z_threshold;
    bool                          passable;
    int                           min_same_type_distance;
    int                           min_distance_all;
    bool                          can_invert;
    int                           max_child_depth;
    int                           min_child_depth;
    int                           child_depth;
    bool                          duplicatable;
    int                           duplication_interval_min;
    int                           duplication_interval_max;
    int                           duplication_interval;
    float                         scale_percentage;
    float                         variability_percentage;
    float                         scale_factor;
    int                           original_canvas_width;
    int                           original_canvas_height;
    bool flipable;

    std::vector<std::string>      tags;
    SDL_BlendMode                 blendmode;

    bool                          interaction;
    bool                          hit;
    bool                          collision;
    bool                          has_light_source;
    bool                          has_shading;
    bool                          has_base_shadow;
    int                           base_shadow_intensity;
    bool                          has_gradient_shadow;
    int                           number_of_gradient_shadows;
    int                           gradient_shadow_intensity;
    bool                          has_casted_shadows;
    int                           number_of_casted_shadows;
    int                           cast_shadow_intensity;


    std::unique_ptr<Area> passability_area;
    bool has_passability_area = false;

    std::unique_ptr<Area> spacing_area;
    bool has_spacing_area = false;

    std::unique_ptr<Area> collision_area;
    bool has_collision_area = false;

    std::unique_ptr<Area> interaction_area;
    bool has_interaction_area = false;

    std::unique_ptr<Area> attack_area;
    bool has_attack_area = false;

    std::map<std::string, Animation> animations;

    std::vector<std::string> child_json_paths;
    std::vector<ChildInfo> children;
    int update_radius;
private:
    void get_area_textures(SDL_Renderer* renderer);

    int offset_x;
    int offset_y;
    void load_base_properties(const nlohmann::json& data);
    void load_lighting_info    (const nlohmann::json& data);
    void generate_lights(SDL_Renderer* renderer);
    void load_shading_info     (const nlohmann::json& data);
    void load_collision_areas  (const nlohmann::json& data,
                                const std::string& dir_path,
                                int offset_x, int offset_y);
    void load_child_json_paths(const nlohmann::json& data,
                               const std::string& dir_path);
    void load_animations       (const nlohmann::json& anims_json,
                                const std::string& dir_path,
                                SDL_Renderer* renderer,
                                SDL_Texture*& base_sprite,
                                int& scaled_sprite_w,
                                int& scaled_sprite_h);
    void try_load_area(const nlohmann::json& data,
                       const std::string& key,
                       const std::string& dir,
                       std::unique_ptr<Area>& area_ref,
                       bool& flag_ref,
                       float scale,
                       int offset_x = 0,
                       int offset_y = 0);

    nlohmann::json anims_json_;
    std::string     dir_path_;
};
