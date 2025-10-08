#include "PlaybackSettingsPanel.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

#include "AnimationDocument.hpp"

#include <nlohmann/json.hpp>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"

namespace {

constexpr int kPanelPadding = 16;
constexpr int kItemGap = 8;

bool parse_bool_value(const nlohmann::json& value, bool fallback) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        return value.get<int>() != 0;
    }
    if (value.is_number_float()) {
        return value.get<double>() != 0.0;
    }
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (text == "true" || text == "1" || text == "yes" || text == "on") {
            return true;
        }
        if (text == "false" || text == "0" || text == "no" || text == "off") {
            return false;
        }
    }
    return fallback;
}

bool parse_bool_field(const nlohmann::json& payload, const char* key, bool fallback) {
    if (!payload.is_object()) {
        return fallback;
    }
    if (!payload.contains(key)) {
        return fallback;
    }
    return parse_bool_value(payload.at(key), fallback);
}

int parse_int_value(const nlohmann::json& value, int fallback) {
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_number()) {
        return static_cast<int>(value.get<double>());
    }
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

int parse_int_field(const nlohmann::json& payload, const char* key, int fallback) {
    if (!payload.is_object()) {
        return fallback;
    }
    if (!payload.contains(key)) {
        return fallback;
    }
    return parse_int_value(payload.at(key), fallback);
}

}  // namespace

namespace animation_editor {

PlaybackSettingsPanel::PlaybackSettingsPanel() {
    ensure_widgets();
}

void PlaybackSettingsPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    sync_from_document();
}

void PlaybackSettingsPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    sync_from_document();
}

void PlaybackSettingsPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
}

void PlaybackSettingsPanel::update() {
    layout_widgets();
}

void PlaybackSettingsPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    layout_widgets();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color& bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bounds_);

    const SDL_Color& border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bounds_);

    if (flip_checkbox_) flip_checkbox_->render(renderer);
    if (reverse_checkbox_) reverse_checkbox_->render(renderer);
    if (locked_checkbox_) locked_checkbox_->render(renderer);
    if (loop_checkbox_) loop_checkbox_->render(renderer);
    if (random_start_checkbox_) random_start_checkbox_->render(renderer);
    if (speed_slider_) speed_slider_->render(renderer);
}

bool PlaybackSettingsPanel::handle_event(const SDL_Event& e) {
    layout_widgets();
    bool used = false;

    auto handle_checkbox = [&](std::unique_ptr<DMCheckbox>& checkbox) {
        if (checkbox && checkbox->handle_event(e)) {
            used = true;
            handle_controls_changed();
        }
    };

    handle_checkbox(flip_checkbox_);
    handle_checkbox(reverse_checkbox_);
    handle_checkbox(locked_checkbox_);
    handle_checkbox(loop_checkbox_);
    handle_checkbox(random_start_checkbox_);

    if (speed_slider_ && speed_slider_->handle_event(e)) {
        used = true;
        handle_controls_changed();
    }

    return used;
}

void PlaybackSettingsPanel::ensure_widgets() {
    auto ensure_checkbox = [&](std::unique_ptr<DMCheckbox>& checkbox, const char* label) {
        if (!checkbox) {
            checkbox = std::make_unique<DMCheckbox>(label, false);
            layout_dirty_ = true;
        }
    };

    ensure_checkbox(flip_checkbox_, "Flip Source Horizontally");
    ensure_checkbox(reverse_checkbox_, "Play Frames In Reverse");
    ensure_checkbox(locked_checkbox_, "Lock Movement To Origin");
    ensure_checkbox(loop_checkbox_, "Loop Animation");
    ensure_checkbox(random_start_checkbox_, "Randomize Starting Frame");

    if (!speed_slider_) {
        speed_slider_ = std::make_unique<DMSlider>("Speed Factor", -20, 20, 1);
        layout_dirty_ = true;
    }
}

void PlaybackSettingsPanel::layout_widgets() const {
    if (!layout_dirty_) {
        return;
    }

    const_cast<PlaybackSettingsPanel*>(this)->ensure_widgets();

    layout_dirty_ = false;

    if (bounds_.w <= 0 || bounds_.h <= 0) {
        return;
    }

    const int padding = kPanelPadding;
    const int gap = kItemGap;
    const int width = std::max(0, bounds_.w - padding * 2);
    int x = bounds_.x + padding;
    int y = bounds_.y + padding;

    auto place_checkbox = [&](DMCheckbox* checkbox) {
        if (!checkbox) {
            return;
        }
        SDL_Rect rect{x, y, width, DMCheckbox::height()};
        checkbox->set_rect(rect);
        y += rect.h + gap;
    };

    place_checkbox(flip_checkbox_.get());
    place_checkbox(reverse_checkbox_.get());
    place_checkbox(locked_checkbox_.get());
    place_checkbox(loop_checkbox_.get());
    place_checkbox(random_start_checkbox_.get());

    if (speed_slider_) {
        int slider_height = speed_slider_->preferred_height(width);
        SDL_Rect slider_rect{x, y, width, slider_height};
        speed_slider_->set_rect(slider_rect);
        y += slider_rect.h + gap;
    }
}

void PlaybackSettingsPanel::apply_state_to_controls(const PlaybackState& state) {
    ensure_widgets();
    if (flip_checkbox_) flip_checkbox_->set_value(state.flipped_source);
    if (reverse_checkbox_) reverse_checkbox_->set_value(state.reverse_source);
    if (locked_checkbox_) locked_checkbox_->set_value(state.locked);
    if (loop_checkbox_) loop_checkbox_->set_value(state.loop);
    if (random_start_checkbox_) random_start_checkbox_->set_value(state.random_start);
    if (speed_slider_) speed_slider_->set_value(state.speed_factor);
}

PlaybackSettingsPanel::PlaybackState PlaybackSettingsPanel::read_controls() const {
    PlaybackState state = state_;
    if (flip_checkbox_) state.flipped_source = flip_checkbox_->value();
    if (reverse_checkbox_) state.reverse_source = reverse_checkbox_->value();
    if (locked_checkbox_) state.locked = locked_checkbox_->value();
    if (loop_checkbox_) state.loop = loop_checkbox_->value();
    if (random_start_checkbox_) state.random_start = random_start_checkbox_->value();

    if (speed_slider_) {
        int raw_speed = speed_slider_->value();
        raw_speed = std::clamp(raw_speed, -20, 20);
        if (raw_speed == 0) {
            if (state_.speed_factor < 0) {
                raw_speed = -1;
            } else if (state_.speed_factor > 0) {
                raw_speed = 1;
            } else {
                raw_speed = 1;
            }
        }
        state.speed_factor = raw_speed;
    }
    return state;
}

void PlaybackSettingsPanel::handle_controls_changed() {
    if (is_syncing_ui_) {
        return;
    }

    PlaybackState new_state = read_controls();
    state_ = new_state;

    if (!document_) {
        return;
    }

    if (has_document_state_ && new_state == document_state_) {
        return;
    }

    commit_changes(new_state);
}

void PlaybackSettingsPanel::sync_from_document() {
    ensure_widgets();

    PlaybackState new_state;
    bool found = false;

    if (document_ && !animation_id_.empty()) {
        if (auto payload = fetch_payload(document_.get(), animation_id_)) {
            nlohmann::json parsed = nlohmann::json::parse(*payload, nullptr, false);
            if (!parsed.is_object()) {
                parsed = nlohmann::json::object();
            }
            new_state = payload_to_state(parsed);
            found = true;
        }
    }

    state_ = new_state;
    document_state_ = new_state;
    has_document_state_ = found;

    is_syncing_ui_ = true;
    apply_state_to_controls(new_state);
    is_syncing_ui_ = false;

    layout_dirty_ = true;
}

void PlaybackSettingsPanel::commit_changes(const PlaybackState& desired_state) {
    if (!document_ || animation_id_.empty()) {
        return;
    }

    auto payload_dump = fetch_payload(document_.get(), animation_id_);
    if (!payload_dump) {
        return;
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (!payload.is_object()) {
        payload = nlohmann::json::object();
    }

    apply_state_to_payload(payload, desired_state);
    document_->replace_animation_payload(animation_id_, payload.dump());

    auto updated_dump = fetch_payload(document_.get(), animation_id_);
    if (!updated_dump) {
        return;
    }

    nlohmann::json updated = nlohmann::json::parse(*updated_dump, nullptr, false);
    if (!updated.is_object()) {
        updated = nlohmann::json::object();
    }

    PlaybackState normalized = payload_to_state(updated);
    document_state_ = normalized;
    state_ = normalized;
    has_document_state_ = true;

    is_syncing_ui_ = true;
    apply_state_to_controls(normalized);
    is_syncing_ui_ = false;
}

std::optional<std::string> PlaybackSettingsPanel::fetch_payload(const AnimationDocument* document,
                                                                const std::string& animation_id) {
    if (!document) {
        return std::nullopt;
    }
    return document->animation_payload(animation_id);
}

PlaybackSettingsPanel::PlaybackState PlaybackSettingsPanel::payload_to_state(const nlohmann::json& payload) {
    PlaybackState state;
    state.flipped_source = parse_bool_field(payload, "flipped_source", false);
    state.reverse_source = parse_bool_field(payload, "reverse_source", false);
    state.locked         = parse_bool_field(payload, "locked", false);
    state.loop           = parse_bool_field(payload, "loop", false);
    state.random_start   = parse_bool_field(payload, "rnd_start", false);

    int speed = parse_int_field(payload, "speed_factor", 1);
    speed     = std::clamp(speed, -20, 20);
    if (speed == 0) {
        speed = 1;
    }
    state.speed_factor = speed;
    return state;
}

void PlaybackSettingsPanel::apply_state_to_payload(nlohmann::json& payload, const PlaybackState& state) {
    if (!payload.is_object()) {
        payload = nlohmann::json::object();
    }
    payload["flipped_source"] = state.flipped_source;
    payload["reverse_source"] = state.reverse_source;
    payload["locked"]         = state.locked;
    payload["loop"]           = state.loop;
    payload["rnd_start"]      = state.random_start;
    payload["speed_factor"]   = state.speed_factor;
}

}  // namespace animation_editor

