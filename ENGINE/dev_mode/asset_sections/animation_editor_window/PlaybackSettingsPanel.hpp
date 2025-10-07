#pragma once

#include <memory>
#include <optional>
#include <string>

#include <SDL.h>

#include <nlohmann/json.hpp>

#include "dev_mode/widgets.hpp"

namespace animation_editor {

class AnimationDocument;

class PlaybackSettingsPanel {
  public:
    PlaybackSettingsPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

  private:
    struct PlaybackState {
        bool flipped_source = false;
        bool reverse_source = false;
        bool locked = false;
        bool loop = false;
        bool random_start = false;
        int speed_factor = 1;

        bool operator==(const PlaybackState& other) const {
            return flipped_source == other.flipped_source &&
                   reverse_source == other.reverse_source &&
                   locked == other.locked &&
                   loop == other.loop &&
                   random_start == other.random_start &&
                   speed_factor == other.speed_factor;
        }

        bool operator!=(const PlaybackState& other) const { return !(*this == other); }
    };

    void ensure_widgets();
    void layout_widgets() const;
    void apply_state_to_controls(const PlaybackState& state);
    PlaybackState read_controls() const;
    void handle_controls_changed();
    void sync_from_document();
    void commit_changes(const PlaybackState& desired_state);
    static std::optional<std::string> fetch_payload(const AnimationDocument* document, const std::string& animation_id);
    static PlaybackState payload_to_state(const nlohmann::json& payload);
    static void apply_state_to_payload(nlohmann::json& payload, const PlaybackState& state);

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};

    std::unique_ptr<DMCheckbox> flip_checkbox_;
    std::unique_ptr<DMCheckbox> reverse_checkbox_;
    std::unique_ptr<DMCheckbox> locked_checkbox_;
    std::unique_ptr<DMCheckbox> loop_checkbox_;
    std::unique_ptr<DMCheckbox> random_start_checkbox_;
    std::unique_ptr<DMSlider> speed_slider_;

    PlaybackState state_{};
    PlaybackState document_state_{};
    bool has_document_state_ = false;
    mutable bool layout_dirty_ = true;
    bool is_syncing_ui_ = false;
};

}  // namespace animation_editor

