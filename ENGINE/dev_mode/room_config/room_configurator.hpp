#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "SlidingWindowContainer.hpp"
#include "widgets.hpp"
#include "../spawn_group_config/SpawnGroupConfig.hpp"

class Input;
class Room;
class TagEditorWidget;
class SpawnGroupConfig;
class DropdownWidget;
class RangeSliderWidget;
class SliderWidget;
class CheckboxWidget;
class TextBoxWidget;
class DMRangeSlider;
class DMSlider;
class DMCheckbox;
class DMTextBox;
class DMDropdown;

class RoomConfigurator {
public:
    RoomConfigurator();
    ~RoomConfigurator();

    void set_bounds(const SDL_Rect& bounds);
    void set_work_area(const SDL_Rect& bounds);
    void set_show_header(bool show);
    void set_on_close(std::function<void()> cb);
    void set_header_visibility_controller(std::function<void(bool)> cb);

    void open(const nlohmann::json& room_data);
    void open(nlohmann::json& room_data,
              std::function<void()> on_change,
              std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change = {},
              SpawnGroupConfig::ConfigureEntryCallback configure_entry = {});
    void open(Room* room);

    bool refresh_spawn_groups(const nlohmann::json& room_data);
    bool refresh_spawn_groups(nlohmann::json& room_data);
    bool refresh_spawn_groups(Room* room);

    void close();
    bool visible() const;
    bool any_panel_visible() const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    const SDL_Rect& panel_rect() const;

    nlohmann::json build_json() const;
    bool is_point_inside(int x, int y) const;

    void set_spawn_group_callbacks(std::function<void(const std::string&)> on_edit,
                                   std::function<void(const std::string&)> on_duplicate,
                                   std::function<void(const std::string&)> on_delete,
                                   std::function<void(const std::string&)> on_move_up,
                                   std::function<void(const std::string&)> on_move_down,
                                   std::function<void()> on_add,
                                   std::function<void(const std::string&)> on_regenerate = {});

    void set_on_room_renamed(std::function<std::string(const std::string&, const std::string&)> cb) {
        on_room_renamed_ = std::move(cb);
    }

private:
    struct State;

    using Row = std::vector<Widget*>;
    using Rows = std::vector<Row>;

    bool apply_room_data(const nlohmann::json& data);
    void rebuild_rows();
    void rebuild_rows_internal();
    void rebuild_spawn_rows(Rows& rows);
    void load_tags_from_json(const nlohmann::json& data);
    void write_tags_to_json(nlohmann::json& object) const;
    std::string selected_geometry() const;
    bool sync_state_from_widgets();
    void ensure_spawn_list();
    const nlohmann::json& live_room_json() const;
    nlohmann::json& live_room_json();
    void set_rows(Rows rows);
    int layout_content(const SlidingWindowContainer::LayoutContext& ctx) const;
    Rows compute_layout_rows() const;
    SDL_Rect clamp_to_work_area(const SDL_Rect& bounds) const;
    void handle_container_closed();
    void reset_scroll();

    std::unique_ptr<State> state_;

    SlidingWindowContainer container_;
    Rows rows_;
    bool show_header_ = true;
    SDL_Rect bounds_override_{0, 0, 0, 0};
    SDL_Rect work_area_{0, 0, 0, 0};
    bool has_bounds_override_ = false;
    int cell_width_ = 0;
    int row_gap_ = 0;
    int col_gap_ = 0;
    int last_screen_w_ = 0;
    int last_screen_h_ = 0;
    std::function<void()> on_close_{};
    bool rebuild_in_progress_ = false;
    bool pending_rebuild_ = false;

    Room* room_ = nullptr;
    nlohmann::json* external_room_json_ = nullptr;
    nlohmann::json loaded_json_;
    bool is_trail_context_ = false;

    std::vector<std::string> geometry_options_;

    std::vector<std::string> room_tags_;
    std::vector<std::string> room_anti_tags_;
    bool tags_dirty_ = false;

    std::unique_ptr<DMTextBox> name_box_;
    std::unique_ptr<TextBoxWidget> name_widget_;
    std::unique_ptr<DMDropdown> geometry_dropdown_;
    std::unique_ptr<DropdownWidget> geometry_widget_;
    std::unique_ptr<DMRangeSlider> width_slider_;
    std::unique_ptr<RangeSliderWidget> width_widget_;
    std::unique_ptr<DMRangeSlider> height_slider_;
    std::unique_ptr<RangeSliderWidget> height_widget_;
    std::unique_ptr<DMSlider> radius_slider_;
    std::unique_ptr<SliderWidget> radius_widget_;
    std::unique_ptr<DMSlider> edge_slider_;
    std::unique_ptr<SliderWidget> edge_widget_;
    std::unique_ptr<DMSlider> curvy_slider_;
    std::unique_ptr<SliderWidget> curvy_widget_;
    std::unique_ptr<DMCheckbox> spawn_checkbox_;
    std::unique_ptr<CheckboxWidget> spawn_widget_;
    std::unique_ptr<DMCheckbox> boss_checkbox_;
    std::unique_ptr<CheckboxWidget> boss_widget_;
    std::unique_ptr<DMCheckbox> inherit_checkbox_;
    std::unique_ptr<CheckboxWidget> inherit_widget_;
    std::unique_ptr<TagEditorWidget> tag_editor_;

    std::unique_ptr<Widget> room_section_label_;
    std::unique_ptr<Widget> geometry_label_;
    std::unique_ptr<Widget> dimensions_label_;
    std::unique_ptr<Widget> toggles_label_;
    std::unique_ptr<Widget> spawn_label_;
    std::unique_ptr<Widget> tags_label_;
    std::unique_ptr<Widget> empty_spawn_label_;

    std::unique_ptr<SpawnGroupConfig> spawn_list_;

    std::function<void(const std::string&)> on_spawn_edit_;
    std::function<void(const std::string&)> on_spawn_duplicate_;
    std::function<void(const std::string&)> on_spawn_delete_;
    std::function<void(const std::string&)> on_spawn_move_up_;
    std::function<void(const std::string&)> on_spawn_move_down_;
    std::function<void()> on_spawn_add_;
    std::function<void(const std::string&)> on_spawn_regenerate_;
    std::function<void()> on_external_spawn_change_;
    std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_external_spawn_entry_change_;
    SpawnGroupConfig::ConfigureEntryCallback external_configure_entry_;
    std::function<std::string(const std::string&, const std::string&)> on_room_renamed_;
};

