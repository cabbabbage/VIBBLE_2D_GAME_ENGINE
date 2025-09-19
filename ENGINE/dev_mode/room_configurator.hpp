#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class DockableCollapsible;
class DropdownWidget;
class RangeSliderWidget;
class CheckboxWidget;
class Input;
class DMDropdown;
class DMRangeSlider;
class DMCheckbox;
class DMTextBox;
class TextBoxWidget;
class Room;

// Top-level room configuration panel with room settings and asset list
class RoomConfigurator {
public:
    RoomConfigurator();
    ~RoomConfigurator();
    void set_bounds(const SDL_Rect& bounds);
    void open(const nlohmann::json& room_data);
    void open(Room* room);
    void close();
    bool visible() const;
    bool any_panel_visible() const;
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    nlohmann::json build_json() const;
    bool is_point_inside(int x, int y) const;
private:
    void rebuild_rows();
    std::unique_ptr<DockableCollapsible> panel_;
    SDL_Rect bounds_{0,0,0,0};
    std::vector<std::string> room_geom_options_;
    Room* room_ = nullptr;
    std::string room_name_;
    int room_w_min_ = 1000;
    int room_w_max_ = 10000;
    int room_h_min_ = 1000;
    int room_h_max_ = 10000;
    int room_geom_ = 0;
    bool room_is_spawn_ = false;
    bool room_is_boss_ = false;
    bool room_inherits_assets_ = false;
    std::unique_ptr<DMRangeSlider> room_w_slider_;
    std::unique_ptr<RangeSliderWidget> room_w_slider_w_;
    std::unique_ptr<DMRangeSlider> room_h_slider_;
    std::unique_ptr<RangeSliderWidget> room_h_slider_w_;
    std::unique_ptr<DMDropdown> room_geom_dd_;
    std::unique_ptr<DropdownWidget> room_geom_dd_w_;
    std::unique_ptr<DMCheckbox> room_spawn_cb_;
    std::unique_ptr<CheckboxWidget> room_spawn_cb_w_;
    std::unique_ptr<DMCheckbox> room_boss_cb_;
    std::unique_ptr<CheckboxWidget> room_boss_cb_w_;
    std::unique_ptr<DMCheckbox> room_inherit_cb_;
    std::unique_ptr<CheckboxWidget> room_inherit_cb_w_;
    std::unique_ptr<DMTextBox> room_name_lbl_;
    std::unique_ptr<TextBoxWidget> room_name_lbl_w_;
};
