#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>
#include <nlohmann/json.hpp>

#include "../../../../../asset/animation_child_data.hpp"

namespace animation_editor {

class AnimationDocument;
class PreviewProvider;
class FrameToolsPanel;
class MovementCanvas;

class FrameChildrenEditor {
  public:
    FrameChildrenEditor();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);
    void set_tools_panel(FrameToolsPanel* panel);
    void set_canvas(MovementCanvas* canvas);
    void set_selected_frame(int index);
    int selected_child_index() const { return selected_child_index_; }
    std::string selected_child_id() const;
    AnimationChildMode selected_child_mode() const;
    void refresh_payload_cache_from_document();

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    bool handle_key_event(const SDL_Event& e);

  private:
    struct ChildFrame {
        int child_index = -1;
        float dx = 0.0f;
        float dy = 0.0f;
        float rotation = 0.0f;
        bool visible = true;
        bool render_in_front = true;
};

    struct MovementFrame {
        float dx = 0.0f;
        float dy = 0.0f;
        bool resort_z = false;
        std::vector<ChildFrame> children;
};

  private:
    void reload_from_document();
    void ensure_child_vectors();
    void refresh_tools_panel() const;
    void select_child(int index);
    void apply_current_to_next();
    void set_child_visible(bool visible);
    void set_child_mode(AnimationChildMode mode);
    void add_or_rename_child(const std::string& raw_name);
    void remove_selected_child();
    void persist_changes();
    void invalidate_child_caches();
    void ensure_child_mode_size();
    AnimationChildMode child_mode(int child_index) const;
    int child_mode_index(AnimationChildMode mode) const;
    std::vector<int> build_child_index_remap(const std::vector<std::string>& previous, const std::vector<std::string>& next) const;
    void remap_child_indices(const std::vector<int>& remap);
    void apply_child_list_change(const std::vector<std::string>& next_children);
    bool timeline_entry_is_static(const nlohmann::json& entry) const;
    ChildFrame child_frame_from_sample(const nlohmann::json& sample, int child_index) const;
    nlohmann::json child_frame_to_json(const ChildFrame& frame) const;
    void apply_child_timelines_from_payload(const nlohmann::json& payload);
    nlohmann::json build_child_timelines_payload(const nlohmann::json& existing_payload);
    MovementFrame* current_frame();
    const MovementFrame* current_frame() const;
    ChildFrame* current_child();
    const ChildFrame* current_child() const;
    SDL_FPoint frame_anchor(int frame_index) const;
    bool point_in_canvas(int x, int y) const;
    SDL_FPoint screen_to_world(SDL_Point screen) const;
    SDL_FPoint world_to_screen(const SDL_FPoint& world) const;
    SDL_FPoint child_screen_position(const ChildFrame& child, const SDL_FPoint& anchor_screen, float offset_scale) const;
    int hit_test_child(int x, int y) const;
    float canvas_pixels_per_unit() const;
    float document_scale_factor() const;
    float child_scale_percentage(const std::string& child_id) const;
    std::filesystem::path resolve_assets_root() const;
    float lookup_scale_from_manifest(const std::string& key) const;
    void ensure_manifest_scale_cache() const;
    std::filesystem::path resolve_manifest_path() const;
    std::filesystem::path resolve_child_asset_directory(const std::string& child_id) const;
    std::filesystem::path resolve_child_frame_path(const std::string& child_id) const;
    std::filesystem::path find_first_frame_in_folder(const std::filesystem::path& folder) const;
    SDL_Texture* acquire_child_texture(SDL_Renderer* renderer, const std::string& child_id, int* tex_w, int* tex_h) const;

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_;
    FrameToolsPanel* tools_panel_ = nullptr;
    MovementCanvas* canvas_ = nullptr;
    std::string animation_id_;
    std::vector<std::string> child_ids_;
    std::vector<AnimationChildMode> child_modes_;
    std::vector<MovementFrame> frames_;
    int selected_frame_index_ = 0;
    int selected_child_index_ = 0;
    bool dragging_child_ = false;
    SDL_Point drag_start_screen_{0, 0};
    ChildFrame drag_snapshot_;

    std::string payload_signature_;
    std::string payload_cache_;
    std::string children_signature_cache_;
    struct ChildPreviewTexture {
        SDL_Renderer* renderer = nullptr;
        std::shared_ptr<SDL_Texture> texture;
        std::filesystem::path source_path;
        std::filesystem::file_time_type last_write_time{};
        bool has_timestamp = false;
        int width = 0;
        int height = 0;
};
    mutable std::unordered_map<std::string, ChildPreviewTexture> child_previews_;
    mutable std::unordered_map<std::string, std::filesystem::path> child_asset_dir_cache_;
    mutable std::filesystem::path cached_assets_root_;
    mutable bool cached_assets_root_valid_ = false;
    mutable std::unordered_map<std::string, float> child_scale_cache_;
    mutable std::unordered_map<std::string, float> manifest_scale_cache_;
    mutable bool manifest_scale_cache_valid_ = false;
    mutable std::filesystem::path cached_manifest_path_;
    mutable bool cached_manifest_path_valid_ = false;
};

}
