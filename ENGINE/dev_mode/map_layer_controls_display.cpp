#include "map_layer_controls_display.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "map_layers_common.hpp"
#include "map_layers_controller.hpp"
#include "room_selector_popup.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

namespace {
constexpr int kAddButtonWidth = 180;
constexpr int kRemoveButtonWidth = 120;

const DMLabelStyle& label_style() {
    return DMStyles::Label();
}

SDL_Point measure_label(const std::string& text) {
    return MeasureLabelText(label_style(), text);
}

}  // namespace

MapLayerControlsDisplay::MapLayerControlsDisplay()
    : room_selector_(std::make_unique<RoomSelectorPopup>()) {
    add_room_button_ = std::make_unique<DMButton>("Add Room", &DMStyles::CreateButton(), kAddButtonWidth, DMButton::height());
}

MapLayerControlsDisplay::~MapLayerControlsDisplay() {
    detach_container();
    if (controller_ && controller_listener_id_ != 0) {
        controller_->remove_listener(controller_listener_id_);
    }
}

void MapLayerControlsDisplay::attach_container(SlidingWindowContainer* container) {
    if (container_ == container) {
        return;
    }
    if (container_) {
        clear_container_callbacks(*container_);
    }
    container_ = container;
    if (container_) {
        configure_container(*container_);
        container_->set_header_text("Layer Controls");
        container_->set_header_visible(true);
        container_->set_scrollbar_visible(true);
        container_->set_close_button_enabled(false);
        container_->set_blocks_editor_interactions(true);
        container_->request_layout();
    }
}

void MapLayerControlsDisplay::detach_container() {
    if (!container_) {
        return;
    }
    clear_container_callbacks(*container_);
    container_ = nullptr;
}

void MapLayerControlsDisplay::set_controller(std::shared_ptr<MapLayersController> controller) {
    if (controller_ == controller) {
        return;
    }
    if (controller_ && controller_listener_id_ != 0) {
        controller_->remove_listener(controller_listener_id_);
        controller_listener_id_ = 0;
    }
    controller_ = std::move(controller);
    if (controller_) {
        controller_listener_id_ = controller_->add_listener([this]() {
            this->mark_dirty();
        });
    }
    mark_dirty();
}

void MapLayerControlsDisplay::set_selected_layer(int index) {
    if (selected_layer_index_ == index) {
        mark_dirty();
        return;
    }
    selected_layer_index_ = index;
    close_room_selector();
    mark_dirty();
}

void MapLayerControlsDisplay::refresh() {
    mark_dirty();
}

void MapLayerControlsDisplay::set_on_change(std::function<void()> cb) {
    on_change_ = std::move(cb);
}

void MapLayerControlsDisplay::configure_container(SlidingWindowContainer& container) {
    container.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        return this->layout_content(ctx);
    });
    container.set_render_function([this](SDL_Renderer* renderer) { this->render(renderer); });
    container.set_event_function([this](const SDL_Event& e) { return this->handle_event(e); });
    container.set_update_function([this](const Input& input, int screen_w, int screen_h) {
        this->update(input, screen_w, screen_h);
    });
}

void MapLayerControlsDisplay::clear_container_callbacks(SlidingWindowContainer& container) {
    container.set_layout_function({});
    container.set_render_function({});
    container.set_event_function({});
    container.set_update_function({});
    container.set_blocks_editor_interactions(false);
}

int MapLayerControlsDisplay::layout_content(const SlidingWindowContainer::LayoutContext& ctx) {
    ensure_data();

    const int gap = ctx.gap > 0 ? ctx.gap : DMSpacing::item_gap();
    const int small_gap = DMSpacing::small_gap();
    int y = ctx.content_top;
    const int x = ctx.content_x;
    const int width = ctx.content_width;
    const int scroll = ctx.scroll_value;

    if (has_layer_data_ && add_room_button_) {
        SDL_Rect add_rect{x, y - scroll, std::min(width, kAddButtonWidth), DMButton::height()};
        add_room_button_->set_rect(add_rect);
        y += add_rect.h + gap;
    } else if (add_room_button_) {
        add_room_button_->set_rect(SDL_Rect{0, 0, 0, 0});
    }

    summary_rects_.clear();
    if (has_layer_data_ && !summary_lines_.empty()) {
        summary_rects_.reserve(summary_lines_.size());
        for (const auto& line : summary_lines_) {
            SDL_Point size = measure_label(line);
            SDL_Rect rect{x, y - scroll, width, size.y};
            summary_rects_.push_back(rect);
            y += size.y + small_gap;
        }
        if (!summary_rects_.empty()) {
            y += gap - small_gap;
        }
    }

    const int label_available_width = std::max(0, width - (has_layer_data_ ? std::min(width, kRemoveButtonWidth) + small_gap : 0));
    for (auto& candidate : candidates_) {
        if (candidate.remove_button) {
            SDL_Rect remove_rect{x + std::max(0, width - kRemoveButtonWidth), y - scroll,
                                 std::min(width, kRemoveButtonWidth), DMButton::height()};
            candidate.remove_button->set_rect(remove_rect);
            candidate.remove_rect = remove_rect;
        }
        candidate.label_rect = SDL_Rect{x, y - scroll, label_available_width, DMButton::height()};
        y += DMButton::height() + small_gap;
        if (candidate.range_slider) {
            SDL_Rect slider_rect{x, y - scroll, width, DMRangeSlider::height()};
            candidate.range_slider->set_rect(slider_rect);
            y += DMRangeSlider::height();
        }
        y += gap;
    }

    if (!empty_state_message_.empty()) {
        SDL_Point size = measure_label(empty_state_message_);
        empty_state_rect_ = SDL_Rect{x, y - scroll, width, size.y};
        y += size.y + gap;
    } else {
        empty_state_rect_ = SDL_Rect{0, 0, 0, 0};
    }

    return y;
}

void MapLayerControlsDisplay::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    ensure_data();

    if (has_layer_data_ && add_room_button_) {
        add_room_button_->render(renderer);
    }

    const DMLabelStyle& style = label_style();
    for (std::size_t i = 0; i < summary_lines_.size() && i < summary_rects_.size(); ++i) {
        const SDL_Rect& rect = summary_rects_[i];
        DrawLabelText(renderer, summary_lines_[i], rect.x, rect.y, style);
    }

    for (const auto& candidate : candidates_) {
        SDL_Point text_size = measure_label(candidate.room_name);
        int text_y = candidate.label_rect.y + std::max(0, (candidate.label_rect.h - text_size.y) / 2);
        DrawLabelText(renderer, candidate.room_name, candidate.label_rect.x, text_y, style);
        if (candidate.remove_button) {
            candidate.remove_button->render(renderer);
        }
        if (candidate.range_slider) {
            candidate.range_slider->render(renderer);
        }
    }

    if (!empty_state_message_.empty() && empty_state_rect_.w > 0 && empty_state_rect_.h > 0) {
        DrawLabelText(renderer, empty_state_message_, empty_state_rect_.x, empty_state_rect_.y, style);
    }

    if (room_selector_) {
        room_selector_->render(renderer);
    }
}

bool MapLayerControlsDisplay::handle_event(const SDL_Event& e) {
    ensure_data();

    if (room_selector_ && room_selector_->visible() && room_selector_->handle_event(e)) {
        return true;
    }

    bool consumed = false;
    if (!has_layer_data_) {
        return consumed;
    }

    if (add_room_button_ && add_room_button_->handle_event(e)) {
        open_room_selector();
        return true;
    }

    for (auto& candidate : candidates_) {
        if (candidate.remove_button && candidate.remove_button->handle_event(e)) {
            if (controller_ && selected_layer_index_ >= 0) {
                if (controller_->remove_candidate(selected_layer_index_, candidate.candidate_index)) {
                    mark_dirty();
                    notify_change();
                }
            }
            consumed = true;
            break;
        }
        if (candidate.range_slider && candidate.range_slider->handle_event(e)) {
            if (handle_slider_change(candidate)) {
                notify_change();
                consumed = true;
            }
            // continue processing other candidates if necessary but event already consumed
            consumed = true;
            break;
        }
    }

    return consumed;
}

void MapLayerControlsDisplay::update(const Input& input, int, int) {
    ensure_data();
    if (room_selector_) {
        room_selector_->update(input);
    }
    if (!container_ || !container_->is_visible()) {
        close_room_selector();
    }
}

void MapLayerControlsDisplay::ensure_data() const {
    if (!data_dirty_) {
        return;
    }
    rebuild_rows();
    data_dirty_ = false;
}

void MapLayerControlsDisplay::rebuild_rows() const {
    candidates_.clear();
    summary_lines_.clear();
    summary_rects_.clear();
    available_rooms_.clear();
    filtered_rooms_.clear();
    layer_name_.clear();
    layer_min_rooms_ = 0;
    layer_max_rooms_ = 0;
    empty_state_message_.clear();
    has_layer_data_ = false;

    if (!controller_ || selected_layer_index_ < 0) {
        empty_state_message_ = "Select a layer to configure.";
        update_header_text();
        return;
    }

    const nlohmann::json* layer = controller_->layer(selected_layer_index_);
    if (!layer || !layer->is_object()) {
        empty_state_message_ = "Select a layer to configure.";
        update_header_text();
        return;
    }

    has_layer_data_ = true;
    layer_name_ = layer->value("name", std::string{});
    if (layer_name_.empty()) {
        layer_name_ = std::string("Layer ") + std::to_string(selected_layer_index_);
    }
    layer_min_rooms_ = layer->value("min_rooms", 0);
    layer_max_rooms_ = layer->value("max_rooms", 0);
    summary_lines_.push_back("Minimum rooms: " + std::to_string(layer_min_rooms_));
    summary_lines_.push_back("Maximum rooms: " + std::to_string(layer_max_rooms_));

    const auto rooms_it = layer->find("rooms");
    if (rooms_it != layer->end() && rooms_it->is_array()) {
        candidates_.reserve(rooms_it->size());
        for (std::size_t i = 0; i < rooms_it->size(); ++i) {
            const nlohmann::json& entry = (*rooms_it)[i];
            if (!entry.is_object()) {
                continue;
            }
            CandidateRow row;
            row.candidate_index = static_cast<int>(i);
            row.room_name = entry.value("name", std::string{});
            row.min_instances = entry.value("min_instances", 0);
            row.max_instances = entry.value("max_instances", 0);
            row.remove_button = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(),
                                                           kRemoveButtonWidth, DMButton::height());
            row.range_slider = std::make_unique<DMRangeSlider>(0,
                                                               map_layers::kCandidateRangeMax,
                                                               row.min_instances,
                                                               row.max_instances);
            row.range_slider->set_defer_commit_until_unfocus(false);
            candidates_.push_back(std::move(row));
        }
    }

    if (candidates_.empty()) {
        empty_state_message_ = "No rooms assigned to this layer.";
    }

    rebuild_available_rooms();
    update_header_text();
}

void MapLayerControlsDisplay::rebuild_available_rooms() const {
    filtered_rooms_.clear();
    if (!controller_) {
        if (room_selector_) {
            room_selector_->set_rooms(filtered_rooms_);
        }
        return;
    }

    available_rooms_ = controller_->available_rooms();
    filtered_rooms_ = available_rooms_;
    if (!candidates_.empty()) {
        filtered_rooms_.erase(std::remove_if(filtered_rooms_.begin(), filtered_rooms_.end(), [this](const std::string& name) {
                                   return std::any_of(candidates_.begin(), candidates_.end(),
                                                      [&](const CandidateRow& row) { return row.room_name == name; });
                               }),
                               filtered_rooms_.end());
    }

    if (room_selector_) {
        room_selector_->set_rooms(filtered_rooms_);
    }
}

void MapLayerControlsDisplay::mark_dirty() const {
    data_dirty_ = true;
    if (container_) {
        container_->request_layout();
    }
}

void MapLayerControlsDisplay::update_header_text() const {
    if (!container_) {
        return;
    }
    if (has_layer_data_ && !layer_name_.empty()) {
        container_->set_header_text(layer_name_ + " Controls");
    } else {
        container_->set_header_text("Layer Controls");
    }
}

void MapLayerControlsDisplay::open_room_selector() {
    rebuild_available_rooms();
    if (!room_selector_) {
        return;
    }
    if (filtered_rooms_.empty()) {
        room_selector_->close();
        return;
    }
    if (container_) {
        room_selector_->set_screen_bounds(container_->panel_rect());
    }
    if (add_room_button_) {
        room_selector_->set_anchor_rect(add_room_button_->rect());
    }
    room_selector_->open(filtered_rooms_, [this](const std::string& room_key) { this->on_room_selected(room_key); });
}

void MapLayerControlsDisplay::close_room_selector() {
    if (room_selector_) {
        room_selector_->close();
    }
}

void MapLayerControlsDisplay::on_room_selected(const std::string& room_key) {
    if (!controller_ || selected_layer_index_ < 0) {
        return;
    }
    if (controller_->add_candidate(selected_layer_index_, room_key)) {
        mark_dirty();
        notify_change();
    }
}

bool MapLayerControlsDisplay::handle_slider_change(CandidateRow& row) {
    if (!controller_ || !row.range_slider || selected_layer_index_ < 0) {
        return false;
    }
    int new_min = row.range_slider->min_value();
    int new_max = row.range_slider->max_value();
    if (new_min == row.min_instances && new_max == row.max_instances) {
        return false;
    }
    row.min_instances = new_min;
    row.max_instances = new_max;
    if (controller_->set_candidate_instance_range(selected_layer_index_, row.candidate_index, new_min, new_max)) {
        mark_dirty();
        return true;
    }
    return false;
}

void MapLayerControlsDisplay::notify_change() {
    if (on_change_) {
        on_change_();
    }
}

