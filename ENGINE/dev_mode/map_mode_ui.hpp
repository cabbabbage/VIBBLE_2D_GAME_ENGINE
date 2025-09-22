#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json_fwd.hpp>

class Assets;
class Input;
class MapLightPanel;
class MapAssetsPanel;
class MapLayersPanel;
class MapLayersController;
class FullScreenCollapsible;
class DockableCollapsible;
struct DMButtonStyle;
struct SDL_Renderer;
union SDL_Event;

// Coordinates interactions between map-mode floating panels (lighting, assets).
class MapModeUI {
public:
    enum class HeaderMode { Map, Room };

    struct HeaderButtonConfig {
        std::string id;
        std::string label;
        bool active = false;
        bool momentary = false;
        const DMButtonStyle* style_override = nullptr;
        std::function<void(bool)> on_toggle;
    };

    explicit MapModeUI(Assets* assets);
    ~MapModeUI();

    void set_map_context(nlohmann::json* map_info, const std::string& map_path);
    void set_screen_dimensions(int w, int h);
    void set_shared_assets_panel(const std::shared_ptr<MapAssetsPanel>& panel);

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    void open_assets_panel();
    void toggle_light_panel();
    void toggle_layers_panel();
    void close_all_panels();

    bool is_assets_panel_visible() const;
    bool is_light_panel_visible() const;
    using LightSaveCallback = std::function<void()>;

    void set_light_save_callback(LightSaveCallback cb);

    void set_map_mode_active(bool active);

    FullScreenCollapsible* get_footer_panel() const;
    void set_footer_always_visible(bool on);
    void set_mode_button_sets(std::vector<HeaderButtonConfig> map_buttons,
                              std::vector<HeaderButtonConfig> room_buttons);
    void set_header_mode(HeaderMode mode);
    void set_button_state(const std::string& id, bool active);
    void set_button_state(HeaderMode mode, const std::string& id, bool active);
    HeaderMode header_mode() const { return header_mode_; }

    bool is_point_inside(int x, int y) const;
    bool is_any_panel_visible() const;

private:
    void ensure_panels();
    void sync_panel_map_info();
    bool save_map_info_to_disk() const;
    void configure_footer_buttons();
    void sync_footer_button_states();
    void update_footer_visibility();
    enum class PanelType { None, Assets, Lights, Layers };
    void set_active_panel(PanelType panel);
    const char* panel_button_id(PanelType panel) const;
    void update_layers_footer(const Input& input);
    bool handle_layers_footer_event(const SDL_Event& e);
    void render_layers_footer(SDL_Renderer* renderer) const;
    bool should_show_layers_footer() const;
    void track_floating_panel(DockableCollapsible* panel);
    void rebuild_floating_stack();
    void bring_panel_to_front(DockableCollapsible* panel);
    bool handle_floating_panel_event(const SDL_Event& e, bool& used);
    bool pointer_inside_floating_panel(int x, int y) const;
    bool is_pointer_event(const SDL_Event& e) const;
    SDL_Point event_point(const SDL_Event& e) const;
    HeaderButtonConfig* find_button(HeaderMode mode, const std::string& id);

private:
    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;

    std::unique_ptr<MapLightPanel> light_panel_;
    std::shared_ptr<MapAssetsPanel> assets_panel_;
    bool owns_assets_panel_ = false;
    std::shared_ptr<MapLayersController> layers_controller_;
    std::unique_ptr<MapLayersPanel> layers_panel_;
    std::unique_ptr<FullScreenCollapsible> footer_panel_;
    bool footer_buttons_configured_ = false;
    bool map_mode_active_ = false;
    bool footer_always_visible_ = false;
    std::vector<HeaderButtonConfig> map_mode_buttons_;
    std::vector<HeaderButtonConfig> room_mode_buttons_;
    HeaderMode header_mode_ = HeaderMode::Map;
    PanelType active_panel_ = PanelType::None;
    bool layers_footer_requested_ = false;
    bool layers_footer_visible_ = false;
    std::vector<DockableCollapsible*> floating_panels_;
    LightSaveCallback light_save_callback_;
};




