#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class DockableCollapsible;
class DropdownWidget;
class RangeSliderWidget;
class SliderWidget;
class ButtonWidget;
class Input;
class DMDropdown;
class DMRangeSlider;
class DMSlider;
class DMButton;

// UI panel for configuring a single asset entry in the spawn JSON
class AssetConfig {
public:
    AssetConfig();
    void set_position(int x, int y);
    void load(const nlohmann::json& asset);
    void open_panel();
    void close();
    bool visible() const;
    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    nlohmann::json to_json() const;
private:
    void rebuild_widgets();
    void rebuild_rows();
    std::unique_ptr<DockableCollapsible> panel_;
    std::vector<std::string> spawn_methods_;
    std::string name_;
    int method_ = 0;
    int min_ = 0;
    int max_ = 0;
    int grid_spacing_ = 100;
    int jitter_ = 0;
    int empty_ = 0;
    int border_ = 0;
    int sector_center_ = 0;
    int sector_range_ = 360;
    int percent_x_min_ = 0;
    int percent_x_max_ = 0;
    int percent_y_min_ = 0;
    int percent_y_max_ = 0;
    std::unique_ptr<DMDropdown> dd_method_;
    std::unique_ptr<DropdownWidget> dd_method_w_;
    std::unique_ptr<DMRangeSlider> s_range_;
    std::unique_ptr<RangeSliderWidget> s_range_w_;
    std::unique_ptr<DMSlider> s_grid_spacing_;
    std::unique_ptr<SliderWidget> s_grid_spacing_w_;
    std::unique_ptr<DMSlider> s_jitter_;
    std::unique_ptr<SliderWidget> s_jitter_w_;
    std::unique_ptr<DMSlider> s_empty_;
    std::unique_ptr<SliderWidget> s_empty_w_;
    std::unique_ptr<DMSlider> s_border_;
    std::unique_ptr<SliderWidget> s_border_w_;
    std::unique_ptr<DMSlider> s_sector_center_;
    std::unique_ptr<SliderWidget> s_sector_center_w_;
    std::unique_ptr<DMSlider> s_sector_range_;
    std::unique_ptr<SliderWidget> s_sector_range_w_;
    std::unique_ptr<DMRangeSlider> s_percent_x_;
    std::unique_ptr<RangeSliderWidget> s_percent_x_w_;
    std::unique_ptr<DMRangeSlider> s_percent_y_;
    std::unique_ptr<RangeSliderWidget> s_percent_y_w_;
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;
};
