#include "trail_editor_suite.hpp"

#include "dev_mode/room_configurator.hpp"
#include "dev_mode/spawn_group_config_ui.hpp"
#include "dev_mode/spawn_groups_config.hpp"
#include "dev_mode/spawn_group_utils.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"

#include "room/room.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

using devmode::sdl::event_point;
using devmode::sdl::is_pointer_event;

using namespace devmode::spawn;

TrailEditorSuite::TrailEditorSuite() = default;
TrailEditorSuite::~TrailEditorSuite() = default;

void TrailEditorSuite::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    update_bounds();
}

void TrailEditorSuite::open(Room* trail) {
    if (!trail) {
        return;
    }
    ensure_ui();
    active_trail_ = trail;
    update_bounds();
    if (configurator_) {
        configurator_->open(trail);
        configurator_->set_bounds(config_bounds_);
    }
    rebuild_spawn_groups_ui();
}

void TrailEditorSuite::close() {
    active_trail_ = nullptr;
    if (spawn_groups_) {
        spawn_groups_->close_all();
        spawn_groups_->close();
    }
    if (configurator_) {
        configurator_->close();
    }
}

bool TrailEditorSuite::is_open() const {
    return configurator_ && configurator_->visible();
}

void TrailEditorSuite::update(const Input& input) {
    if (configurator_ && configurator_->visible()) {
        configurator_->update(input, screen_w_, screen_h_);
    }
    if (spawn_groups_) {
        spawn_groups_->update(input, screen_w_, screen_h_);
    }
}

bool TrailEditorSuite::handle_event(const SDL_Event& event) {
    bool used = false;
    if (spawn_groups_ && spawn_groups_->handle_event(event)) {
        used = true;
    }
    if (configurator_ && configurator_->handle_event(event)) {
        used = true;
    }
    if (used) {
        return true;
    }
    if (!is_pointer_event(event)) {
        return false;
    }
    SDL_Point p = event_point(event);
    return contains_point(p.x, p.y);
}

void TrailEditorSuite::render(SDL_Renderer* renderer) const {
    if (configurator_) {
        configurator_->render(renderer);
    }
    if (spawn_groups_) {
        spawn_groups_->render(renderer);
    }
}

bool TrailEditorSuite::contains_point(int x, int y) const {
    if (configurator_ && configurator_->is_point_inside(x, y)) {
        return true;
    }
    if (spawn_groups_ && spawn_groups_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

void TrailEditorSuite::ensure_ui() {
    if (!configurator_) {
        configurator_ = std::make_unique<RoomConfigurator>();
        if (configurator_) {
            configurator_->set_on_close([this]() { this->close(); });
            configurator_->set_spawn_group_callbacks(
                [this](const std::string& id) { open_spawn_group_editor(id); },
                [this](const std::string& id) { duplicate_spawn_group(id); },
                [this](const std::string& id) { delete_spawn_group(id); },
                [this]() { add_spawn_group(); });
        }
    }
    if (!spawn_groups_) {
        spawn_groups_ = std::make_unique<SpawnGroupsConfig>();
    }
    update_bounds();
    if (configurator_) {
        configurator_->set_bounds(config_bounds_);
        configurator_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
    if (spawn_groups_) {
        SDL_Point anchor{config_bounds_.x + config_bounds_.w + 16, config_bounds_.y};
        spawn_groups_->set_anchor(anchor.x, anchor.y);
    }
}

void TrailEditorSuite::update_bounds() {
    const int margin = 48;
    const int max_width = std::max(320, screen_w_ - 2 * margin);
    const int desired_width = std::max(360, screen_w_ / 3);
    const int width = std::min(max_width, desired_width);
    const int height = std::max(240, screen_h_ - 2 * margin);
    const int x = std::max(margin, screen_w_ - width - margin);
    const int y = margin;
    config_bounds_ = SDL_Rect{x, y, width, height};
    if (configurator_) {
        configurator_->set_bounds(config_bounds_);
        configurator_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
    if (spawn_groups_) {
        SDL_Point anchor{x + width + 16, y};
        spawn_groups_->set_anchor(anchor.x, anchor.y);
    }
}

void TrailEditorSuite::rebuild_spawn_groups_ui() {
    if (!active_trail_ || !spawn_groups_) {
        return;
    }
    ensure_ui();
    auto& root = active_trail_->assets_data();
    auto reopen = spawn_groups_->capture_open_spawn_group();
    spawn_groups_->close_all();
    auto& groups = ensure_spawn_groups_array(root);
    sanitize_perimeter_spawn_groups(groups);

    auto on_change = [this]() {
        if (!active_trail_) {
            return;
        }
        active_trail_->save_assets_json();
        if (configurator_) {
            configurator_->refresh_spawn_groups(active_trail_);
        }
    };

    auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupConfigUI::ChangeSummary& summary) {
        if (!active_trail_) {
            return;
        }
        auto& root = active_trail_->assets_data();
        auto& arr = ensure_spawn_groups_array(root);
        const bool sanitized = sanitize_perimeter_spawn_groups(arr);
        active_trail_->save_assets_json();
        if (configurator_) {
            configurator_->refresh_spawn_groups(active_trail_);
        }
        if (sanitized || summary.method_changed || summary.quantity_changed) {
            rebuild_spawn_groups_ui();
        }
    };

    spawn_groups_->load(groups, on_change, on_entry_change, {});
    if (configurator_) {
        configurator_->refresh_spawn_groups(active_trail_);
    }
    if (reopen && !reopen->id.empty()) {
        spawn_groups_->restore_open_spawn_group(*reopen);
    }
}

void TrailEditorSuite::open_spawn_group_editor(const std::string& id) {
    if (!spawn_groups_) {
        return;
    }
    SDL_Point anchor{config_bounds_.x + config_bounds_.w + 16, config_bounds_.y};
    spawn_groups_->set_anchor(anchor.x, anchor.y);
    spawn_groups_->request_open_spawn_group(id, anchor.x, anchor.y);
}

void TrailEditorSuite::duplicate_spawn_group(const std::string& id) {
    if (id.empty() || !active_trail_) {
        return;
    }
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    nlohmann::json* original = find_spawn_entry(id);
    if (!original) {
        return;
    }
    nlohmann::json duplicate = *original;
    const std::string new_id = generate_spawn_id();
    duplicate["spawn_id"] = new_id;
    if (duplicate.contains("display_name") && duplicate["display_name"].is_string()) {
        duplicate["display_name"] = duplicate["display_name"].get<std::string>() + " Copy";
    }
    groups.push_back(duplicate);
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    rebuild_spawn_groups_ui();
    open_spawn_group_editor(new_id);
}

void TrailEditorSuite::delete_spawn_group(const std::string& id) {
    if (id.empty() || !active_trail_) {
        return;
    }
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    auto it = std::remove_if(groups.begin(), groups.end(), [&](nlohmann::json& entry) {
        if (!entry.is_object()) {
            return false;
        }
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) {
            return false;
        }
        return entry["spawn_id"].get<std::string>() == id;
    });
    if (it == groups.end()) {
        return;
    }
    groups.erase(it, groups.end());
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    if (spawn_groups_) {
        spawn_groups_->close_all();
    }
    rebuild_spawn_groups_ui();
}

void TrailEditorSuite::add_spawn_group() {
    if (!active_trail_) {
        return;
    }
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    nlohmann::json entry;
    entry["spawn_id"] = generate_spawn_id();
    entry["display_name"] = "New Spawn";
    entry["position"] = "Exact";
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["check_overlap"] = false;
    entry["enforce_spacing"] = false;
    entry["chance_denominator"] = 100;
    entry["candidates"] = nlohmann::json::array();
    entry["candidates"].push_back({{"name", "null"}, {"chance", 0}});
    groups.push_back(entry);
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    rebuild_spawn_groups_ui();
    if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
        open_spawn_group_editor(entry["spawn_id"].get<std::string>());
    }
}

nlohmann::json* TrailEditorSuite::find_spawn_entry(const std::string& id) {
    if (!active_trail_ || id.empty()) {
        return nullptr;
    }
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    for (auto& entry : groups) {
        if (!entry.is_object()) {
            continue;
        }
        if (!entry.contains("spawn_id") || !entry["spawn_id"].is_string()) {
            continue;
        }
        if (entry["spawn_id"].get<std::string>() == id) {
            return &entry;
        }
    }
    return nullptr;
}

