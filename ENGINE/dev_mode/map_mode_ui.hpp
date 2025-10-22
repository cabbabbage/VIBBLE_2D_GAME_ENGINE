#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

class Assets;
namespace devmode::core {
class ManifestStore;
}
class Input;
class MapLightPanel;
class MapShadowPanel;
class MapLightPreviewPanel;
class MapLayersPreviewPanel;
class MapLayersPanel;
class MapLayerControlsDisplay;
class MapLayersController;
class RoomConfigurator;
class SlidingWindowContainer;
class MapGridPanel;
class DevFooterBar;
class DockableCollapsible;
struct DMButtonStyle;
struct SDL_Renderer;
class MapRoomsDisplay;

class MapModeUI {
public:
    enum class HeaderMode { Map, Room, Area };

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

    void set_manifest_store(devmode::core::ManifestStore* store);

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    void open_layers_panel();
    void open_light_panel();
    void close_light_panel();
    void toggle_light_panel();
    void open_light_map_panel();
    void close_light_map_panel();
    void toggle_light_map_panel();
    void open_shading_panel();
    void close_shading_panel();
    void toggle_shading_panel();
    void refresh_reactive_shadow_settings();
    void clear_reactive_shadow_settings();
    void open_grid_panel();
    void close_grid_panel();
    void toggle_grid_panel();
    void toggle_layers_panel();
    void close_all_panels();

    bool is_light_panel_visible() const;
    bool is_shading_panel_visible() const;
    bool is_light_map_panel_visible() const;
    bool is_grid_panel_visible() const;
    using LightSaveCallback = std::function<bool()>;
    using GridSaveCallback = std::function<bool()>;
    using GridRegenCallback = std::function<void()>;

    void set_light_save_callback(LightSaveCallback cb);
    void set_map_grid_callbacks(GridSaveCallback save_cb, GridRegenCallback regen_cb);

    void set_map_mode_active(bool active);

    DevFooterBar* get_footer_bar() const;
    void set_footer_always_visible(bool on);
    void set_headers_suppressed(bool suppressed);
    void set_sliding_headers_hidden(bool hidden);
    void set_dev_sliding_headers_hidden(bool hidden);
    void set_mode_button_sets(std::vector<HeaderButtonConfig> map_buttons,
                              std::vector<HeaderButtonConfig> room_buttons,
                              std::vector<HeaderButtonConfig> area_buttons = {});
    void set_header_mode(HeaderMode mode);
    void set_button_state(const std::string& id, bool active);
    void set_button_state(HeaderMode mode, const std::string& id, bool active);
    HeaderMode header_mode() const { return header_mode_; }
    void set_on_mode_changed(std::function<void(HeaderMode)> cb) { on_mode_changed_ = std::move(cb); }

    const std::vector<HeaderButtonConfig>& map_mode_button_configs() const { return map_mode_buttons_; }
    const std::vector<HeaderButtonConfig>& room_mode_button_configs() const { return room_mode_buttons_; }
    const std::vector<HeaderButtonConfig>& area_mode_button_configs() const { return area_mode_buttons_; }

    bool is_point_inside(int x, int y) const;
    bool is_any_panel_visible() const;
    bool is_layers_panel_visible() const;

private:
    void ensure_panels();
    void sync_panel_map_info();
    bool save_map_info_to_disk() const;
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
    void open_room_configuration(const std::string& room_key);
    void close_room_configuration(bool show_rooms_list = false);
    SDL_Rect room_config_bounds() const;
    void show_sliding_panel(SlidingPanel panel, bool preserve_layers_panel = false);

private:
    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    std::string map_id_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;

    devmode::core::ManifestStore* manifest_store_ = nullptr;
    std::unique_ptr<MapLightPanel> light_panel_;
    std::unique_ptr<MapShadowPanel> shadow_panel_;
    std::unique_ptr<MapLightPreviewPanel> preview_panel_;
    std::unique_ptr<MapLayersPreviewPanel> layers_preview_panel_;
    std::shared_ptr<MapLayersController> layers_controller_;
    std::unique_ptr<SlidingWindowContainer> room_config_container_;
    std::unique_ptr<SlidingWindowContainer> rooms_list_container_;
    std::unique_ptr<SlidingWindowContainer> layer_controls_container_;
    std::unique_ptr<MapLayerControlsDisplay> layer_controls_display_;
    std::unique_ptr<MapRoomsDisplay> rooms_display_;
    std::unique_ptr<MapLayersPanel> layers_panel_;
    std::unique_ptr<MapGridPanel> grid_panel_;
    std::unique_ptr<DevFooterBar> footer_bar_;
    bool footer_buttons_configured_ = false;
    bool map_mode_active_ = false;
    bool footer_always_visible_ = false;
    std::vector<HeaderButtonConfig> map_mode_buttons_;
    std::vector<HeaderButtonConfig> room_mode_buttons_;
    std::vector<HeaderButtonConfig> area_mode_buttons_;
    HeaderMode header_mode_ = HeaderMode::Map;
    PanelType active_panel_ = PanelType::None;
    bool headers_suppressed_ = false;
    bool base_headers_suppressed_ = false;
    bool sliding_headers_hidden_external_ = false;
    bool dev_sliding_headers_hidden_ = false;
    std::vector<DockableCollapsible*> floating_panels_;
    LightSaveCallback light_save_callback_;
    GridSaveCallback grid_save_callback_;
    GridRegenCallback grid_regen_callback_;
    std::function<void(HeaderMode)> on_mode_changed_;
    bool light_panel_centered_ = false;
    bool shading_panel_centered_ = false;
    bool preview_panel_centered_ = false;
    bool last_lights_visible_ = false;
    bool last_shading_visible_ = false;
    bool last_preview_visible_ = false;
    std::unique_ptr<RoomConfigurator> room_configurator_;
    std::string active_room_config_key_;
    SlidingPanel active_sliding_panel_ = SlidingPanel::None;
};

