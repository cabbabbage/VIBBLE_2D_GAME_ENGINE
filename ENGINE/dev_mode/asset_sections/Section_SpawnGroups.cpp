#include "Section_SpawnGroups.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "dev_mode/room_config/spawn_group_list.hpp"
#include "dev_mode/room_config/spawn_group_utils.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "asset/asset_info.hpp"
#include "dev_mode/asset_info_ui.hpp"

namespace {
static nlohmann::json ensure_array(nlohmann::json& root, const char* key) {
    if (!root.is_object()) root = nlohmann::json::object();
    if (!root.contains(key) || !root[key].is_array()) root[key] = nlohmann::json::array();
    return root[key];
}
}

Section_SpawnGroups::Section_SpawnGroups()
    : DockableCollapsible("Spawn Groups", false) {
    set_scroll_enabled(true);
    set_cell_width(260);
}

void Section_SpawnGroups::build() {
    DockableCollapsible::Rows rows;
    if (!list_) list_ = std::make_unique<SpawnGroupList>();
    if (list_) list_->set_embedded_mode(true);
    reload_from_file();

    // Bind live JSON so inline editor updates can persist
    auto on_change = [this]() {
        (void)this->save_to_file();
        this->build();
    };
    SpawnGroupList::Callbacks cb{};
    cb.on_duplicate = [this](const std::string& id){ duplicate_spawn_group(id); };
    cb.on_delete    = [this](const std::string& id){ delete_spawn_group(id); };
    cb.on_move_up   = [this](const std::string& id){ move_spawn_group(id, -1); };
    cb.on_move_down = [this](const std::string& id){ move_spawn_group(id, +1); };
    cb.on_add       = [this](){ add_spawn_group(); };
    list_->set_callbacks(std::move(cb));
    const auto expanded = list_->expanded_groups();
    list_->load(groups_, on_change);
    list_->set_on_layout_changed([this]() {
        if (!list_) return;
        DockableCollapsible::Rows rows;
        list_->append_rows(rows);
        this->set_rows(rows);
    });
    list_->restore_expanded_groups(expanded);
    list_->append_rows(rows);

    // Top-level Add Spawn Group button is provided by SpawnGroupList widget now.

    set_rows(rows);
}

void Section_SpawnGroups::layout() {
    DockableCollapsible::layout();
}

void Section_SpawnGroups::update(const Input& input, int screen_w, int screen_h) {
    screen_w_ = screen_w > 0 ? screen_w : screen_w_;
    screen_h_ = screen_h > 0 ? screen_h : screen_h_;
    if (list_) {
        list_->set_screen_dimensions(screen_w_, screen_h_);
        SDL_Point anchor = editor_anchor_point();
        list_->set_anchor(anchor.x, anchor.y);
        list_->update(input, screen_w_, screen_h_);
    }
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool Section_SpawnGroups::handle_event(const SDL_Event& e) {
    bool used = DockableCollapsible::handle_event(e);
    if (list_ && list_->handle_event(e)) return true;
    return used;
}

void Section_SpawnGroups::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
    if (list_) list_->render(r);
}

void Section_SpawnGroups::reload_from_file() {
    groups_ = nlohmann::json::array();
    if (!info_) return;
    try {
        std::ifstream in(info_->info_json_path());
        if (!in.is_open()) return;
        nlohmann::json root;
        in >> root;
        if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
            groups_ = root["spawn_groups"];
        }
    } catch (...) {
        groups_ = nlohmann::json::array();
    }
}

bool Section_SpawnGroups::save_to_file() {
    if (!info_) return false;
    try {
        nlohmann::json root;
        {
            std::ifstream in(info_->info_json_path());
            if (in.is_open()) {
                in >> root;
            } else {
                root = nlohmann::json::object();
            }
        }
        ensure_array(root, "spawn_groups");
        root["spawn_groups"] = groups_.is_array() ? groups_ : nlohmann::json::array();
        // Also ensure each has priority consistent with array order
        if (root["spawn_groups"].is_array()) {
            for (size_t i = 0; i < root["spawn_groups"].size(); ++i) {
                if (root["spawn_groups"][i].is_object()) root["spawn_groups"][i]["priority"] = static_cast<int>(i);
            }
        }
        std::ofstream out(info_->info_json_path());
        if (!out.is_open()) return false;
        out << root.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

void Section_SpawnGroups::renumber_priorities() {
    if (!groups_.is_array()) return;
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].is_object()) groups_[i]["priority"] = static_cast<int>(i);
    }
}

int Section_SpawnGroups::index_of(const std::string& id) const {
    if (!groups_.is_array()) return -1;
    for (size_t i = 0; i < groups_.size(); ++i) {
        const auto& e = groups_[i];
        if (!e.is_object()) continue;
        if (e.contains("spawn_id") && e["spawn_id"].is_string() && e["spawn_id"].get<std::string>() == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Section_SpawnGroups::add_spawn_group() {
    if (!groups_.is_array()) groups_ = nlohmann::json::array();
    nlohmann::json entry = nlohmann::json::object();
    entry["spawn_id"] = devmode::spawn::generate_spawn_id();
    entry["display_name"] = "New Spawn";
    entry["position"] = "Exact";
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["check_overlap"] = false;
    entry["enforce_spacing"] = false;
    entry["chance_denominator"] = 100;
    entry["candidates"] = nlohmann::json::array();
    entry["candidates"].push_back({{"name", "null"}, {"chance", 0}});
    const std::string new_id = entry["spawn_id"].get<std::string>();
    groups_.push_back(entry);
    renumber_priorities();
    (void)save_to_file();
    build();
    if (list_) {
        SDL_Point anchor = editor_anchor_point();
        list_->request_open_spawn_group(new_id, anchor.x, anchor.y);
    }
}

void Section_SpawnGroups::duplicate_spawn_group(const std::string& id) {
    const int idx = index_of(id);
    if (idx < 0) return;
    nlohmann::json duplicate = groups_[idx];
    duplicate["spawn_id"] = devmode::spawn::generate_spawn_id();
    if (duplicate.contains("display_name") && duplicate["display_name"].is_string()) {
        duplicate["display_name"] = duplicate["display_name"].get<std::string>() + " Copy";
    }
    groups_.push_back(std::move(duplicate));
    renumber_priorities();
    (void)save_to_file();
    build();
}

void Section_SpawnGroups::delete_spawn_group(const std::string& id) {
    if (!groups_.is_array()) return;
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [&](nlohmann::json& e){
        return e.is_object() && e.value("spawn_id", std::string{}) == id;
    }), groups_.end());
    renumber_priorities();
    (void)save_to_file();
    build();
}

void Section_SpawnGroups::move_spawn_group(const std::string& id, int dir) {
    if (!groups_.is_array()) return;
    const int idx = index_of(id);
    if (idx < 0) return;
    const int target = idx + (dir < 0 ? -1 : +1);
    if (target < 0 || target >= static_cast<int>(groups_.size())) return;
    std::swap(groups_[idx], groups_[target]);
    renumber_priorities();
    (void)save_to_file();
    build();
}

SDL_Point Section_SpawnGroups::editor_anchor_point() const {
    // Center-left of the asset info panel approximation: left of this section
    SDL_Rect r = rect();
    int x = std::max(16, r.x - 320);
    int y = std::max(16, r.y + r.h / 4);
    return SDL_Point{x, y};
}

// Editing is handled by SpawnGroupList's own editor; no direct edit method needed.

