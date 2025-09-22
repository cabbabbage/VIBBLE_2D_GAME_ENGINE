#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class DockableCollapsible;
class Widget;
class LabelWidget;
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

// UI panel for configuring a single spawn group entry in the spawn JSON
class SpawnGroupConfigUI {
public:
    SpawnGroupConfigUI();
    ~SpawnGroupConfigUI();

    struct ChangeSummary {
        bool method_changed = false;
        bool quantity_changed = false;
        std::string method;
    };

    void set_position(int x, int y);
    SDL_Point position() const;
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

    void set_ownership_label(const std::string& label, SDL_Color color);

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
        std::unique_ptr<LabelWidget> chance_label;
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

    std::unique_ptr<LabelWidget> ownership_label_;
    std::string ownership_text_;
    SDL_Color ownership_color_{255, 255, 255, 255};
    bool has_ownership_color_ = false;

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
    std::unique_ptr<LabelWidget> s_minmax_label_;

    // Percent (read-only summary)
    std::unique_ptr<LabelWidget> percent_x_label_;
    std::unique_ptr<LabelWidget> percent_y_label_;

    // Exact offsets (read-only summary)
    std::unique_ptr<LabelWidget> exact_offset_label_;
    std::unique_ptr<LabelWidget> exact_room_label_;

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

    int total_chance() const;
    void refresh_chance_labels(int total_chance);
};
