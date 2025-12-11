#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

#include "utils/ranged_color.hpp"

class Assets;
namespace devmode::core {
class ManifestStore;
}
class Input;
class MapLightPanel;
class MapLayersPreviewPanel;
class MapLayersPanel;
class MapLayerControlsDisplay;
class MapLayersController;
class RoomConfigurator;
class SlidingWindowContainer;
class DevFooterBar;
class DockableCollapsible;
struct DMButtonStyle;
struct SDL_Renderer;
class MapRoomsDisplay;

class MapModeUI {
public:
    enum class HeaderMode { Map, Room };

    struct HeaderButtonConfig {
        std::string id;
        std::string label;
        bool active = false;
        bool momentary = false;
        const DMButtonStyle* style_override = nullptr;
        const DMButtonStyle* active_style_override = nullptr;
        std::function<void(bool)> on_toggle;
};

    explicit MapModeUI(Assets* assets);
    ~MapModeUI();

    void set_map_context(nlohmann::json* map_info, const std::string& map_path);
    void set_screen_dimensions(int w, int h);
    void set_sliding_area_bounds(const SDL_Rect& bounds);

    void set_manifest_store(devmode::core::ManifestStore* store);

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    void open_layers_panel();
    void open_light_panel();
    void close_light_panel();
    void toggle_light_panel();
    void toggle_layers_panel();
    void close_all_panels();

    bool is_light_panel_visible() const;
    using LightSaveCallback = std::function<bool()>;

    void set_light_save_callback(LightSaveCallback cb);

    void set_map_mode_active(bool active);

    DevFooterBar* get_footer_bar() const;
    void collect_sliding_container_rects(std::vector<SDL_Rect>& out) const;
    void set_footer_always_visible(bool on);
    void set_headers_suppressed(bool suppressed);
    void set_sliding_headers_hidden(bool hidden);
    void set_dev_sliding_headers_hidden(bool hidden);
    void set_mode_button_sets(std::vector<HeaderButtonConfig> map_buttons, std::vector<HeaderButtonConfig> room_buttons);
    void set_header_mode(HeaderMode mode);
    void set_button_state(const std::string& id, bool active);
    void set_button_state(HeaderMode mode, const std::string& id, bool active);
    void register_floating_panel(DockableCollapsible* panel);
    HeaderMode header_mode() const { return header_mode_; }
    void set_on_mode_changed(std::function<void(HeaderMode)> cb) { on_mode_changed_ = std::move(cb); }

    const std::vector<HeaderButtonConfig>& map_mode_button_configs() const { return map_mode_buttons_; }
    const std::vector<HeaderButtonConfig>& room_mode_button_configs() const { return room_mode_buttons_; }

    bool is_point_inside(int x, int y) const;
    bool is_any_panel_visible() const;
    bool is_layers_panel_visible() const;

private:
    void ensure_panels();
    void sync_panel_map_info();
    bool save_map_info_to_disk() const;
    bool auto_save_layers_data();
    void create_room_from_layers_controls();
    void configure_footer_buttons();
    void sync_footer_button_states();
    void update_footer_visibility();
    enum class PanelType { None, Layers, Grid };
    enum class SlidingPanel { None, RoomConfig, RoomsList, LayerControls };
    void set_active_panel(PanelType panel);
    void refresh_header_suppression_state();
    void track_floating_panel(DockableCollapsible* panel);
    void rebuild_floating_stack();
    void bring_panel_to_front(DockableCollapsible* panel);
    bool handle_floating_panel_event(const SDL_Event& e, bool& used);
    bool pointer_inside_floating_panel(int x, int y) const;
    bool is_pointer_event(const SDL_Event& e) const;
    SDL_Point event_point(const SDL_Event& e) const;
    HeaderButtonConfig* find_button(HeaderMode mode, const std::string& id);
    bool ensure_panel_unlocked(DockableCollapsible* panel, const char* panel_name) const;
    void ensure_light_and_shading_positions();
    void ensure_room_configurator();
    void open_room_configuration(const std::string& room_key, SlidingPanel return_panel = SlidingPanel::RoomsList);
    void close_room_configuration(bool show_rooms_list = false);
    void create_room_from_panel(SlidingPanel return_panel);
    SDL_Rect room_config_bounds() const;
    void show_sliding_panel(SlidingPanel panel, bool preserve_layers_panel = false);
    SDL_Rect sanitize_sliding_area(const SDL_Rect& bounds) const;
    SDL_Rect effective_work_area() const;
    void apply_sliding_area_bounds();
    nlohmann::json* active_room_entry();
    std::string rename_active_room(const std::string& old_name, const std::string& desired_name);
    void delete_active_room_spawn_group(const std::string& spawn_id);
    void reorder_active_room_spawn_group(const std::string& spawn_id, size_t index);
    void handle_rooms_data_mutated(bool refresh_rooms_list);
    void update_room_config_header_controls();
    void begin_map_color_sampling(const utils::color::RangedColor& current, std::function<void(SDL_Color)> on_sample, std::function<void()> on_cancel);
    void cancel_map_color_sampling(bool silent = false);
    void complete_map_color_sampling(SDL_Color color);

private:
    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    std::string map_id_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;
    SDL_Rect sliding_area_bounds_{0, 0, 0, 0};

    devmode::core::ManifestStore* manifest_store_ = nullptr;
    std::unique_ptr<MapLightPanel> light_panel_;
    std::unique_ptr<MapLayersPreviewPanel> layers_preview_panel_;
    std::shared_ptr<MapLayersController> layers_controller_;
    std::unique_ptr<SlidingWindowContainer> room_config_container_;
    std::unique_ptr<SlidingWindowContainer> rooms_list_container_;
    std::unique_ptr<SlidingWindowContainer> layer_controls_container_;
    std::unique_ptr<MapLayerControlsDisplay> layer_controls_display_;
    std::unique_ptr<MapRoomsDisplay> rooms_display_;
    std::unique_ptr<MapLayersPanel> layers_panel_;
    std::unique_ptr<DevFooterBar> footer_bar_;
    bool footer_buttons_configured_ = false;
    bool map_mode_active_ = false;
    bool footer_always_visible_ = false;
    std::vector<HeaderButtonConfig> map_mode_buttons_;
    std::vector<HeaderButtonConfig> room_mode_buttons_;
    HeaderMode header_mode_ = HeaderMode::Map;
    PanelType active_panel_ = PanelType::None;
    bool headers_suppressed_ = false;
    bool sliding_only_header_suppression_ = false;
    bool base_headers_suppressed_ = false;
    int sliding_header_request_count_ = 0;
    bool dev_sliding_headers_hidden_ = false;
    std::vector<DockableCollapsible*> floating_panels_;
    LightSaveCallback light_save_callback_;
    std::function<void(HeaderMode)> on_mode_changed_;
    bool light_panel_centered_ = false;
    bool last_lights_visible_ = false;
    std::unique_ptr<RoomConfigurator> room_configurator_;
    std::string active_room_config_key_;
    SlidingPanel active_sliding_panel_ = SlidingPanel::None;
    SlidingPanel room_config_return_panel_ = SlidingPanel::RoomsList;

    bool map_color_sampling_active_ = false;
    mutable bool map_color_sampling_preview_valid_ = false;
    SDL_Point map_color_sampling_cursor_{0, 0};
    mutable SDL_Color map_color_sampling_preview_{0, 0, 0, 255};
    SDL_Cursor* map_color_sampling_cursor_handle_ = nullptr;
    SDL_Cursor* map_color_sampling_prev_cursor_ = nullptr;
    std::function<void(SDL_Color)> map_color_sampling_apply_{};
    std::function<void()> map_color_sampling_cancel_{};
};
