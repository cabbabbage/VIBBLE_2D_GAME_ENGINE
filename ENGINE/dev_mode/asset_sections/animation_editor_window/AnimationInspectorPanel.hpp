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

class AnimationInspectorPanel {
  public:
    AnimationInspectorPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    void rebuild_widgets();
    void refresh_totals();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::unique_ptr<SourceConfigPanel> source_config_;
    std::unique_ptr<PlaybackSettingsPanel> playback_settings_;
    std::unique_ptr<MovementSummaryWidget> movement_summary_;
    std::unique_ptr<OnEndSelector> on_end_selector_;
    std::unique_ptr<AudioPanel> audio_panel_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
};

}  // namespace animation_editor

