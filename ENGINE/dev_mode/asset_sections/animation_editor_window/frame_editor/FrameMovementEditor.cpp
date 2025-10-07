#include "FrameMovementEditor.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "../AnimationDocument.hpp"
#include "FramePropertiesPanel.hpp"
#include "MovementCanvas.hpp"
#include "TotalsPanel.hpp"

namespace animation_editor {

namespace {

constexpr int kPanelPadding = 16;
constexpr int kSidePanelWidth = 240;
constexpr int kTotalsHeight = 120;

int clamp_index(int index, int max_value) {
    if (max_value <= 0) return 0;
    return std::clamp(index, 0, max_value - 1);
}

std::vector<MovementFrame> parse_movement_frames(const nlohmann::json& payload) {
    std::vector<MovementFrame> frames;
    if (!payload.is_array()) {
        frames.push_back(MovementFrame{});
        return frames;
    }
    for (const auto& entry : payload) {
        MovementFrame frame;
        if (entry.is_array()) {
            if (!entry.empty() && entry[0].is_number()) {
                frame.dx = entry[0].get<float>();
            }
            if (entry.size() > 1 && entry[1].is_number()) {
                frame.dy = entry[1].get<float>();
            }
            if (entry.size() > 2 && entry[2].is_boolean()) {
                frame.resort_z = entry[2].get<bool>();
            }
        } else if (entry.is_object()) {
            frame.dx = entry.value("dx", 0.0f);
            frame.dy = entry.value("dy", 0.0f);
            frame.resort_z = entry.value("resort_z", false);
        }
        frames.push_back(frame);
    }
    if (frames.empty()) {
        frames.push_back(MovementFrame{});
    }
    frames.front().dx = 0.0f;
    frames.front().dy = 0.0f;
    return frames;
}

nlohmann::json serialize_frames_to_json(const std::vector<MovementFrame>& frames) {
    nlohmann::json movement = nlohmann::json::array();
    for (size_t i = 0; i < frames.size(); ++i) {
        const MovementFrame& frame = frames[i];
        int dx = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dx));
        int dy = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (frame.resort_z) {
            entry.push_back(frame.resort_z);
        }
        movement.push_back(entry);
    }
    if (movement.empty()) {
        movement.push_back(nlohmann::json::array({0, 0}));
    }
    movement[0][0] = 0;
    movement[0][1] = 0;
    return movement;
}

bool frames_equal(const std::vector<MovementFrame>& a, const std::vector<MovementFrame>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const MovementFrame& lhs = a[i];
        const MovementFrame& rhs = b[i];
        if (lhs.resort_z != rhs.resort_z) return false;
        if (std::fabs(lhs.dx - rhs.dx) > 0.001f) return false;
        if (std::fabs(lhs.dy - rhs.dy) > 0.001f) return false;
    }
    return true;
}

}  // namespace

FrameMovementEditor::FrameMovementEditor() { ensure_children(); }

void FrameMovementEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    load_frames_from_document();
}

void FrameMovementEditor::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    load_frames_from_document();
}

void FrameMovementEditor::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    update_layout();
}

void FrameMovementEditor::set_close_callback(CloseCallback callback) { close_callback_ = std::move(callback); }

void FrameMovementEditor::update() {
    ensure_children();
    if (canvas_) {
        canvas_->update();
        if (selected_index_ != canvas_->selected_index()) {
            selected_index_ = canvas_->selected_index();
            synchronize_selection();
        }
    }
    if (totals_panel_) totals_panel_->update();
    if (properties_panel_) {
        properties_panel_->update();
        if (properties_panel_->take_dirty_flag()) {
            mark_dirty();
        }
    }

    if (dirty_) {
        apply_changes();
        dirty_ = false;
    }
}

void FrameMovementEditor::render(SDL_Renderer* renderer) const {
    if (canvas_) canvas_->render(renderer);
    if (totals_panel_) totals_panel_->render(renderer);
    if (properties_panel_) properties_panel_->render(renderer);
}

bool FrameMovementEditor::handle_event(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (close_callback_) close_callback_();
        return true;
    }

    bool consumed = false;
    if (canvas_ && canvas_->handle_event(e)) {
        const auto& updated_frames = canvas_->frames();
        bool changed = !frames_equal(frames_, updated_frames);
        frames_ = updated_frames;
        selected_index_ = canvas_->selected_index();
        if (totals_panel_) totals_panel_->set_frames(frames_);
        if (properties_panel_) {
            properties_panel_->set_frames(&frames_);
            properties_panel_->refresh_from_selection();
        }
        if (changed) {
            mark_dirty();
        } else {
            synchronize_selection();
        }
        consumed = true;
    }

    if (totals_panel_ && totals_panel_->handle_event(e)) {
        synchronize_selection();
        consumed = true;
    }

    if (properties_panel_ && properties_panel_->handle_event(e)) {
        mark_dirty();
        consumed = true;
    }

    return consumed;
}

void FrameMovementEditor::load_frames_from_document() {
    ensure_children();
    frames_.clear();
    selected_index_ = 0;
    if (!document_ || animation_id_.empty()) {
        frames_.push_back(MovementFrame{});
    } else {
        auto payload_dump = document_->animation_payload(animation_id_);
        nlohmann::json payload = nlohmann::json::object();
        if (payload_dump.has_value()) {
            payload = nlohmann::json::parse(*payload_dump, nullptr, false);
            if (!payload.is_object()) {
                payload = nlohmann::json::object();
            }
        }
        nlohmann::json movement = nlohmann::json::array();
        if (payload.contains("movement")) {
            movement = payload["movement"];
        }
        frames_ = parse_movement_frames(movement);
    }

    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));

    if (canvas_) {
        canvas_->set_frames(frames_, true);
        canvas_->set_selected_index(selected_index_);
    }
    if (totals_panel_) {
        totals_panel_->set_frames(frames_);
    }
    if (properties_panel_) {
        properties_panel_->set_frames(&frames_);
        properties_panel_->refresh_from_selection();
    }
    dirty_ = false;
}

void FrameMovementEditor::apply_changes() {
    if (!document_ || animation_id_.empty()) return;

    auto payload_dump = document_->animation_payload(animation_id_);
    nlohmann::json payload = nlohmann::json::object();
    if (payload_dump.has_value()) {
        payload = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (!payload.is_object()) {
            payload = nlohmann::json::object();
        }
    }
    payload["movement"] = serialize_frames_to_json(frames_);
    document_->replace_animation_payload(animation_id_, payload.dump());
    if (totals_panel_) totals_panel_->set_frames(frames_);
}

void FrameMovementEditor::ensure_children() {
    if (!canvas_) {
        canvas_ = std::make_unique<MovementCanvas>();
    }
    if (!totals_panel_) {
        totals_panel_ = std::make_unique<TotalsPanel>();
        totals_panel_->set_selected_index(&selected_index_);
        totals_panel_->set_on_selection_changed([this](int index) {
            selected_index_ = clamp_index(index, static_cast<int>(frames_.size()));
            synchronize_selection();
        });
    } else {
        totals_panel_->set_selected_index(&selected_index_);
    }
    if (!properties_panel_) {
        properties_panel_ = std::make_unique<FramePropertiesPanel>();
        properties_panel_->set_frames(&frames_);
        properties_panel_->set_selected_index(&selected_index_);
        properties_panel_->set_on_frame_changed([this]() { mark_dirty(); });
    } else {
        properties_panel_->set_frames(&frames_);
        properties_panel_->set_selected_index(&selected_index_);
    }
    update_layout();
}

void FrameMovementEditor::update_layout() {
    if (bounds_.w <= 0 || bounds_.h <= 0) return;

    const int canvas_width = std::max(0, bounds_.w - kSidePanelWidth - kPanelPadding * 3);
    const int canvas_height = std::max(0, bounds_.h - kTotalsHeight - kPanelPadding * 3);

    SDL_Rect canvas_bounds{bounds_.x + kPanelPadding, bounds_.y + kPanelPadding, canvas_width, canvas_height};
    SDL_Rect totals_bounds{canvas_bounds.x, canvas_bounds.y + canvas_bounds.h + kPanelPadding, canvas_width, kTotalsHeight};
    SDL_Rect properties_bounds{canvas_bounds.x + canvas_width + kPanelPadding, bounds_.y + kPanelPadding,
                               kSidePanelWidth, bounds_.h - 2 * kPanelPadding};

    if (canvas_) canvas_->set_bounds(canvas_bounds);
    if (totals_panel_) totals_panel_->set_bounds(totals_bounds);
    if (properties_panel_) properties_panel_->set_bounds(properties_bounds);
}

void FrameMovementEditor::synchronize_selection() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    if (canvas_) canvas_->set_selected_index(selected_index_);
    if (properties_panel_) properties_panel_->refresh_from_selection();
}

void FrameMovementEditor::mark_dirty() {
    dirty_ = true;
    if (canvas_) {
        canvas_->set_frames(frames_, true);
        canvas_->set_selected_index(selected_index_);
    }
    if (totals_panel_) totals_panel_->set_frames(frames_);
}

}  // namespace animation_editor

