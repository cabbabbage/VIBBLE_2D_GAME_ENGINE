#pragma once

#include <SDL.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace devmode::core { class ManifestStore; }
namespace devmode::core { class ManifestStore; }

#include "SlidingWindowContainer.hpp"
#include "widgets.hpp"
#include "../spawn_group_config/SpawnGroupConfig.hpp"

class Input;
class Room;
class TagEditorWidget;
class SpawnGroupConfig;
class DropdownWidget;
class SliderWidget;
class RangeSliderWidget;
class CheckboxWidget;
class TextBoxWidget;
class ButtonWidget;
class DockableCollapsible;
class DMSlider;
class DMCheckbox;
class DMTextBox;
class DMRangeSlider;
class DMDropdown;
class DMButton;

class RoomConfigurator {
public:
    RoomConfigurator();
    ~RoomConfigurator();

    void set_bounds(const SDL_Rect& bounds);
    void set_work_area(const SDL_Rect& bounds);
    void set_show_header(bool show);
    void set_on_close(std::function<void()> cb);
    void set_header_visibility_controller(std::function<void(bool)> cb);
    void set_blocks_editor_interactions(bool block);
    void attach_container(SlidingWindowContainer* container);
    void detach_container();
    SlidingWindowContainer* container();
    const SlidingWindowContainer* container() const;

    void open(const nlohmann::json& room_data);
    void open(nlohmann::json& room_data,
              std::function<void()> on_change,
              std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change = {},
              SpawnGroupConfig::ConfigureEntryCallback configure_entry = {});
    void open(Room* room);

    void set_manifest_store(class devmode::core::ManifestStore* store);

    bool refresh_spawn_groups(const nlohmann::json& room_data);
    bool refresh_spawn_groups(nlohmann::json& room_data);
    bool refresh_spawn_groups(Room* room);

    void notify_spawn_groups_mutated();

    void close();
    bool visible() const;
    bool any_panel_visible() const;
    bool is_locked() const;

    void update(const Input& input, int screen_w, int screen_h);
    void prepare_for_event(int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

    const SDL_Rect& panel_rect() const;

    std::string current_header_text() const;

    nlohmann::json build_json() const;
    bool is_point_inside(int x, int y) const;

    void set_spawn_group_callbacks(std::function<void(const std::string&)> on_edit,
                                   std::function<void(const std::string&)> on_delete,
                                   std::function<void(const std::string&, size_t)> on_reorder,
                                   std::function<void()> on_add,
                                   std::function<void(const std::string&)> on_regenerate = {});
    bool focus_spawn_group(const std::string& spawn_id);
    void set_spawn_area_open_callback(std::function<void(const std::string&, const std::string&)> cb,
                                      std::string stack_key = {});

    void set_on_room_renamed(std::function<std::string(const std::string&, const std::string&)> cb) {
        on_room_renamed_ = std::move(cb);
    }

private:
    class devmode::core::ManifestStore* manifest_store_ = nullptr;
    struct State;

    bool apply_room_data(const nlohmann::json& data);
    void rebuild_rows();
    void rebuild_rows_internal();
    void rebuild_spawn_rows(bool force_collapse_sections = false);

    void request_rebuild();
    void load_tags_from_json(const nlohmann::json& data);
    void write_tags_to_json(nlohmann::json& object) const;
    std::string selected_geometry() const;
    bool sync_state_from_widgets();
    const nlohmann::json& live_room_json() const;
    nlohmann::json& live_room_json();
    int layout_content(const SlidingWindowContainer::LayoutContext& ctx) const;
    SDL_Rect clamp_to_work_area(const SDL_Rect& bounds) const;
    void handle_container_closed();
    void reset_scroll();
    bool add_spawn_group_direct();
    void renumber_spawn_group_priorities(nlohmann::json& groups) const;
    void ensure_base_panels();
    void refresh_base_panel_rows();
    void request_container_layout();
    void configure_container(SlidingWindowContainer& container);
    void clear_container_callbacks(SlidingWindowContainer& container);
    void prune_collapsible_caches();
    int cached_collapsible_height(const DockableCollapsible* panel) const;
    void update_collapsible_height_cache(const DockableCollapsible* panel, int new_height);
    void forget_collapsible(const DockableCollapsible* panel);
    bool base_panel_expanded(const std::string& key) const;
    void set_base_panel_expanded(const std::string& key, bool expanded);
    void persist_spawn_group_changes();
    void handle_spawn_groups_mutated();
    void handle_spawn_group_entry_changed(const nlohmann::json& entry, const SpawnGroupConfig::ChangeSummary& summary);
    void focus_panel(DockableCollapsible* panel);
    void clear_panel_focus();
    void apply_panel_focus_states();
    DockableCollapsible* panel_at_point(SDL_Point p) const;
    bool handle_panel_focus_event(const SDL_Event& e);
    void initialize_radius_slider(bool request_layout);
    void expand_radius_slider_range_if_needed();
    int compute_radius_slider_initial_range() const;

    std::unique_ptr<State> state_;
    std::unique_ptr<SlidingWindowContainer> default_container_;
    SlidingWindowContainer* container_ = nullptr;
    bool blocks_editor_interactions_ = true;
    bool show_header_ = true;
    SDL_Rect bounds_override_{0, 0, 0, 0};
    SDL_Rect work_area_{0, 0, 0, 0};
    bool has_bounds_override_ = false;
    int last_screen_w_ = 0;
    int last_screen_h_ = 0;
    std::function<void()> on_close_{};
    bool rebuild_in_progress_ = false;
    bool pending_rebuild_ = false;
    bool deferred_rebuild_ = false;
    bool spawn_callbacks_active_ = false;

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
    std::unique_ptr<DMTextBox> width_min_box_;
    std::unique_ptr<TextBoxWidget> width_min_widget_;
    std::unique_ptr<DMTextBox> width_max_box_;
    std::unique_ptr<TextBoxWidget> width_max_widget_;
    std::unique_ptr<DMTextBox> height_min_box_;
    std::unique_ptr<TextBoxWidget> height_min_widget_;
    std::unique_ptr<DMTextBox> height_max_box_;
    std::unique_ptr<TextBoxWidget> height_max_widget_;
    std::unique_ptr<DMRangeSlider> radius_slider_;
    std::unique_ptr<RangeSliderWidget> radius_widget_;
    int radius_slider_max_range_ = 0;
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

    std::unique_ptr<DockableCollapsible> geometry_panel_;
    std::unique_ptr<DockableCollapsible> tags_panel_;
    std::unique_ptr<DockableCollapsible> types_panel_;
    std::vector<DockableCollapsible*> ordered_base_panels_;
    mutable std::vector<SDL_Rect> ordered_panel_bounds_;
    mutable std::vector<SDL_Rect> spawn_config_bounds_;
    mutable SDL_Rect add_spawn_bounds_{0,0,0,0};

    std::vector<std::unique_ptr<SpawnGroupConfig>> spawn_group_configs_;
    std::vector<std::string> spawn_group_config_ids_;
    std::unique_ptr<DMButton> add_spawn_button_;
    std::unique_ptr<ButtonWidget> add_spawn_widget_;
    std::unordered_map<const DockableCollapsible*, int> collapsible_height_cache_;
    std::unordered_map<const DockableCollapsible*, std::string> base_panel_keys_;
    std::unordered_map<std::string, bool> base_panel_expanded_state_;
    DockableCollapsible* focused_panel_ = nullptr;

    bool reset_expanded_state_pending_ = false;

    std::function<void(const std::string&)> on_spawn_edit_;
    std::function<void(const std::string&)> on_spawn_delete_;
    std::function<void(const std::string&, size_t)> on_spawn_reorder_;
    std::function<void()> on_spawn_add_;
    std::function<void(const std::string&)> on_spawn_regenerate_;
    std::function<void(const std::string&, const std::string&)> on_spawn_area_open_;
    std::string spawn_area_stack_key_;
    std::function<void()> on_external_spawn_change_;
    std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_external_spawn_entry_change_;
    SpawnGroupConfig::ConfigureEntryCallback external_configure_entry_;
    std::function<std::string(const std::string&, const std::string&)> on_room_renamed_;
    std::function<void(bool)> header_visibility_controller_{};
};

