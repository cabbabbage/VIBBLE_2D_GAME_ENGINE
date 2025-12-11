#include "PlaybackSettingsPanel.hpp"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

#include "AnimationDocument.hpp"

#include <nlohmann/json.hpp>

#include "PanelLayoutConstants.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/widgets.hpp"
#include "string_utils.hpp"

namespace {

constexpr int kItemGap = 8;

using animation_editor::kPanelPadding;
using animation_editor::strings::trim_copy;

int message_block_height(const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return 0;
    }
    const DMLabelStyle& style = DMStyles::Label();
    const int line_height = style.font_size + DMSpacing::small_gap();
    return static_cast<int>(lines.size()) * line_height;
}

void render_message_lines(SDL_Renderer* renderer, const SDL_Rect& rect, const std::vector<std::string>& lines) {
    if (!renderer || lines.empty()) {
        return;
    }
    const DMLabelStyle& style = DMStyles::Label();
    const int line_height = style.font_size + DMSpacing::small_gap();
    int y = rect.y;
    for (const auto& line : lines) {
        DMFontCache::instance().draw_text(renderer, style, line, rect.x, y);
        y += line_height;
    }
}

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

}

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

int PlaybackSettingsPanel::preferred_height(int width) const {
    const int padding = kPanelPadding;
    const int gap = kItemGap;
    const int checkbox_height = DMCheckbox::height();

    int height = padding;
    auto add_checkbox_group = [&](int count) {
        if (count <= 0) {
            return;
        }
        for (int i = 0; i < count; ++i) {
            height += checkbox_height;
            if (i + 1 < count) {
                height += gap;
            }
        }
};

    if (derived_from_animation_) {

        int count = 4;
        if (state_.inherit_source_movement) count += 2;
        add_checkbox_group(count);
    } else {
        add_checkbox_group(2);
    }

    if (derived_from_animation_) {
        if (!inherited_message_lines_.empty()) {
            height += gap;
            height += message_block_height(inherited_message_lines_);
        }
    } else {
        if (random_start_visible()) {
            height += gap;
            height += checkbox_height;
        }
    }

    height += padding;
    return height;
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
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    auto render_checkbox = [&](const std::unique_ptr<DMCheckbox>& checkbox, bool visible) {
        if (visible && checkbox) {
            checkbox->render(renderer);
        }
};

    const bool show_flip_controls = derived_from_animation_;
    render_checkbox(flip_checkbox_, show_flip_controls);
    render_checkbox(flip_vertical_checkbox_, show_flip_controls);

    render_checkbox(inherit_movement_checkbox_, derived_from_animation_);
    const bool show_movement_flip = derived_from_animation_ && inherit_movement_checkbox_ && inherit_movement_checkbox_->value();
    render_checkbox(flip_movement_horizontal_checkbox_, show_movement_flip);
    render_checkbox(flip_movement_vertical_checkbox_, show_movement_flip);

    render_checkbox(reverse_checkbox_, derived_from_animation_);
    render_checkbox(locked_checkbox_, !derived_from_animation_);
    if (!derived_from_animation_ && random_start_checkbox_ && (!locked_checkbox_ || !locked_checkbox_->value())) {
        random_start_checkbox_->render(renderer);
    }

    DMWidgetTooltipRender(renderer, bounds_, info_tooltip_);
}

bool PlaybackSettingsPanel::handle_event(const SDL_Event& e) {
    layout_widgets();
    bool used = false;

    if (DMWidgetTooltipHandleEvent(e, bounds_, info_tooltip_)) {
        return true;
    }

    auto handle_checkbox = [&](std::unique_ptr<DMCheckbox>& checkbox) {
        if (checkbox && checkbox->handle_event(e)) {
            used = true;
            handle_controls_changed();
        }
};

    auto handle_checkbox_if_visible = [&](std::unique_ptr<DMCheckbox>& checkbox, bool visible) {
        if (visible) {
            handle_checkbox(checkbox);
        }
};

    const bool show_flip = derived_from_animation_;
    handle_checkbox_if_visible(flip_checkbox_, show_flip);
    handle_checkbox_if_visible(flip_vertical_checkbox_, show_flip);

    handle_checkbox_if_visible(inherit_movement_checkbox_, derived_from_animation_);
    const bool show_movement_flip = derived_from_animation_ && inherit_movement_checkbox_ && inherit_movement_checkbox_->value();
    handle_checkbox_if_visible(flip_movement_horizontal_checkbox_, show_movement_flip);
    handle_checkbox_if_visible(flip_movement_vertical_checkbox_, show_movement_flip);

    handle_checkbox_if_visible(reverse_checkbox_, derived_from_animation_);
    if (!derived_from_animation_) handle_checkbox(locked_checkbox_);
    if (!derived_from_animation_ && (!locked_checkbox_ || !locked_checkbox_->value())) {
        handle_checkbox(random_start_checkbox_);
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
    ensure_checkbox(flip_vertical_checkbox_, "Flip Source Vertically");
    ensure_checkbox(inherit_movement_checkbox_, "Inherit Source Movement");
    ensure_checkbox(flip_movement_horizontal_checkbox_, "Flip Movement Horizontally");
    ensure_checkbox(flip_movement_vertical_checkbox_, "Flip Movement Vertically");
    ensure_checkbox(reverse_checkbox_, "Play Frames In Reverse");
    ensure_checkbox(locked_checkbox_, "Locked (animation must finish before another can play)");
    ensure_checkbox(random_start_checkbox_, "Randomize Starting Frame");
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

    auto place_checkbox = [&](DMCheckbox* checkbox, bool visible, bool& placed_any) {
        if (!checkbox) {
            return;
        }
        if (!visible) {
            checkbox->set_rect(SDL_Rect{0, 0, 0, 0});
            return;
        }
        if (placed_any) {
            y += gap;
        }
        SDL_Rect rect{x, y, width, DMCheckbox::height()};
        checkbox->set_rect(rect);
        y += rect.h;
        placed_any = true;
};

    bool placed_any_checkbox = false;
    const bool show_flip_controls = derived_from_animation_;
    place_checkbox(flip_checkbox_.get(), show_flip_controls, placed_any_checkbox);
    place_checkbox(flip_vertical_checkbox_.get(), show_flip_controls, placed_any_checkbox);

    place_checkbox(inherit_movement_checkbox_.get(), derived_from_animation_, placed_any_checkbox);

    bool inherit_on = false;
    if (derived_from_animation_ && inherit_movement_checkbox_) {
        inherit_on = inherit_movement_checkbox_->value();
    }
    if (derived_from_animation_ && inherit_on) {
        int indent = 16;
        int sub_x = x + indent;
        int sub_width = std::max(0, width - indent);
        if (flip_movement_horizontal_checkbox_) {
            SDL_Rect rect{sub_x, y, sub_width, DMCheckbox::height()};
            flip_movement_horizontal_checkbox_->set_rect(rect);
            y += rect.h;
            placed_any_checkbox = true;
        }
        y += gap;
        if (flip_movement_vertical_checkbox_) {
            SDL_Rect rect{sub_x, y, sub_width, DMCheckbox::height()};
            flip_movement_vertical_checkbox_->set_rect(rect);
            y += rect.h;
            placed_any_checkbox = true;
        }
    } else {
        if (flip_movement_horizontal_checkbox_) flip_movement_horizontal_checkbox_->set_rect(SDL_Rect{0,0,0,0});
        if (flip_movement_vertical_checkbox_) flip_movement_vertical_checkbox_->set_rect(SDL_Rect{0,0,0,0});
    }

    place_checkbox(reverse_checkbox_.get(), derived_from_animation_, placed_any_checkbox);
    place_checkbox(locked_checkbox_.get(), !derived_from_animation_, placed_any_checkbox);

    if (derived_from_animation_) {
        if (random_start_checkbox_) {
            random_start_checkbox_->set_rect(SDL_Rect{0, 0, 0, 0});
        }
        int message_height = message_block_height(inherited_message_lines_);
        if (message_height > 0) {
            if (placed_any_checkbox) {
                y += gap;
            }
            inherited_message_rect_ = SDL_Rect{x, y, width, message_height};
            y += message_height;
        } else {
            inherited_message_rect_ = SDL_Rect{0, 0, 0, 0};
        }
    } else {
        if (random_start_visible()) {
            place_checkbox(random_start_checkbox_.get(), true, placed_any_checkbox);
        } else if (random_start_checkbox_) {
            random_start_checkbox_->set_rect(SDL_Rect{0, 0, 0, 0});
        }
        inherited_message_rect_ = SDL_Rect{0, 0, 0, 0};
    }
}

void PlaybackSettingsPanel::apply_state_to_controls(const PlaybackState& state) {
    ensure_widgets();
    if (flip_checkbox_) flip_checkbox_->set_value(state.flipped_source);
    if (flip_vertical_checkbox_) flip_vertical_checkbox_->set_value(state.flip_vertical);
    if (inherit_movement_checkbox_) inherit_movement_checkbox_->set_value(state.inherit_source_movement);
    if (flip_movement_horizontal_checkbox_)
        flip_movement_horizontal_checkbox_->set_value(state.flip_movement_horizontal);
    if (flip_movement_vertical_checkbox_)
        flip_movement_vertical_checkbox_->set_value(state.flip_movement_vertical);
    if (reverse_checkbox_) reverse_checkbox_->set_value(state.reverse_source);
    if (locked_checkbox_) locked_checkbox_->set_value(state.locked);
    if (random_start_checkbox_) {
        if (!random_start_visible_for_state(state) && random_start_checkbox_->value()) {
            random_start_checkbox_->set_value(false);
        }
        if (random_start_visible_for_state(state)) {
            random_start_checkbox_->set_value(state.random_start);
        } else {
            random_start_checkbox_->set_value(false);
        }
    }
}

PlaybackSettingsPanel::PlaybackState PlaybackSettingsPanel::read_controls() const {
    PlaybackState state = state_;
    if (derived_from_animation_) {
        if (flip_checkbox_) state.flipped_source = flip_checkbox_->value();
        if (flip_vertical_checkbox_) state.flip_vertical = flip_vertical_checkbox_->value();
        if (inherit_movement_checkbox_) state.inherit_source_movement = inherit_movement_checkbox_->value();
        if (state.inherit_source_movement) {
            if (flip_movement_horizontal_checkbox_)
                state.flip_movement_horizontal = flip_movement_horizontal_checkbox_->value();
            if (flip_movement_vertical_checkbox_)
                state.flip_movement_vertical = flip_movement_vertical_checkbox_->value();
        } else {
            state.flip_movement_horizontal = false;
            state.flip_movement_vertical = false;
        }
    }

    if (derived_from_animation_) {
        if (reverse_checkbox_) state.reverse_source = reverse_checkbox_->value();
    } else {
        state.reverse_source = false;
    }
    if (!derived_from_animation_) {
        if (locked_checkbox_) state.locked = locked_checkbox_->value();
    }
    if (!derived_from_animation_) {

        if (flip_checkbox_) state.flipped_source = state_.flipped_source;
        state.flip_vertical = false;
        state.flip_movement_horizontal = false;
        state.flip_movement_vertical = false;
    }
    if (!derived_from_animation_ && random_start_checkbox_ && (!locked_checkbox_ || !locked_checkbox_->value())) {
        state.random_start = random_start_checkbox_->value();
    } else {
        state.random_start = false;
    }

    if (state.locked) {
        state.random_start = false;
    }
    return state;
}

void PlaybackSettingsPanel::handle_controls_changed() {
    if (is_syncing_ui_) {
        return;
    }

    const bool previous_visibility = random_start_visible();
    const bool previous_inherit = state_.inherit_source_movement;
    PlaybackState new_state = read_controls();
    const bool new_visibility = random_start_visible_for_state(new_state);

    if (!new_visibility && random_start_checkbox_ && random_start_checkbox_->value()) {
        is_syncing_ui_ = true;
        random_start_checkbox_->set_value(false);
        is_syncing_ui_ = false;
        new_state.random_start = false;
    }

    state_ = new_state;

    if (previous_visibility != new_visibility || previous_inherit != new_state.inherit_source_movement) {
        layout_dirty_ = true;
    }

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
    nlohmann::json parsed_payload = nlohmann::json::object();

    if (document_ && !animation_id_.empty()) {
        if (auto payload = fetch_payload(document_.get(), animation_id_)) {
            parsed_payload = nlohmann::json::parse(*payload, nullptr, false);
            if (!parsed_payload.is_object()) {
                parsed_payload = nlohmann::json::object();
            }
            new_state = payload_to_state(parsed_payload);
            found = true;
        }
    }

    if (!found) {
        parsed_payload = nlohmann::json::object();
    }

    update_inherited_state(parsed_payload);

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

    update_inherited_state(updated);

    PlaybackState normalized = payload_to_state(updated);
    document_state_ = normalized;
    const bool previous_visibility = random_start_visible();
    state_ = normalized;
    has_document_state_ = true;

    is_syncing_ui_ = true;
    apply_state_to_controls(normalized);
    is_syncing_ui_ = false;

    if (previous_visibility != random_start_visible()) {
        layout_dirty_ = true;
    }
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
    state.flip_vertical = false;
    state.flip_movement_horizontal = false;
    state.flip_movement_vertical = false;
    state.inherit_source_movement = true;
    state.locked         = parse_bool_field(payload, "locked", false);
    state.random_start   = parse_bool_field(payload, "rnd_start", false);
    if (state.locked) {
        state.random_start = false;
    }

    bool source_is_animation = false;
    if (payload.contains("source") && payload["source"].is_object()) {
        const nlohmann::json& source = payload["source"];
        std::string kind = source.value("kind", std::string{});
        if (kind == "animation") {
            source_is_animation = true;
            state.inherit_source_movement = parse_bool_field(payload, "inherit_source_movement", true);
            if (payload.contains("derived_modifiers") && payload["derived_modifiers"].is_object()) {
                const auto& modifiers = payload["derived_modifiers"];
                state.reverse_source = parse_bool_field(modifiers, "reverse", state.reverse_source);
                state.flipped_source = parse_bool_field(modifiers, "flipX", state.flipped_source);
                state.flip_vertical = parse_bool_field(modifiers, "flipY", false);
                if (state.inherit_source_movement) {
                    state.flip_movement_horizontal = parse_bool_field(modifiers, "flipMovementX", false);
                    state.flip_movement_vertical = parse_bool_field(modifiers, "flipMovementY", false);
                } else {
                    state.flip_movement_horizontal = false;
                    state.flip_movement_vertical = false;
                }
            }
        }
    }

    if (!source_is_animation) {
        state.reverse_source = false;
    }

    return state;
}

void PlaybackSettingsPanel::apply_state_to_payload(nlohmann::json& payload, const PlaybackState& state) {
    if (!payload.is_object()) {
        payload = nlohmann::json::object();
    }
    payload["flipped_source"] = state.flipped_source;
    payload["reverse_source"] = state.reverse_source;
    if (!derived_from_animation_) {
        payload["locked"] = state.locked;
    } else {
        payload.erase("locked");
    }
    if (derived_from_animation_) {
        payload.erase("rnd_start");
        payload.erase("speed_factor");
        payload.erase("fps");
        payload["inherit_source_movement"] = state.inherit_source_movement;
        nlohmann::json modifiers = nlohmann::json::object();
        modifiers["reverse"] = state.reverse_source;
        modifiers["flipX"] = state.flipped_source;
        modifiers["flipY"] = state.flip_vertical;
        if (state.inherit_source_movement) {
            modifiers["flipMovementX"] = state.flip_movement_horizontal;
            modifiers["flipMovementY"] = state.flip_movement_vertical;
        } else {
            modifiers.erase("flipMovementX");
            modifiers.erase("flipMovementY");
        }
        payload["derived_modifiers"] = std::move(modifiers);
    } else {
        payload["rnd_start"]    = state.random_start && !state.locked;
        payload.erase("derived_modifiers");
        payload.erase("inherit_source_movement");
        payload.erase("fps");
        payload.erase("speed_factor");
    }
}

void PlaybackSettingsPanel::update_inherited_state(const nlohmann::json& payload) {
    bool previous_flag = derived_from_animation_;
    std::string previous_source = derived_source_id_;

    derived_from_animation_ = false;
    derived_source_id_.clear();
    inherited_modifiers_.clear();

    if (payload.is_object() && payload.contains("source") && payload["source"].is_object()) {
        const nlohmann::json& source = payload["source"];
        std::string kind = source.value("kind", std::string{});
        if (kind == "animation") {
            derived_from_animation_ = true;
            if (source.contains("name") && source["name"].is_string()) {
                derived_source_id_ = trim_copy(source["name"].get<std::string>());
            }
            if (derived_source_id_.empty()) {
                derived_source_id_ = trim_copy(source.value("path", std::string{}));
            }
            bool reverse = parse_bool_field(payload, "reverse_source", false);
            bool flip_x = parse_bool_field(payload, "flipped_source", false);
            bool flip_y = false;
            bool flip_movement_x = false;
            bool flip_movement_y = false;
            if (payload.contains("derived_modifiers") && payload["derived_modifiers"].is_object()) {
                const auto& modifiers = payload["derived_modifiers"];
                reverse = parse_bool_field(modifiers, "reverse", reverse);
                flip_x = parse_bool_field(modifiers, "flipX", flip_x);
                flip_y = parse_bool_field(modifiers, "flipY", false);
                flip_movement_x = parse_bool_field(modifiers, "flipMovementX", false);
                flip_movement_y = parse_bool_field(modifiers, "flipMovementY", false);
            }
            if (reverse) inherited_modifiers_.push_back("Reverse");
            if (flip_x) inherited_modifiers_.push_back("Flip X");
            if (flip_y) inherited_modifiers_.push_back("Flip Y");
            if (flip_movement_x) inherited_modifiers_.push_back("Flip Movement X");
            if (flip_movement_y) inherited_modifiers_.push_back("Flip Movement Y");
        }
    }

    refresh_inherited_message();

    if (previous_flag != derived_from_animation_ || previous_source != derived_source_id_) {
        layout_dirty_ = true;
    }
}

bool PlaybackSettingsPanel::random_start_visible_for_state(const PlaybackState& state) const {
    return !derived_from_animation_ && !state.locked;
}

void PlaybackSettingsPanel::refresh_inherited_message() {
    std::vector<std::string> previous_lines = inherited_message_lines_;
    inherited_message_lines_.clear();
    inherited_message_rect_ = SDL_Rect{0, 0, 0, 0};

    if (derived_from_animation_) {
        std::string target = derived_source_id_.empty() ? std::string("the source animation") : "animation '" + derived_source_id_ + "'";
        inherited_message_lines_.push_back("Lock and starting frame inherit from " + target + ".");
        if (!inherited_modifiers_.empty()) {
            std::string joined;
            for (size_t i = 0; i < inherited_modifiers_.size(); ++i) {
                if (i > 0) joined.append(", ");
                joined.append(inherited_modifiers_[i]);
            }
            inherited_message_lines_.push_back("Applied modifiers: " + joined + ".");
        }
        inherited_message_lines_.push_back("Edit the source animation to change them.");
    }

    if (inherited_message_lines_ != previous_lines) {
        layout_dirty_ = true;
    }

    if (derived_from_animation_) {
        std::string tip;
        for (size_t i = 0; i < inherited_message_lines_.size(); ++i) {
            if (i > 0) tip.append(" ");
            tip.append(inherited_message_lines_[i]);
        }
        info_tooltip_.text = std::move(tip);
        info_tooltip_.enabled = !info_tooltip_.text.empty();
        DMWidgetTooltipResetHover(info_tooltip_);
    } else {
        info_tooltip_.enabled = false;
        info_tooltip_.text.clear();
        DMWidgetTooltipResetHover(info_tooltip_);
    }
}

}

