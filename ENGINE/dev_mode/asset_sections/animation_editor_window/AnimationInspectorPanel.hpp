#pragma once

#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

struct SDL_Rect;
union SDL_Event;
struct SDL_Renderer;
#include <SDL.h>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "EditorUIPrimitives.hpp"
#include "PreviewTimeline.hpp"
#include "AudioPanel.hpp"
#include "OnEndSelector.hpp"
#include "MovementSummaryWidget.hpp"
#include "PlaybackSettingsPanel.hpp"
#include "SourceConfigPanel.hpp"

namespace devmode::core {
class ManifestStore;
}

class DMButton;
class DMTextBox;
class DMSlider;

namespace animation_editor {

class AnimationDocument;
class SourceConfigPanel;
class PlaybackSettingsPanel;
class MovementSummaryWidget;
class OnEndSelector;
class AudioPanel;
class PreviewProvider;
class AsyncTaskQueue;
class AudioImporter;
class PreviewTimeline;

using DMButton = ::DMButton;
using DMTextBox = ::DMTextBox;

class AnimationInspectorPanel {
  public:
    using PathPicker = std::function<std::optional<std::filesystem::path>()>;
    using MultiPathPicker = std::function<std::vector<std::filesystem::path>()>;
    using AnimationPicker = std::function<std::optional<std::string>()>;
    using StatusCallback = std::function<void(const std::string&)>;
    using FrameEditCallback = std::function<void(const std::string&)>;
    using AudioFilePicker = std::function<std::optional<std::filesystem::path>()>;
    using AnimationNavigateCallback = std::function<void(const std::string&)>;

    AnimationInspectorPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_preview_provider(std::shared_ptr<PreviewProvider> provider);

    void set_task_queue(std::shared_ptr<AsyncTaskQueue> tasks);
    void set_source_folder_picker(PathPicker picker);
    void set_source_animation_picker(AnimationPicker picker);
    void set_source_gif_picker(PathPicker picker);
    void set_source_png_sequence_picker(MultiPathPicker picker);
    void set_source_status_callback(StatusCallback callback);
    void set_frame_edit_callback(FrameEditCallback callback);
    void set_navigate_to_animation_callback(AnimationNavigateCallback callback);
    void set_audio_importer(std::shared_ptr<AudioImporter> importer);
    void set_audio_file_picker(AudioFilePicker picker);
    void set_manifest_store(devmode::core::ManifestStore* store);
    void set_on_animation_properties_changed(std::function<void(const std::string&, const nlohmann::json&)> callback) { on_animation_properties_changed_ = std::move(callback); }

    int height_for_width(int width) const;

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    void apply_dropdown_selections();

    void set_scrub_mode(bool enable);
    void set_scrub_frame(int frame);

  private:
    void rebuild_widgets();
    void refresh_totals();
    void layout_widgets() const;
    void ensure_preview_controls();
    void update_preview_playback();
    void render_preview(SDL_Renderer* renderer) const;
    void render_preview_controls(SDL_Renderer* renderer) const;
    void sync_slider_to_current_frame();
    void sync_timeline_to_slider(int display_frame);
    int display_frame_from_timeline(int timeline_frame) const;
    int timeline_frame_from_display(int display_frame) const;
    void commit_rename();
    void refresh_start_indicator();
    void apply_dependencies();
    void update_source_mode_button_styles();
    void refresh_preview_metadata() const;
    bool handle_scroll_wheel(const SDL_Event& e);
    void update_scrollbar_geometry() const;
    void render_scrollbar(SDL_Renderer* renderer) const;
    void render_overlays(SDL_Renderer* renderer) const;

    enum class FocusTarget {
        kNone = -1,
        kName = 0,
        kStart,
        kSourceFrames,
        kSourceAnimation,
};

    std::vector<FocusTarget> focus_order() const;
    void set_focus(FocusTarget target);
    void announce_focus(FocusTarget target) const;
    void activate_focus_target(FocusTarget target);
    void refresh_focus_index() const;

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
    std::unique_ptr<DMButton> source_frames_button_;
    std::unique_ptr<DMButton> source_animation_button_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};
    mutable SDL_Rect header_rect_{0, 0, 0, 0};
    mutable SDL_Rect source_selector_rect_{0, 0, 0, 0};
    mutable SDL_Rect source_summary_rect_{0, 0, 0, 0};
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};
    mutable SDL_Rect source_rect_{0, 0, 0, 0};
    mutable SDL_Rect playback_rect_{0, 0, 0, 0};
    mutable SDL_Rect movement_rect_{0, 0, 0, 0};
    mutable SDL_Rect on_end_rect_{0, 0, 0, 0};
    mutable SDL_Rect audio_rect_{0, 0, 0, 0};
    mutable SDL_Rect scrollbar_track_{0, 0, 0, 0};
    mutable SDL_Rect scrollbar_thumb_{0, 0, 0, 0};
    mutable bool layout_dirty_ = true;
    mutable bool scrollbar_visible_ = false;
    mutable std::string preview_signature_;
    mutable bool preview_reverse_ = false;
    mutable bool preview_flip_x_ = false;
    mutable bool preview_flip_y_ = false;
    mutable bool preview_flip_movement_x_ = false;
    mutable bool preview_flip_movement_y_ = false;
    mutable std::vector<std::string> preview_modifier_badges_;
    bool rename_pending_ = false;
    bool is_start_animation_ = false;
    int focus_index_ = -1;
    FocusTarget current_focus_target_ = FocusTarget::kNone;
    bool source_uses_animation_ = false;

    std::unique_ptr<PreviewTimeline> preview_timeline_;
    std::unique_ptr<DMButton> preview_play_button_;
    std::unique_ptr<DMSlider> preview_scrub_slider_;
    mutable SDL_Rect preview_controls_rect_{0, 0, 0, 0};
    int preview_slider_max_frame_ = 0;
    bool preview_scrubbing_active_ = false;
    mutable bool was_playing_before_scrub_ = false;

    std::shared_ptr<AsyncTaskQueue> task_queue_;
    PathPicker folder_picker_;
    AnimationPicker animation_picker_;
    PathPicker gif_picker_;
    MultiPathPicker png_sequence_picker_;
    StatusCallback status_callback_;
    FrameEditCallback frame_edit_callback_;
    AnimationNavigateCallback navigate_to_animation_callback_;
    std::shared_ptr<AudioImporter> audio_importer_;
    AudioFilePicker audio_file_picker_;
    std::function<void(const std::string&, const nlohmann::json&)> on_animation_properties_changed_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    ui::WidgetRegistry widget_registry_;

    mutable int current_frame_ = 0;
    mutable int frame_count_ = 1;

    bool scrub_mode_ = false;
    int scrub_frame_ = 0;

    mutable int content_height_ = 0;
    mutable ui::ScrollController scroll_controller_;
};

}
