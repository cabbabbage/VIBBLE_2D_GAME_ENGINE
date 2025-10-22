#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"

class Input;
class MapLayersController;

class MapLayersPreviewPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    explicit MapLayersPreviewPanel(int x = 72, int y = 40);
    ~MapLayersPreviewPanel() override;

    // Data bindings
    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_controller(std::shared_ptr<MapLayersController> controller);

    // Selection callbacks into the details sliding container
    void set_on_select_layer(std::function<void(int)> cb) { on_select_layer_ = std::move(cb); }
    void set_on_select_room(std::function<void(const std::string&)> cb) { on_select_room_ = std::move(cb); }
    void set_on_show_room_list(std::function<void()> cb) { on_show_room_list_ = std::move(cb); }

    // Panel lifecycle
    void update(const Input& input, int screen_w = 0, int screen_h = 0) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    bool is_point_inside(int x, int y) const override;

protected:
    void render_content(SDL_Renderer* renderer) const override;

private:
    class PreviewWidget;

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

    // Model -> visuals
    void rebuild_visuals();
    void recalculate_preview_scale();
    double compute_preview_scale() const;
    SDL_Color layer_color(int index) const;
    std::string display_name_for_room(const std::string& key) const;
    const nlohmann::json& layers_array() const;
    const nlohmann::json* rooms_data() const;
    void create_new_room_entry();

    // Interaction helpers
    void update_hover_state(int layer_index, const std::string& room_key);
    void clear_hover_state();
    void handle_preview_click(int layer_index, const std::string& room_key);
    int  hit_test_layer(int x, int y) const;
    std::string hit_test_room(int x, int y) const;

    // UI composition
    void build_rows();

private:
    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_{};
    std::shared_ptr<MapLayersController> controller_;
    std::size_t controller_listener_id_ = 0;

    std::vector<std::unique_ptr<class Widget>> owned_widgets_;
    PreviewWidget* preview_widget_ = nullptr;

    // Action buttons
    std::unique_ptr<class DMButton> add_layer_btn_;
    std::unique_ptr<class DMButton> create_room_btn_;
    std::unique_ptr<class DMButton> save_btn_;
    std::unique_ptr<class DMButton> reload_btn_;

    SDL_Rect preview_rect_{0, 0, 0, 0};
    SDL_Point preview_center_{0, 0};
    mutable int screen_w_ = 0;
    mutable int screen_h_ = 0;

    std::vector<LayerVisual> layer_visuals_;
    double max_visual_radius_ = 1.0;
    mutable double preview_scale_ = 1.0;

    int hovered_layer_index_ = -1;
    std::string hovered_room_key_;

    // Callbacks into details panel
    std::function<void(int)> on_select_layer_{};
    std::function<void(const std::string&)> on_select_room_{};
    std::function<void()> on_show_room_list_{};
};

