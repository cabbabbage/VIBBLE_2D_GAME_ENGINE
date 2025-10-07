#pragma once

#include <SDL.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class AssetInfo;
class Input;

namespace animation_editor {

class AnimationDocument;
class AnimationListPanel;
class PreviewProvider;
class CroppingService;
class AsyncTaskQueue;
class AudioImporter;
class FrameMovementEditor;
class DMButton;

class AnimationEditorWindow {
  public:
    AnimationEditorWindow();

    void set_visible(bool visible);
    bool is_visible() const { return visible_; }
    void toggle_visible();

    void set_bounds(const SDL_Rect& bounds);
    const SDL_Rect& bounds() const { return bounds_; }

    void set_info(const std::shared_ptr<AssetInfo>& info);
    void clear_info();

    void update(const Input& input, int screen_w, int screen_h);
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void layout_children();
    void configure_list_panel();
    void render_background(SDL_Renderer* renderer) const;
    void render_header(SDL_Renderer* renderer) const;
    void render_status(SDL_Renderer* renderer) const;
    void render_movement_overlay(SDL_Renderer* renderer) const;
    bool handle_header_event(const SDL_Event& e);
    void set_status_message(const std::string& message, int frames = 300);
    void open_movement_editor(const std::string& animation_id);
    void close_movement_editor();
    void create_animation_via_prompt();
    void save_document();
    void reload_document();

    std::optional<std::filesystem::path> pick_folder() const;
    std::optional<std::filesystem::path> pick_gif() const;
    std::vector<std::filesystem::path> pick_png_sequence() const;
    std::optional<std::string> pick_animation_reference() const;
    std::optional<std::filesystem::path> pick_audio_file() const;

  private:
    bool visible_ = false;
    SDL_Rect bounds_{0, 0, 0, 0};
    std::weak_ptr<AssetInfo> info_;
    std::filesystem::path info_path_;
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::shared_ptr<CroppingService> cropping_service_;
    std::shared_ptr<AsyncTaskQueue> task_queue_;
    std::shared_ptr<AudioImporter> audio_importer_;
    std::unique_ptr<AnimationListPanel> list_panel_;
    std::unique_ptr<FrameMovementEditor> movement_editor_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<DMButton> save_button_;
    std::unique_ptr<DMButton> reload_button_;
    std::unique_ptr<DMButton> close_button_;
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect list_rect_{0, 0, 0, 0};
    SDL_Rect status_rect_{0, 0, 0, 0};
    SDL_Rect movement_editor_rect_{0, 0, 0, 0};
    std::string status_message_;
    int status_timer_frames_ = 0;
    bool movement_editor_visible_ = false;
    std::string movement_editor_animation_id_;
};

}  // namespace animation_editor

