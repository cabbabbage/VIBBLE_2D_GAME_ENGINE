#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json.hpp>

#include "dev_mode/widgets.hpp"

class DMButton;
class DMSlider;
class DMCheckbox;

namespace animation_editor {

class AnimationDocument;
class AudioImporter;

using DMButton = ::DMButton;
using DMSlider = ::DMSlider;
using DMCheckbox = ::DMCheckbox;

class AudioPanel {
  public:
    AudioPanel();

    using FilePicker = std::function<std::optional<std::filesystem::path>()>;

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_importer(std::shared_ptr<AudioImporter> importer);
    void set_file_picker(FilePicker picker);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

  private:
    void attach_audio();
    void replace_audio();
    void remove_audio();
    void preview_audio();

    void ensure_widgets();
    void layout_widgets() const;
    void sync_from_document();
    void apply_state_to_controls();
    void commit_audio_state();
    std::filesystem::path resolve_audio_path() const;
    void update_inherited_state(const nlohmann::json& payload);
    void refresh_inherited_message();

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<AudioImporter> importer_;
    FilePicker file_picker_;
    std::string animation_id_;
    std::string audio_name_;
    SDL_Rect bounds_{0, 0, 0, 0};
    int volume_ = 100;
    bool effects_enabled_ = false;
    bool has_audio_ = false;
    mutable bool layout_dirty_ = true;

    std::unique_ptr<DMButton> attach_button_;
    std::unique_ptr<DMButton> replace_button_;
    std::unique_ptr<DMButton> remove_button_;
    std::unique_ptr<DMButton> preview_button_;
    std::unique_ptr<DMSlider> volume_slider_;
    std::unique_ptr<DMCheckbox> effects_checkbox_;
    bool derived_from_animation_ = false;
    std::string inherited_source_id_;
    std::vector<std::string> inherited_message_lines_;
    mutable SDL_Rect inherited_message_rect_{0, 0, 0, 0};

    DMWidgetTooltipState info_tooltip_{};
};

}

