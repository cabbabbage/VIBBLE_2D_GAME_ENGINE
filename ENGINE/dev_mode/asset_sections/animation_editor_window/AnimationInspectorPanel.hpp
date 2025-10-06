#pragma once

#include <memory>
#include <string>

struct SDL_Rect;
struct SDL_Event;
struct SDL_Renderer;

namespace animation_editor {

class AnimationDocument;
class SourceConfigPanel;
class PlaybackSettingsPanel;
class MovementSummaryWidget;
class OnEndSelector;
class AudioPanel;
class PreviewProvider;
class DMButton;
class DMTextBox;

class AnimationInspectorPanel {
  public:
    AnimationInspectorPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);

    int height_for_width(int width) const;

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void rebuild_widgets();
    void refresh_totals();
    void layout_widgets() const;
    void commit_rename();
    void refresh_start_indicator();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::unique_ptr<SourceConfigPanel> source_config_;
    std::unique_ptr<PlaybackSettingsPanel> playback_settings_;
    std::unique_ptr<MovementSummaryWidget> movement_summary_;
    std::unique_ptr<OnEndSelector> on_end_selector_;
    std::unique_ptr<AudioPanel> audio_panel_;
    std::unique_ptr<DMTextBox> name_box_;
    std::unique_ptr<DMButton> start_button_;
    std::unique_ptr<DMButton> delete_button_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    mutable SDL_Rect header_rect_{0, 0, 0, 0};
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};
    mutable SDL_Rect source_rect_{0, 0, 0, 0};
    mutable SDL_Rect playback_rect_{0, 0, 0, 0};
    mutable SDL_Rect movement_rect_{0, 0, 0, 0};
    mutable SDL_Rect on_end_rect_{0, 0, 0, 0};
    mutable SDL_Rect audio_rect_{0, 0, 0, 0};
    mutable bool layout_dirty_ = true;
    bool rename_pending_ = false;
    bool is_start_animation_ = false;
};

}  // namespace animation_editor

