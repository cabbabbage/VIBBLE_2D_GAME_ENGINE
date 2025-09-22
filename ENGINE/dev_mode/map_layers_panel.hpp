#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"

class Input;
union SDL_Event;
struct SDL_Renderer;
class MapLayersController;

// Floating dockable panel for editing map_layers in map_info.json.
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
    void mark_dirty();
    void mark_clean();

private:
    class LayerCanvasWidget;
    class PanelSidebarWidget;
    class LayerConfigPanel;
    class RoomSelectorPopup;
    class RoomCandidateWidget;

    friend class LayerCanvasWidget;
    friend class PanelSidebarWidget;
    friend class LayerConfigPanel;
    friend class RoomCandidateWidget;
    friend class RoomSelectorPopup;

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
    void delete_layer_internal(int index);
    void open_layer_config_internal(int index);
    void handle_layer_range_changed(int index, int min_rooms, int max_rooms);
    void handle_layer_name_changed(int index, const std::string& name);
    void handle_candidate_range_changed(int layer_index, int candidate_index, int min_instances, int max_instances);
    void handle_candidate_removed(int layer_index, int candidate_index);
    void handle_candidate_child_added(int layer_index, int candidate_index, const std::string& child);
    void handle_candidate_child_removed(int layer_index, int candidate_index, const std::string& child);
    void handle_candidate_added(int layer_index, const std::string& room_name);
    void update_save_button_state();
    bool save_layers_to_disk();
    bool reload_layers_from_disk();
    void ensure_layer_config_valid();
    void request_room_selection(const std::function<void(const std::string&)>& cb);
    void request_preview_regeneration();
    void regenerate_preview();
    double compute_map_radius_from_layers();
    int append_layer_entry(const std::string& display_name = {});
    bool ensure_child_room_exists(int parent_layer_index, const std::string& child, bool* layer_created = nullptr);

private:
    struct PreviewNode {
        SDL_FPoint center{0.0f, 0.0f};
        double width = 0.0;
        double height = 0.0;
        bool is_circle = false;
        int layer = 0;
        SDL_Color color{255, 255, 255, 255};
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

    std::vector<std::unique_ptr<PreviewNode>> preview_nodes_;
    std::vector<PreviewEdge> preview_edges_;
    double preview_extent_ = 0.0;
    bool preview_dirty_ = true;

    std::vector<std::string> available_rooms_;
    int selected_layer_ = -1;
    bool dirty_ = false;

    std::shared_ptr<MapLayersController> controller_;
    bool embedded_mode_ = false;
};
