#pragma once

#include <SDL.h>
#include <vector>
#include <string>
#include <memory>

class Input;
class AssetInfo;
class AssetLibrary;






class AssetLibraryUI {
public:
    AssetLibraryUI();
    ~AssetLibraryUI();
    
    void toggle();
    bool is_visible() const { return visible_; }
    void open() { visible_ = true; }
    void close();
    
    void update(const Input& input,
                int screen_w,
                int screen_h,
                AssetLibrary& lib);
    
    void render(SDL_Renderer* r,
                AssetLibrary& lib,
                int screen_w,
                int screen_h) const;
    
    std::shared_ptr<AssetInfo> consume_selection();
private:
    void ensure_items(AssetLibrary& lib);
    SDL_Texture* get_default_frame_texture(const AssetInfo& info) const;
private:
    bool visible_ = false;
    
    std::vector<std::shared_ptr<AssetInfo>> items_;
    bool items_cached_ = false;
    
    int panel_w_ = 200;
    int padding_ = 10;
    int tile_size_ = 180;
    int gap_y_ = 10;
    int scroll_offset_ = 0;
    int max_scroll_ = 0;
    int hover_index_ = -1;
    std::shared_ptr<AssetInfo> selection_{};
};
