#include "map_assets_modals.hpp"

#include <algorithm>
#include "spawn_group_list/SpawnGroupList.hpp"
#include "utils/input.hpp"
#include "room_config/spawn_group_utils.hpp"

using nlohmann::json;

SingleSpawnGroupModal::SingleSpawnGroupModal() = default;
SingleSpawnGroupModal::~SingleSpawnGroupModal() = default;

namespace {

void ensure_candidate_list(json& entry) {
    if (!entry.contains("candidates") || !entry["candidates"].is_array()) {
        entry["candidates"] = json::array();
    }
    auto& candidates = entry["candidates"];
    if (candidates.empty()) {
        json null_cand = json::object();
        null_cand["name"] = "null";
        null_cand["chance"] = 0;
        candidates.push_back(std::move(null_cand));
    }
}

void ensure_spawn_group_defaults(json& entry, const std::string& default_display_name) {
    if (!entry.is_object()) {
        entry = json::object();
    }
    if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string() || entry["spawn_id"].get<std::string>().empty()) {
        entry["spawn_id"] = devmode::spawn::generate_spawn_id();
    }
    if (!entry.contains("display_name") || !entry["display_name"].is_string() || entry["display_name"].get<std::string>().empty()) {
        entry["display_name"] = default_display_name;
    }
    if (!entry.contains("position") || !entry["position"].is_string() || entry["position"].get<std::string>().empty()) {
        entry["position"] = "Random";
    }
    int min_number = entry.value("min_number", 1);
    int max_number = entry.value("max_number", min_number);
    min_number = std::max(1, min_number);
    if (max_number < min_number) {
        max_number = min_number;
    }
    entry["min_number"] = min_number;
    entry["max_number"] = max_number;
    if (!entry.contains("enforce_spacing") || !entry["enforce_spacing"].is_boolean()) {
        entry["enforce_spacing"] = false;
    }
    ensure_candidate_list(entry);
}

}  // namespace

void SingleSpawnGroupModal::ensure_single_group(json& section,
                                                const std::string& default_display_name) {
    if (!section.is_object()) {
        section = json::object();
    }
    if (!section.contains("spawn_groups") || !section["spawn_groups"].is_array()) {
        section["spawn_groups"] = json::array();
    }
    auto& groups = section["spawn_groups"];
    if (groups.empty()) {
        json entry = json::object();
        ensure_spawn_group_defaults(entry, default_display_name);
        groups.push_back(std::move(entry));
    } else {
        ensure_spawn_group_defaults(groups[0], default_display_name);
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
    if (!list_) list_ = std::make_unique<SpawnGroupList>(true);
    list_->set_screen_dimensions(screen_w_, screen_h_);
    // Ensure the floating panel provides enough room for long lists while
    // remaining scrollable so content is accessible on smaller displays.
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
    // Open a floating SpawnGroupList panel bound to the current single-group array
    list_->open(groups, [this, default_display_name](const json& updated_array) {
        if (!this->map_info_ || !this->section_) return;
        auto& groups = (*section_)["spawn_groups"];
        groups = updated_array;
        // Enforce single group for this modal
        ensure_single_group(*section_, default_display_name);
        bool ok = true;
        if (on_save_) ok = on_save_();
        (void)ok; // Errors are reflected elsewhere if needed
    });
    // Center the list panel roughly
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
    // No stack key support for SpawnGroupList floating panel
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

