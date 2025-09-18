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

// Live, in-scene area editing overlay for dev mode.
// - Renders the current area mask over the selected asset using camera mapping
// - Floating toolbox (Draw / Erase / Save) positions to the left of the asset
// - Drawing appends, erasing clears, save replaces the area in AssetInfo
class AreaOverlayEditor {
public:
    AreaOverlayEditor();
    ~AreaOverlayEditor();

    void attach_assets(Assets* a) { assets_ = a; }

    // Begin editing `area_name` for the first selected asset (or provided asset)
    bool begin(AssetInfo* info, Asset* asset, const std::string& area_name);
    void cancel();
    bool is_active() const { return active_; }
    // Returns true exactly once after a successful save, then resets.
    bool consume_saved_flag() { bool v = saved_since_begin_; saved_since_begin_ = false; return v; }
    Uint8 mask_alpha_; 
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r);

private:
    void ensure_toolbox();
    void rebuild_toolbox_rows();
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
    enum class Mode { Draw, Erase };
    Mode mode_ = Mode::Draw;
    int brush_radius_ = 10;
    bool drawing_ = false;

    // UI
    std::unique_ptr<DockableCollapsible> toolbox_;
    std::unique_ptr<DMButton> btn_draw_;
    std::unique_ptr<DMButton> btn_erase_;
    std::unique_ptr<DMButton> btn_save_;
    std::vector<std::unique_ptr<class Widget>> owned_widgets_;

    bool saved_since_begin_ = false;
    bool toolbox_autoplace_done_ = false;

    bool camera_override_active_ = false;
    bool prev_camera_realism_enabled_ = true;
    bool prev_camera_parallax_enabled_ = true;
};


