#pragma once

#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

class Assets;
class Input;
class MapLightPanel;
class MapAssetsPanel;
struct SDL_Renderer;
struct SDL_Event;

// Coordinates interactions between map-mode floating panels (lighting, assets).
class MapModeUI {
public:
    explicit MapModeUI(Assets* assets);
    ~MapModeUI();

    void set_map_context(nlohmann::json* map_info, const std::string& map_path);
    void set_screen_dimensions(int w, int h);

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* renderer) const;

    void open_assets_panel();
    void toggle_light_panel();
    void close_all_panels();

    bool is_point_inside(int x, int y) const;
    bool is_any_panel_visible() const;

private:
    void ensure_panels();
    void sync_panel_map_info();
    bool save_map_info_to_disk() const;

private:
    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    std::string map_path_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;

    std::unique_ptr<MapLightPanel> light_panel_;
    std::unique_ptr<MapAssetsPanel> assets_panel_;
};

