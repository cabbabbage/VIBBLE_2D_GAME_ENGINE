#include "map_layer_controls_display.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "dm_icons.hpp"
#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "map_layers_common.hpp"
#include "map_layers_controller.hpp"
#include "room_search_panel.hpp"
#include "room_selector_popup.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

namespace {
constexpr int kNewButtonWidth = 160;
constexpr int kRemoveButtonWidth = 120;
constexpr int kAddChildButtonWidth = 220;
constexpr int kChildRemoveButtonWidth = 36;
constexpr const char* kChildSectionLabel = "Required child rooms";
constexpr const char* kNoChildMessage = "No required child rooms configured.";
constexpr const char* kEmptySelectionMessage = "Select a layer to configure.";
constexpr const char* kNewRoomLabel = "New Room";
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

std::string trim_copy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool equals_ignore_case(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string ensure_hashtag(std::string value) {
    value = trim_copy(value);
    if (value.empty()) {
        return {};
    }
    if (value.front() != '#') {
        value.insert(value.begin(), '#');
    }
    return value;
}

void append_tag_values(const nlohmann::json& node, std::set<std::string>& tags) {
    if (node.is_string()) {
        std::string normalized = ensure_hashtag(node.get<std::string>());
        if (!normalized.empty()) {
            tags.insert(std::move(normalized));
        }
        return;
    }
    if (node.is_array()) {
        for (const auto& value : node) {
            append_tag_values(value, tags);
        }
        return;
    }
    if (node.is_object()) {
        for (const auto& [_, value] : node.items()) {
            append_tag_values(value, tags);
        }
    }
}

void collect_room_tags(const nlohmann::json& node, std::set<std::string>& tags) {
    if (!node.is_object()) {
        if (node.is_array()) {
            for (const auto& value : node) {
                collect_room_tags(value, tags);
            }
        }
        return;
    }
    for (const auto& [key, value] : node.items()) {
        if (key == "tags" || key == "anti_tags") {
            append_tag_values(value, tags);
        } else if (value.is_object() || value.is_array()) {
            collect_room_tags(value, tags);
        }
    }
}

}  // namespace

MapLayerControlsDisplay::MapLayerControlsDisplay()
    : child_selector_(std::make_unique<RoomSelectorPopup>()) {
    room_search_panel_ = std::make_unique<RoomSearchPanel>();
    if (room_search_panel_) {
        room_search_panel_->set_embedded_mode(true);
        room_search_panel_->set_on_select([this](const RoomSearchPanel::Selection& selection) {
            if (!controller_ || selected_layer_index_ < 0) {
                return;
            }
            if (selection.value.empty()) {
                return;
            }
            if (controller_->add_candidate(selected_layer_index_, selection.value)) {
                mark_dirty();
                notify_change();
            }
        });
    }
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
    if (room_search_panel_) {
        room_search_panel_->set_embedded_mode(true);
        room_search_panel_->set_visible(has_layer_data_);
    }
}

void MapLayerControlsDisplay::detach_container() {
    if (!container_) {
        return;
    }
    container_->clear_header_navigation_button();
    container_->set_header_navigation_alignment_right(false);
    clear_container_callbacks(*container_);
    container_ = nullptr;
    if (room_search_panel_) {
        room_search_panel_->set_visible(false);
    }
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
        controller_listener_id_ = controller_->add_listener([this]() { this->mark_dirty(); });
    }
    close_child_selector();
    mark_dirty();
}

void MapLayerControlsDisplay::set_map_info(nlohmann::json* map_info) {
    if (map_info_ == map_info) {
        return;
    }
    map_info_ = map_info;
    mark_dirty();
}

void MapLayerControlsDisplay::set_selected_layer(int index) {
    if (selected_layer_index_ == index) {
        mark_dirty();
        return;
    }
    selected_layer_index_ = index;
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
        container_->set_header_navigation_button(kCloseButtonLabel, [this]() { this->handle_back_to_rooms(); });
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

    if (room_search_panel_) {
        if (has_layer_data_) {
            room_search_panel_->set_visible(true);
            int search_height = room_search_panel_->embedded_height_for_width(width);
            SDL_Rect search_rect{x, y - scroll, width, search_height};
            room_search_panel_->set_embedded_rect(search_rect);
            y += search_height + gap;
        } else {
            room_search_panel_->set_visible(false);
            room_search_panel_->set_embedded_rect(SDL_Rect{x, y - scroll, width, 0});
        }
    }

    if (has_layer_data_ && new_room_button_) {
        int new_width = std::min(width, new_room_button_->preferred_width() > 0 ? new_room_button_->preferred_width() : kNewButtonWidth);
        new_width = std::max(0, new_width);
        SDL_Rect new_rect{x, y - scroll, new_width, button_height};
        new_room_button_->set_rect(new_rect);
        y += button_height + gap;
    } else if (new_room_button_) {
        new_room_button_->set_rect(SDL_Rect{0, 0, 0, 0});
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

    if (room_search_panel_) {
        room_search_panel_->render(renderer);
    }
    if (has_layer_data_) {
        if (new_room_button_) {
            new_room_button_->render(renderer);
        }
    }

    const DMLabelStyle& style = label_style();
    for (std::size_t i = 0; i < info_lines_.size() && i < info_rects_.size(); ++i) {
        const SDL_Rect& rect = info_rects_[i];
        DrawLabelText(renderer, info_lines_[i], rect.x, rect.y, style);
    }

    const SDL_Color row_bg = DMStyles::ButtonBaseFill();
    const SDL_Color row_border = DMStyles::Border();
    for (const auto& candidate : candidates_) {
        if (candidate.background_rect.w > 0 && candidate.background_rect.h > 0) {
            SDL_SetRenderDrawColor(renderer, row_bg.r, row_bg.g, row_bg.b, row_bg.a);
            SDL_RenderFillRect(renderer, &candidate.background_rect);
            SDL_SetRenderDrawColor(renderer, row_border.r, row_border.g, row_border.b, row_border.a);
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

    if (child_selector_) {
        child_selector_->render(renderer);
    }
}

bool MapLayerControlsDisplay::handle_event(const SDL_Event& e) {
    ensure_data();

    if (room_search_panel_ && room_search_panel_->visible() && room_search_panel_->handle_event(e)) {
        return true;
    }
    if (child_selector_ && child_selector_->visible() && child_selector_->handle_event(e)) {
        return true;
    }

    if (!has_layer_data_) {
        return false;
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
        if (candidate.range_slider && candidate.range_slider->handle_event(e)) {
            if (handle_slider_change(candidate)) {
                notify_change();
            }
            return true;
        }
        if (candidate.add_child_button && candidate.add_child_button->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                open_child_selector(candidate.candidate_index);
            }
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
    if (room_search_panel_) {
        room_search_panel_->update(input);
    }
    if (child_selector_) {
        child_selector_->update(input);
    }
    if (!container_ || !container_->is_visible()) {
        if (room_search_panel_) {
            room_search_panel_->set_visible(false);
        }
        close_child_selector();
    } else if (room_search_panel_) {
        room_search_panel_->set_visible(has_layer_data_);
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
            row.range_slider->set_defer_commit_until_unfocus(false);

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
    update_room_search_entries();
    update_header_text();
}

void MapLayerControlsDisplay::rebuild_available_rooms() const {
    if (!controller_) {
        if (room_search_panel_) {
            room_search_panel_->set_entries({});
            room_search_panel_->set_excluded_values({});
        }
        return;
    }

    available_rooms_ = controller_->available_rooms();
}

void MapLayerControlsDisplay::update_room_search_entries() const {
    if (!room_search_panel_) {
        return;
    }

    std::vector<std::string> excluded;
    excluded.reserve(candidates_.size());
    for (const auto& row : candidates_) {
        if (!row.room_key.empty()) {
            excluded.push_back(row.room_key);
        }
    }

    if (!map_info_ || !map_info_->is_object()) {
        room_search_panel_->set_entries({});
        room_search_panel_->set_excluded_values(std::move(excluded));
        return;
    }

    auto rooms_it = map_info_->find("rooms_data");
    if (rooms_it == map_info_->end() || !rooms_it->is_object()) {
        room_search_panel_->set_entries({});
        room_search_panel_->set_excluded_values(std::move(excluded));
        return;
    }

    const nlohmann::json& rooms_data = *rooms_it;
    std::vector<RoomSearchPanel::Entry> entries;
    entries.reserve(available_rooms_.size());

    for (const auto& key : available_rooms_) {
        RoomSearchPanel::Entry entry;
        entry.value = key;

        const nlohmann::json* room_json = nullptr;
        auto room_it = rooms_data.find(key);
        if (room_it != rooms_data.end() && room_it->is_object()) {
            room_json = &(*room_it);
        }

        std::string display = room_display_label(key);
        std::string name;
        if (room_json) {
            name = room_json->value("name", std::string{});
            if (!name.empty()) {
                display = name;
            }
        }
        if (!name.empty() && !key.empty() && !equals_ignore_case(name, key)) {
            display = name + " (" + key + ")";
        }

        entry.label = display;
        entry.search_terms.push_back(key);
        if (!name.empty()) {
            entry.search_terms.push_back(name);
        }

        if (room_json) {
            std::set<std::string> tags;
            collect_room_tags(*room_json, tags);
            entry.tags.assign(tags.begin(), tags.end());
        }

        entries.push_back(std::move(entry));
    }

    room_search_panel_->set_entries(std::move(entries));
    room_search_panel_->set_excluded_values(std::move(excluded));
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
        container_->set_header_text(std::string("Layer Controls: ") + layer_name_);
    } else {
        container_->set_header_text("Layer Controls");
    }
}

void MapLayerControlsDisplay::open_child_selector(int candidate_index) {
    ensure_data();
    if (!child_selector_ || !container_ || candidate_index < 0) {
        return;
    }
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

void MapLayerControlsDisplay::handle_back_to_rooms() {
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
