#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "widgets.hpp"
#include "map_generation/map_layers_geometry.hpp"

class MapLayersController;

class MapLayersPreviewWidget : public Widget {
public:
    using SelectLayerCallback = std::function<void(int)>;
    using SelectRoomCallback = std::function<void(const std::string&)>;
    using ShowRoomListCallback = std::function<void()>;

    MapLayersPreviewWidget();
    ~MapLayersPreviewWidget() override;

    void set_map_info(nlohmann::json* map_info);
    void set_controller(std::shared_ptr<MapLayersController> controller);

    void set_on_select_layer(SelectLayerCallback cb);
    void set_on_select_room(SelectRoomCallback cb);
    void set_on_show_room_list(ShowRoomListCallback cb);
    void set_on_change(std::function<void()> cb);
    void set_selected_layer(int index);
    void set_layer_diagnostics(const std::vector<int>& invalid_layers, const std::vector<int>& warning_layers, const std::vector<int>& dependency_layers);

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;
    bool wants_full_row() const override { return true; }

    void mark_dirty();
    void create_new_room_entry();
    void regenerate_preview();

private:
    struct RoomVisual {
        std::string key;
        std::string display_name;
        int layer_index = -1;
        double radius = 0.0;
        double angle = 0.0;
        double extent = 0.0;
        SDL_FPoint position{0.0f, 0.0f};
        SDL_Color color{255, 255, 255, 255};
};

    struct LayerVisual {
        int index = -1;
        std::string name;
        double radius = 0.0;
        double inner_radius = 0.0;
        double extent = 0.0;
        SDL_Color color{255, 255, 255, 255};
        int min_rooms = 0;
        int max_rooms = 0;
        int room_count = 0;
        bool invalid = false;
        bool warning = false;
        bool dependency = false;
        bool selected = false;
        std::vector<RoomVisual> rooms;
};

    struct RoomLegendEntry {
        std::string key;
        std::string display_name;
        SDL_Color color{255, 255, 255, 255};
};

    void rebuild_visuals();
    void ensure_latest_visuals() const;
    void recalculate_preview_scale();
    double compute_preview_scale() const;
    SDL_Color layer_color(int index) const;
    SDL_Color room_color(const std::string& key) const;
    std::string display_name_for_room(const std::string& key) const;
    const nlohmann::json& layers_array() const;
    const nlohmann::json* rooms_data() const;

    void update_hover_state(int layer_index, const std::string& room_key);
    void clear_hover_state();
    void handle_preview_click(int layer_index, const std::string& room_key);
    int hit_test_layer(int x, int y) const;
    std::string hit_test_room(int x, int y) const;

    void ensure_listener();
    void remove_listener();

    void render_preview(SDL_Renderer* renderer) const;
    void render_room_legend(SDL_Renderer* renderer) const;
    void render_refresh_button(SDL_Renderer* renderer) const;

private:
    nlohmann::json* map_info_ = nullptr;
    std::shared_ptr<MapLayersController> controller_;
    std::size_t controller_listener_id_ = 0;

    SDL_Rect rect_{0, 0, 0, 0};
    SDL_Point preview_center_{0, 0};
    SDL_Rect preview_rect_{0, 0, 0, 0};
    SDL_Rect legend_rect_{0, 0, 0, 0};
    SDL_Rect refresh_button_rect_{0, 0, 0, 0};

    mutable bool dirty_ = true;

    std::vector<LayerVisual> layer_visuals_;
    std::vector<RoomLegendEntry> room_legend_entries_;
    double max_visual_radius_ = 1.0;
    mutable double preview_scale_ = 1.0;
    double min_edge_distance_ = static_cast<double>(map_layers::kDefaultMinEdgeDistance);
    std::uint64_t preview_seed_ = 0;

    int hovered_layer_index_ = -1;
    std::string hovered_room_key_;
    int selected_layer_index_ = -1;
    std::unordered_set<int> invalid_layers_;
    std::unordered_set<int> warning_layers_;
    std::unordered_set<int> dependency_layers_;
    mutable bool refresh_hovered_ = false;

    SelectLayerCallback on_select_layer_{};
    SelectRoomCallback on_select_room_{};
    ShowRoomListCallback on_show_room_list_{};
    std::function<void()> on_change_{};
};

