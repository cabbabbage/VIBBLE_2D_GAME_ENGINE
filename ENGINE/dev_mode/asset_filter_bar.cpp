#include "asset_filter_bar.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/dm_icons.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "map_generation/room.hpp"

#include <algorithm>
#include <cctype>
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
constexpr const char* kSettingsRenderDarkMaskKey = "dev.asset_filter.render_dark_mask";
constexpr const char* kSettingsFiltersExpandedKey = "dev.asset_filter.filters_expanded";
constexpr const char* kSettingsMethodPrefix = "dev.asset_filter.methods.";

std::string make_type_setting_key(const std::string& type) {
    std::string canonical = asset_types::canonicalize(type);
    std::string key = "dev.asset_filter.types.";
    key += canonical;
    return key;
}

std::string canonicalize_method_string(const std::string& method) {
    std::string canonical;
    canonical.reserve(method.size());
    for (unsigned char ch : method) {
        if (std::isalnum(ch)) {
            canonical.push_back(static_cast<char>(std::tolower(ch)));
        } else if (std::isspace(ch) || ch == '_' || ch == '-') {
            if (canonical.empty() || canonical.back() == '_') continue;
            canonical.push_back('_');
        }
    }
    return canonical;
}

std::string make_method_setting_key(const std::string& method) {
    std::string key = kSettingsMethodPrefix;
    key += canonicalize_method_string(method);
    return key;
}

}

AssetFilterBar::FilterState& AssetFilterBar::persistent_state() {
    static FilterState state{};
    return state;
}

bool& AssetFilterBar::persistent_state_initialized_flag() {
    static bool initialized = false;
    return initialized;
}

bool& AssetFilterBar::persistent_state_loaded_flag() {
    static bool loaded = false;
    return loaded;
}

bool& AssetFilterBar::persistent_filters_expanded_flag() {
    static bool expanded = false;
    return expanded;
}

void AssetFilterBar::ensure_persistent_state_loaded() {
    if (persistent_state_loaded_flag()) {
        return;
    }
    persistent_state_loaded_flag() = true;
    persistent_state_initialized_flag() = devmode::ui_settings::load_bool(kSettingsInitializedKey, false);
    if (!persistent_state_initialized_flag()) {
        FilterState& state = persistent_state();
        state.map_assets = true;
        state.current_room = true;
        state.render_dark_mask = true;
        persistent_filters_expanded_flag() = false;
        return;
    }
    FilterState& state = persistent_state();
    state.map_assets = devmode::ui_settings::load_bool(kSettingsMapAssetsKey, true);
    state.current_room = devmode::ui_settings::load_bool(kSettingsCurrentRoomKey, true);
    state.render_dark_mask = devmode::ui_settings::load_bool(kSettingsRenderDarkMaskKey, true);
    persistent_filters_expanded_flag() = devmode::ui_settings::load_bool(kSettingsFiltersExpandedKey, false);
}

AssetFilterBar::AssetFilterBar() = default;
AssetFilterBar::~AssetFilterBar() = default;

AssetFilterBar::FilterState& AssetFilterBar::mutable_state() {
    if (!state_) {
        ensure_persistent_state_loaded();
        state_ = &persistent_state();
        has_saved_state_ = persistent_state_initialized_flag();
        filters_expanded_ = persistent_filters_expanded_flag();
        if (!has_saved_state_) {
            state_->map_assets = true;
            state_->current_room = true;
            state_->render_dark_mask = true;
        }
    }
    return *state_;
}

const AssetFilterBar::FilterState& AssetFilterBar::state() const {
    return const_cast<AssetFilterBar*>(this)->mutable_state();
}

void AssetFilterBar::initialize() {
    entries_.clear();
    load_persisted_state();

    FilterState& state_ref = mutable_state();
    const bool use_saved_state = has_saved_state_;

    FilterEntry map_entry;
    map_entry.id = "map_assets";
    map_entry.kind = FilterKind::MapAssets;
    const bool map_assets_value = use_saved_state ? state_ref.map_assets : true;
    map_entry.checkbox = std::make_unique<DMCheckbox>("Map Assets", map_assets_value);
    if (!use_saved_state) {
        state_ref.map_assets = map_assets_value;
    }
    entries_.push_back(std::move(map_entry));

    FilterEntry room_entry;
    room_entry.id = "current_room";
    room_entry.kind = FilterKind::CurrentRoom;
    const bool current_room_value = use_saved_state ? state_ref.current_room : true;
    room_entry.checkbox = std::make_unique<DMCheckbox>("Current Room", current_room_value);
    if (!use_saved_state) {
        state_ref.current_room = current_room_value;
    }
    entries_.push_back(std::move(room_entry));

    FilterEntry dark_mask_entry;
    dark_mask_entry.id = "render_dark_mask";
    dark_mask_entry.kind = FilterKind::RenderDarkMask;
    const bool render_dark_mask_value = use_saved_state ? state_ref.render_dark_mask : true;
    dark_mask_entry.checkbox = std::make_unique<DMCheckbox>("Render Dark Mask", render_dark_mask_value);
    if (!use_saved_state) {
        state_ref.render_dark_mask = render_dark_mask_value;
    }
    entries_.push_back(std::move(dark_mask_entry));

    static const std::vector<std::string> kSpawnMethods = {
        "Random",
        "Perimeter",
        "Edge",
        "Exact",
        "Exact Position",
        "Percent",
        "Center",
        "ChildRandom",
};

    std::unordered_set<std::string> known_methods;
    known_methods.reserve(kSpawnMethods.size());
    for (const std::string& method : kSpawnMethods) {
        const std::string canonical = canonicalize_method(method);
        FilterEntry entry;
        entry.id = canonical;
        entry.kind = FilterKind::SpawnMethod;
        bool checkbox_value = default_method_enabled(canonical);
        if (use_saved_state) {
            checkbox_value = load_method_filter_value(canonical, checkbox_value);
        }
        entry.checkbox = std::make_unique<DMCheckbox>(format_method_label(method), checkbox_value);
        state_ref.method_filters[canonical] = checkbox_value;
        known_methods.insert(canonical);
        entries_.push_back(std::move(entry));
    }

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
        state_ref.type_filters[canonical] = checkbox_value;
        known_types.insert(canonical);
        entries_.push_back(std::move(entry));
    }

    if (use_saved_state) {
        for (auto it = state_ref.type_filters.begin(); it != state_ref.type_filters.end();) {
            if (known_types.find(it->first) == known_types.end()) {
                it = state_ref.type_filters.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = state_ref.method_filters.begin(); it != state_ref.method_filters.end();) {
            if (known_methods.find(it->first) == known_methods.end()) {
                it = state_ref.method_filters.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!use_saved_state) {
        filters_expanded_ = false;
    }
    filter_toggle_button_ = std::make_unique<DMButton>(std::string(DMIcons::CollapseExpanded()), &DMStyles::HeaderButton(), std::max(DMButton::height(), kToggleButtonMinWidth), DMButton::height());
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
    if (screen_w_ == width && screen_h_ == height) {
        return;
    }
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
            int right_limit = std::max(mode_bar_rect_.x, toggle_rect.x - DMSpacing::item_gap());

            if (right_accessory_width_ > 0) {
                right_limit -= (right_accessory_width_ + DMSpacing::item_gap());
                right_limit = std::max(mode_bar_rect_.x, right_limit);
            }
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

    extra_panel_rect_ = SDL_Rect{0,0,0,0};
    if (extra_panel_height_ > 0) {
        const int top_gap = DMSpacing::item_gap();
        const int extra_y = filters_rect_.y + filters_rect_.h + top_gap;
        extra_panel_rect_ = SDL_Rect{ filters_rect_.x, extra_y, filters_rect_.w, extra_panel_height_ };

        filters_rect_.h += top_gap + extra_panel_height_;
    }
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
    dm_draw::DrawBeveledRect( renderer, layout_bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), panel_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    const SDL_Color border = DMStyles::Border();
    dm_draw::DrawRoundedOutline( renderer, layout_bounds_, DMStyles::CornerRadius(), kPanelOutlineThickness, border);

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

    if (extra_panel_rect_.w > 0 && extra_panel_rect_.h > 0 && extra_renderer_) {
        extra_renderer_(renderer, extra_panel_rect_);
    }
}

bool AssetFilterBar::handle_event(const SDL_Event& event) {
    if (!enabled_ || header_suppressed_) {
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

    if (extra_panel_rect_.w > 0 && extra_panel_rect_.h > 0 && extra_event_handler_) {

        if (extra_event_handler_(event, extra_panel_rect_)) {
            used = true;
        }
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
            case FilterKind::RenderDarkMask:
                entry.checkbox->set_value(true);
                break;
            case FilterKind::Type:
                entry.checkbox->set_value(default_type_enabled(entry.id));
                break;
            case FilterKind::SpawnMethod:
                entry.checkbox->set_value(default_method_enabled(entry.id));
                break;
        }
    }
    FilterState& state_ref = mutable_state();
    state_ref.map_assets = true;
    state_ref.current_room = true;
    state_ref.render_dark_mask = true;
    for (auto& kv : state_ref.type_filters) {
        kv.second = default_type_enabled(kv.first);
    }
    for (auto& kv : state_ref.method_filters) {
        kv.second = default_method_enabled(kv.first);
    }
    sync_state_from_ui();
    notify_state_changed();
}

bool AssetFilterBar::default_type_enabled(const std::string& type) const {
    return true;
}

bool AssetFilterBar::default_method_enabled(const std::string& method) const {
    (void)method;
    return true;
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
    const std::string method = canonicalize_method(asset.spawn_method);
    if (!method.empty() && !method_filter_enabled(method)) {
        return false;
    }
    const bool is_map_asset = !asset.spawn_id.empty() && map_spawn_ids_.find(asset.spawn_id) != map_spawn_ids_.end();
    const FilterState& state_ref = state();
    if (is_map_asset && !state_ref.map_assets) {
        return false;
    }
    const bool is_room_asset = !asset.spawn_id.empty() && room_spawn_ids_.find(asset.spawn_id) != room_spawn_ids_.end();
    if (is_room_asset && !state_ref.current_room) {
        return false;
    }
    return true;
}

bool AssetFilterBar::render_dark_mask_enabled() const {
    return state().render_dark_mask;
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
    FilterState& state_ref = mutable_state();
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        const bool value = entry.checkbox->value();
        switch (entry.kind) {
        case FilterKind::MapAssets:
            state_ref.map_assets = value;
            break;
        case FilterKind::CurrentRoom:
            state_ref.current_room = value;
            break;
        case FilterKind::RenderDarkMask:
            state_ref.render_dark_mask = value;
            break;
        case FilterKind::Type:
            state_ref.type_filters[entry.id] = value;
            break;
        case FilterKind::SpawnMethod:
            state_ref.method_filters[entry.id] = value;
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
    filter_toggle_button_->set_text(filters_expanded_ ? std::string(DMIcons::CollapseExpanded()) : std::string(DMIcons::CollapseCollapsed()));
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
    const int section_gap = DMSpacing::section_gap();
    const int checkbox_width = 180;
    const int checkbox_height = DMCheckbox::height();
    const int available_width = std::max(0, filters_rect_.w - margin_x * 2);
    if (available_width <= 0) {
        return;
    }

    std::vector<FilterEntry*> primary_entries;
    std::vector<FilterEntry*> advanced_entries;
    primary_entries.reserve(entries_.size());
    advanced_entries.reserve(entries_.size());
    FilterEntry* dark_mask_entry = nullptr;
    for (auto& entry : entries_) {
        if (!entry.checkbox) {
            continue;
        }
        switch (entry.kind) {
        case FilterKind::MapAssets:
        case FilterKind::CurrentRoom:
            primary_entries.push_back(&entry);
            break;
        case FilterKind::RenderDarkMask:
            dark_mask_entry = &entry;
            break;
        default:
            advanced_entries.push_back(&entry);
            break;
        }
    }

    auto build_rows_for = [&](const std::vector<FilterEntry*>& source) {
        std::vector<std::vector<FilterEntry*>> section_rows(1);
        for (FilterEntry* entry : source) {
            if (!entry || !entry->checkbox) {
                continue;
            }
            auto& current_row = section_rows.back();
            int current_width = 0;
            if (!current_row.empty()) {
                current_width = static_cast<int>(current_row.size()) * checkbox_width + static_cast<int>(current_row.size() - 1) * margin_x;
            }
            int width_with_new = current_width + checkbox_width;
            if (!current_row.empty()) {
                width_with_new += margin_x;
            }
            if (!current_row.empty() && width_with_new > available_width) {
                section_rows.emplace_back();
            }
            section_rows.back().push_back(entry);
        }
        if (!section_rows.empty() && section_rows.back().empty()) {
            section_rows.pop_back();
        }
        return section_rows;
};

    const auto primary_rows = build_rows_for(primary_entries);
    const auto advanced_rows = build_rows_for(advanced_entries);
    auto rows_have_content = [](const std::vector<std::vector<FilterEntry*>>& rows) {
        for (const auto& row : rows) {
            if (!row.empty()) {
                return true;
            }
        }
        return false;
};

    const bool has_primary = rows_have_content(primary_rows);
    const bool has_advanced = rows_have_content(advanced_rows);
    const bool has_dark_mask = dark_mask_entry && dark_mask_entry->checkbox;
    if (!has_primary && !has_advanced && !has_dark_mask) {
        return;
    }

    int y = filters_rect_.y + margin_y;
    const int left_limit = filters_rect_.x + margin_x;
    const int right_limit = filters_rect_.x + filters_rect_.w - margin_x;

    auto layout_rows = [&](const std::vector<std::vector<FilterEntry*>>& rows) {
        for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
            const auto& row = rows[row_idx];
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

            y += checkbox_height;

            bool more_rows_pending = false;
            for (size_t next_row = row_idx + 1; next_row < rows.size(); ++next_row) {
                if (!rows[next_row].empty()) {
                    more_rows_pending = true;
                    break;
                }
            }
            if (more_rows_pending) {
                y += row_gap;
            }
        }
};

    bool section_emitted = false;
    if (has_primary) {
        layout_rows(primary_rows);
        section_emitted = true;
    }

    if (has_dark_mask) {
        if (section_emitted) {
            y += section_gap;
        }
        int x = filters_rect_.x + (filters_rect_.w - checkbox_width) / 2;
        if (x + checkbox_width > right_limit) {
            x = right_limit - checkbox_width;
        }
        if (x < left_limit) {
            x = left_limit;
        }
        SDL_Rect rect{x, y, checkbox_width, checkbox_height};
        dark_mask_entry->checkbox->set_rect(rect);
        y += checkbox_height;
        section_emitted = true;
    }

    if (has_advanced) {
        if (section_emitted) {
            y += section_gap;
        }
        layout_rows(advanced_rows);
    }

    y += margin_y;
    filters_rect_.h = y - filters_rect_.y;
}

bool AssetFilterBar::type_filter_enabled(const std::string& type) const {
    const FilterState& state_ref = state();
    auto it = state_ref.type_filters.find(type);
    if (it == state_ref.type_filters.end()) {
        return true;
    }
    return it->second;
}

bool AssetFilterBar::method_filter_enabled(const std::string& method) const {
    const FilterState& state_ref = state();
    auto it = state_ref.method_filters.find(method);
    if (it == state_ref.method_filters.end()) {
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

bool AssetFilterBar::load_method_filter_value(const std::string& method, bool default_value) const {
    if (!has_saved_state_) {
        return default_value;
    }
    return devmode::ui_settings::load_bool(make_method_setting_key(method), default_value);
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

std::string AssetFilterBar::format_method_label(const std::string& method) const {
    if (method.empty()) {
        return std::string{};
    }
    std::string label;
    label.reserve(method.size() + 4);
    char prev = '\0';
    for (unsigned char ch : method) {
        if (ch == '_' || ch == '-') {
            if (!label.empty() && label.back() != ' ') {
                label.push_back(' ');
            }
            prev = ch;
            continue;
        }
        if (std::isupper(ch) && !label.empty() &&
            (std::islower(static_cast<unsigned char>(prev)) || std::isdigit(static_cast<unsigned char>(prev)))) {
            label.push_back(' ');
        }
        label.push_back(static_cast<char>(ch));
        prev = static_cast<char>(ch);
    }
    bool start = true;
    for (char& ch : label) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch)) {
            start = true;
            continue;
        }
        if (start) {
            ch = static_cast<char>(std::toupper(uch));
            start = false;
        } else {
            ch = static_cast<char>(std::tolower(uch));
        }
    }
    return label;
}

std::string AssetFilterBar::canonicalize_method(const std::string& method) {
    return canonicalize_method_string(method);
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
    ensure_persistent_state_loaded();
    FilterState& state_ref = mutable_state();
    state_ref.type_filters.clear();
    state_ref.method_filters.clear();
    has_saved_state_ = persistent_state_initialized_flag();
    if (!has_saved_state_) {
        state_ref.map_assets = true;
        state_ref.current_room = true;
        state_ref.render_dark_mask = true;
        filters_expanded_ = false;
        return;
    }
    state_ref.map_assets = devmode::ui_settings::load_bool(kSettingsMapAssetsKey, true);
    state_ref.current_room = devmode::ui_settings::load_bool(kSettingsCurrentRoomKey, true);
    state_ref.render_dark_mask = devmode::ui_settings::load_bool(kSettingsRenderDarkMaskKey, true);
    filters_expanded_ = persistent_filters_expanded_flag();
}

void AssetFilterBar::persist_state() {
    FilterState& state_ref = mutable_state();
    devmode::ui_settings::save_bool(kSettingsInitializedKey, true);
    devmode::ui_settings::save_bool(kSettingsMapAssetsKey, state_ref.map_assets);
    devmode::ui_settings::save_bool(kSettingsCurrentRoomKey, state_ref.current_room);
    devmode::ui_settings::save_bool(kSettingsRenderDarkMaskKey, state_ref.render_dark_mask);
    for (const auto& kv : state_ref.type_filters) {
        devmode::ui_settings::save_bool(make_type_setting_key(kv.first), kv.second);
    }
    for (const auto& kv : state_ref.method_filters) {
        devmode::ui_settings::save_bool(make_method_setting_key(kv.first), kv.second);
    }
    has_saved_state_ = true;
    persistent_state_initialized_flag() = true;
}

void AssetFilterBar::persist_filters_expanded() const {
    persistent_filters_expanded_flag() = filters_expanded_;
    devmode::ui_settings::save_bool(kSettingsFiltersExpandedKey, filters_expanded_);
}
