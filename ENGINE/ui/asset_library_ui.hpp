#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <memory>

class Input;
class AssetInfo;
class AssetLibrary;

// Lightweight overlay to browse/select assets.
// - Fixed panel on the left (200px width, full height)
// - Semi-transparent background; right side remains untouched (fully transparent)
// - Scrollable list of square thumbnails; hover highlights orange-ish
// - Click selects and closes; selection retrievable via consume_selection()
class AssetLibraryUI {
public:
    AssetLibraryUI();
    ~AssetLibraryUI();
    // Toggle visibility
    void toggle();
    bool is_visible() const { return visible_; }
    void open() { visible_ = true; }
    void close();
    // Update state from input (scroll/hover/click)
    void update(const Input& input,
                int screen_w,
                int screen_h,
                AssetLibrary& lib);
    // Render overlay elements
    void render(SDL_Renderer* r,
                AssetLibrary& lib,
                int screen_w,
                int screen_h) const;
    // Retrieve selected asset info (if any) and clear it
    std::shared_ptr<AssetInfo> consume_selection();
private:
    void ensure_items(AssetLibrary& lib);
    SDL_Texture* get_default_frame_texture(const AssetInfo& info) const;
private:
    bool visible_ = false;
    // Cached ordered items of library for stable UI ordering
    std::vector<std::shared_ptr<AssetInfo>> items_;
    bool items_cached_ = false;
    // UI state
    int panel_w_ = 200;
    int padding_ = 10;
    int tile_size_ = 180;
    int gap_y_ = 10;
    int scroll_offset_ = 0;
    int max_scroll_ = 0;
    int hover_index_ = -1;
    std::shared_ptr<AssetInfo> selection_{};
};
