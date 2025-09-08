#ifndef ASSET_HPP
#define ASSET_HPP

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <SDL.h>
#include <limits>

#include "animation_manager.hpp" // ensure complete type for unique_ptr deleter

#include "utils/area.hpp"
#include "asset_info.hpp"
#include "asset_library.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "utils/light_source.hpp"

#include "asset_controller.hpp"

class view;
class Assets;
class Input;
// AnimationManager used via unique_ptr; complete type included above

struct StaticLight {
 LightSource* source = nullptr;
 int offset_x = 0;
 int offset_y = 0;
 double alpha_percentage = 1.0;
};
/*
  asset runtime object
  rendering and animation state
  controller attached by factory
*/
class Asset {
public:
 Area get_area(const std::string& name) const;

 Asset(std::shared_ptr<AssetInfo> info,
       const Area& spawn_area,
       int start_pos_X,
       int start_pos_Y,
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
 void set_position(int x, int y);
  void update();
  void change_animation(const std::string& name);

  // Allow controllers to drive animation progression explicitly.
  void update_animation_manager();

 SDL_Texture* get_current_frame() const;

  std::string get_current_animation() const;
  std::string get_type() const;

  // True when the current animation is marked locked and is still in progress.
  // For non-looping locked animations: returns true until the last frame is reached.
  // For looping locked animations: always returns true.
  bool is_current_animation_locked_in_progress() const;

  // Helpers for controllers: query current animation status
  bool is_current_animation_last_frame() const;
  bool is_current_animation_looping() const;

 void add_child(Asset* child);
 inline const std::vector<Asset*>& get_children() const { return children; }

 void add_static_light_source(LightSource* light, int world_x, int world_y, Asset* owner);

 void set_render_player_light(bool value);
 bool get_render_player_light() const;

 void set_z_offset(int z);
 // Recompute z-index based on current position and thresholds
 void recompute_z_index();
 void set_shading_group(int x);
 bool is_shading_group_set() const;
 int  get_shading_group() const;

 SDL_Texture* get_final_texture() const;
 void set_final_texture(SDL_Texture* tex);

 void set_screen_position(int sx, int sy);
 inline int get_screen_x() const { return screen_X; }
 inline int get_screen_y() const { return screen_Y; }

 void set_view(view* v) { window = v; }

 // lifetime and ownership
 void set_assets(Assets* a);
 Assets* get_assets() const { return assets_; }

 // misc flags
 void deactivate();
 bool get_merge();

 void set_hidden(bool state);
 bool is_hidden();
  void set_remove();
  bool needs_removal() const;
  // Remove this asset from its owner and destroy it.
  void Delete();

 void set_highlighted(bool state);
 bool is_highlighted();
 void set_selected(bool state);
 bool is_selected();

 // public data commonly read in render
 Asset* parent = nullptr;
 std::shared_ptr<AssetInfo> info;
 std::string current_animation;
 int pos_X = 0;
 int pos_Y = 0;
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

  // Runtime helper: Euclidean distance to the current player (in world units).
  // Updated once per frame by AssetsManager after the player moves.
  float distance_to_player = std::numeric_limits<float>::infinity();

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

 // spawn metadata
 std::string spawn_id;
 std::string spawn_method;

private:
 // animation manager drives private animation state
 friend class AnimationManager;
 friend class Move; // allow Move helper to adjust z-index

 view* window = nullptr;
 bool highlighted = false;
 bool hidden = false;
 bool merged = false;
 bool remove = false;
 bool selected = false;

 void set_flip();
 void set_z_index();

 // animation internals
 std::string next_animation;
 int   current_frame_index = 0;
 float frame_progress = 0.0f;

 int  shading_group = 0;
 bool shading_group_set = false;

 SDL_Texture* final_texture = nullptr;
 std::unordered_map<std::string, std::vector<SDL_Texture*>> custom_frames;

 Assets* assets_ = nullptr;

 // runtime helpers
 std::unique_ptr<AssetController>   controller_;
 std::unique_ptr<AnimationManager>  anim_;
};

#endif
