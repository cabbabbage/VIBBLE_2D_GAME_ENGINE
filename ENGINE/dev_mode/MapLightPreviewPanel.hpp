#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include "DockableCollapsible.hpp"
#include "render/light_map_manager.hpp"

class Assets;
class Input;
class LightMap;
class LightMapManager;

class MapLightPreviewPanel : public DockableCollapsible {
public:
    explicit MapLightPreviewPanel(Assets* assets, int x = 72, int y = 40);
    ~MapLightPreviewPanel() override;

    void update(const Input& input, int screen_w = 0, int screen_h = 0);
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    bool is_point_inside(int x, int y) const override;

    void set_assets(Assets* assets);
    int  selected_quadrant() const { return selected_quadrant_; }

protected:
    void render_content(SDL_Renderer* renderer) const override;
    void layout_custom_content(int screen_w, int screen_h) const override;

private:
    const class LightMap*        current_light_map() const;
    const LightMapManager*       light_map_manager() const;
    const LightMapManager::QuadrantSnapshot* snapshot_for_quadrant(int index) const;
    std::optional<SDL_Point>      player_screen_position() const;
    std::vector<std::string>      assets_in_quadrant(int quadrant) const;
    int                           quadrant_index_from_point(int x, int y) const;
    void                          render_preview(SDL_Renderer* renderer) const;

    Assets* assets_ = nullptr;
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};
    mutable SDL_Rect preview_grid_rect_{0, 0, 0, 0};
    mutable std::vector<SDL_Rect> quadrant_preview_rects_{};
    mutable std::vector<LightMapManager::QuadrantSnapshot> quadrant_snapshots_{};
    mutable std::vector<bool> quadrant_snapshot_valid_{};
    mutable int screen_width_px_ = 0;
    mutable int screen_height_px_ = 0;
    int selected_quadrant_ = -1;
    std::string quadrant_note_text_;
};

