#pragma once

#include <functional>

#include <memory>
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
    class PreviewWidget;
    class DetailsWidget;
    friend class PreviewWidget;
    friend class DetailsWidget;

    enum class DetailsMode {
        None,
        RoomList,
        LayerDetails,
        RoomDetails
    };

    struct RoomVisual {
        std::string key;
        std::string display_name;
        int layer_index = -1;
        double radius = 0.0;
        double angle = 0.0;
        double extent = 0.0;
        SDL_FPoint position{0.0f, 0.0f};
    };

    struct LayerVisual {
        int index = -1;
        std::string name;
        double radius = 0.0;
        SDL_Color color{255, 255, 255, 255};
        std::vector<RoomVisual> rooms;
    };

    void layout_rows();

    void rebuild_visuals();
    void refresh_room_list();
    void refresh_layer_details();
    void refresh_room_details();
    void recalculate_preview_scale();
    double compute_preview_scale() const;

    void ensure_details_container();
    void update_details_container();
    void apply_details_bounds();

    void open_room_list();
    void open_layer_details(int layer_index);
    void open_room_details(const std::string& room_key);

    void update_hover_state(int layer_index, const std::string& room_key);
    void clear_hover_state();
    void handle_preview_click(int layer_index, const std::string& room_key);

    int hit_test_layer(int x, int y) const;
    std::string hit_test_room(int x, int y) const;

    void notify_header_visibility() const;

    const nlohmann::json& layers_array() const;
    nlohmann::json& layers_array();
    const nlohmann::json* rooms_data() const;
    nlohmann::json* room_entry(const std::string& key);
    std::string display_name_for_room(const std::string& key) const;

    void render_preview(SDL_Renderer* renderer) const;
    void render_details(SDL_Renderer* renderer) const;
    bool handle_details_event(const SDL_Event& e);

    SDL_Color layer_color(int index) const;

private:
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    SaveCallback on_save_{};

    std::shared_ptr<MapLayersController> controller_;

    PreviewWidget* preview_widget_ = nullptr;
    DetailsWidget* details_widget_ = nullptr;
    std::vector<std::unique_ptr<Widget>> owned_widgets_;

    std::unique_ptr<SlidingWindowContainer> details_container_;

    SDL_Rect work_area_{0, 0, 0, 0};
    SDL_Rect embedded_bounds_{0, 0, 0, 0};
    SDL_Rect preview_rect_{0, 0, 0, 0};
    SDL_Rect details_rect_{0, 0, 0, 0};
    SDL_Point preview_center_{0, 0};

    std::vector<LayerVisual> layer_visuals_;
    double max_visual_radius_ = 1.0;
    double preview_scale_ = 1.0;

    int hovered_layer_index_ = -1;
    std::string hovered_room_key_;

    int selected_layer_index_ = -1;
    std::string selected_room_key_;

    DetailsMode details_mode_ = DetailsMode::None;
    struct RoomButtonEntry {
        std::string key;
        std::unique_ptr<DMButton> button;
    };
    std::vector<RoomButtonEntry> room_buttons_;
    std::vector<std::string> detail_lines_;
    std::vector<SDL_Rect> detail_line_rects_;

    bool dirty_ = false;
    bool preview_dirty_ = true;
    bool embedded_mode_ = false;
    int screen_w_ = 0;
    int screen_h_ = 0;

    std::function<void(bool)> header_visibility_callback_{};
};
