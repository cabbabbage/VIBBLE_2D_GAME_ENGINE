#pragma once

#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>
#include <nlohmann/json_fwd.hpp>

namespace devmode::core {
class ManifestStore;
}

class DMButton;
class SearchAssets;
struct SDL_Texture;

namespace animation_editor {

class AnimationDocument;

class ChildrenPanel {
  public:
    using StatusCallback = std::function<void(const std::string&)>;

    ChildrenPanel();
    ~ChildrenPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_manifest_store(devmode::core::ManifestStore* store);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_status_callback(StatusCallback callback);
    void set_layout_dirty_callback(std::function<void()> callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    void render_overlays(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

    // Returns true if an embedded overlay (e.g., search panel) is active
    // and should be allowed to receive pointer events outside of the
    // inspector panel's strict bounds.
    bool allow_out_of_bounds_pointer_events() const;

  private:
    void refresh_from_document();
    std::vector<std::string> read_local_children(const nlohmann::json& payload) const;
    std::vector<std::string> resolve_inherited_children(const nlohmann::json& payload, int depth = 0) const;
    void commit_children();
    void add_child_entry(const std::string& entry);
    void remove_child_entry(size_t index);
    void ensure_search_panel();
    void open_search_panel();
    void close_search_panel();
    void position_search_panel() const;
    void update_layout() const;
    void request_layout() const;
    void request_status(const std::string& message) const;
    bool point_inside(const SDL_Rect& rect, int x, int y) const;

    struct PreviewEntry {
        SDL_Renderer* renderer = nullptr;
        std::shared_ptr<SDL_Texture> texture;
        std::filesystem::path frame_path;
        std::filesystem::file_time_type last_write_time{};
    };

    SDL_Texture* acquire_child_icon(SDL_Renderer* renderer, const std::string& child_id) const;
    std::optional<std::filesystem::path> resolve_frame_path(const nlohmann::json& animation_json,
                                                            const std::string& asset_name,
                                                            const std::string& animation_key) const;
    std::filesystem::path resolve_candidate_path(const std::filesystem::path& candidate,
                                                 const std::string& asset_name) const;
    std::filesystem::path detect_repo_root() const;
    static std::filesystem::path ensure_png_in_folder(const std::filesystem::path& folder, int frame_count);

  private:
    std::shared_ptr<AnimationDocument> document_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    StatusCallback status_callback_;
    std::function<void()> layout_dirty_callback_;

    std::vector<std::string> local_children_;
    std::vector<std::string> inherited_children_;
    std::vector<std::string> display_children_;
    std::vector<std::string> inherited_message_lines_;
    std::string inherited_source_id_;
    bool inherits_children_ = false;

    std::unique_ptr<DMButton> add_button_;

    mutable bool layout_dirty_ = true;
    mutable SDL_Rect header_rect_{0, 0, 0, 0};
    mutable SDL_Rect message_rect_{0, 0, 0, 0};
    mutable SDL_Rect list_rect_{0, 0, 0, 0};
    mutable SDL_Rect add_button_rect_{0, 0, 0, 0};
    mutable std::vector<SDL_Rect> row_rects_;
    mutable std::vector<SDL_Rect> remove_rects_;
    mutable std::vector<SDL_Rect> icon_rects_;

    std::string payload_signature_;
    mutable std::filesystem::path cached_repo_root_;

    mutable std::unordered_map<std::string, PreviewEntry> preview_cache_;

    std::unique_ptr<SearchAssets> search_assets_;
    mutable SDL_Rect search_anchor_rect_{0, 0, 0, 0};

    int row_height_ = 44;
    int icon_size_ = 36;
};

}  // namespace animation_editor
