#include "asset_filter_bar.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "map_generation/room.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace {
constexpr int kToggleButtonMinWidth = 36;
constexpr int kPanelOutlineThickness = 1;

constexpr const char* kSettingsInitializedKey = "dev.asset_filter.initialized";
constexpr const char* kSettingsMapAssetsKey = "dev.asset_filter.map_assets";
constexpr const char* kSettingsCurrentRoomKey = "dev.asset_filter.current_room";
constexpr const char* kSettingsFiltersExpandedKey = "dev.asset_filter.filters_expanded";

std::string make_type_setting_key(const std::string& type) {
    std::string canonical = asset_types::canonicalize(type);
    std::string key = "dev.asset_filter.types.";
    key += canonical;
    return key;
}
}

AssetFilterBar::AssetFilterBar() = default;
AssetFilterBar::~AssetFilterBar() = default;

void AssetFilterBar::initialize() {
    entries_.clear();
    load_persisted_state();

    const bool use_saved_state = has_saved_state_;

    FilterEntry map_entry;
    map_entry.id = "map_assets";
    map_entry.kind = FilterKind::MapAssets;
    const bool map_assets_value = use_saved_state ? state_.map_assets : true;
    map_entry.checkbox = std::make_unique<DMCheckbox>("Map Assets", map_assets_value);
    if (!use_saved_state) {
        state_.map_assets = map_assets_value;
    }
    entries_.push_back(std::move(map_entry));

    FilterEntry room_entry;
    room_entry.id = "current_room";
    room_entry.kind = FilterKind::CurrentRoom;
    const bool current_room_value = use_saved_state ? state_.current_room : true;
    room_entry.checkbox = std::make_unique<DMCheckbox>("Current Room", current_room_value);
    if (!use_saved_state) {
        state_.current_room = current_room_value;
    }
    entries_.push_back(std::move(room_entry));

    const auto all_types = asset_types::all_as_strings();
    std::unordered_set<std::string> known_types;
    known_types.reserve(all_types.size());
    for (const std::string& type : all_types) {
        const std::string canonical = asset_types::canonicalize(type);
        FilterEntry entry;
        entry.id = canonical;
        entry.kind = FilterKind::Type;
        const bool default_enabled = default_type_enabled(canonical);
        bool checkbox_value = default_enabled;
        if (use_saved_state) {
            checkbox_value = load_type_filter_value(canonical, checkbox_value);
        }
        entry.checkbox = std::make_unique<DMCheckbox>(format_type_label(type), checkbox_value);
        state_.type_filters[canonical] = checkbox_value;
        known_types.insert(canonical);
        entries_.push_back(std::move(entry));
    }

    if (use_saved_state) {
        for (auto it = state_.type_filters.begin(); it != state_.type_filters.end();) {
            if (known_types.find(it->first) == known_types.end()) {
                it = state_.type_filters.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!use_saved_state) {
        filters_expanded_ = false;
    }
    filter_toggle_button_ = std::make_unique<DMButton>("▲", &DMStyles::HeaderButton(), std::max(DMButton::height(), kToggleButtonMinWidth), DMButton::height());
    update_filter_toggle_label();
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

void AssetFilterBar::set_mode_buttons(std::vector<ModeButtonConfig> buttons) {
    mode_buttons_.clear();
    mode_buttons_.reserve(buttons.size());
    for (auto& cfg : buttons) {
        ModeButtonEntry entry;
        entry.config = std::move(cfg);
        entry.button = std::make_unique<DMButton>(entry.config.label, entry.config.active ? &DMStyles::AccentButton() : &DMStyles::HeaderButton(), 180, DMButton::height());
        mode_buttons_.push_back(std::move(entry));
    }
    layout_dirty_ = true;
}

void AssetFilterBar::set_mode_changed_callback(std::function<void(const std::string&)> cb) {
    on_mode_selected_ = std::move(cb);
}

void AssetFilterBar::set_active_mode(const std::string& id, bool trigger_callback) {
    bool changed = false;
    for (auto& entry : mode_buttons_) {
        const bool should_be_active = (entry.config.id == id);
        if (entry.config.active != should_be_active) {
            entry.config.active = should_be_active;
            if (entry.button) {
                entry.button->set_style(entry.config.active ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
            }
            changed = true;
        }
    }
    if (changed) {
        layout_dirty_ = true;
        if (trigger_callback && on_mode_selected_) {
            on_mode_selected_(id);
        }
    } else if (trigger_callback && on_mode_selected_) {
        on_mode_selected_(id);
    }
}

void AssetFilterBar::set_filters_expanded(bool expanded) {
    if (filters_expanded_ == expanded) {
        return;
    }
    filters_expanded_ = expanded;
    update_filter_toggle_label();
    persist_filters_expanded();
    layout_dirty_ = true;
}

void AssetFilterBar::refresh_layout() {
    layout_dirty_ = true;
    ensure_layout();
}

void AssetFilterBar::ensure_layout() {
    if (!layout_dirty_) {
        return;
    }
    layout_dirty_ = false;
    rebuild_layout();
}

void AssetFilterBar::rebuild_layout() {
    layout_bounds_ = SDL_Rect{0, 0, 0, 0};
    mode_bar_rect_ = SDL_Rect{0, 0, 0, 0};
    header_rect_ = SDL_Rect{0, 0, 0, 0};
    filters_rect_ = SDL_Rect{0, 0, 0, 0};

    clear_checkbox_rects();

    if (!enabled_ || screen_w_ <= 0) {
        return;
    }

    const int available_width = std::max(0, screen_w_);
    if (available_width <= 0) {
        return;
    }

    auto merge_bounds = [this](const SDL_Rect& rect) {
        if (rect.w <= 0 || rect.h <= 0) {
            return;
        }
        if (layout_bounds_.w <= 0 || layout_bounds_.h <= 0) {
            layout_bounds_ = rect;
            return;
        }
        int min_x = std::min(layout_bounds_.x, rect.x);
        int min_y = std::min(layout_bounds_.y, rect.y);
        int max_x = std::max(layout_bounds_.x + layout_bounds_.w, rect.x + rect.w);
        int max_y = std::max(layout_bounds_.y + layout_bounds_.h, rect.y + rect.h);
        layout_bounds_ = SDL_Rect{min_x, min_y, max_x - min_x, max_y - min_y};
};

    const int header_height = DMButton::height() + DMSpacing::item_gap() * 2;
    const int toggle_button_width = std::max(DMButton::height(), kToggleButtonMinWidth);
    header_rect_ = SDL_Rect{0, 0, available_width, header_height};

    if (filter_toggle_button_) {
        const int button_height = DMButton::height();
        int button_x = header_rect_.x + header_rect_.w - toggle_button_width - DMSpacing::item_gap();
        const int min_button_x = header_rect_.x + DMSpacing::item_gap();
        if (button_x < min_button_x) {
            button_x = min_button_x;
        }
        int button_y = header_rect_.y + (header_rect_.h - button_height) / 2;
        if (button_y < header_rect_.y) {
            button_y = header_rect_.y;
        }
        filter_toggle_button_->set_rect(SDL_Rect{button_x, button_y, toggle_button_width, button_height});
    }

    mode_bar_rect_ = header_rect_;
    if (filter_toggle_button_) {
        const SDL_Rect& toggle_rect = filter_toggle_button_->rect();
        if (toggle_rect.w > 0) {
            const int right_limit = std::max(mode_bar_rect_.x, toggle_rect.x - DMSpacing::item_gap());
            mode_bar_rect_.w = std::max(0, right_limit - mode_bar_rect_.x);
        }
    }

    merge_bounds(header_rect_);

    layout_mode_buttons();

    if (!filters_expanded_) {
        return;
    }

    int current_y = header_rect_.y + header_rect_.h;
    filters_rect_ = SDL_Rect{0, current_y, available_width, 0};
    layout_filter_checkboxes();
    merge_bounds(filters_rect_);
}

void AssetFilterBar::render(SDL_Renderer* renderer) const {
    if (!enabled_ || !renderer) {
        return;
    }
    const_cast<AssetFilterBar*>(this)->ensure_layout();
    if (layout_bounds_.w <= 0 || layout_bounds_.h <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const SDL_Color panel_bg = DMStyles::PanelBG();
    const SDL_Color highlight = DMStyles::HighlightColor();
    const SDL_Color shadow = DMStyles::ShadowColor();
    dm_draw::DrawBeveledRect(
        renderer,
        layout_bounds_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        panel_bg,
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    const SDL_Color border = DMStyles::Border();
    dm_draw::DrawRoundedOutline(
        renderer,
        layout_bounds_,
        DMStyles::CornerRadius(),
        kPanelOutlineThickness,
        border);

    const SDL_Color header_bg = DMStyles::PanelHeader();
    if (header_rect_.w > 0 && header_rect_.h > 0) {
        SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, 240);
        SDL_RenderFillRect(renderer, &header_rect_);
    }

    if (filter_toggle_button_) {
        filter_toggle_button_->render(renderer);
    }

    for (const auto& entry : mode_buttons_) {
        if (entry.button) {
            entry.button->render(renderer);
        }
    }

    if (!filters_expanded_) {
        return;
    }

    if (filters_rect_.w > 0 && filters_rect_.h > 0) {
        const SDL_Color content_bg = DMStyles::PanelBG();
        SDL_SetRenderDrawColor(renderer, content_bg.r, content_bg.g, content_bg.b, 220);
        SDL_RenderFillRect(renderer, &filters_rect_);
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
    bool used = false;
    auto handle_button = [&](ModeButtonEntry& entry) {
        if (!entry.button) {
            return;
        }
        if (entry.button->handle_event(event)) {
            used = true;
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                set_active_mode(entry.config.id, true);
            }
        }
};

    for (auto& entry : mode_buttons_) {
        handle_button(entry);
    }

    if (filter_toggle_button_ && filter_toggle_button_->handle_event(event)) {
        used = true;
        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            set_filters_expanded(!filters_expanded_);
            ensure_layout();
        }
    }

    if (!filters_expanded_) {
        return used;
    }

    bool checkbox_used = false;
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        if (entry.checkbox->handle_event(event)) {
            checkbox_used = true;
        }
    }
    if (checkbox_used) {
        used = true;
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
    if (layout_bounds_.w > 0 && layout_bounds_.h > 0 && SDL_PointInRect(&p, &layout_bounds_)) {
        return true;
    }
    return false;
}

void AssetFilterBar::reset() {
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        switch (entry.kind) {
            case FilterKind::MapAssets:
                entry.checkbox->set_value(true);
                break;
            case FilterKind::CurrentRoom:
                entry.checkbox->set_value(true);
                break;
            case FilterKind::Type:
                entry.checkbox->set_value(default_type_enabled(entry.id));
                break;
        }
    }
    state_.map_assets = true;
    state_.current_room = true;
    for (auto& kv : state_.type_filters) {
        kv.second = default_type_enabled(kv.first);
    }
    sync_state_from_ui();
    notify_state_changed();
}

bool AssetFilterBar::default_type_enabled(const std::string& type) const {
    const std::string canonical = asset_types::canonicalize(type);
    return canonical != asset_types::player;
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
    persist_state();
}

void AssetFilterBar::notify_state_changed() {
    if (on_state_changed_) {
        on_state_changed_();
    }
}

void AssetFilterBar::update_filter_toggle_label() {
    if (!filter_toggle_button_) {
        return;
    }
    filter_toggle_button_->set_text(filters_expanded_ ? "▲" : "▼");
}

void AssetFilterBar::clear_checkbox_rects() {
    for (auto& entry : entries_) {
        if (entry.checkbox) {
            entry.checkbox->set_rect(SDL_Rect{0, 0, 0, 0});
        }
    }
}

void AssetFilterBar::layout_mode_buttons() {
    if (mode_buttons_.empty()) {
        return;
    }

    const int count = static_cast<int>(mode_buttons_.size());
    for (auto& entry : mode_buttons_) {
        if (entry.button) {
            entry.button->set_style(entry.config.active ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
        }
    }

    if (mode_bar_rect_.w <= 0 || mode_bar_rect_.h <= 0) {
        for (auto& entry : mode_buttons_) {
            if (entry.button) {
                entry.button->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
        return;
    }

    const int padding = DMSpacing::item_gap();
    const int inner_gap = DMSpacing::small_gap();
    const int left = mode_bar_rect_.x + padding;
    const int right = mode_bar_rect_.x + mode_bar_rect_.w - padding;
    if (right <= left || count <= 0) {
        for (auto& entry : mode_buttons_) {
            if (entry.button) {
                entry.button->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
        return;
    }

    const int available_width = right - left;
    if (available_width <= 0) {
        for (auto& entry : mode_buttons_) {
            if (entry.button) {
                entry.button->set_rect(SDL_Rect{0, 0, 0, 0});
            }
        }
        return;
    }

    int base_segment = available_width / count;
    int remainder = available_width % count;

    int y = mode_bar_rect_.y + (mode_bar_rect_.h - DMButton::height()) / 2;
    if (y < mode_bar_rect_.y) {
        y = mode_bar_rect_.y;
    }

    int current_x = left;
    for (int i = 0; i < count; ++i) {
        auto& entry = mode_buttons_[i];
        if (!entry.button) {
            continue;
        }

        int segment = base_segment;
        if (remainder > 0) {
            ++segment;
            --remainder;
        }

        if (segment <= 0) {
            entry.button->set_rect(SDL_Rect{0, 0, 0, 0});
            continue;
        }

        int button_x = current_x + inner_gap;
        int button_width = segment - inner_gap * 2;
        if (button_width <= 0) {
            button_x = current_x;
            button_width = segment;
        }

        entry.button->set_rect(SDL_Rect{button_x, y, button_width, DMButton::height()});
        current_x += segment;
    }
}

void AssetFilterBar::layout_filter_checkboxes() {
    clear_checkbox_rects();
    filters_rect_.h = 0;
    if (!filters_expanded_ || filters_rect_.w <= 0) {
        return;
    }

    const int margin_x = DMSpacing::item_gap();
    const int margin_y = DMSpacing::item_gap();
    const int row_gap = DMSpacing::small_gap();
    const int checkbox_width = 180;
    const int checkbox_height = DMCheckbox::height();
    const int available_width = std::max(0, filters_rect_.w - margin_x * 2);
    if (available_width <= 0) {
        return;
    }

    std::vector<std::vector<FilterEntry*>> rows(1);
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        auto& current_row = rows.back();
        int current_width = 0;
        if (!current_row.empty()) {
            current_width = static_cast<int>(current_row.size()) * checkbox_width + static_cast<int>(current_row.size() - 1) * margin_x;
        }
        int width_with_new = current_width + checkbox_width;
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
        return;
    }

    const int checkbox_rows_height = row_count * checkbox_height + (row_count - 1) * row_gap;
    filters_rect_.h = margin_y + checkbox_rows_height + margin_y;

    int y = filters_rect_.y + margin_y;
    const int left_limit = filters_rect_.x + margin_x;
    const int right_limit = filters_rect_.x + filters_rect_.w - margin_x;

    for (const auto& row : rows) {
        if (row.empty()) {
            continue;
        }
        const int row_width = static_cast<int>(row.size()) * checkbox_width + static_cast<int>(row.size() - 1) * margin_x;
        int x = filters_rect_.x + (filters_rect_.w - row_width) / 2;
        if (row_width > (right_limit - left_limit)) {
            x = left_limit;
        } else {
            if (x < left_limit) x = left_limit;
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
            x += checkbox_width + margin_x;
        }

        y += checkbox_height + row_gap;
    }
}

bool AssetFilterBar::type_filter_enabled(const std::string& type) const {
    auto it = state_.type_filters.find(type);
    if (it == state_.type_filters.end()) {
        return true;
    }
    return it->second;
}

bool AssetFilterBar::load_type_filter_value(const std::string& type, bool default_value) const {
    if (!has_saved_state_) {
        return default_value;
    }
    return devmode::ui_settings::load_bool(make_type_setting_key(type), default_value);
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

void AssetFilterBar::load_persisted_state() {
    state_.type_filters.clear();
    has_saved_state_ = devmode::ui_settings::load_bool(kSettingsInitializedKey, false);
    if (!has_saved_state_) {
        state_.map_assets = true;
        state_.current_room = true;
        filters_expanded_ = false;
        return;
    }
    state_.map_assets = devmode::ui_settings::load_bool(kSettingsMapAssetsKey, true);
    state_.current_room = devmode::ui_settings::load_bool(kSettingsCurrentRoomKey, true);
    filters_expanded_ = devmode::ui_settings::load_bool(kSettingsFiltersExpandedKey, false);
}

void AssetFilterBar::persist_state() {
    devmode::ui_settings::save_bool(kSettingsInitializedKey, true);
    devmode::ui_settings::save_bool(kSettingsMapAssetsKey, state_.map_assets);
    devmode::ui_settings::save_bool(kSettingsCurrentRoomKey, state_.current_room);
    for (const auto& kv : state_.type_filters) {
        devmode::ui_settings::save_bool(make_type_setting_key(kv.first), kv.second);
    }
    has_saved_state_ = true;
}

void AssetFilterBar::persist_filters_expanded() const {
    devmode::ui_settings::save_bool(kSettingsFiltersExpandedKey, filters_expanded_);
}

