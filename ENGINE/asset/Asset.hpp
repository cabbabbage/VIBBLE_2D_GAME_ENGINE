#ifndef ASSET_HPP
#define ASSET_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <SDL.h>
#include <limits>

#include "animation_manager.hpp"

#include "utils/area.hpp"
#include "asset_info.hpp"
#include "asset_library.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "utils/light_source.hpp"

#include "asset_controller.hpp"

class view;
class Assets;
class Input;

struct StaticLight {
    LightSource* source = nullptr;
    SDL_Point offset{0, 0};
    double alpha_percentage = 1.0;
};


class Asset {

	public:
    Area get_area(const std::string& name) const;
    Asset(std::shared_ptr<AssetInfo> info,
          const Area& spawn_area,
          SDL_Point start_pos,
          int depth,
          Asset* parent = nullptr,
          const std::string& spawn_id = std::string{},
          const std::string& spawn_method = std::string{});
    Asset(const Asset& other);
    Asset& operator=(const Asset& other);
    Asset(Asset&&) noexcept = default;
    Asset& operator=(Asset&&) noexcept = default;
    ~Asset();
    void finalize_setup();
    void set_position(SDL_Point p);
    void update();
    void change_animation_now(const std::string& name);
    void change_animation_qued(const std::string& name);
    void update_animation_manager();
    SDL_Texture* get_current_frame() const;
    std::string get_current_animation() const;
    std::string get_type() const;
    bool is_current_animation_locked_in_progress() const;
    bool is_current_animation_last_frame() const;
    bool is_current_animation_looping() const;
    void add_child(Asset* child);
    inline const std::vector<Asset*>& get_children() const { return children; }
    void add_static_light_source(LightSource* light, SDL_Point world, Asset* owner);
    void set_render_player_light(bool value);
    bool get_render_player_light() const;
    void set_z_offset(int z);
    void recompute_z_index();
    void set_shading_group(int x);
    bool is_shading_group_set() const;
    int  get_shading_group() const;
    int   current_frame_index = 0;
    SDL_Texture* get_final_texture() const;
    void set_final_texture(SDL_Texture* tex);
    void set_screen_position(SDL_Point s);
    inline int get_screen_x() const { return screen_X; }
    inline int get_screen_y() const { return screen_Y; }
    void set_view(view* v) { window = v; }
    void set_assets(Assets* a);
    Assets* get_assets() const { return assets_; }
    void deactivate();
    bool get_merge();
    void set_hidden(bool state);
    bool is_hidden();
    void Delete();
    void set_highlighted(bool state);
    bool is_highlighted();
    void set_selected(bool state);
    bool is_selected();
    Asset* parent = nullptr;
    std::shared_ptr<AssetInfo> info;
    std::string current_animation;
    SDL_Point pos{0, 0};
    int screen_X = 0;
    int screen_Y = 0;
    int z_index = 0;
    int z_offset = 0;
    int player_speed = 10;
    bool is_lit = false;
    bool has_base_shadow = false;
    bool active = false;
    bool flipped = false;
    bool render_player_light = false;
    double alpha_percentage = 1.0;
    float distance_to_player_sq = std::numeric_limits<float>::infinity();
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
    int cached_w = 0;
    int cached_h = 0;
    std::string spawn_id;
    std::string spawn_method;
    std::string next_animation;
	private:
    friend class AnimationManager;
    friend class Move;
    view* window = nullptr;
    bool highlighted = false;
    bool hidden = false;
    bool merged = false;
    bool selected = false;
    void set_flip();
    void set_z_index();

    float frame_progress = 0.0f;
    int  shading_group = 0;
    bool shading_group_set = false;
    SDL_Texture* final_texture = nullptr;
    std::unordered_map<std::string, std::vector<SDL_Texture*>> custom_frames;
    Assets* assets_ = nullptr;
    std::unique_ptr<AssetController>   controller_;
    std::unique_ptr<AnimationManager>  anim_;
};

#endif
