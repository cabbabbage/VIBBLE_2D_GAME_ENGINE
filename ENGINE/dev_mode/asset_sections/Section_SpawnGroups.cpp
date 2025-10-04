#include "Section_SpawnGroups.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "dev_mode/spawn_group_list.hpp"
#include "dev_mode/spawn_group_config_ui.hpp"
#include "dev_mode/spawn_group_utils.hpp"
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
    reload_from_file();

    // Wire callbacks
    SpawnGroupList::Callbacks cb{};
    cb.on_edit      = [this](const std::string& id){ edit_spawn_group(id); };
    cb.on_duplicate = [this](const std::string& id){ duplicate_spawn_group(id); };
    cb.on_delete    = [this](const std::string& id){ delete_spawn_group(id); };
    cb.on_move_up   = [this](const std::string& id){ move_spawn_group(id, -1); };
    cb.on_move_down = [this](const std::string& id){ move_spawn_group(id, +1); };
    list_->set_callbacks(std::move(cb));
    list_->load(groups_);
    list_->append_rows(rows);

    if (!add_btn_) add_btn_ = std::make_unique<DMButton>("Add Group", &DMStyles::CreateButton(), 140, DMButton::height());
    if (!add_btn_w_) add_btn_w_ = std::make_unique<ButtonWidget>(add_btn_.get(), [this](){ add_spawn_group(); });
    rows.push_back({ add_btn_w_.get() });

    set_rows(rows);
}

void Section_SpawnGroups::layout() {
    DockableCollapsible::layout();
}

void Section_SpawnGroups::update(const Input& input, int screen_w, int screen_h) {
    screen_w_ = screen_w > 0 ? screen_w : screen_w_;
    screen_h_ = screen_h > 0 ? screen_h : screen_h_;
    if (editor_) {
        editor_->set_screen_dimensions(screen_w_, screen_h_);
        editor_->update(input, screen_w_, screen_h_);
    }
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool Section_SpawnGroups::handle_event(const SDL_Event& e) {
    bool used = DockableCollapsible::handle_event(e);
    if (editor_ && editor_->handle_event(e)) return true;
    return used;
}

void Section_SpawnGroups::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
    if (!editor_) return;

    SDL_Rect prev_clip{};
    SDL_RenderGetClipRect(r, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(r);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(r, nullptr);
    editor_->render(r);
    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(r, &prev_clip);
    } else {
        SDL_RenderSetClipRect(r, nullptr);
    }
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
    groups_.push_back(entry);
    renumber_priorities();
    (void)save_to_file();
    build();
    edit_spawn_group(entry["spawn_id"].get<std::string>());
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

void Section_SpawnGroups::edit_spawn_group(const std::string& id) {
    if (!groups_.is_array()) return;
    int idx = index_of(id);
    if (idx < 0) return;
    nlohmann::json entry = groups_[idx];
    if (!editor_) editor_ = std::make_unique<SpawnGroupsConfigPanel>();
    editor_->set_screen_dimensions(screen_w_, screen_h_);
    SDL_Point anchor = editor_anchor_point();
    editor_->set_position(anchor.x, anchor.y);
    editor_->open(entry, [this, idx](const nlohmann::json& updated){
        if (!groups_.is_array()) return;
        if (idx >= 0 && idx < static_cast<int>(groups_.size())) {
            groups_[idx] = updated;
            renumber_priorities();
            (void)save_to_file();
            build();
        }
    });
}

