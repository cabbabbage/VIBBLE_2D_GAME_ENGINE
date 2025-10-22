#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

#include "DockableCollapsible.hpp"
#include "world/chunk.hpp"
#include "render/light_map_manager.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettings.hpp"
#include "dev_mode/PreviewViewport.hpp"

class Assets;
class Input;
class LightMap;
class LightMapManager;

class MapLightPreviewPanel : public DockableCollapsible {
public:
    class PreviewWidget;

    explicit MapLightPreviewPanel(Assets* assets, int x = 72, int y = 40);
    ~MapLightPreviewPanel() override;

    using SaveCallback = std::function<bool()>;

    void update(const Input& input, int screen_w = 0, int screen_h = 0);
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* renderer) const override;

    bool is_point_inside(int x, int y) const override;

    void set_assets(Assets* assets);
    void set_map_info(nlohmann::json* map_info, SaveCallback on_save = nullptr);
    void set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings);
    int  selected_chunk() const { return selected_chunk_; }

protected:
    void render_content(SDL_Renderer* renderer) const override;
    void layout_custom_content(int screen_w, int screen_h) const override;

private:
    friend class PreviewWidget;

    const class LightMap*        current_light_map() const;
    const LightMapManager*       light_map_manager() const;
    const LightMapManager::ChunkSnapshot* snapshot_for_chunk(int index) const;
    std::optional<SDL_Point>      player_screen_position() const;
    std::vector<std::string>      assets_in_chunk(int Chunk) const;
    int                           chunk_index_from_point(int x, int y) const;
    void                          render_preview(SDL_Renderer* renderer) const;
    bool                          handle_preview_event(const SDL_Event& e);
    void                          cycle_preview_stage(int delta);
    int                           preview_height_for_width(int width) const;
    int                           estimated_detail_line_count() const;
    static int                    count_lines(std::string_view text);
    void                          rebuild_rows();
    void                          build_ui();
    void                          sync_ui_from_json();
    void                          sync_json_from_ui();
    void                          apply_immediate_settings();
    render_pipeline::shading::ReactiveShadowSettings current_settings_from_ui() const;
    void                          set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings);
    render_pipeline::shading::ReactiveShadowSettings load_reactive_settings_from_dev_settings();
    void                          persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const;
    void                          write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings);
    nlohmann::json&               ensure_reactive_settings_json();
    void                          force_shading_refresh_if_needed(bool force_refresh);
    void                          handle_chunk_resolution_changed();

    Assets* assets_ = nullptr;
    nlohmann::json* map_info_ = nullptr;
    SaveCallback on_save_{};
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};
    mutable SDL_Rect preview_widget_bounds_{0, 0, 0, 0};
    mutable SDL_Rect preview_grid_rect_{0, 0, 0, 0};
    mutable std::vector<SDL_Rect> chunk_preview_rects_{};
    mutable std::vector<LightMapManager::ChunkSnapshot> chunk_snapshots_{};
    mutable std::vector<bool> chunk_snapshot_valid_{};
    mutable int screen_width_px_ = 0;
    mutable int screen_height_px_ = 0;
    int selected_chunk_ = -1;
    std::string chunk_note_text_;

    std::unique_ptr<class DMSlider> horizontal_falloff_;
    std::unique_ptr<class DMSlider> vertical_falloff_;
    std::unique_ptr<class DMSlider> max_offset_x_;
    std::unique_ptr<class DMSlider> max_offset_y_;
    std::unique_ptr<class DMSlider> search_radius_;
    std::unique_ptr<class DMSlider> chunk_resolution_;
    std::vector<std::unique_ptr<class Widget>> widget_wrappers_{};

    bool needs_sync_to_json_ = false;

    render_pipeline::shading::ReactiveShadowSettings last_applied_settings_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    render_pipeline::shading::ReactiveShadowSettings* reactive_settings_shared_ = nullptr;
    bool reactive_settings_initialized_ = false;
    render_pipeline::shading::ReactiveShadowSettings forced_settings_snapshot_ =
        render_pipeline::shading::sanitize_reactive_shadow_settings({});
    int last_chunk_resolution_ = 0;

    mutable PreviewViewport preview_viewport_{};
    int                    preview_stage_index_ = 0;
};




