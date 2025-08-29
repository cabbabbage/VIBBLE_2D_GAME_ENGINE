#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include "button.hpp"

class AssetInfo;
class Input;
class Slider;
class Checkbox;
class TextBox;

// Right-side editor panel for AssetInfo basic values
// - Occupies right 1/3 of the screen with semi-transparent background
// - Groups similar fields and uses Slider/Checkbox/TextBox
// - Persists changes to info.json immediately on adjustment
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

    // Feed high-level input (for scrolling)
    void update(const Input& input, int screen_w, int screen_h);
    // Feed raw SDL events for widgets (mouse + text input)
    void handle_event(const SDL_Event& e);

    void render(SDL_Renderer* r, int screen_w, int screen_h) const;

private:
    void build_widgets();
    void layout_widgets(int screen_w, int screen_h) const;
    void commit_scalar_changes();
    void save_now() const;

    // Areas UI helpers
    void rebuild_area_buttons();
    SDL_Texture* get_default_frame_texture() const;
    SDL_Texture* ensure_default_frame_texture(SDL_Renderer* r) const;
    bool has_area_key(const std::string& key) const;
    void apply_area_from_points(const std::string& key,
                                const std::vector<std::pair<int,int>>& pts) const;

private:
    bool visible_ = false;
    std::shared_ptr<AssetInfo> info_{};
    std::unique_ptr<Button> b_close_;
    // Widgets (owned)
    std::unique_ptr<Slider>   s_z_threshold_;
    std::unique_ptr<Slider>   s_min_same_type_;
    std::unique_ptr<Slider>   s_min_all_;
    std::unique_ptr<Slider>   s_scale_pct_;

    std::unique_ptr<Checkbox> c_passable_;
    std::unique_ptr<Checkbox> c_shading_;
    std::unique_ptr<Checkbox> c_flipable_;

    std::unique_ptr<TextBox>  t_type_;
    std::unique_ptr<TextBox>  t_tags_;
    std::unique_ptr<TextBox>  t_blend_;

    // Scroll state
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;

    // Cached panel rect (computed per frame)
    mutable SDL_Rect panel_ {0,0,0,0};

    // Last known screen size (for event-time layout refresh)
    mutable int last_screen_w_ = 0;
    mutable int last_screen_h_ = 0;

    // Guard to avoid recursive synthetic -> handle_event -> update loops
    mutable bool synthesizing_ = false;

    // Request to rebuild animations (e.g., after scale change)
    mutable bool reload_pending_ = false;

    // Area edit/create buttons (ordered)
    std::vector<std::pair<std::string, std::unique_ptr<Button>>> area_buttons_;
    // Human-readable names for keys (parallel to area_buttons_ order)
    std::vector<std::string> area_labels_;

    // Pending action from event to perform during render (needs renderer)
    mutable bool pending_area_action_ = false;
    mutable std::string pending_area_key_;
    mutable bool pending_create_ = false;
};

