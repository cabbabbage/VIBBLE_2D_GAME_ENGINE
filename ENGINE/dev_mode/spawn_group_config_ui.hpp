#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "DockableCollapsible.hpp"

class Input;
class ButtonWidget;
class CheckboxWidget;
class DropdownWidget;
class LabelWidget;
class RangeSliderWidget;
class SliderWidget;
class TextBoxWidget;
class DMButton;
class DMCheckbox;
class DMDropdown;
class DMRangeSlider;
class DMSlider;
class DMTextBox;
class SearchAssets;

class SpawnGroupsConfigPanel : public DockableCollapsible {
public:
    SpawnGroupsConfigPanel(int start_x = 32, int start_y = 32);
    ~SpawnGroupsConfigPanel();

    struct ChangeSummary {
        bool method_changed = false;
        bool quantity_changed = false;
        std::string method;
};

    void load(const nlohmann::json& asset);
    void open(const nlohmann::json& data, std::function<void(const nlohmann::json&)> on_save);
    void set_screen_dimensions(int width, int height);
    void open_panel();
    void close();
    bool visible() const;
    bool is_open() const;
    void set_position(int x, int y);
    SDL_Point position() const;

    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;
    nlohmann::json to_json() const;
    bool is_point_inside(int x, int y) const;
    SDL_Rect rect() const;
    ChangeSummary consume_change_summary();

    void set_ownership_label(const std::string& label, SDL_Color color);
    void lock_method_to(const std::string& method);
    void set_quantity_hidden(bool hidden);
    void set_on_close(std::function<void()> cb);
    size_t add_on_close_callback(std::function<void()> cb);
    void remove_on_close_callback(size_t handle);
    void clear_on_close_callbacks();
    void set_floating_stack_key(std::string key);
    // Provide area names for link-to-area feature; if unset, link UI is hidden
    void set_area_names_provider(std::function<std::vector<std::string>()> provider);

private:
    struct CandidateRow {
        std::unique_ptr<DMTextBox> name_box;
        std::unique_ptr<TextBoxWidget> name_widget;
        std::unique_ptr<DMSlider> chance_slider;
        std::unique_ptr<SliderWidget> chance_widget;
        std::unique_ptr<DMButton> remove_button;
        std::unique_ptr<ButtonWidget> remove_widget;
        std::string last_name;
        int last_chance = 0;
};

    void rebuild_layout();
    void rebuild_method_widget();
    void rebuild_quantity_widget();
    void rebuild_perimeter_widget();
    void rebuild_candidate_summary();
    void ensure_candidate_controls();
    void sync_from_widgets();
    void sync_candidates();
    void add_candidate(const std::string& name, int chance);
    void remove_candidate(const CandidateRow* row);
    void clamp_to_screen();
    void dispatch_save();

    std::vector<std::string> spawn_methods_;
    nlohmann::json entry_;
    std::string spawn_id_;
    std::string panel_title_;

    std::unique_ptr<LabelWidget> header_label_;
    std::unique_ptr<LabelWidget> ownership_label_;
    std::unique_ptr<LabelWidget> locked_method_label_;
    std::unique_ptr<LabelWidget> quantity_label_;
    std::unique_ptr<LabelWidget> candidate_summary_label_;

    std::unique_ptr<DMDropdown> method_dropdown_;
    std::unique_ptr<DropdownWidget> method_widget_;
    std::unique_ptr<DMRangeSlider> quantity_slider_;
    std::unique_ptr<RangeSliderWidget> quantity_widget_;
    std::unique_ptr<DMCheckbox> overlap_checkbox_;
    std::unique_ptr<CheckboxWidget> overlap_widget_;
    std::unique_ptr<DMCheckbox> spacing_checkbox_;
    std::unique_ptr<CheckboxWidget> spacing_widget_;
    std::unique_ptr<DMSlider> perimeter_slider_;
    std::unique_ptr<SliderWidget> perimeter_widget_;
    std::unique_ptr<DMButton> add_candidate_button_;
    std::unique_ptr<ButtonWidget> add_candidate_widget_;
    std::unique_ptr<DMButton> done_button_;
    std::unique_ptr<ButtonWidget> done_widget_;
    std::unique_ptr<DMButton> link_area_button_;
    std::unique_ptr<ButtonWidget> link_area_widget_;
    std::unique_ptr<DMButton> unlink_area_button_;
    std::unique_ptr<ButtonWidget> unlink_area_widget_;

    std::vector<std::unique_ptr<CandidateRow>> candidates_;

    std::unique_ptr<SearchAssets> asset_search_;
    // area picking panel
    struct AreaPicker;
    std::unique_ptr<AreaPicker> area_picker_;
    std::function<std::vector<std::string>()> area_names_provider_;

    std::function<void(const nlohmann::json&)> on_save_callback_;
    std::function<void()> on_close_callback_;
    bool save_dispatched_ = false;

    ChangeSummary pending_summary_;
    std::string baseline_method_;
    int baseline_min_ = 0;
    int baseline_max_ = 0;

    int method_index_ = 0;
    int quantity_min_ = 1;
    int quantity_max_ = 1;
    bool overlap_enabled_ = false;
    bool spacing_enabled_ = false;
    int perimeter_radius_ = 0;

    int screen_w_ = 1920;
    int screen_h_ = 1080;

    bool method_locked_ = false;
    std::string forced_method_;
    bool quantity_hidden_ = false;
    std::string ownership_text_;
    SDL_Color ownership_color_{255, 255, 255, 255};
    bool has_ownership_color_ = false;

    struct CloseCallbackEntry {
        size_t id = 0;
        std::function<void()> cb;
};
    std::vector<CloseCallbackEntry> close_callbacks_;
    size_t next_close_callback_id_ = 1;
    std::string floating_stack_key_;

    void notify_close_listeners();
};

using SpawnGroupConfigUI [[deprecated("Use SpawnGroupsConfigPanel instead")]] = SpawnGroupsConfigPanel;
