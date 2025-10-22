#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"
#include "SlidingWindowContainer.hpp"
#include "widgets.hpp"

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

    enum class SidePanel { None, RoomsList, LayerControls };
    void set_side_panel_callback(std::function<void(SidePanel)> cb);
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

    struct LayerRow {
        int index = -1;
        std::string name;
        SDL_Rect rect{0, 0, 0, 0};
    };

    void rebuild_layers();
    void update_layer_row_geometry();
    int list_height_for_width(int w) const;
    void render_layers_list(SDL_Renderer* renderer) const;
    void commit_layer_name_edit();
    void ensure_listener();
    void remove_listener();
    void notify_header_visibility() const;
    void notify_side_panel(SidePanel panel) const;
    void set_hovered_layer(int index);
    void clear_hover();

    const nlohmann::json& layers_array() const;
    nlohmann::json& layers_array();

private:
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    SaveCallback on_save_{};

    std::shared_ptr<MapLayersController> controller_;
    std::size_t controller_listener_id_ = 0;

    std::function<void(bool)> header_visibility_callback_{};
    std::function<void(const std::string&)> on_configure_room_{};
    std::function<void(SidePanel)> side_panel_callback_{};

    SlidingWindowContainer* rooms_list_container_ = nullptr;
    SlidingWindowContainer* layer_controls_container_ = nullptr;

    bool embedded_mode_ = false;
    SDL_Rect embedded_bounds_{0, 0, 0, 0};

    std::unique_ptr<DMButton> add_layer_button_;
    std::unique_ptr<DMButton> delete_layer_button_;
    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<DMButton> reload_button_;
    std::unique_ptr<DMTextBox> layer_name_box_;

    std::vector<std::unique_ptr<Widget>> owned_widgets_;
    LayersListWidget* list_widget_ = nullptr;
    MapLayersPreviewWidget* preview_widget_ = nullptr;
    DMTextBox* layer_name_box_raw_ = nullptr;

    std::vector<LayerRow> layer_rows_;
    int hovered_layer_index_ = -1;
    int selected_layer_index_ = -1;
    std::string pending_room_selection_;
    std::string current_layer_name_;

    bool data_dirty_ = true;
};

