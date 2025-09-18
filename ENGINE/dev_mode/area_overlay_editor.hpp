#pragma once

#include <SDL.h>

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

// Live, in-scene area editing overlay for dev mode.
// - Renders the current area mask over the selected asset using camera mapping
// - Floating toolbox (Draw / Erase / Save) positions to the left of the asset
// - Drawing appends, erasing clears, save replaces the area in AssetInfo
class AreaOverlayEditor {
public:
    AreaOverlayEditor();
    ~AreaOverlayEditor();

    void attach_assets(Assets* a) { assets_ = a; }

    bool begin(AssetInfo* info, Asset* asset, const std::string& area_name);
    void cancel();
    bool is_active() const { return active_; }
    bool consume_saved_flag() { bool v = saved_since_begin_; saved_since_begin_ = false; return v; }
    Uint8 mask_alpha_;
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r);

private:
    enum class Mode { Draw, Erase, Mask };

    void ensure_toolbox();
    void rebuild_toolbox_rows();
    void set_mode(Mode mode);
    void update_tool_button_states();
    void reset_mask_crop_values();
    bool generate_mask_from_asset(SDL_Renderer* renderer);
    void apply_mask_crop();
    void discard_autogen_base();
    void position_toolbox_left_of_asset(int screen_w, int screen_h);
    void clear_mask();
    void upload_mask();
    void stamp(int cx, int cy, int radius, bool erase);
    void apply_camera_override(bool enable);
    void ensure_mask_contains(int lx, int ly, int radius);
    void init_mask_from_existing_area();
    std::vector<SDL_Point> extract_edge_points(int step = 1) const;
    std::vector<SDL_Point> trace_polygon_from_mask() const;
    void save_area();

private:
    Assets* assets_ = nullptr;
    AssetInfo* info_ = nullptr;
    Asset* asset_ = nullptr;
    std::string area_name_;
    bool active_ = false;

    // Mask in asset-local (unflipped) canvas coordinates
    SDL_Surface* mask_ = nullptr;
    SDL_Texture* mask_tex_ = nullptr; // updated from mask_ for rendering
    int canvas_w_ = 0;
    int canvas_h_ = 0;
    // Local-coordinate origin of mask_ top-left relative to asset's local canvas space
    // Pivot (asset pos) corresponds to local (canvas_w_/2, canvas_h_)
    int mask_origin_x_ = 0;
    int mask_origin_y_ = 0;

    // Edit state
    Mode mode_ = Mode::Draw;
    int brush_radius_ = 10;
    bool drawing_ = false;

    // UI
    std::unique_ptr<DockableCollapsible> toolbox_;
    std::unique_ptr<DMButton> btn_draw_;
    std::unique_ptr<DMButton> btn_erase_;
    std::unique_ptr<DMButton> btn_mask_;
    std::unique_ptr<DMButton> btn_save_;
    std::unique_ptr<DMSlider> brush_slider_;
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

    bool camera_override_active_ = false;
    bool prev_camera_realism_enabled_ = true;
    bool prev_camera_parallax_enabled_ = true;
};
