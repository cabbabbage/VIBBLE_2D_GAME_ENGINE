#ifndef ASSET_HPP
#define ASSET_HPP

#include <string>
#include <vector>
#include <memory>
#include <SDL.h>
#include <limits>

#include "utils/area.hpp"
#include "asset_info.hpp"
#include "utils/light_source.hpp"

#include "asset_controller.hpp"
#include "animation_update.hpp"

class camera;
class Assets;
class Input;
class AnimationFrame;
class AssetInfoUI;
class RenderAsset;

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

    void update();
    SDL_Texture* get_current_frame() const;
    std::string get_current_animation() const;
    bool is_current_animation_locked_in_progress() const;
    bool is_current_animation_last_frame() const;
    bool is_current_animation_looping() const;
    void add_child(Asset* child);

    void add_static_light_source(LightSource* light, SDL_Point world, Asset* owner);
    void set_render_player_light(bool value);
    bool get_render_player_light() const;
    void set_z_offset(int z);
    void set_shading_group(int x);
    bool is_shading_group_set() const;
    int  get_shading_group() const;
    class AnimationFrame* current_frame = nullptr;
    SDL_Texture* get_final_texture() const;
    void set_final_texture(SDL_Texture* tex);
    void set_camera(camera* v) { window = v; }
    void set_assets(Assets* a);
    Assets* get_assets() const { return assets_; }
    void deactivate();

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
    int z_index = 0;
    int z_offset = 0;
    bool active = false;
    bool flipped = false;
    bool render_player_light = false;
    double alpha_percentage = 1.0;
    float distance_to_player_sq = std::numeric_limits<float>::infinity();
    float distance_from_camera = 0.0f;
    float angle_from_camera = 0.0f;

    std::vector<Asset*> children;
    std::vector<StaticLight> static_lights;
    int depth = 0;
    bool is_shaded = false;
    bool dead = false;
    bool static_frame = true;
    int cached_w = 0;
    int cached_h = 0;
    std::string spawn_id;
    std::string spawn_method;
    std::unique_ptr<AnimationUpdate> anim_;
        private:
    friend class AnimationUpdate;
    friend class Move;
    friend class AssetInfoUI;
    friend class RenderAsset;
    camera* window = nullptr;
    bool highlighted = false;
    bool hidden = false;
    bool selected = false;
    void set_flip();
    void set_z_index();

    float frame_progress = 0.0f;
    int  shading_group = 0;
    bool shading_group_set = false;
    SDL_Texture* final_texture = nullptr;
    Assets* assets_ = nullptr;
    std::unique_ptr<AssetController>   controller_;

    struct DownscaleCacheEntry {
        float        scale   = 1.0f;
        int          width   = 0;
        int          height  = 0;
        SDL_Texture* texture = nullptr;
};

    void clear_downscale_cache();

    std::vector<DownscaleCacheEntry> downscale_cache_;

    SDL_Texture* last_scaled_texture_      = nullptr;
    SDL_Texture* last_scaled_source_       = nullptr;
    int          last_scaled_w_            = 0;
    int          last_scaled_h_            = 0;
    float        last_scaled_camera_scale_ = -1.0f;
};

#endif
