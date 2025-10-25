#include "map_layer_controls_display.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "dm_icons.hpp"
#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "map_layers_common.hpp"
#include "map_layers_controller.hpp"
#include "room_selector_popup.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

namespace {
constexpr int kAddButtonWidth = 180;
constexpr int kNewButtonWidth = 180;
constexpr int kRemoveButtonWidth = 48;
constexpr int kAddChildButtonWidth = 220;
constexpr int kChildRemoveButtonWidth = 36;
constexpr const char* kChildSectionLabel = "Required child rooms";
constexpr const char* kNoChildMessage = "No required child rooms configured.";
constexpr const char* kEmptySelectionMessage = "Select a layer to configure.";
constexpr const char* kNewRoomLabel = "Create Room";
constexpr const char* kCloseButtonLabel = "X";

const DMLabelStyle& label_style() {
    return DMStyles::Label();
}

SDL_Point measure_label(const std::string& text) {
    return MeasureLabelText(label_style(), text);
}

std::string room_display_label(const std::string& room_key) {
    if (room_key.empty()) {
        return "<unnamed room>";
    }
    return room_key;
}

}

MapLayerControlsDisplay::MapLayerControlsDisplay()
    : room_selector_(std::make_unique<RoomSelectorPopup>()),
      child_selector_(std::make_unique<RoomSelectorPopup>()) {
    add_room_button_ = std::make_unique<DMButton>("Add Room", &DMStyles::CreateButton(), kAddButtonWidth, DMButton::height());
    new_room_button_ = std::make_unique<DMButton>(kNewRoomLabel, &DMStyles::CreateButton(), kNewButtonWidth, DMButton::height());
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
        container_->clear_header_navigation_button();
        container_->set_header_navigation_alignment_right(false);
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
        container_->set_header_navigation_alignment_right(true);
        update_header_navigation_button();
        container_->request_layout();
    }
}

void MapLayerControlsDisplay::detach_container() {
    if (!container_) {
        return;
    }
    end_slider_dirty_suppression(nullptr);
    container_->clear_header_navigation_button();
    container_->set_header_navigation_alignment_right(false);
    clear_container_callbacks(*container_);
    container_ = nullptr;
}

void MapLayerControlsDisplay::set_controller(std::shared_ptr<MapLayersController> controller) {
    if (controller_ == controller) {
        return;
    }
    end_slider_dirty_suppression(nullptr);
    if (controller_ && controller_listener_id_ != 0) {
        controller_->remove_listener(controller_listener_id_);
        controller_listener_id_ = 0;
    }
    controller_ = std::move(controller);
    if (controller_) {
        controller_listener_id_ = controller_->add_listener([this]() { this->mark_dirty(); });
    }
    close_child_selector();
    mark_dirty();
}

void MapLayerControlsDisplay::set_selected_layer(int index) {
    if (selected_layer_index_ == index) {
        mark_dirty();
        return;
    }
    end_slider_dirty_suppression(nullptr);
    selected_layer_index_ = index;
    close_room_selector();
    close_child_selector();
    mark_dirty();
}

void MapLayerControlsDisplay::refresh() {
    mark_dirty();
}

void MapLayerControlsDisplay::set_on_change(std::function<void()> cb) {
    on_change_ = std::move(cb);
}

void MapLayerControlsDisplay::set_on_show_rooms_list(std::function<void()> cb) {
    on_show_rooms_list_ = std::move(cb);
    update_header_navigation_button();
}

void MapLayerControlsDisplay::set_on_create_room(std::function<void()> cb) {
    on_create_room_ = std::move(cb);
    mark_dirty();
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

void MapLayerControlsDisplay::update_header_navigation_button() {
    if (!container_) {
        return;
    }
    if (on_show_rooms_list_) {
        container_->set_header_navigation_button(
            kCloseButtonLabel,
            [this]() { this->handle_back_to_rooms(); },
            &DMStyles::DeleteButton());
    } else {
        container_->clear_header_navigation_button();
    }
}

int MapLayerControlsDisplay::layout_content(const SlidingWindowContainer::LayoutContext& ctx) {
    ensure_data();

    const int gap = ctx.gap > 0 ? ctx.gap : DMSpacing::item_gap();
    const int small_gap = DMSpacing::small_gap();
    const int top_spacing = DMSpacing::section_gap();
    const int button_height = DMButton::height();
    const int slider_height = DMRangeSlider::height();
    const int x = ctx.content_x;
    const int width = ctx.content_width;
    const int scroll = ctx.scroll_value;
    int y = ctx.content_top + top_spacing;

    if (has_layer_data_ && add_room_button_ && new_room_button_) {
        int add_width = std::min(width, add_room_button_->preferred_width() > 0 ? add_room_button_->preferred_width() : kAddButtonWidth);
        int new_width = std::min(width, new_room_button_->preferred_width() > 0 ? new_room_button_->preferred_width() : kNewButtonWidth);
        add_width = std::max(0, add_width);
        new_width = std::max(0, new_width);
        int row_width = add_width + small_gap + new_width;
        if (row_width > width) {
            int excess = row_width - width;
            int reduction = (excess + 1) / 2;
            add_width = std::max(0, add_width - reduction);
            new_width = std::max(0, new_width - (excess - reduction));
        }
        SDL_Rect add_rect{x, y - scroll, add_width, button_height};
        add_room_button_->set_rect(add_rect);
        SDL_Rect new_rect{x + add_width + small_gap, y - scroll, new_width, button_height};
        new_room_button_->set_rect(new_rect);
        y += button_height + gap;
    } else {
        if (add_room_button_) {
            add_room_button_->set_rect(SDL_Rect{0, 0, 0, 0});
        }
        if (new_room_button_) {
            new_room_button_->set_rect(SDL_Rect{0, 0, 0, 0});
        }
    }

    info_rects_.clear();
    if (has_layer_data_ && !info_lines_.empty()) {
        info_rects_.reserve(info_lines_.size());
        for (const auto& line : info_lines_) {
            SDL_Point size = measure_label(line);
            SDL_Rect rect{x, y - scroll, width, size.y};
            info_rects_.push_back(rect);
            y += size.y + small_gap;
        }
        if (!info_rects_.empty()) {
            y += gap;
        }
    }

    for (auto& candidate : candidates_) {
        const int remove_width = std::min(width, kRemoveButtonWidth);
        SDL_Rect row_rect{x, y - scroll, width, button_height};
        candidate.background_rect = row_rect;
        candidate.label_rect = SDL_Rect{x + small_gap, y - scroll, std::max(0, width - remove_width - small_gap * 2), button_height};
        if (candidate.remove_button) {
            SDL_Rect remove_rect{x + width - remove_width, y - scroll, remove_width, button_height};
            candidate.remove_button->set_rect(remove_rect);
        }
        y += button_height + small_gap;

        if (candidate.range_slider) {
            SDL_Rect slider_rect{x, y - scroll, width, slider_height};
            candidate.range_slider->set_rect(slider_rect);
            y += slider_height + small_gap;
        }

        bool has_children = !candidate.children.empty();
        if (has_children) {
            SDL_Point header_size = measure_label(kChildSectionLabel);
            candidate.children_header_rect = SDL_Rect{x, y - scroll, width, header_size.y};
            candidate.children_placeholder_rect = SDL_Rect{0, 0, 0, 0};
            y += header_size.y + small_gap;
        } else {
            candidate.children_header_rect = SDL_Rect{0, 0, 0, 0};
            SDL_Point placeholder_size = measure_label(kNoChildMessage);
            candidate.children_placeholder_rect = SDL_Rect{x, y - scroll, width, placeholder_size.y};
            y += placeholder_size.y + small_gap;
        }

        for (auto& child : candidate.children) {
            SDL_Rect label_rect{x + small_gap, y - scroll, std::max(0, width - kChildRemoveButtonWidth - small_gap * 3), button_height};
            child.label_rect = label_rect;
            if (child.remove_button) {
                SDL_Rect remove_rect{label_rect.x + label_rect.w + small_gap, y - scroll, kChildRemoveButtonWidth, button_height};
                child.remove_button->set_rect(remove_rect);
            }
            y += button_height + small_gap;
        }

        if (candidate.add_child_button) {
            SDL_Rect add_child_rect{x, y - scroll, std::min(width, kAddChildButtonWidth), button_height};
            candidate.add_child_button->set_rect(add_child_rect);
            y += button_height;
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

    if (has_layer_data_) {
        if (add_room_button_) {
            add_room_button_->render(renderer);
        }
        if (new_room_button_) {
            new_room_button_->render(renderer);
        }
    }

    const DMLabelStyle& style = label_style();
    for (std::size_t i = 0; i < info_lines_.size() && i < info_rects_.size(); ++i) {
        const SDL_Rect& rect = info_rects_[i];
        DrawLabelText(renderer, info_lines_[i], rect.x, rect.y, style);
    }

    const SDL_Color base_row_bg = DMStyles::PanelBG();
    const SDL_Color hover_row_bg = DMStyles::ButtonBaseFill();
    const SDL_Color active_row_bg = DMStyles::ButtonHoverFill();
    const SDL_Color row_border = DMStyles::Border();
    const SDL_Color active_border = DMStyles::HighlightColor();
    for (const auto& candidate : candidates_) {
        if (candidate.background_rect.w > 0 && candidate.background_rect.h > 0) {
            SDL_Color fill = base_row_bg;
            SDL_Color border = row_border;
            if (candidate.hovered) {
                fill = hover_row_bg;
            }
            if (candidate.slider_active) {
                fill = active_row_bg;
                border = active_border;
            }
            SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
            SDL_RenderFillRect(renderer, &candidate.background_rect);
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderDrawRect(renderer, &candidate.background_rect);
        }

        SDL_Point label_size = measure_label(candidate.display_label);
        int label_y = candidate.label_rect.y + std::max(0, (candidate.label_rect.h - label_size.y) / 2);
        DrawLabelText(renderer, candidate.display_label, candidate.label_rect.x, label_y, style);
        if (candidate.remove_button) {
            candidate.remove_button->render(renderer);
        }
        if (candidate.range_slider) {
            candidate.range_slider->render(renderer);
        }

        if (candidate.children_header_rect.w > 0 && candidate.children_header_rect.h > 0) {
            DrawLabelText(renderer, kChildSectionLabel, candidate.children_header_rect.x, candidate.children_header_rect.y, style);
        } else if (candidate.children_placeholder_rect.w > 0 && candidate.children_placeholder_rect.h > 0) {
            DrawLabelText(renderer, kNoChildMessage, candidate.children_placeholder_rect.x, candidate.children_placeholder_rect.y, style);
        }

        for (const auto& child : candidate.children) {
            SDL_Point child_size = measure_label(child.room_key);
            int child_y = child.label_rect.y + std::max(0, (child.label_rect.h - child_size.y) / 2);
            DrawLabelText(renderer, child.room_key, child.label_rect.x, child_y, style);
            if (child.remove_button) {
                child.remove_button->render(renderer);
            }
        }

        if (candidate.add_child_button) {
            candidate.add_child_button->render(renderer);
        }
    }

    if (!empty_state_message_.empty() && empty_state_rect_.w > 0 && empty_state_rect_.h > 0) {
        DrawLabelText(renderer, empty_state_message_, empty_state_rect_.x, empty_state_rect_.y, style);
    }

    if (room_selector_) {
        room_selector_->render(renderer);
    }
    if (child_selector_) {
        child_selector_->render(renderer);
    }
}

bool MapLayerControlsDisplay::handle_event(const SDL_Event& e) {
    ensure_data();

    auto update_candidate_hover = [this](SDL_Point pointer) {
        for (auto& candidate : candidates_) {
            candidate.hovered = SDL_PointInRect(&pointer, &candidate.background_rect) == SDL_TRUE;
        }
};

    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point pointer{e.motion.x, e.motion.y};
        update_candidate_hover(pointer);
    } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point pointer{e.button.x, e.button.y};
        update_candidate_hover(pointer);
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            for (auto& candidate : candidates_) {
                if (!candidate.range_slider) {
                    candidate.slider_active = false;
                    continue;
                }
                if (SDL_PointInRect(&pointer, &candidate.range_slider->rect()) != SDL_TRUE) {
                    candidate.slider_active = false;
                }
            }
        }
    }

    if (room_selector_ && room_selector_->visible() && room_selector_->handle_event(e)) {
        return true;
    }
    if (child_selector_ && child_selector_->visible() && child_selector_->handle_event(e)) {
        return true;
    }

    if (!has_layer_data_) {
        return false;
    }

    if (add_room_button_ && add_room_button_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            open_room_selector();
        }
        return true;
    }

    if (new_room_button_ && new_room_button_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            handle_create_room();
        }
        return true;
    }

    for (auto& candidate : candidates_) {
        if (candidate.remove_button && candidate.remove_button->handle_event(e)) {
            if (controller_ && selected_layer_index_ >= 0) {
                if (controller_->remove_candidate(selected_layer_index_, candidate.candidate_index)) {
                    mark_dirty();
                    notify_change();
                    close_child_selector();
                }
            }
            return true;
        }
        bool slider_handled = false;
        bool slider_changed = false;
        if (candidate.range_slider) {
            slider_handled = candidate.range_slider->handle_event(e);
            const bool mouse_down = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
            const bool mouse_up = e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT;
            if (mouse_down) {
                if (slider_handled) {
                    candidate.slider_active = true;
                    begin_slider_dirty_suppression(candidate.range_slider.get());
                } else {
                    candidate.slider_active = false;
                }
            }
            slider_changed = handle_slider_change(candidate);
            if (slider_changed) {
                notify_change();
            }
            if (mouse_up) {
                if (!slider_handled && candidate.slider_active) {
                    candidate.slider_active = false;
                }
                if (slider_handled || candidate.slider_active) {
                    end_slider_dirty_suppression(candidate.range_slider.get());
                }
                candidate.slider_active = false;
            }
        }
        if (candidate.add_child_button && candidate.add_child_button->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                open_child_selector(candidate.candidate_index);
            }
            return true;
        }
        if (slider_handled) {
            return true;
        }
        for (auto& child : candidate.children) {
            if (child.remove_button && child.remove_button->handle_event(e)) {
                if (controller_ && selected_layer_index_ >= 0) {
                    if (controller_->remove_candidate_child(selected_layer_index_, candidate.candidate_index, child.room_key)) {
                        mark_dirty();
                        notify_change();
                        close_child_selector();
                    }
                }
                return true;
            }
        }
    }

    return false;
}

void MapLayerControlsDisplay::update(const Input& input, int, int) {
    ensure_data();
    if (room_selector_) {
        room_selector_->update(input);
    }
    if (child_selector_) {
        child_selector_->update(input);
    }
    if (!container_ || !container_->is_visible()) {
        close_room_selector();
        close_child_selector();
    }
}

void MapLayerControlsDisplay::ensure_data() const {
    if (!data_dirty_) {
        return;
    }
    rebuild_content();
    data_dirty_ = false;
}

void MapLayerControlsDisplay::rebuild_content() const {
    candidates_.clear();
    info_lines_.clear();
    info_rects_.clear();
    available_rooms_.clear();
    filtered_rooms_.clear();
    layer_name_.clear();
    empty_state_message_.clear();
    has_layer_data_ = false;

    if (!controller_ || selected_layer_index_ < 0) {
        empty_state_message_ = kEmptySelectionMessage;
        update_header_text();
        return;
    }

    const nlohmann::json* layer = controller_->layer(selected_layer_index_);
    if (!layer || !layer->is_object()) {
        empty_state_message_ = kEmptySelectionMessage;
        update_header_text();
        return;
    }

    has_layer_data_ = true;
    layer_name_ = layer->value("name", std::string{});
    if (layer_name_.empty()) {
        layer_name_ = std::string("Layer ") + std::to_string(selected_layer_index_);
    }

    const int min_rooms = layer->value("min_rooms", 0);
    const int max_rooms = layer->value("max_rooms", 0);
    info_lines_.push_back("Target rooms: " + std::to_string(min_rooms) + "-" + std::to_string(max_rooms));

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
            row.room_key = entry.value("name", std::string{});
            row.display_label = room_display_label(row.room_key);
            row.min_instances = entry.value("min_instances", 0);
            row.max_instances = entry.value("max_instances", 0);
            row.remove_button = std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), kRemoveButtonWidth, DMButton::height());
            row.range_slider = std::make_unique<DMRangeSlider>(0, map_layers::kCandidateRangeMax, row.min_instances, row.max_instances);
            row.range_slider->set_defer_commit_until_unfocus(true);

            const auto required_it = entry.find("required_children");
            if (required_it != entry.end() && required_it->is_array()) {
                for (const auto& child_entry : *required_it) {
                    if (!child_entry.is_string()) {
                        continue;
                    }
                    std::string child_name = child_entry.get<std::string>();
                    if (child_name.empty()) {
                        continue;
                    }
                    CandidateRow::ChildRow child_row;
                    child_row.room_key = child_name;
                    child_row.remove_button = std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), kChildRemoveButtonWidth, DMButton::height());
                    row.children.push_back(std::move(child_row));
                }
            }

            row.add_child_button = std::make_unique<DMButton>("Add Required Child", &DMStyles::AccentButton(), kAddChildButtonWidth, DMButton::height());
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
                                   return std::any_of(candidates_.begin(), candidates_.end(), [&](const CandidateRow& row) {
                                       return row.room_key == name;
                                   });
                               }),
                               filtered_rooms_.end());
    }

    if (room_selector_) {
        room_selector_->set_rooms(filtered_rooms_);
    }
}

void MapLayerControlsDisplay::mark_dirty() const {
    if (suppress_slider_dirty_notifications_) {
        pending_slider_dirty_refresh_ = true;
        return;
    }
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
        container_->set_header_text(std::string("Layer Controls: ") + layer_name_);
    } else {
        container_->set_header_text("Layer Controls");
    }
}

void MapLayerControlsDisplay::open_room_selector() {
    if (!controller_ || selected_layer_index_ < 0) {
        return;
    }
    rebuild_available_rooms();
    if (!room_selector_ || filtered_rooms_.empty()) {
        if (room_selector_) {
            room_selector_->close();
        }
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

void MapLayerControlsDisplay::open_child_selector(int candidate_index) {
    ensure_data();
    if (!child_selector_ || !container_ || candidate_index < 0) {
        return;
    }
    rebuild_available_rooms();
    auto it = std::find_if(candidates_.begin(), candidates_.end(),
                           [candidate_index](const CandidateRow& row) { return row.candidate_index == candidate_index; });
    if (it == candidates_.end()) {
        return;
    }

    pending_child_candidate_index_ = candidate_index;
    child_selector_rooms_ = available_rooms_;
    child_selector_rooms_.erase(std::remove_if(child_selector_rooms_.begin(), child_selector_rooms_.end(),
                                               [&](const std::string& name) {
                                                   if (name.empty() || name == it->room_key) {
                                                       return true;
                                                   }
                                                   return std::any_of(it->children.begin(), it->children.end(),
                                                                      [&](const CandidateRow::ChildRow& child) {
                                                                          return child.room_key == name;
                                                                      });
                                               }),
                               child_selector_rooms_.end());

    if (child_selector_rooms_.empty()) {
        close_child_selector();
        return;
    }

    child_selector_->set_screen_bounds(container_->panel_rect());
    if (it->add_child_button) {
        child_selector_->set_anchor_rect(it->add_child_button->rect());
    } else {
        child_selector_->set_anchor_rect(container_->panel_rect());
    }
    child_selector_->open(child_selector_rooms_, [this](const std::string& room_key) { this->on_child_room_selected(room_key); });
}

void MapLayerControlsDisplay::close_child_selector() {
    pending_child_candidate_index_ = -1;
    if (child_selector_) {
        child_selector_->close();
    }
}

void MapLayerControlsDisplay::on_child_room_selected(const std::string& room_key) {
    if (!controller_ || selected_layer_index_ < 0) {
        close_child_selector();
        return;
    }
    const int candidate_index = pending_child_candidate_index_;
    pending_child_candidate_index_ = -1;
    if (candidate_index < 0) {
        close_child_selector();
        return;
    }
    if (controller_->add_candidate_child(selected_layer_index_, candidate_index, room_key)) {
        mark_dirty();
        notify_change();
    }
    close_child_selector();
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
        return true;
    }
    return false;
}

void MapLayerControlsDisplay::notify_change() {
    if (on_change_) {
        on_change_();
    }
}

void MapLayerControlsDisplay::begin_slider_dirty_suppression(const DMRangeSlider* slider) const {
    if (active_slider_dirty_owner_ != slider) {
        active_slider_dirty_owner_ = slider;
    }
    suppress_slider_dirty_notifications_ = true;
}

void MapLayerControlsDisplay::end_slider_dirty_suppression(const DMRangeSlider* slider) const {
    if (!suppress_slider_dirty_notifications_) {
        active_slider_dirty_owner_ = nullptr;
        return;
    }
    if (active_slider_dirty_owner_ && slider && active_slider_dirty_owner_ != slider) {
        return;
    }
    active_slider_dirty_owner_ = nullptr;
    suppress_slider_dirty_notifications_ = false;
    if (pending_slider_dirty_refresh_) {
        pending_slider_dirty_refresh_ = false;
        data_dirty_ = true;
        if (container_) {
            container_->request_layout();
        }
    }
}

void MapLayerControlsDisplay::handle_back_to_rooms() {
    close_room_selector();
    close_child_selector();
    if (on_show_rooms_list_) {
        on_show_rooms_list_();
    }
}

void MapLayerControlsDisplay::handle_create_room() {
    if (!on_create_room_) {
        return;
    }
    on_create_room_();
    mark_dirty();
}
