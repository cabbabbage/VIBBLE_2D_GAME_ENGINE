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
    void build_widgets();
    void layout_widgets(int screen_w, int screen_h) const;
    void commit_scalar_changes();
    void save_now() const;
    void rebuild_area_widgets();
    void open_area_editor(const std::string& name);
    void create_new_area();

	private:
    bool visible_ = false;
    std::shared_ptr<AssetInfo> info_{};
    std::unique_ptr<Button> b_close_;
    std::unique_ptr<Button> b_config_anim_;
    mutable SDL_Renderer* last_renderer_ = nullptr;
    bool areas_expanded_ = false;
    std::unique_ptr<Button> b_areas_toggle_;
    std::vector<std::unique_ptr<Button>> area_buttons_;
    std::unique_ptr<Button> b_create_area_;
    bool prompt_new_area_ = false;
    std::unique_ptr<TextBox> t_new_area_name_;
    std::unique_ptr<Slider>   s_z_threshold_;
    std::unique_ptr<Slider>   s_min_same_type_;
    std::unique_ptr<Slider>   s_min_all_;
    std::unique_ptr<Slider>   s_scale_pct_;
    std::unique_ptr<Checkbox> c_passable_;
    std::unique_ptr<Checkbox> c_flipable_;
    std::unique_ptr<TextBox>  t_type_;
    std::unique_ptr<TextBox>  t_tags_;
    mutable int scroll_ = 0;
    mutable int max_scroll_ = 0;
    mutable SDL_Rect panel_ {0,0,0,0};
};
