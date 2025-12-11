#include "Section_SpawnGroups.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

#include <nlohmann/json.hpp>

#include "dev_mode/spawn_group_config/SpawnGroupConfig.hpp"
#include "dev_mode/spawn_group_config/spawn_group_utils.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "asset/asset_info.hpp"
#include "dev_mode/asset_info_ui.hpp"
#include "utils/map_grid_settings.hpp"

Section_SpawnGroups::Section_SpawnGroups()
    : DockableCollapsible("Spawn Groups", false) {
    set_scroll_enabled(false);
    set_cell_width(260);
}

Section_SpawnGroups::~Section_SpawnGroups() = default;

void Section_SpawnGroups::build() {
    if (rebuilding_) {
        rebuild_requested_ = true;
        return;
    }

    rebuilding_ = true;
    rebuild_requested_ = false;

    DockableCollapsible::Rows rows;
    if (!info_) {
        if (!empty_state_widget_) {
            empty_state_widget_ = std::make_unique<ReadOnlyTextBoxWidget>( "", "No asset selected. Select an asset from the library or scene to view and edit its information.");
        }
        rows.push_back({ empty_state_widget_.get() });
        set_rows(rows);
        rebuilding_ = false;
        return;
    }
    if (!list_) list_ = std::make_unique<SpawnGroupConfig>();
    if (list_) {
        list_->set_default_resolution(MapGridSettings::defaults().resolution);
        list_->set_embedded_mode(true);
        list_->set_manifest_store(manifest_store_);
    }
    reload_from_file();

    auto on_change = [this]() {
        (void)this->save_to_file();
        this->schedule_rebuild();
};
    auto on_entry_change = [this](const nlohmann::json& entry, const SpawnGroupConfig::ChangeSummary&) {
        notify_spawn_config_listeners(entry);
};
    SpawnGroupConfig::Callbacks cb{};
    cb.on_delete    = [this](const std::string& id){ delete_spawn_group(id); };
    cb.on_reorder   = [this](const std::string& id, size_t index){ reorder_spawn_group(id, index); };
    cb.on_add       = [this](){ add_spawn_group(); };
    cb.on_regenerate = [this](const std::string& id) {
        const int idx = index_of(id);
        if (idx < 0) return;
        const auto& entry = groups_.at(static_cast<std::size_t>(idx));
        notify_spawn_config_listeners(entry);
};
    list_->set_callbacks(std::move(cb));
    const auto expanded = list_->expanded_groups();
    SpawnGroupConfig::ConfigureEntryCallback configure_entry;
    if (info_) {
        std::weak_ptr<AssetInfo> weak_info = info_;
        configure_entry = [weak_info](SpawnGroupConfig::EntryController& entry, const nlohmann::json&) {
            (void)weak_info;

};
        list_->load(groups_, on_change, std::move(on_entry_change), std::move(configure_entry));
    } else {
        const nlohmann::json& readonly = groups_;
        list_->load(readonly);
    }
    list_->set_on_layout_changed([this]() {
        if (!list_) return;
        DockableCollapsible::Rows rows;
        list_->append_rows(rows);
        this->set_rows(rows);
        this->layout();
    });
    list_->restore_expanded_groups(expanded);
    list_->append_rows(rows);

    set_rows(rows);

    const bool run_again = rebuild_requested_;
    rebuild_requested_ = false;
    rebuilding_ = false;
    if (run_again) {
        build();
    }
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
    if (list_ && list_->handle_event(e)) {
        return true;
    }
    return DockableCollapsible::handle_event(e);
}

void Section_SpawnGroups::render(SDL_Renderer* r) const {
    if (!r) return;
    DockableCollapsible::render(r);
    if (list_) list_->render(r);
}

void Section_SpawnGroups::reload_from_file() {
    groups_ = nlohmann::json::array();
    if (!info_) return;

    if (!manifest_store_) {
        std::cerr << "[Section_SpawnGroups] Manifest store unavailable; cannot load spawn groups for '"
                  << info_->name << "'\n";
        return;
    }

    bool found_groups = false;
    auto view = manifest_store_->get_asset(info_->name);
    if (view && view->is_object()) {
        const auto it = view->find("spawn_groups");
        if (it != view->end() && it->is_array()) {
            groups_ = *it;
            found_groups = true;
        }
    }

    if (info_) {
        if (found_groups) {
            info_->set_spawn_groups_payload(groups_);
        } else {
            info_->set_spawn_groups_payload(nlohmann::json());
        }
    }

    if (info_) {
        info_->set_spawn_groups(groups_);
    }
}

bool Section_SpawnGroups::save_to_file() {
    if (!info_) return false;
    if (!manifest_store_) {
        std::cerr << "[Section_SpawnGroups] Manifest store unavailable; cannot save spawn groups for '"
                  << info_->name << "'\n";
        return false;
    }
    renumber_priorities();
    nlohmann::json sanitized = groups_.is_array() ? groups_ : nlohmann::json::array();
    if (!sanitized.is_array()) {
        sanitized = nlohmann::json::array();
    }

    auto session = manifest_store_->begin_asset_edit(info_->name, true);
    if (!session) {
        std::cerr << "[Section_SpawnGroups] Failed to open manifest session for '" << info_->name << "'\n";
        return false;
    }

    nlohmann::json& payload = session.data();
    if (!payload.is_object()) {
        payload = nlohmann::json::object();
    }
    payload["spawn_groups"] = sanitized;
    if (!session.commit()) {
        std::cerr << "[Section_SpawnGroups] Failed to commit spawn group payload for '" << info_->name << "'\n";
        session.cancel();
        return false;
    }

    manifest_store_->flush();
    groups_ = std::move(sanitized);
    if (info_) {
        info_->set_spawn_groups_payload(groups_);
        info_->set_spawn_groups(groups_);
    }
    return true;
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
    entry["position"] = "Exact";
    devmode::spawn::ensure_spawn_group_entry_defaults(entry, "New Spawn");
    const std::string new_id = entry["spawn_id"].get<std::string>();
    groups_.push_back(entry);
    renumber_priorities();
    (void)save_to_file();
    schedule_rebuild();
    if (list_) {
        SDL_Point anchor = editor_anchor_point();
        list_->request_open_spawn_group(new_id, anchor.x, anchor.y);
    }
    notify_spawn_config_listeners(groups_.back());
}

void Section_SpawnGroups::delete_spawn_group(const std::string& id) {
    if (!groups_.is_array()) return;
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [&](nlohmann::json& e){
        return e.is_object() && e.value("spawn_id", std::string{}) == id;
    }), groups_.end());
    renumber_priorities();
    (void)save_to_file();
    schedule_rebuild();
    notify_spawn_group_removed(id);
}

void Section_SpawnGroups::reorder_spawn_group(const std::string& id, size_t new_index) {
    if (!groups_.is_array() || groups_.empty()) return;
    const int current_index = index_of(id);
    if (current_index < 0) return;
    const size_t bounded_index = std::min(new_index, groups_.size() - 1);
    const size_t from = static_cast<size_t>(current_index);
    if (from == bounded_index) return;

    nlohmann::json entry = std::move(groups_[from]);
    const auto erase_pos = groups_.begin() + static_cast<nlohmann::json::difference_type>(from);
    groups_.erase(erase_pos);
    size_t insert_index = std::min(bounded_index, groups_.size());
    const auto insert_pos = groups_.begin() + static_cast<nlohmann::json::difference_type>(insert_index);
    groups_.insert(insert_pos, std::move(entry));

    renumber_priorities();
    (void)save_to_file();
    schedule_rebuild();
    const int idx = index_of(id);
    if (idx >= 0) {
        notify_spawn_config_listeners(groups_.at(static_cast<std::size_t>(idx)));
    }
}

SDL_Point Section_SpawnGroups::editor_anchor_point() const {

    SDL_Rect r = rect();
    int x = std::max(16, r.x - 320);
    int y = std::max(16, r.y + r.h / 4);
    return SDL_Point{x, y};
}

void Section_SpawnGroups::schedule_rebuild() {
    if (rebuilding_) {
        rebuild_requested_ = true;
        return;
    }

    rebuild_requested_ = false;
    build();
}

void Section_SpawnGroups::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
}

void Section_SpawnGroups::set_spawn_config_listener(std::function<void(const nlohmann::json&)> listener) {
    spawn_config_listener_ = std::move(listener);
}

void Section_SpawnGroups::set_spawn_group_removed_listener(std::function<void(const std::string&)> listener) {
    spawn_group_removed_listener_ = std::move(listener);
}

void Section_SpawnGroups::notify_spawn_config_listeners(const nlohmann::json& entry) {
    if (spawn_config_listener_) {
        spawn_config_listener_(entry);
    }
}

void Section_SpawnGroups::notify_spawn_group_removed(const std::string& id) {
    if (spawn_group_removed_listener_) {
        spawn_group_removed_listener_(id);
    }
}

