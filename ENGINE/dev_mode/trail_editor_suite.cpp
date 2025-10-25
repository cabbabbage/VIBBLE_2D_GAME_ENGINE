#include "trail_editor_suite.hpp"

#include "room_config/room_configurator.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"

#include "map_generation/room.hpp"
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
}

void TrailEditorSuite::close() {
    active_trail_ = nullptr;
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
}

bool TrailEditorSuite::handle_event(const SDL_Event& event) {
    bool used = false;
    if (configurator_) {
        if (configurator_->visible()) {
            configurator_->prepare_for_event(screen_w_, screen_h_);
        }
        if (configurator_->handle_event(event)) {
            used = true;
        }
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
}

bool TrailEditorSuite::contains_point(int x, int y) const {
    if (configurator_ && configurator_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

void TrailEditorSuite::set_on_open_area(
    std::function<void(const std::string&, const std::string&)> cb,
    std::string stack_key) {
    on_open_area_ = std::move(cb);
    open_area_stack_key_ = std::move(stack_key);
    if (configurator_) {
        configurator_->set_spawn_area_open_callback(on_open_area_, open_area_stack_key_);
    }
}

void TrailEditorSuite::ensure_ui() {
    if (!configurator_) {
        configurator_ = std::make_unique<RoomConfigurator>();
        if (configurator_) {
            configurator_->detach_container();
            if (auto* container = configurator_->container()) {
                container->set_header_visible(true);
                container->set_scrollbar_visible(true);
                container->set_close_button_enabled(true);
                container->set_blocks_editor_interactions(true);
            }
            configurator_->set_show_header(true);
            configurator_->set_on_close([this]() { this->close(); });
            configurator_->set_spawn_group_callbacks(
                {},
                [this](const std::string& id) { delete_spawn_group(id); },
                [this](const std::string& id, size_t index) { reorder_spawn_group(id, index); },
                [this]() { add_spawn_group(); });
            configurator_->set_spawn_area_open_callback(on_open_area_, open_area_stack_key_);
        }
    }
    update_bounds();
    if (configurator_) {
        configurator_->set_bounds(config_bounds_);
        configurator_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
}

void TrailEditorSuite::update_bounds() {
    const int side_margin = 0;
    const int vertical_margin = 48;
    const int min_width = 320;
    const int available_width = std::max(1, screen_w_ - side_margin);
    const int desired_width = std::max(360, screen_w_ / 3);
    const int width = std::min(available_width, std::max(min_width, desired_width));
    const int height = std::max(240, screen_h_ - 2 * vertical_margin);
    const int x = std::max(0, screen_w_ - width - side_margin);
    const int y = vertical_margin;
    config_bounds_ = SDL_Rect{x, y, width, height};
    if (configurator_) {
        configurator_->set_bounds(config_bounds_);
        configurator_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
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

    if (configurator_) {
        configurator_->refresh_spawn_groups(active_trail_);
        configurator_->notify_spawn_groups_mutated();
    }
}

void TrailEditorSuite::reorder_spawn_group(const std::string& id, size_t new_index) {
    if (!active_trail_ || id.empty()) {
        return;
    }
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    if (!groups.is_array() || groups.size() <= 1) {
        return;
    }

    size_t current = groups.size();
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& entry = groups[i];
        if (!entry.is_object()) {
            continue;
        }
        if (entry.contains("spawn_id") && entry["spawn_id"].is_string() && entry["spawn_id"].get<std::string>() == id) {
            current = i;
            break;
        }
    }
    if (current >= groups.size()) {
        return;
    }

    const size_t bounded_index = std::min(new_index, groups.size() - 1);
    if (current == bounded_index) {
        return;
    }

    nlohmann::json entry = std::move(groups[current]);
    const auto erase_pos = groups.begin() + static_cast<nlohmann::json::difference_type>(current);
    groups.erase(erase_pos);
    size_t insert_index = std::min(bounded_index, groups.size());
    const auto insert_pos = groups.begin() + static_cast<nlohmann::json::difference_type>(insert_index);
    groups.insert(insert_pos, std::move(entry));

    for (size_t i = 0; i < groups.size(); ++i) {
        auto& element = groups[i];
        if (element.is_object()) {
            element["priority"] = static_cast<int>(i);
        }
    }

    active_trail_->save_assets_json();
    if (configurator_) {
        configurator_->refresh_spawn_groups(active_trail_);
        configurator_->notify_spawn_groups_mutated();
    }
}

void TrailEditorSuite::add_spawn_group() {
    if (!active_trail_) {
        return;
    }
    auto& root = active_trail_->assets_data();
    auto& groups = ensure_spawn_groups_array(root);
    nlohmann::json entry;
    entry["spawn_id"] = generate_spawn_id();
    entry["position"] = "Exact";
    devmode::spawn::ensure_spawn_group_entry_defaults(entry, "New Spawn");
    groups.push_back(entry);
    sanitize_perimeter_spawn_groups(groups);
    active_trail_->save_assets_json();
    if (configurator_) {
        configurator_->refresh_spawn_groups(active_trail_);
        configurator_->notify_spawn_groups_mutated();
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

