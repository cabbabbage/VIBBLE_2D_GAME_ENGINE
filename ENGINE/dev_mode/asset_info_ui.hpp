#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include "ui/button.hpp"

class AssetInfo;
class Input;
class Area;

class AssetInfoUI {

	public:
    AssetInfoUI();
    ~AssetInfoUI();
    void set_info(const std::shared_ptr<AssetInfo>& info);
    void clear_info();
    void open();
    void close();
    void toggle();
    bool is_visible() const { return visible_; }
    void update(const Input& input, int screen_w, int screen_h);
    void handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r, int screen_w, int screen_h) const;

  private:
    void layout_widgets(int screen_w, int screen_h) const;
    void save_now() const;
    void open_area_editor(const std::string& name);

  private:
    bool visible_ = false;
    std::shared_ptr<AssetInfo> info_{};
    std::unique_ptr<Button> b_config_anim_;
    mutable SDL_Renderer* last_renderer_ = nullptr;
    // Section-based UI
    std::vector<std::unique_ptr<class CollapsibleSection>> sections_;
    class Section_Areas* areas_section_ = nullptr; // non-owning ptr into sections_
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;
    mutable SDL_Rect panel_ {0,0,0,0};
};
