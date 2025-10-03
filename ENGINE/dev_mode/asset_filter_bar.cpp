#include "asset_filter_bar.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/full_screen_collapsible.hpp"
#include "dev_mode/widgets.hpp"
#include "room/room.hpp"

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>

AssetFilterBar::AssetFilterBar() = default;
AssetFilterBar::~AssetFilterBar() = default;

void AssetFilterBar::initialize() {
    entries_.clear();
    state_.type_filters.clear();

    FilterEntry map_entry;
    map_entry.id = "map_assets";
    map_entry.kind = FilterKind::MapAssets;
    map_entry.checkbox = std::make_unique<DMCheckbox>("Map Assets", false);
    entries_.push_back(std::move(map_entry));

    FilterEntry room_entry;
    room_entry.id = "current_room";
    room_entry.kind = FilterKind::CurrentRoom;
    room_entry.checkbox = std::make_unique<DMCheckbox>("Current Room", true);
    entries_.push_back(std::move(room_entry));

    for (const std::string& type : asset_types::all_as_strings()) {
        FilterEntry entry;
        entry.id = type;
        entry.kind = FilterKind::Type;
        const bool default_enabled =
            (type == asset_types::npc) || (type == asset_types::object);
        entry.checkbox = std::make_unique<DMCheckbox>(format_type_label(type), default_enabled);
        state_.type_filters[type] = default_enabled;
        entries_.push_back(std::move(entry));
    }

    state_.map_assets = false;
    state_.current_room = true;
    sync_state_from_ui();
    layout_dirty_ = true;
    ensure_layout();
}

void AssetFilterBar::set_state_changed_callback(StateChangedCallback cb) {
    on_state_changed_ = std::move(cb);
}

void AssetFilterBar::set_enabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    layout_dirty_ = true;
}

void AssetFilterBar::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    layout_dirty_ = true;
}

void AssetFilterBar::set_footer_panel(FullScreenCollapsible* footer) {
    footer_ = footer;
    layout_dirty_ = true;
}

void AssetFilterBar::set_map_info(nlohmann::json* map_info) {
    map_info_json_ = map_info;
    rebuild_map_spawn_ids();
    notify_state_changed();
}

void AssetFilterBar::set_current_room(Room* room) {
    current_room_ = room;
    rebuild_room_spawn_ids();
    notify_state_changed();
}

void AssetFilterBar::refresh_layout() {
    layout_dirty_ = true;
    ensure_layout();
}

void AssetFilterBar::ensure_layout() {
    if (!footer_ || screen_w_ <= 0) {
        rebuild_layout(SDL_Rect{0, 0, 0, 0});
        cached_header_rect_ = SDL_Rect{0, 0, 0, 0};
        layout_dirty_ = false;
        return;
    }

    SDL_Rect header = footer_->header_rect();
    const bool header_changed =
        header.x != cached_header_rect_.x ||
        header.y != cached_header_rect_.y ||
        header.w != cached_header_rect_.w ||
        header.h != cached_header_rect_.h;

    if (!layout_dirty_ && !header_changed) {
        return;
    }

    layout_dirty_ = false;
    rebuild_layout(header);
    cached_header_rect_ = footer_->header_rect();
}

void AssetFilterBar::rebuild_layout(const SDL_Rect& header_rect) {
    layout_bounds_ = SDL_Rect{0, 0, 0, 0};

    auto clear_rects = [this]() {
        for (auto& entry : entries_) {
            if (entry.checkbox) {
                entry.checkbox->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
};

    if (!footer_ || screen_w_ <= 0) {
        clear_rects();
        return;
    }

    SDL_Rect header = header_rect;
    if (header.w <= 0 || header.h <= 0) {
        clear_rects();
        return;
    }

    const int margin_x = DMSpacing::item_gap();
    const int margin_y = DMSpacing::item_gap();
    const int row_gap = DMSpacing::small_gap();
    const int checkbox_width = 180;
    const int checkbox_height = DMCheckbox::height();

    const int available_width = std::max(0, screen_w_ - margin_x * 2);
    if (available_width <= 0) {
        clear_rects();
        return;
    }

    std::vector<std::vector<FilterEntry*>> rows(1);
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        auto& current_row = rows.back();
        int current_row_width = 0;
        if (!current_row.empty()) {
            current_row_width = static_cast<int>(current_row.size()) * checkbox_width + static_cast<int>(current_row.size() - 1) * margin_x;
        }
        int width_with_new = current_row_width + checkbox_width;
        if (!current_row.empty()) {
            width_with_new += margin_x;
        }
        if (!current_row.empty() && width_with_new > available_width) {
            rows.emplace_back();
        }
        rows.back().push_back(&entry);
    }

    if (!rows.empty() && rows.back().empty()) {
        rows.pop_back();
    }

    int row_count = 0;
    for (const auto& row : rows) {
        if (!row.empty()) {
            ++row_count;
        }
    }

    if (row_count == 0) {
        clear_rects();
        return;
    }

    const int checkbox_rows_height = row_count * checkbox_height + (row_count - 1) * row_gap;
    const int desired_header_height = margin_y + DMButton::height() + row_gap + checkbox_rows_height + margin_y;
    footer_->set_header_height(desired_header_height);

    header = footer_->header_rect();
    if (header.w <= 0 || header.h <= 0) {
        clear_rects();
        return;
    }

    int y = header.y + margin_y + DMButton::height() + row_gap;
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    const int left_limit = header.x + margin_x;
    const int right_limit = header.x + header.w - margin_x;

    for (const auto& row : rows) {
        if (row.empty()) {
            continue;
        }

        const int row_width = static_cast<int>(row.size()) * checkbox_width + static_cast<int>(row.size() - 1) * margin_x;
        int x = header.x + (header.w - row_width) / 2;
        if (row_width > (right_limit - left_limit)) {
            x = left_limit;
        } else {
            x = std::max(x, left_limit);
            if (x + row_width > right_limit) {
                x = right_limit - row_width;
            }
        }

        for (FilterEntry* entry : row) {
            if (!entry || !entry->checkbox) {
                continue;
            }

            SDL_Rect rect{x, y, checkbox_width, checkbox_height};
            entry->checkbox->set_rect(rect);

            min_x = std::min(min_x, rect.x);
            min_y = std::min(min_y, rect.y);
            max_x = std::max(max_x, rect.x + rect.w);
            max_y = std::max(max_y, rect.y + rect.h);

            x += checkbox_width + margin_x;
        }

        y += checkbox_height + row_gap;
    }

    if (max_x > min_x && max_y > min_y) {
        layout_bounds_ = SDL_Rect{min_x, min_y, max_x - min_x, max_y - min_y};
    } else {
        layout_bounds_ = SDL_Rect{0, 0, 0, 0};
    }
}

void AssetFilterBar::render(SDL_Renderer* renderer) const {
    if (!enabled_ || !renderer) {
        return;
    }
    const_cast<AssetFilterBar*>(this)->ensure_layout();
    if (layout_bounds_.w <= 0 || layout_bounds_.h <= 0) {
        return;
    }
    for (const auto& entry : entries_) {
        if (entry.checkbox) {
            entry.checkbox->render(renderer);
        }
    }
}

bool AssetFilterBar::handle_event(const SDL_Event& event) {
    if (!enabled_) {
        return false;
    }
    ensure_layout();
    if (layout_bounds_.w <= 0 || layout_bounds_.h <= 0) {
        return false;
    }
    bool used = false;
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        if (entry.checkbox->handle_event(event)) {
            used = true;
        }
    }
    if (used) {
        sync_state_from_ui();
        notify_state_changed();
    }
    return used;
}

bool AssetFilterBar::contains_point(int x, int y) const {
    if (!enabled_) {
        return false;
    }
    const_cast<AssetFilterBar*>(this)->ensure_layout();
    SDL_Point p{x, y};
    for (const auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        const SDL_Rect& rect = entry.checkbox->rect();
        if (rect.w <= 0 || rect.h <= 0) {
            continue;
        }
        if (SDL_PointInRect(&p, &rect)) {
            return true;
        }
    }
    return false;
}

void AssetFilterBar::reset() {
    for (auto& entry : entries_) {
        if (entry.checkbox) {
            entry.checkbox->set_value(true);
        }
    }
    state_.map_assets = true;
    state_.current_room = true;
    for (auto& kv : state_.type_filters) {
        kv.second = true;
    }
    sync_state_from_ui();
    notify_state_changed();
}

bool AssetFilterBar::passes(const Asset& asset) const {
    if (!enabled_) {
        return true;
    }
    if (!asset.info) {
        return true;
    }
    const std::string type = asset_types::canonicalize(asset.info->type);
    if (!type_filter_enabled(type)) {
        return false;
    }
    const bool is_map_asset = !asset.spawn_id.empty() && map_spawn_ids_.find(asset.spawn_id) != map_spawn_ids_.end();
    if (is_map_asset && !state_.map_assets) {
        return false;
    }
    const bool is_room_asset = !asset.spawn_id.empty() && room_spawn_ids_.find(asset.spawn_id) != room_spawn_ids_.end();
    if (is_room_asset && !state_.current_room) {
        return false;
    }
    return true;
}

void AssetFilterBar::rebuild_map_spawn_ids() {
    map_spawn_ids_.clear();
    if (!map_info_json_) {
        return;
    }
    try {
        auto it = map_info_json_->find("map_assets_data");
        if (it != map_info_json_->end()) {
            collect_spawn_ids(*it, map_spawn_ids_);
        }
    } catch (...) {
    }
}

void AssetFilterBar::rebuild_room_spawn_ids() {
    room_spawn_ids_.clear();
    if (!current_room_) {
        return;
    }
    try {
        nlohmann::json& data = current_room_->assets_data();
        collect_spawn_ids(data, room_spawn_ids_);
    } catch (...) {
    }
}

void AssetFilterBar::sync_state_from_ui() {
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        const bool value = entry.checkbox->value();
        switch (entry.kind) {
        case FilterKind::MapAssets:
            state_.map_assets = value;
            break;
        case FilterKind::CurrentRoom:
            state_.current_room = value;
            break;
        case FilterKind::Type:
            state_.type_filters[entry.id] = value;
            break;
        }
    }
}

void AssetFilterBar::notify_state_changed() {
    if (on_state_changed_) {
        on_state_changed_();
    }
}

bool AssetFilterBar::type_filter_enabled(const std::string& type) const {
    auto it = state_.type_filters.find(type);
    if (it == state_.type_filters.end()) {
        return true;
    }
    return it->second;
}

std::string AssetFilterBar::format_type_label(const std::string& type) const {
    if (type.empty()) {
        return std::string{};
    }
    std::string label = type;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    return label;
}

void AssetFilterBar::collect_spawn_ids(const nlohmann::json& node, std::unordered_set<std::string>& out) const {
    if (node.is_object()) {
        auto sg = node.find("spawn_groups");
        if (sg != node.end() && sg->is_array()) {
            for (const auto& entry : *sg) {
                if (!entry.is_object()) {
                    continue;
                }
                auto id_it = entry.find("spawn_id");
                if (id_it != entry.end() && id_it->is_string()) {
                    out.insert(id_it->get<std::string>());
                }
            }
        }
        for (const auto& item : node.items()) {
            if (item.key() == "spawn_groups") {
                continue;
            }
            collect_spawn_ids(item.value(), out);
        }
    } else if (node.is_array()) {
        for (const auto& element : node) {
            collect_spawn_ids(element, out);
        }
    }
}

