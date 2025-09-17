#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class DockableCollapsible;
class Widget;
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
class SearchAssets;

// UI panel for configuring a single asset entry in the spawn JSON
class AssetConfigUI {
public:
    AssetConfigUI();

    struct ChangeSummary {
        bool method_changed = false;
        bool quantity_changed = false;
        std::string method;
    };

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
    ChangeSummary consume_change_summary();

private:
    struct CandidateRow {
        std::string name;
        int chance = 0;
        size_t index = 0;
        bool placeholder = false;

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
    void remove_candidate(size_t index);
    bool method_forces_single_quantity(const std::string& method) const;
    void ensure_search();
    void handle_method_change();

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
    bool overlap_ = false;
    bool spacing_ = false;
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
    std::unique_ptr<DMRangeSlider> s_perimeter_offset_x_;
    std::unique_ptr<RangeSliderWidget> s_perimeter_offset_x_w_;
    std::unique_ptr<DMRangeSlider> s_perimeter_offset_y_;
    std::unique_ptr<RangeSliderWidget> s_perimeter_offset_y_w_;

    // Percent (read-only summary)
    std::unique_ptr<DMTextBox> percent_x_box_;
    std::unique_ptr<Widget> percent_x_w_;
    std::unique_ptr<DMTextBox> percent_y_box_;
    std::unique_ptr<Widget> percent_y_w_;

    // Exact offsets (read-only summary)
    std::unique_ptr<DMTextBox> exact_offset_box_;
    std::unique_ptr<Widget> exact_offset_w_;
    std::unique_ptr<DMTextBox> exact_room_box_;
    std::unique_ptr<Widget> exact_room_w_;

    // Checkboxes
    std::unique_ptr<DMCheckbox> cb_overlap_;
    std::unique_ptr<CheckboxWidget> cb_overlap_w_;
    std::unique_ptr<DMCheckbox> cb_spacing_;
    std::unique_ptr<CheckboxWidget> cb_spacing_w_;

    // Done button
    std::unique_ptr<DMButton> b_done_;
    std::unique_ptr<ButtonWidget> b_done_w_;

    std::unique_ptr<SearchAssets> search_;

    ChangeSummary pending_summary_;
    std::string baseline_method_;
    int baseline_min_ = 0;
    int baseline_max_ = 0;
};
