
#ifndef ASSET_HPP
#define ASSET_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <SDL.h>

#include "utils/area.hpp"
#include "asset_info.hpp"
#include "asset_library.hpp"
#include "spawn\asset_spawn_planner.hpp"
#include "utils\light_source.hpp"


class view;

struct StaticLight {
    LightSource* source = nullptr;
    int offset_x = 0;
    int offset_y = 0;
    double alpha_percentage = 1.0;
};

class Asset {
public:
    Area get_area(const std::string& name) const;

    Asset(std::shared_ptr<AssetInfo> info,
          const Area& spawn_area,
          int start_pos_X,
          int start_pos_Y,
          int depth,
          Asset* parent = nullptr);

    void finalize_setup();
    void set_position(int x, int y);
    void update();
    void change_animation(const std::string& name);

    SDL_Texture* get_current_frame() const;

    std::string get_current_animation() const;
    std::string get_type() const;

    
    void add_child(Asset* child);
    inline const std::vector<Asset*>& get_children() const { return children; }

    void add_static_light_source(LightSource* light, int world_x, int world_y, Asset* owner);

    void set_render_player_light(bool value);
    bool get_render_player_light() const;

    void set_z_offset(int z);
    void set_shading_group(int x);
    bool is_shading_group_set() const;
    int get_shading_group() const;

    SDL_Texture* get_final_texture() const;
    void set_final_texture(SDL_Texture* tex);

    // Screen-space position (post-parallax) updated each frame
    void set_screen_position(int sx, int sy);
    inline int get_screen_x() const { return screen_X; }
    inline int get_screen_y() const { return screen_Y; }

    
    void set_view(view* v) { window = v; }

    Asset* parent = nullptr;
    std::shared_ptr<AssetInfo> info;
    std::string current_animation;
    int pos_X = 0;
    int pos_Y = 0;
    int screen_X = 0; // computed by Parallax each frame
    int screen_Y = 0; // computed by Parallax each frame
    int z_index = 0;
    int z_offset = 0;
    int player_speed = 10;
    bool is_lit = false;
    bool has_base_shadow = false;
    bool active = false;
    bool flipped = false;
    bool render_player_light = false;

    double alpha_percentage = 1.0;

    Area spawn_area_local;
    std::vector<Area> base_areas;
    std::vector<Area> areas;

    std::vector<Asset*> children;

    std::vector<StaticLight> static_lights;
    int gradient_shadow = 0;
    int depth = 0;
    bool has_shading = false;
    bool dead = false;
    bool static_frame = true;

    void deactivate();
    int cached_w = 0;
    int cached_h = 0;
    bool get_merge();

    void set_hidden(bool state);
    bool is_hidden();
    void set_remove();

    void set_highlighted(bool state);
    bool is_highlighted();
    void set_selected(bool state);
    bool is_selected();

private:
    view* window = nullptr;        
    bool highlighted = false;
    bool hidden = false;
    bool merged = false;
    bool remove = false;

    void set_flip();
    void set_z_index();
    bool selected = false;
    std::string next_animation;
    int current_frame_index = 0;
    float frame_progress = 0.0f;
    int shading_group = 0;
    bool shading_group_set = false;

    SDL_Texture* final_texture = nullptr;
    std::unordered_map<std::string, std::vector<SDL_Texture*>> custom_frames;
};

#endif 
