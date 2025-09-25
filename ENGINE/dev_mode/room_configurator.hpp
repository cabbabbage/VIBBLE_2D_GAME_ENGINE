#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "DockableCollapsible.hpp"
class DropdownWidget;
class RangeSliderWidget;
class SliderWidget;
class CheckboxWidget;
class ButtonWidget;
class Input;
class DMDropdown;
class DMRangeSlider;
class DMCheckbox;
class DMTextBox;
class DMButton;
class TextBoxWidget;
class Room;
class Widget;

// Top-level room configuration panel with room settings and asset list
class RoomConfigurator : public DockableCollapsible {
public:
    RoomConfigurator();
    ~RoomConfigurator();
    void set_bounds(const SDL_Rect& bounds);
    void open(const nlohmann::json& room_data);
    void open(Room* room);
    bool refresh_spawn_groups(const nlohmann::json& room_data);
    bool refresh_spawn_groups(Room* room);
    void close();
    bool visible() const;
    bool any_panel_visible() const;
    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    nlohmann::json build_json() const;
    bool is_point_inside(int x, int y) const;
    void set_spawn_group_callbacks(std::function<void(const std::string&)> on_edit,
                                   std::function<void(const std::string&)> on_duplicate,
                                   std::function<void(const std::string&)> on_delete,
                                   std::function<void()> on_add);
private:
    void load_from_json(const nlohmann::json& data);
    void apply_bounds_if_needed();
    void rebuild_rows();
    std::string selected_geometry() const;
    bool should_rebuild_with(const nlohmann::json& data) const;
    SDL_Rect bounds_{0,0,0,0};
    SDL_Rect applied_bounds_{-1,-1,0,0};
    bool using_docked_bounds_ = false;
    std::vector<std::string> room_geom_options_;
    Room* room_ = nullptr;
    nlohmann::json loaded_json_;
    std::string room_name_;
    int room_w_min_ = 1500;
    int room_w_max_ = 10000;
    int room_h_min_ = 1500;
    int room_h_max_ = 10000;
    int room_geom_ = 0;
    bool spawn_groups_from_assets_ = false;
    bool room_is_spawn_ = false;
    bool room_is_boss_ = false;
    bool room_inherits_assets_ = false;
    bool is_trail_context_ = false;
    int edge_smoothness_ = 2;
    int curvyness_ = 2;
    std::unique_ptr<DMRangeSlider> room_w_slider_;
    std::unique_ptr<RangeSliderWidget> room_w_slider_w_;
    std::unique_ptr<DMRangeSlider> room_h_slider_;
    std::unique_ptr<RangeSliderWidget> room_h_slider_w_;
    std::unique_ptr<Widget> room_w_label_;
    std::unique_ptr<Widget> room_h_label_;
    std::unique_ptr<DMDropdown> room_geom_dd_;
    std::unique_ptr<DropdownWidget> room_geom_dd_w_;
    std::unique_ptr<DMSlider> edge_smoothness_sl_;
    std::unique_ptr<SliderWidget> edge_smoothness_w_;
    std::unique_ptr<DMSlider> curvyness_sl_;
    std::unique_ptr<SliderWidget> curvyness_w_;
    std::unique_ptr<DMCheckbox> room_spawn_cb_;
    std::unique_ptr<CheckboxWidget> room_spawn_cb_w_;
    std::unique_ptr<DMCheckbox> room_boss_cb_;
    std::unique_ptr<CheckboxWidget> room_boss_cb_w_;
    std::unique_ptr<DMCheckbox> room_inherit_cb_;
    std::unique_ptr<CheckboxWidget> room_inherit_cb_w_;
    std::unique_ptr<DMTextBox> room_name_lbl_;
    std::unique_ptr<TextBoxWidget> room_name_lbl_w_;
    struct SpawnGroupRow {
        std::string spawn_id;
        std::unique_ptr<Widget> summary;
        std::unique_ptr<DMButton> edit_btn;
        std::unique_ptr<ButtonWidget> edit_btn_w;
        std::unique_ptr<DMButton> duplicate_btn;
        std::unique_ptr<ButtonWidget> duplicate_btn_w;
        std::unique_ptr<DMButton> delete_btn;
        std::unique_ptr<ButtonWidget> delete_btn_w;
    };
    std::function<void(const std::string&)> on_spawn_edit_;
    std::function<void(const std::string&)> on_spawn_duplicate_;
    std::function<void(const std::string&)> on_spawn_delete_;
    std::function<void()> on_spawn_add_;
    std::unique_ptr<Widget> room_section_label_;
    std::unique_ptr<Widget> spawn_groups_label_;
    std::vector<std::unique_ptr<SpawnGroupRow>> spawn_rows_;
    std::unique_ptr<DMButton> add_group_btn_;
    std::unique_ptr<ButtonWidget> add_group_btn_w_;
    std::unique_ptr<Widget> empty_spawn_label_;
};
