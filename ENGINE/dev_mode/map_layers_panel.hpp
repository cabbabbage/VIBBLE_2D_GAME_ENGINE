#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"
#include "room_selector_popup.hpp"

class Input;
union SDL_Event;
struct SDL_Renderer;
class MapLayersController;
class RoomConfigurator;

class MapLayersPanel : public DockableCollapsible {
public:
    using SaveCallback = std::function<bool()>;

    explicit MapLayersPanel(int x = 128, int y = 128);
    ~MapLayersPanel() override;

    void set_map_info(nlohmann::json* map_info, const std::string& map_path);
    void set_on_save(SaveCallback cb);
    void set_controller(std::shared_ptr<MapLayersController> controller);

    void open();
    void close();
    bool is_visible() const;

    void set_embedded_mode(bool embedded);
    bool embedded_mode() const { return embedded_mode_; }
    void set_embedded_bounds(const SDL_Rect& bounds);

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    bool is_point_inside(int x, int y) const;

    int selected_layer() const { return selected_layer_; }
    void select_layer(int index);
    void mark_dirty(bool trigger_preview = true);
    void mark_clean();

private:
    class LayerCanvasWidget;
    class PanelSidebarWidget;
    class LayerConfigPanel;
    class RoomCandidateWidget;
    struct PreviewNode;
    struct PreviewEdge;

    friend class LayerCanvasWidget;
    friend class PanelSidebarWidget;
    friend class LayerConfigPanel;
    friend class RoomCandidateWidget;
    void ensure_layers_array();
    void ensure_layer_indices();
    nlohmann::json& layers_array();
    const nlohmann::json& layers_array() const;
    nlohmann::json* layer_at(int index);
    const nlohmann::json* layer_at(int index) const;

    void rebuild_rows();
    void rebuild_available_rooms();
    void refresh_canvas();
    void add_layer_internal();
    void add_room_to_selected_layer();
    std::string create_new_room(const std::string& desired_name, bool open_config = false);
    std::string suggest_room_name() const;
    void delete_layer_internal(int index);
    void open_layer_config_internal(int index);

    bool is_spawn_room(const std::string& room_key) const;
    int find_spawn_layer_index() const;
    bool is_layer_locked(int index) const;
    std::vector<std::string> available_rooms_for_layer(int layer_index) const;
    void handle_layer_name_changed(int index, const std::string& name);
    void handle_candidate_min_changed(int layer_index, int candidate_index, int min_instances);
    void handle_candidate_max_changed(int layer_index, int candidate_index, int max_instances);
    void handle_candidate_removed(int layer_index, int candidate_index);
    void handle_candidate_child_added(int layer_index, int candidate_index, const std::string& child);
    void handle_candidate_child_removed(int layer_index, int candidate_index, const std::string& child);
    void handle_candidate_added(int layer_index, const std::string& room_name);
    bool save_layers_to_disk();
    bool reload_layers_from_disk();
    void ensure_layer_config_valid();
    void request_room_selection_for_layer(int layer_index, const std::function<void(const std::string&)>& cb);
    void request_preview_regeneration();
    void regenerate_preview();
    double compute_map_radius_from_layers();
    void recalculate_radii_from_layer(int layer_index);
    int append_layer_entry(const std::string& display_name = {});
    bool ensure_child_room_exists(int parent_layer_index, const std::string& child, bool* layer_created = nullptr);
    bool handle_preview_room_click(int px, int py, int center_x, int center_y, double scale);
    const PreviewNode* find_room_at(int px, int py, int center_x, int center_y, double scale) const;
    int find_layer_at(int px, int py, int center_x, int center_y, double scale) const;
    void update_hover_target(int layer_index, const std::string& room_key);
    void update_click_target(int layer_index, const std::string& room_key);
    void clear_hover_target();
    void open_room_config_for(const std::string& room_name);
    void ensure_room_configurator();
    nlohmann::json* ensure_room_entry(const std::string& room_name);
    SDL_Rect compute_room_config_bounds() const;

    std::string rename_room_everywhere(const std::string& old_key, const std::string& desired_key);

private:
    struct PreviewNode {
        SDL_FPoint center{0.0f, 0.0f};
        double width = 0.0;
        double height = 0.0;
        double radius = 0.0;
        bool is_circle = false;
        int layer = 0;
        SDL_Color color{255, 255, 255, 255};
        std::vector<SDL_FPoint> outline;
        std::string name;
        PreviewNode* parent = nullptr;
        PreviewNode* left_sibling = nullptr;
        PreviewNode* right_sibling = nullptr;
        std::vector<PreviewNode*> children;
};
    struct PreviewEdge {
        const PreviewNode* from = nullptr;
        const PreviewNode* to = nullptr;
        SDL_Color color{180, 180, 180, 255};
        bool is_trail = false;
};

    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    SaveCallback on_save_;

    std::unique_ptr<LayerCanvasWidget> canvas_widget_;
    std::unique_ptr<PanelSidebarWidget> sidebar_widget_;
    std::unique_ptr<LayerConfigPanel> layer_config_;
    std::unique_ptr<RoomSelectorPopup> room_selector_;
    std::unique_ptr<RoomConfigurator> room_configurator_;

    std::vector<std::unique_ptr<PreviewNode>> preview_nodes_;
    std::vector<PreviewEdge> preview_edges_;
    double preview_extent_ = 0.0;
    bool preview_dirty_ = true;
    std::string active_room_config_key_;

    std::vector<std::string> available_rooms_;
    int selected_layer_ = -1;
    bool dirty_ = false;

    int hovered_layer_index_ = -1;
    std::string hovered_room_key_;
    int clicked_layer_index_ = -1;
    std::string clicked_room_key_;

    std::shared_ptr<MapLayersController> controller_;
    bool embedded_mode_ = false;
    SDL_Rect screen_bounds_{0, 0, 0, 0};
};
