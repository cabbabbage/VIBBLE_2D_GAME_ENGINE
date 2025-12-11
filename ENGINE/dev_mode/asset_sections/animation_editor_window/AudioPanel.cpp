#include "AudioPanel.hpp"

#include "AnimationDocument.hpp"
#include "AudioImporter.hpp"
#include "PanelLayoutConstants.hpp"

#include <SDL.h>
#include <SDL_log.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "string_utils.hpp"

namespace animation_editor {

namespace {

constexpr int kItemGap = 8;

using animation_editor::strings::trim_copy;

void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y, int max_width = -1,
                  SDL_Color color = DMStyles::Label().color) {
    if (!renderer || text.empty()) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;

    std::string clipped = text;
    if (max_width > 0) {
        int w = 0;
        int h = 0;
        if (TTF_SizeUTF8(font, clipped.c_str(), &w, &h) == 0 && w > max_width) {
            const std::string ellipsis = "...";
            while (!clipped.empty()) {
                clipped.pop_back();
                std::string candidate = clipped + ellipsis;
                if (TTF_SizeUTF8(font, candidate.c_str(), &w, &h) != 0) continue;
                if (w <= max_width) {
                    clipped = std::move(candidate);
                    break;
                }
            }
        }
    }

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, clipped.c_str(), color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

int label_height() {
    const DMLabelStyle& style = DMStyles::Label();
    return style.font_size + DMSpacing::small_gap();
}

int message_block_height(const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return 0;
    }
    return static_cast<int>(lines.size()) * label_height();
}

}

AudioPanel::AudioPanel() { ensure_widgets(); }

void AudioPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    sync_from_document();
}

void AudioPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    sync_from_document();
}

void AudioPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
}

void AudioPanel::set_importer(std::shared_ptr<AudioImporter> importer) {
    importer_ = std::move(importer);
}

void AudioPanel::set_file_picker(FilePicker picker) { file_picker_ = std::move(picker); }

int AudioPanel::preferred_height(int width) const {
    const int padding = kPanelPadding;
    const int gap = kItemGap;
    const int label_h = label_height();
    const int slider_area_width = std::max(0, width - padding * 2);
    const int slider_h = volume_slider_ ? volume_slider_->preferred_height(slider_area_width) : DMSlider::height();
    int height = padding;
    height += label_h;
    if (derived_from_animation_) {
        height += message_block_height(inherited_message_lines_);
    } else if (has_audio_) {
        height += label_h;
        height += DMButton::height();
        height += gap;
        height += slider_h;
        height += gap;
        height += DMCheckbox::height();
        height += gap;
        height += DMButton::height();
    } else {
        height += label_h;
        height += DMButton::height();
    }
    height += padding;
    return height;
}

void AudioPanel::ensure_widgets() {
    if (!attach_button_) {
        attach_button_ = std::make_unique<DMButton>("Attach Audio", &DMStyles::CreateButton(), 160, DMButton::height());
    }
    if (!replace_button_) {
        replace_button_ = std::make_unique<DMButton>("Replace Audio", &DMStyles::AccentButton(), 160, DMButton::height());
    }
    if (!remove_button_) {
        remove_button_ = std::make_unique<DMButton>("Remove Audio", &DMStyles::DeleteButton(), 160, DMButton::height());
    }
    if (!preview_button_) {
        preview_button_ = std::make_unique<DMButton>("Play Preview", &DMStyles::HeaderButton(), 160, DMButton::height());
    }
    if (!volume_slider_) {
        volume_slider_ = std::make_unique<DMSlider>("Volume", 0, 100, volume_);
    }
    if (!effects_checkbox_) {
        effects_checkbox_ = std::make_unique<DMCheckbox>("Apply Audio Effects", effects_enabled_);
    }
}

void AudioPanel::layout_widgets() const {
    if (!layout_dirty_) return;
    layout_dirty_ = false;
    const int padding = kPanelPadding;
    const int gap = kItemGap;
    const int content_x = bounds_.x + padding;
    const int content_w = std::max(0, bounds_.w - padding * 2);
    int cursor_y = bounds_.y + padding + label_height() + DMSpacing::small_gap();

    if (derived_from_animation_) {
        if (attach_button_) attach_button_->set_rect(SDL_Rect{0, 0, 0, 0});
        if (preview_button_) preview_button_->set_rect(SDL_Rect{0, 0, 0, 0});
        if (replace_button_) replace_button_->set_rect(SDL_Rect{0, 0, 0, 0});
        if (remove_button_) remove_button_->set_rect(SDL_Rect{0, 0, 0, 0});
        if (volume_slider_) volume_slider_->set_rect(SDL_Rect{0, 0, 0, 0});
        if (effects_checkbox_) effects_checkbox_->set_rect(SDL_Rect{0, 0, 0, 0});
        int message_height = message_block_height(inherited_message_lines_);
        inherited_message_rect_ = SDL_Rect{content_x, cursor_y, content_w, message_height};
        return;
    }

    inherited_message_rect_ = SDL_Rect{0, 0, 0, 0};

    if (has_audio_) {
        if (preview_button_) {
            int width = std::min(content_w, preview_button_->rect().w);
            int offset = std::max(0, (content_w - width) / 2);
            SDL_Rect r{content_x + offset, cursor_y, width, DMButton::height()};
            preview_button_->set_rect(r);
            cursor_y += DMButton::height() + gap;
        }
        if (volume_slider_) {
            SDL_Rect r{content_x, cursor_y, content_w, DMSlider::height()};
            volume_slider_->set_rect(r);
            cursor_y += volume_slider_->rect().h + gap;
        }
        if (effects_checkbox_) {
            SDL_Rect r{content_x, cursor_y, content_w, DMCheckbox::height()};
            effects_checkbox_->set_rect(r);
            cursor_y += DMCheckbox::height() + gap;
        }
        if (replace_button_ && remove_button_) {
            int button_gap = DMSpacing::small_gap();
            int button_width = (content_w - button_gap) / 2;
            button_width = std::max(button_width, 120);
            button_width = std::min(button_width, replace_button_->rect().w);
            int pair_width = button_width * 2 + button_gap;
            int offset = std::max(0, (content_w - pair_width) / 2);
            SDL_Rect replace_rect{content_x + offset, cursor_y, button_width, DMButton::height()};
            SDL_Rect remove_rect{replace_rect.x + button_width + button_gap, cursor_y, button_width, DMButton::height()};
            replace_button_->set_rect(replace_rect);
            remove_button_->set_rect(remove_rect);
        }
    } else {
        if (attach_button_) {
            int width = std::min(content_w, attach_button_->rect().w);
            int offset = std::max(0, (content_w - width) / 2);
            SDL_Rect r{content_x + offset, cursor_y, width, DMButton::height()};
            attach_button_->set_rect(r);
        }
    }
}

void AudioPanel::update() {
    layout_widgets();
    if (derived_from_animation_) {
        return;
    }
    if (preview_button_ && importer_) {
        preview_button_->set_text(importer_->is_previewing() ? "Stop Preview" : "Play Preview");
    }
}

void AudioPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    layout_widgets();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    int padding = kPanelPadding;
    int max_label_width = std::max(0, bounds_.w - padding * 2);
    int label_y = bounds_.y + padding;
    render_label(renderer, "Audio", bounds_.x + padding, label_y);

    label_y += label_height();
    if (derived_from_animation_) {

        DMWidgetTooltipRender(renderer, bounds_, info_tooltip_);
    } else if (has_audio_) {
        std::string clip_text = "Clip: " + audio_name_;
        render_label(renderer, clip_text, bounds_.x + padding, label_y, max_label_width);
        label_y += label_height();
        if (preview_button_) preview_button_->render(renderer);
        if (volume_slider_) volume_slider_->render(renderer);
        if (effects_checkbox_) effects_checkbox_->render(renderer);
        if (replace_button_) replace_button_->render(renderer);
        if (remove_button_) remove_button_->render(renderer);
    } else {
        std::string none_text = "No audio attached.";
        render_label(renderer, none_text, bounds_.x + padding, label_y, max_label_width);
        if (attach_button_) attach_button_->render(renderer);
    }
}

bool AudioPanel::handle_event(const SDL_Event& e) {

    if (DMWidgetTooltipHandleEvent(e, bounds_, info_tooltip_)) {
        return true;
    }
    if (derived_from_animation_) {
        return false;
    }
    layout_widgets();

    bool consumed = false;
    auto handle_button = [&](const std::unique_ptr<DMButton>& button, void (AudioPanel::*callback)()) {
        if (button && button->handle_event(e)) {
            (this->*callback)();
            consumed = true;
        }
};

    if (!has_audio_) {
        handle_button(attach_button_, &AudioPanel::attach_audio);
    } else {
        handle_button(preview_button_, &AudioPanel::preview_audio);
        handle_button(replace_button_, &AudioPanel::replace_audio);
        handle_button(remove_button_, &AudioPanel::remove_audio);
        if (volume_slider_ && volume_slider_->handle_event(e)) {
            volume_ = volume_slider_->value();
            commit_audio_state();
            consumed = true;
        }
        if (effects_checkbox_ && effects_checkbox_->handle_event(e)) {
            effects_enabled_ = effects_checkbox_->value();
            commit_audio_state();
            consumed = true;
        }
    }

    return consumed;
}

void AudioPanel::attach_audio() {
    if (derived_from_animation_) return;
    if (!importer_ || !file_picker_) return;
    auto selection = file_picker_();
    if (!selection) return;

    std::filesystem::path dest = importer_->import_audio_file(*selection);
    if (dest.empty()) return;

    if (importer_) importer_->stop_preview();

    audio_name_ = dest.stem().string();
    volume_ = 100;
    effects_enabled_ = false;
    has_audio_ = !audio_name_.empty();
    apply_state_to_controls();
    commit_audio_state();
    layout_dirty_ = true;
}

void AudioPanel::replace_audio() {
    if (derived_from_animation_) return;
    attach_audio();
}

void AudioPanel::remove_audio() {
    if (derived_from_animation_) return;
    if (importer_) importer_->stop_preview();
    audio_name_.clear();
    has_audio_ = false;
    volume_ = 100;
    effects_enabled_ = false;
    apply_state_to_controls();
    commit_audio_state();
    layout_dirty_ = true;
}

void AudioPanel::preview_audio() {
    if (derived_from_animation_) return;
    if (!importer_ || !has_audio_) return;
    std::filesystem::path clip_path = resolve_audio_path();
    if (clip_path.empty()) return;
    if (importer_->is_previewing()) {
        importer_->stop_preview();
    } else {
        importer_->play_preview(clip_path);
    }
}

void AudioPanel::sync_from_document() {
    ensure_widgets();

    audio_name_.clear();
    has_audio_ = false;
    volume_ = 100;
    effects_enabled_ = false;

    nlohmann::json payload = nlohmann::json::object();
    if (document_ && !animation_id_.empty()) {
        auto payload_dump = document_->animation_payload(animation_id_);
        if (payload_dump && !payload_dump->empty()) {
            nlohmann::json parsed = nlohmann::json::parse(*payload_dump, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) {
                payload = std::move(parsed);
            }
        }
    }

    update_inherited_state(payload);

    if (!derived_from_animation_ && payload.contains("audio") && payload["audio"].is_object()) {
        const nlohmann::json& audio = payload["audio"];
        std::string name = audio.value("name", std::string{});
        if (!name.empty()) {
            audio_name_ = std::move(name);
            has_audio_ = true;
            volume_ = std::clamp(audio.value("volume", volume_), 0, 100);
            effects_enabled_ = audio.value("effects", effects_enabled_);
        }
    }

    apply_state_to_controls();
    layout_dirty_ = true;
}

void AudioPanel::apply_state_to_controls() {
    ensure_widgets();
    if (volume_slider_) volume_slider_->set_value(volume_);
    if (effects_checkbox_) effects_checkbox_->set_value(effects_enabled_);
}

void AudioPanel::commit_audio_state() {
    if (!document_ || animation_id_.empty() || derived_from_animation_) return;

    nlohmann::json payload = nlohmann::json::object();
    auto payload_dump = document_->animation_payload(animation_id_);
    if (payload_dump && !payload_dump->empty()) {
        nlohmann::json parsed = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            payload = std::move(parsed);
        }
    }

    if (has_audio_ && !audio_name_.empty()) {
        payload["audio"] = nlohmann::json{{"name", audio_name_}, {"volume", volume_}, {"effects", effects_enabled_}};
    } else {
        payload.erase("audio");
    }

    try {
        document_->replace_animation_payload(animation_id_, payload.dump());
    } catch (const std::exception& ex) {
        SDL_Log("AudioPanel: failed to commit audio payload: %s", ex.what());
    }
}

std::filesystem::path AudioPanel::resolve_audio_path() const {
    if (audio_name_.empty()) return {};
    if (!importer_) return {};
    std::filesystem::path relative = audio_name_;
    if (relative.has_extension()) {
        relative.replace_extension(".wav");
    } else {
        relative += ".wav";
    }
    return importer_->resolve_asset_path(relative);
}

void AudioPanel::update_inherited_state(const nlohmann::json& payload) {
    bool previous_flag = derived_from_animation_;
    std::string previous_id = inherited_source_id_;

    derived_from_animation_ = false;
    inherited_source_id_.clear();

    if (payload.is_object() && payload.contains("source") && payload["source"].is_object()) {
        const nlohmann::json& source = payload["source"];
        std::string kind = source.value("kind", std::string{});
        if (kind == "animation") {
            derived_from_animation_ = true;
            if (source.contains("name") && source["name"].is_string()) {
                inherited_source_id_ = trim_copy(source["name"].get<std::string>());
            }
            if (inherited_source_id_.empty()) {
                inherited_source_id_ = trim_copy(source.value("path", std::string{}));
            }
        }
    }

    refresh_inherited_message();

    if (previous_flag != derived_from_animation_ || previous_id != inherited_source_id_) {
        layout_dirty_ = true;
    }
}

void AudioPanel::refresh_inherited_message() {
    std::vector<std::string> previous_lines = inherited_message_lines_;
    inherited_message_lines_.clear();
    inherited_message_rect_ = SDL_Rect{0, 0, 0, 0};

    if (derived_from_animation_) {
        std::string target = inherited_source_id_.empty() ? std::string("the source animation") : "animation '" + inherited_source_id_ + "'";
        inherited_message_lines_.push_back("Audio settings inherit from " + target + ".");
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

