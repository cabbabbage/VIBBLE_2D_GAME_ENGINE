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
class AssetsConfig;
class Room;

// Top-level room configuration panel with room settings and asset list
class RoomConfigurator {
public:
    RoomConfigurator();
    ~RoomConfigurator();
    void set_position(int x, int y);
    void open(const nlohmann::json& room_data);
    void open(Room* room);
    void close();
    bool visible() const;
    bool any_panel_visible() const;
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    nlohmann::json build_json() const;
    void open_asset_config(const std::string& id, int x, int y);
    void close_asset_configs();
    bool is_point_inside(int x, int y) const;
private:
    void rebuild_rows();
    std::unique_ptr<DockableCollapsible> panel_;
    std::unique_ptr<AssetsConfig> assets_cfg_;
    std::vector<std::string> room_geom_options_;
    int room_w_min_ = 0;
    int room_w_max_ = 0;
    int room_h_min_ = 0;
    int room_h_max_ = 0;
    int room_geom_ = 0;
    bool room_is_spawn_ = false;
    bool room_is_boss_ = false;
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
};
