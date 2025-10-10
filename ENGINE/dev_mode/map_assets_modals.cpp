#include "map_assets_modals.hpp"

#include <algorithm>
#include "spawn_group_config/SpawnGroupConfig.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "utils/input.hpp"

using nlohmann::json;

SingleSpawnGroupModal::SingleSpawnGroupModal() = default;
SingleSpawnGroupModal::~SingleSpawnGroupModal() = default;

void SingleSpawnGroupModal::ensure_single_group(json& section,
                                                const std::string& default_display_name) {
    if (!section.is_object()) {
        section = json::object();
    }
    auto& groups = devmode::spawn::ensure_spawn_groups_array(section);
    if (groups.empty()) {
        json entry = json::object();
        devmode::spawn::ensure_spawn_group_entry_defaults(entry, default_display_name);
        groups.push_back(std::move(entry));
    } else {
        devmode::spawn::ensure_spawn_group_entry_defaults(groups[0], default_display_name);
        if (groups.size() > 1) {
            json first = groups[0];
            groups = json::array();
            groups.push_back(std::move(first));
        }
    }
}

void SingleSpawnGroupModal::open(json& map_info,
                                 const std::string& section_key,
                                 const std::string& default_display_name,
                                 const std::string& ownership_label,
                                 SDL_Color ownership_color,
                                 SaveCallback on_save) {
    map_info_ = &map_info;
    on_save_ = std::move(on_save);
    section_ = &(*map_info_)[section_key];
    ensure_single_group(*section_, default_display_name);

    auto& groups = (*section_)["spawn_groups"];
    auto& entry = groups.front();

    if (!list_) list_ = std::make_unique<SpawnGroupConfig>(true);
    list_->set_embedded_mode(false);
    list_->set_screen_dimensions(screen_w_, screen_h_);

    constexpr int kPanelCellWidth   = 360;
    constexpr int kMinVisibleHeight = 420;
    constexpr int kHeightMargin     = 200;
    list_->set_cell_width(kPanelCellWidth);
    list_->set_scroll_enabled(true);
    int visible_height = kMinVisibleHeight;
    if (screen_h_ > 0) {
        visible_height = std::max(kMinVisibleHeight, screen_h_ - kHeightMargin);
    }
    list_->set_visible_height(visible_height);

    auto relay_save = [this, default_display_name]() {
        if (!this->section_ || !this->section_->is_object()) return;
        ensure_single_group(*section_, default_display_name);
        auto& groups = devmode::spawn::ensure_spawn_groups_array(*section_);
        if (!groups.is_array() || groups.empty()) return;
        bool ok = true;
        if (on_save_) ok = on_save_();
        (void)ok;
};

    SpawnGroupConfig::ConfigureEntryCallback configure_entry =
        [this, ownership_label, ownership_color](SpawnGroupConfig::EntryController& entry, const json&) {
            if (!ownership_label.empty()) {
                entry.set_ownership_label(ownership_label, ownership_color);
            } else {
                entry.clear_ownership_label();
            }
            entry.set_open_area_handler(on_open_area_, stack_key_);
    };

    list_->set_on_layout_changed([this]() { ensure_visible_position(); });
    position_initialized_ = false;
    list_->bind_entry(entry,
                      relay_save,
                      [relay_save](const json&, const SpawnGroupConfig::ChangeSummary&) { relay_save(); },
                      {},
                      std::move(configure_entry));
    list_->DockableCollapsible::open();
    list_->force_pointer_ready();

    ensure_visible_position();
}

void SingleSpawnGroupModal::close() {
    if (list_) list_->close();
}

bool SingleSpawnGroupModal::visible() const {
    return list_ && list_->is_visible();
}

void SingleSpawnGroupModal::update(const Input& input) {
    if (list_) list_->update(input, screen_w_, screen_h_);
}

bool SingleSpawnGroupModal::handle_event(const SDL_Event& e) {
    if (!list_) return false;
    return list_->handle_event(e);
}

void SingleSpawnGroupModal::render(SDL_Renderer* r) const {
    if (list_) list_->render(r);
}

bool SingleSpawnGroupModal::is_point_inside(int x, int y) const {
    if (!list_) return false;
    return list_->is_point_inside(x, y);
}

void SingleSpawnGroupModal::set_screen_dimensions(int width, int height) {
    screen_w_ = std::max(width, 0);
    screen_h_ = std::max(height, 0);
    if (list_) list_->set_screen_dimensions(screen_w_, screen_h_);
    ensure_visible_position();
}

void SingleSpawnGroupModal::set_floating_stack_key(std::string key) {
    stack_key_ = std::move(key);
    if (list_) {
        list_->refresh_row_configuration();
    }
}

void SingleSpawnGroupModal::set_on_open_area(
    std::function<void(const std::string&, const std::string&)> cb) {
    on_open_area_ = std::move(cb);
    if (list_) {
        list_->refresh_row_configuration();
    }
}

void SingleSpawnGroupModal::ensure_visible_position() {
    if (!list_) return;
    SDL_Rect rect = list_->rect();
    constexpr int kPreferredWidth = 460;
    if (rect.w <= 0) rect.w = kPreferredWidth;
    rect.w = std::max(rect.w, kPreferredWidth);
    if (rect.h <= 0) rect.h = 540;
    const int margin = 16;
    const bool have_w = screen_w_ > 0;
    const bool have_h = screen_h_ > 0;
    int max_x = have_w ? std::max(margin, screen_w_ - rect.w - margin) : 0;
    int max_y = have_h ? std::max(margin, screen_h_ - rect.h - margin) : 0;
    SDL_Point pos = list_->position();
    bool reposition = !position_initialized_;
    if (have_w && (pos.x < margin || pos.x > max_x)) reposition = true;
    if (have_h && (pos.y < margin || pos.y > max_y)) reposition = true;
    if (!reposition) return;
    int x = pos.x;
    int y = pos.y;
    if (have_w) {
        int centered = screen_w_ / 2 - rect.w / 2;
        x = std::clamp(centered, margin, max_x);
    }
    if (have_h) {
        int centered = screen_h_ / 2 - rect.h / 2;
        y = std::clamp(centered, margin, max_y);
    }
    if (have_w || have_h) {
        list_->set_position(x, y);
        position_initialized_ = true;
    }
}

