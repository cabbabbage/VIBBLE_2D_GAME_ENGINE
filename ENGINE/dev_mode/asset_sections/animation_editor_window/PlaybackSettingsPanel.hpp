#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL.h>

#include <nlohmann/json.hpp>

#include "dev_mode/widgets.hpp"

class DMCheckbox;
class DMSlider;

namespace animation_editor {

class AnimationDocument;

using DMCheckbox = ::DMCheckbox;
using DMSlider = ::DMSlider;

class PlaybackSettingsPanel {
  public:
    PlaybackSettingsPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);

    void update();
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

  private:
    struct PlaybackState {
        bool flipped_source = false;
        bool reverse_source = false;
        bool flip_vertical = false;
        bool flip_movement_horizontal = false;
        bool flip_movement_vertical = false;
        bool inherit_source_movement = true;
        bool locked = false;
        bool random_start = false;

        bool operator==(const PlaybackState& other) const {
            return flipped_source == other.flipped_source &&
                   reverse_source == other.reverse_source &&
                   flip_vertical == other.flip_vertical &&
                   flip_movement_horizontal == other.flip_movement_horizontal &&
                   flip_movement_vertical == other.flip_movement_vertical &&
                   inherit_source_movement == other.inherit_source_movement &&
                   locked == other.locked &&
                   random_start == other.random_start;
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
    PlaybackState payload_to_state(const nlohmann::json& payload);
    void apply_state_to_payload(nlohmann::json& payload, const PlaybackState& state);
    void update_inherited_state(const nlohmann::json& payload);
    void refresh_inherited_message();
    bool random_start_visible_for_state(const PlaybackState& state) const;
    bool random_start_visible() const { return random_start_visible_for_state(state_); }

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::string animation_id_;
    SDL_Rect bounds_{0, 0, 0, 0};

    std::unique_ptr<DMCheckbox> flip_checkbox_;
    std::unique_ptr<DMCheckbox> flip_vertical_checkbox_;
    std::unique_ptr<DMCheckbox> inherit_movement_checkbox_;
    std::unique_ptr<DMCheckbox> flip_movement_horizontal_checkbox_;
    std::unique_ptr<DMCheckbox> flip_movement_vertical_checkbox_;
    std::unique_ptr<DMCheckbox> reverse_checkbox_;
    std::unique_ptr<DMCheckbox> locked_checkbox_;
    std::unique_ptr<DMCheckbox> random_start_checkbox_;
    std::unique_ptr<DMSlider> speed_slider_;

    PlaybackState state_{};
    PlaybackState document_state_{};
    bool has_document_state_ = false;
    mutable bool layout_dirty_ = true;
    bool is_syncing_ui_ = false;
    bool derived_from_animation_ = false;
    std::string derived_source_id_;
    std::vector<std::string> inherited_message_lines_;
    std::vector<std::string> inherited_modifiers_;
    mutable SDL_Rect inherited_message_rect_{0, 0, 0, 0};

    DMWidgetTooltipState info_tooltip_{};
};

}

