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
class TextBoxWidget;
class CheckboxWidget;
class Input;
class DMDropdown;
class DMRangeSlider;
class DMSlider;
class DMButton;
class DMTextBox;
class DMCheckbox;

// UI panel for configuring a single asset entry in the spawn JSON
class AssetConfigUI {
public:
    AssetConfigUI();

    void set_position(int x, int y);
    void load(const nlohmann::json& asset);
    void open_panel();
    void close();
    bool visible() const;

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    nlohmann::json to_json() const;
    bool is_point_inside(int x, int y) const;

private:
    struct CandidateRow {
        std::string name;
        int chance = 0;

        std::unique_ptr<DMTextBox> name_box;
        std::unique_ptr<DMSlider> chance_slider;
        std::unique_ptr<DMButton> del_button;

        std::unique_ptr<TextBoxWidget> name_w;
        std::unique_ptr<SliderWidget> chance_w;
        std::unique_ptr<ButtonWidget> del_w;
    };

    void rebuild_widgets();
    void rebuild_rows();
    void sync_json();
    void add_candidate(const std::string& name, int chance);
    void remove_candidate(const std::string& name);
    bool method_forces_single_quantity(const std::string& method) const;

    std::unique_ptr<DockableCollapsible> panel_;
    std::vector<std::string> spawn_methods_;
    std::string spawn_id_;

    nlohmann::json entry_;

    // Candidate rows
    std::vector<CandidateRow> candidates_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<ButtonWidget> add_button_w_;

    // Common config
    int method_ = 0;
    int min_number_ = 1;
    int max_number_ = 1;
    bool inherited_ = false;
    bool overlap_ = false;
    bool spacing_ = false;
    bool tag_ = false;
    int ep_x_min_ = 50;
    int ep_x_max_ = 50;
    int ep_y_min_ = 50;
    int ep_y_max_ = 50;

    // Method widgets
    std::unique_ptr<DMDropdown> dd_method_;
    std::unique_ptr<DropdownWidget> dd_method_w_;
    std::unique_ptr<DMRangeSlider> s_minmax_;
    std::unique_ptr<RangeSliderWidget> s_minmax_w_;

    // Perimeter
    std::unique_ptr<DMSlider> s_border_;
    std::unique_ptr<SliderWidget> s_border_w_;
    std::unique_ptr<DMSlider> s_sector_center_;
    std::unique_ptr<SliderWidget> s_sector_center_w_;
    std::unique_ptr<DMSlider> s_sector_range_;
    std::unique_ptr<SliderWidget> s_sector_range_w_;

    // Percent
    std::unique_ptr<DMRangeSlider> s_percent_x_;
    std::unique_ptr<RangeSliderWidget> s_percent_x_w_;
    std::unique_ptr<DMRangeSlider> s_percent_y_;
    std::unique_ptr<RangeSliderWidget> s_percent_y_w_;

    // Checkboxes
    std::unique_ptr<DMCheckbox> cb_inherited_;
    std::unique_ptr<CheckboxWidget> cb_inherited_w_;
    std::unique_ptr<DMCheckbox> cb_overlap_;
    std::unique_ptr<CheckboxWidget> cb_overlap_w_;
    std::unique_ptr<DMCheckbox> cb_spacing_;
    std::unique_ptr<CheckboxWidget> cb_spacing_w_;
    std::unique_ptr<DMCheckbox> cb_tag_;
    std::unique_ptr<CheckboxWidget> cb_tag_w_;

    // Done button
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;
};
