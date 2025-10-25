#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <chrono>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"
#include "SlidingWindowContainer.hpp"
#include "widgets.hpp"
#include "map_generation/map_layers_geometry.hpp"

class Input;
struct SDL_Renderer;
class MapLayersController;
class MapLayersPreviewWidget;

class MapLayersPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    explicit MapLayersPanel(int x = 128, int y = 128);
    ~MapLayersPanel() override;

    void set_map_info(nlohmann::json* map_info, const std::string& map_path);
    void set_on_save(SaveCallback cb);
    void set_controller(std::shared_ptr<MapLayersController> controller);
    void set_header_visibility_callback(std::function<void(bool)> cb);

    void set_work_area(const SDL_Rect& bounds);

    void open();
    void close();
    bool is_visible() const;
    bool room_config_visible() const;

    void hide_main_container();

    void show_room_list();
    void select_room(const std::string& room_key);
    void hide_details_panel();

    void set_on_configure_room(std::function<void(const std::string&)> cb);
    void set_on_layer_selected(std::function<void(int)> cb);

    enum class SidePanel { None, RoomsList, LayerControls };
    void set_side_panel_callback(std::function<void(SidePanel)> cb);
    void force_layer_controls_on_next_select();
    void set_rooms_list_container(SlidingWindowContainer* container);
    void set_layer_controls_container(SlidingWindowContainer* container);

    void set_embedded_mode(bool embedded);
    bool embedded_mode() const { return embedded_mode_; }
    void set_embedded_bounds(const SDL_Rect& bounds);

    void update(const Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    bool is_point_inside(int x, int y) const override;

    int selected_layer() const { return selected_layer_index_; }
    void select_layer(int index);
    void mark_dirty(bool trigger_preview = true);
    void mark_clean();

private:
    class LayersListWidget;
    class ValidationSummaryWidget;
    class MinEdgeWidget;

    friend class LayersListWidget;
    friend class ValidationSummaryWidget;
    friend class MinEdgeWidget;

    struct LayerRow {
        int index = -1;
        std::string name;
        SDL_Rect rect{0, 0, 0, 0};
        SDL_Rect delete_button_rect{0, 0, 0, 0};
        std::string summary;
        bool invalid = false;
        bool warning = false;
        bool dependency_highlight = false;
        bool deletable = true;
};

    void rebuild_layers();
    void update_layer_row_geometry();
    int list_height_for_width(int w) const;
    void render_layers_list(SDL_Renderer* renderer) const;
    int validation_summary_height(int w) const;
    void render_validation_summary(SDL_Renderer* renderer, const SDL_Rect& rect) const;
    void update_validation_summary_layout(const std::vector<std::string>& errors, const std::vector<std::string>& warnings);
    void trigger_save();
    void ensure_listener();
    void remove_listener();
    void notify_header_visibility() const;
    void notify_side_panel(SidePanel panel) const;
    void set_hovered_layer(int index);
    void clear_hover();
    void set_hovered_delete_layer(int index);
    void on_delete_layer_clicked(int index);
    bool delete_layer_at(int index);

    void on_layers_list_mouse_down(int index, int mouse_y);
    void on_layers_list_mouse_motion(int mouse_y, Uint32 buttons);
    void on_layers_list_mouse_up(int mouse_y, Uint8 button);
    bool is_dragging_layer() const { return dragging_layer_active_; }
    void cancel_drag();
    int drop_slot_for_position(int y) const;
    int find_visual_position(int layer_index) const;
    void apply_dependency_highlights();
    bool validate_layers();
    void perform_save();
    void update_preview_state();
    void recalculate_dependency_highlights();
    void rebuild_layer_rows_from_json(const nlohmann::json& layers);
    void update_embedded_layout_constraints();

    const nlohmann::json& layers_array() const;
    nlohmann::json& layers_array();

    void sync_min_edge_textbox();
    bool handle_min_edge_event(const SDL_Event& e);
    void on_min_edge_text_changed();
    void on_min_edge_edit_finished();
    void apply_min_edge_value(int value);
    void show_min_edge_note(const std::string& message, SDL_Color color);
    void clear_min_edge_note();
    void update_min_edge_note();
    bool min_edge_note_visible() const;
    int min_edge_widget_height_for_width(int w) const;
    void layout_min_edge_input(const SDL_Rect& bounds);
    void render_min_edge_input(SDL_Renderer* renderer, const SDL_Rect& bounds) const;

private:
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    SaveCallback on_save_{};

    std::shared_ptr<MapLayersController> controller_;
    std::size_t controller_listener_id_ = 0;

    std::function<void(bool)> header_visibility_callback_{};
    std::function<void(const std::string&)> on_configure_room_{};
    std::function<void(SidePanel)> side_panel_callback_{};
    std::function<void(int)> on_layer_selected_{};

    SlidingWindowContainer* rooms_list_container_ = nullptr;
    SlidingWindowContainer* layer_controls_container_ = nullptr;

    bool embedded_mode_ = false;
    SDL_Rect embedded_bounds_{0, 0, 0, 0};
    int target_body_height_ = 0;
    int default_visible_height_ = 400;

    std::unique_ptr<DMButton> add_layer_button_;
    std::unique_ptr<DMButton> reload_button_;
    std::vector<std::unique_ptr<Widget>> owned_widgets_;
    LayersListWidget* list_widget_ = nullptr;
    MapLayersPreviewWidget* preview_widget_ = nullptr;
    ValidationSummaryWidget* validation_widget_ = nullptr;
    MinEdgeWidget* min_edge_widget_ = nullptr;
    std::unique_ptr<DMTextBox> min_edge_textbox_;

    std::vector<LayerRow> layer_rows_;
    int hovered_layer_index_ = -1;
    int hovered_delete_layer_index_ = -1;
    int selected_layer_index_ = -1;
    std::string pending_room_selection_;
    bool data_dirty_ = true;
    bool validation_dirty_ = true;
    bool pending_save_ = false;
    bool save_blocked_ = false;
    bool force_layer_controls_on_select_ = false;

    struct ValidationLine {
        std::string text;
        SDL_Color color{255, 255, 255, 255};
};
    std::vector<ValidationLine> validation_lines_;
    bool validation_has_errors_ = false;
    bool validation_has_warnings_ = false;
    double estimated_map_radius_ = 0.0;
    std::string root_room_summary_;

    std::vector<int> invalid_layers_;
    std::vector<int> warning_layers_;
    std::vector<std::vector<int>> layer_dependency_children_;
    std::vector<std::vector<int>> layer_dependency_parents_;
    std::vector<int> dependency_highlight_layers_;

    bool dragging_layer_active_ = false;
    bool drag_moved_ = false;
    int dragging_layer_index_ = -1;
    int dragging_start_slot_ = -1;
    int drop_target_slot_ = -1;
    int drag_start_mouse_y_ = 0;

    int min_edge_value_ = map_layers::kDefaultMinEdgeDistance;
    std::string last_valid_min_edge_text_;
    SDL_Rect min_edge_note_rect_{0, 0, 0, 0};
    std::string min_edge_note_;
    SDL_Color min_edge_note_color_{255, 255, 255, 255};
    std::chrono::steady_clock::time_point min_edge_note_expiration_{};
};

