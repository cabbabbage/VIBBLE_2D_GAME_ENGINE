#include "OnEndSelector.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <unordered_set>

#include "AnimationDocument.hpp"
#include "string_utils.hpp"
#include "PanelLayoutConstants.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"

namespace {

using animation_editor::kPanelPadding;

constexpr std::array<const char*, 3> kSpecialOnEndOptions = {"kill", "lock", "reverse"};

std::string payload_signature(const std::optional<std::string>& payload) {
    if (!payload.has_value()) {
        return {};
    }
    return *payload;
}

std::string parse_on_end(const std::optional<std::string>& payload_dump) {
    if (!payload_dump.has_value() || payload_dump->empty()) {
        return "default";
    }
    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        return "default";
    }
    if (payload.contains("on_end")) {
        const nlohmann::json& value = payload["on_end"];
        if (value.is_string()) {
            return value.get<std::string>();
        }
    }
    return "default";
}

}

namespace animation_editor {

OnEndSelector::OnEndSelector() = default;

void OnEndSelector::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    rebuild_options();
    sync_from_document();
}

void OnEndSelector::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    rebuild_options();
    sync_from_document();
}

void OnEndSelector::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
    layout_dropdown();
}

int OnEndSelector::preferred_height(int) const {
    return kPanelPadding * 2 + DMDropdown::height();
}

bool OnEndSelector::allow_out_of_bounds_pointer_events() const {
    return dropdown_ && dropdown_->expanded();
}

void OnEndSelector::update() {
    layout_dropdown();
    if (!document_ || animation_id_.empty()) {
        return;
    }

    {
        auto ids = document_->animation_ids();
        std::sort(ids.begin(), ids.end());
        std::string sig;
        sig.reserve(ids.size() * 8);
        for (const auto& id : ids) {
            if (!sig.empty()) sig.push_back('|');
            sig.append(id);
        }
        if (sig != ids_signature_) {
            ids_signature_.swap(sig);
            rebuild_options();
            sync_from_document();
        }
    }
    auto payload = document_->animation_payload(animation_id_);
    std::string signature = payload_signature(payload);
    if (signature != payload_signature_) {
        payload_signature_ = signature;
        sync_from_document();
    }
}

void OnEndSelector::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    layout_dropdown();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    if (dropdown_) {
        dropdown_->render(renderer);
    }
}

bool OnEndSelector::handle_event(const SDL_Event& e) {
    layout_dropdown();
    if (!dropdown_) {
        return false;
    }

    int before = dropdown_->selected();
    if (dropdown_->handle_event(e)) {
        if (dropdown_->selected() != before) {
            commit_selection();
        }
        return true;
    }
    return false;
}

void OnEndSelector::rebuild_options() {
    options_.clear();
    std::unordered_set<std::string> seen;
    options_.push_back("default");
    seen.insert("default");
    for (const char* value : kSpecialOnEndOptions) {
        options_.emplace_back(value);
        seen.insert(options_.back());
    }

    if (document_) {
        auto ids = document_->animation_ids();
        std::sort(ids.begin(), ids.end());
        for (const auto& id : ids) {
            if (seen.insert(id).second) {
                options_.push_back(id);
            }
        }
    }

    dropdown_.reset();
    layout_dirty_ = true;
}

void OnEndSelector::sync_from_document() {
    if (!document_ || animation_id_.empty()) {
        dropdown_.reset();
        return;
    }

    auto payload = document_->animation_payload(animation_id_);
    payload_signature_ = payload_signature(payload);

    std::string on_end = parse_on_end(payload);
    if (on_end.empty()) {
        on_end = "default";
    }
    if (strings::is_reserved_animation_name(on_end)) {
        on_end = strings::to_lower_copy(on_end);
    }

    if (std::find(options_.begin(), options_.end(), on_end) == options_.end()) {
        if (!on_end.empty()) {
            options_.push_back(on_end);
        } else {
            on_end = "default";
        }
    }

    int index = find_option_index(on_end);
    if (index < 0) {
        index = 0;
    }

    dropdown_ = std::make_unique<DMDropdown>("On End", options_, index);
    layout_dirty_ = true;
    layout_dropdown();
}

void OnEndSelector::layout_dropdown() const {
    if (!dropdown_ || !layout_dirty_) {
        return;
    }

    auto* self = const_cast<OnEndSelector*>(this);
    self->layout_dirty_ = false;

    const int width = std::max(0, bounds_.w - kPanelPadding * 2);
    SDL_Rect rect{bounds_.x + kPanelPadding, bounds_.y + kPanelPadding, width,
                  std::max(0, bounds_.h - kPanelPadding * 2)};
    dropdown_->set_rect(rect);
}

void OnEndSelector::commit_selection() {
    if (!document_ || animation_id_.empty() || !dropdown_) {
        return;
    }

    if (options_.empty()) {
        options_.push_back("default");
    }

    int index = dropdown_->selected();
    if (index < 0 || index >= static_cast<int>(options_.size())) {
        index = 0;
    }
    std::string selected = options_[index];
    if (selected.empty()) {
        selected = "default";
    }

    nlohmann::json payload = nlohmann::json::object();
    auto payload_dump = document_->animation_payload(animation_id_);
    if (payload_dump && !payload_dump->empty()) {
        nlohmann::json parsed = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            payload = std::move(parsed);
        }
    }
    payload["on_end"] = selected;

    std::string updated = payload.dump();
    document_->replace_animation_payload(animation_id_, updated);
    payload_signature_ = std::move(updated);
}

int OnEndSelector::find_option_index(const std::string& value) const {
    for (size_t i = 0; i < options_.size(); ++i) {
        if (options_[i] == value) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}

