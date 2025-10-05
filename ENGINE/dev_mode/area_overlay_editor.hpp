#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class Assets;
class Asset;
class AssetInfo;
class Input;
class DockableCollapsible;
class DMButton;
class DMSlider;
class camera;
class Room;

class AreaOverlayEditor {
public:
    AreaOverlayEditor();
    ~AreaOverlayEditor();

    void attach_assets(Assets* a) { assets_ = a; }

    // Begin editor anchored to an asset (uses asset bottom-center as anchor)
    bool begin(AssetInfo* info, Asset* asset, const std::string& area_name);
    // Begin editor anchored to an arbitrary map point; optional asset may be provided for mask autogen
    bool begin_at_point(AssetInfo* info, SDL_Point anchor_world, const std::string& area_name, Asset* asset = nullptr);
    // Begin editor for a room-level area (trigger/spawning etc.) optionally focusing around a specific world point
    bool begin_for_room(Room* room, const std::string& area_name, const std::string& area_type);
    bool begin_for_room(Room* room, const std::string& area_name, const std::string& area_type, SDL_Point focus_world);
    void cancel();
    bool is_active() const { return active_; }
    bool consume_saved_flag() { bool v = saved_since_begin_; saved_since_begin_ = false; return v; }
    Uint8 mask_alpha_;
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r);
    void set_on_saved(std::function<void()> cb) { on_saved_callback_ = std::move(cb); }

private:
    enum class Mode { Mask, Geometry };

    void ensure_toolbox();
    void rebuild_toolbox_rows();
    void set_mode(Mode mode);
    void reset_mask_crop_values();
    bool generate_mask_from_asset(SDL_Renderer* renderer);
    void apply_mask_crop();
    void discard_autogen_base();
    void position_toolbox_near_anchor(int screen_w, int screen_h);
    void clear_mask();
    void upload_mask();
    void ensure_mask_contains(int lx, int ly, int radius);
    void init_mask_from_existing_area();
    std::vector<SDL_Point> extract_edge_points(int step = 1) const;
    std::vector<SDL_Point> trace_polygon_from_mask() const;
    void save_area();
    void rebuild_mask_from_geometry();

    struct OverlayTransform {
        SDL_Point anchor_screen{0, 0};
        SDL_Rect  dst{0, 0, 0, 0};
        SDL_Point pivot_in_dst{0, 0};
        float     scale_x = 1.0f;
        float     scale_y = 1.0f;
    };
    bool compute_overlay_transform(class camera& cam, OverlayTransform& out) const;

private:
    Assets* assets_ = nullptr;
    AssetInfo* info_ = nullptr;
    Asset* asset_ = nullptr;
    Room* room_ = nullptr;
    std::string area_name_;
    std::string room_area_type_;
    bool active_ = false;

    SDL_Surface* mask_ = nullptr;
    SDL_Texture* mask_tex_ = nullptr;
    int canvas_w_ = 0;
    int canvas_h_ = 0;

    int mask_origin_x_ = 0;
    int mask_origin_y_ = 0;

    Mode mode_ = Mode::Mask;

    std::unique_ptr<DockableCollapsible> toolbox_;
    std::unique_ptr<DMButton> btn_mask_;
    std::unique_ptr<DMButton> btn_geom_;
    std::unique_ptr<DMButton> btn_save_;
    std::unique_ptr<DMSlider> crop_left_slider_;
    std::unique_ptr<DMSlider> crop_right_slider_;
    std::unique_ptr<DMSlider> crop_top_slider_;
    std::unique_ptr<DMSlider> crop_bottom_slider_;
    std::vector<std::unique_ptr<class Widget>> owned_widgets_;

    int crop_left_px_ = 0;
    int crop_right_px_ = 0;
    int crop_top_px_ = 0;
    int crop_bottom_px_ = 0;

    int applied_crop_left_ = -1;
    int applied_crop_right_ = -1;
    int applied_crop_top_ = -1;
    int applied_crop_bottom_ = -1;

    SDL_Surface* mask_autogen_base_ = nullptr;
    bool pending_mask_generation_ = false;

    bool saved_since_begin_ = false;
    bool toolbox_autoplace_done_ = false;

    std::function<void()> on_saved_callback_;

    // Anchor handling
    SDL_Point anchor_world_{0, 0};
    bool      has_anchor_ = false;
    bool      flipped_ = false; // mirrors asset flip when asset-driven; false for point-only usage

    // Geometry tool state
    std::vector<SDL_Point> geometry_points_;
    bool geometry_dirty_ = false;

};
