#include "spawn_groups_config.hpp"
#include "spawn_group_config_ui.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "FloatingDockableManager.hpp"

#include <algorithm>

namespace {
constexpr int kStandaloneWidth = 1920;
constexpr int kStandaloneHeight = 1080;
constexpr int kSpawnGroupsMaxHeight = 560;

nlohmann::json normalize_spawn_assets(const nlohmann::json& assets) {
    if (assets.is_array()) {
        return assets;
    }
    return nlohmann::json::array();
}
}

SpawnGroupsConfig::SpawnGroupsConfig(bool floatable)
    : DockableCollapsible("Spawn Groups", floatable, 32, 32),
      floatable_mode_(floatable) {
    set_expanded(true);
    set_visible(false);
    if (!floatable_mode_) {
        set_show_header(false);
        set_scroll_enabled(true);
    } else {
        set_work_area(SDL_Rect{0, 0, 0, 0});
    }
    set_cell_width(120);
    set_available_height_override(kSpawnGroupsMaxHeight);
    set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    DockableCollapsible::set_on_close([this]() {
        close_all();
    });
}

bool SpawnGroupsConfig::should_rebuild_with(const nlohmann::json& normalized_assets) const {
    if (!is_visible()) {
        return true;
    }
    if (!entries_loaded_) {
        return true;
    }
    if (last_loaded_source_ != &temp_assets_) {
        return true;
    }
    return loaded_snapshot_ != normalized_assets;
}

void SpawnGroupsConfig::open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close) {
    if (!floatable_mode_) {
        return;
    }
    on_close_ = std::move(on_close);
    FloatingDockableManager::instance().open_floating(
        "Spawn Groups", this, [this]() {
            this->set_visible(false);
        });

    nlohmann::json normalized = normalize_spawn_assets(assets);
    const bool was_visible = is_visible();
    if (!should_rebuild_with(normalized)) {
        set_visible(true);
        if (!was_visible) set_expanded(true);
        Input dummy;
        update(dummy, screen_w_, screen_h_);
        return;
    }

    temp_assets_ = normalized;
    load(temp_assets_, [](){});
    if (!b_done_) {
        b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 80, DMButton::height());
        b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() {
            if (on_close_) on_close_(to_json());
            close();
        });
    }
    DockableCollapsible::Rows rows;
    append_rows(rows);
    if (b_done_w_) rows.push_back({ b_done_w_.get() });
    set_rows(rows);
    set_visible(true);
    if (!was_visible) set_expanded(true);
    Input dummy;
    update(dummy, screen_w_, screen_h_);
}

void SpawnGroupsConfig::close() { DockableCollapsible::set_visible(false); }

bool SpawnGroupsConfig::visible() const { return is_visible(); }

void SpawnGroupsConfig::set_position(int x, int y) { DockableCollapsible::set_position(x, y); }

void SpawnGroupsConfig::set_screen_dimensions(int width, int height) {
    if (width > 0) {
        screen_w_ = width;
    }
    if (height > 0) {
        screen_h_ = height;
    }
    set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    for (auto& e : entries_) {
        if (e.cfg) {
            e.cfg->set_screen_dimensions(screen_w_, screen_h_);
        }
    }
}

void SpawnGroupsConfig::load(nlohmann::json& assets,
                        std::function<void()> on_change,
                        std::function<void(const nlohmann::json&, const SpawnGroupsConfigPanel::ChangeSummary&)> on_entry_change,
                        ConfigureEntryCallback configure_entry) {
    nlohmann::json normalized = normalize_spawn_assets(assets);
    const bool source_changed = (last_loaded_source_ != &assets);
    const bool content_changed = (loaded_snapshot_ != normalized);

    assets_json_ = &assets;
    on_change_ = std::move(on_change);
    on_entry_change_ = std::move(on_entry_change);
    configure_entry_ = std::move(configure_entry);
    last_loaded_source_ = &assets;

    if (entries_loaded_ && !source_changed && !content_changed) {
        if (configure_entry_) {
            for (auto& entry : entries_) {
                if (entry.cfg && entry.json) {
                    configure_entry_(*entry.cfg, *entry.json);
                }
            }
        }
        return;
    }

    entries_.clear();
    if (!assets.is_array()) {
        loaded_snapshot_ = std::move(normalized);
        entries_loaded_ = true;
        return;
    }

    for (auto& it : assets) {
        Entry e;
        e.id = it.value("spawn_id", std::string{});
        if (e.id.empty()) {
            if (it.contains("name") && it["name"].is_string()) e.id = it["name"].get<std::string>();
            else if (it.contains("tag") && it["tag"].is_string()) e.id = "#" + it["tag"].get<std::string>();
        }
        if (e.id.empty()) {
            e.id = std::string("Spawn Group ") + std::to_string(entries_.size() + 1);
        }
        e.json = &it;
        e.cfg = std::make_unique<SpawnGroupsConfigPanel>();
        e.cfg->set_screen_dimensions(screen_w_, screen_h_);
        e.cfg->load(it);
        if (configure_entry_) {
            configure_entry_(*e.cfg, it);
        }
        e.btn = std::make_unique<DMButton>(e.id, &DMStyles::HeaderButton(), 100, DMButton::height());
        std::string id_copy = e.id;
        e.btn_w = std::make_unique<ButtonWidget>(e.btn.get(), [this, id_copy]() {
            this->request_open_spawn_group(id_copy, anchor_x_, anchor_y_);
        });
        entries_.push_back(std::move(e));
    }
    loaded_snapshot_ = std::move(normalized);
    entries_loaded_ = true;
}

void SpawnGroupsConfig::append_rows(DockableCollapsible::Rows& rows) {
    for (auto& e : entries_) {
        rows.push_back({ e.btn_w.get() });
    }
}

void SpawnGroupsConfig::set_anchor(int x, int y) {
    const int dx = x - anchor_x_;
    const int dy = y - anchor_y_;
    anchor_x_ = x;
    anchor_y_ = y;
    if (dx == 0 && dy == 0) {
        return;
    }
    for (auto& e : entries_) {
        if (!e.cfg || !e.cfg->visible()) {
            continue;
        }
        SDL_Point pos = e.cfg->position();
        e.cfg->set_position(pos.x + dx, pos.y + dy);
    }
}

void SpawnGroupsConfig::update(const Input& input, int screen_w, int screen_h) {
    if (screen_w > 0) {
        screen_w_ = screen_w;
    }
    if (screen_h > 0) {
        screen_h_ = screen_h;
    }
    set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    if (pending_open_) {
        if (entries_loaded_) {
            auto request = *pending_open_;
            auto it = std::find_if(entries_.begin(), entries_.end(),
                                   [&request](const Entry& entry) { return entry.id == request.id; });
            if (it != entries_.end()) {
                pending_open_.reset();
                open_spawn_group(request.id, request.x, request.y);
            } else {
                pending_open_.reset();
            }
        }
    }
    if (is_visible()) {
        DockableCollapsible::update(input, screen_w_, screen_h_);
    }
    for (auto& e : entries_) {
        if (e.cfg) {
            e.cfg->set_screen_dimensions(screen_w_, screen_h_);
            e.cfg->update(input, screen_w_, screen_h_);
        }
    }
}

bool SpawnGroupsConfig::handle_event(const SDL_Event& ev) {
    bool used = false;
    if (is_visible()) used |= DockableCollapsible::handle_event(ev);
    for (auto& e : entries_) {
        if (e.cfg && e.cfg->handle_event(ev)) {
            if (e.json) *e.json = e.cfg->to_json();
            if (on_entry_change_) {
                auto summary = e.cfg->consume_change_summary();
                if (summary.method_changed || summary.quantity_changed) {
                    on_entry_change_(*e.json, summary);
                }
            } else {
                e.cfg->consume_change_summary();
            }
            if (on_change_) on_change_();
            used = true;
        }
    }
    return used;
}

void SpawnGroupsConfig::render(SDL_Renderer* r) const {
    if (is_visible()) DockableCollapsible::render(r);
    for (const auto& e : entries_) {
        if (e.cfg) e.cfg->render(r);
    }
}

void SpawnGroupsConfig::open_spawn_group(const std::string& id, int x, int y) {
    close_all();
    for (auto& e : entries_) {
        if (e.id == id) {
            open_entry(e, x, y);
            break;
        }
    }
}

void SpawnGroupsConfig::request_open_spawn_group(const std::string& id, int x, int y) {
    pending_open_ = PendingOpenRequest{id, x, y};
}

void SpawnGroupsConfig::close_all() {
    for (auto& e : entries_) {
        if (e.cfg) e.cfg->close();
    }
}

void SpawnGroupsConfig::open_entry(Entry& entry, int x, int y) {
    if (!entry.cfg) {
        return;
    }
    entry.cfg->set_screen_dimensions(screen_w_, screen_h_);
    entry.cfg->set_position(x, y);
    SpawnGroupsConfigPanel* cfg_ptr = entry.cfg.get();
    nlohmann::json* json_ptr = entry.json;
    entry.cfg->open(json_ptr ? *json_ptr : nlohmann::json::object(), [this, cfg_ptr, json_ptr](const nlohmann::json& updated) {
        if (json_ptr) *json_ptr = updated;
        if (on_entry_change_ && json_ptr) {
            auto summary = cfg_ptr->consume_change_summary();
            if (summary.method_changed || summary.quantity_changed) {
                on_entry_change_(*json_ptr, summary);
            }
        } else {
            cfg_ptr->consume_change_summary();
        }
        if (on_change_) on_change_();
    });
}

std::optional<SpawnGroupsConfig::OpenSpawnGroupState> SpawnGroupsConfig::capture_open_spawn_group() const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!e.cfg) continue;
        if (e.cfg->visible()) {
            SpawnGroupsConfig::OpenSpawnGroupState state;
            state.id = e.id;
            state.position = e.cfg->position();
            state.index = i;
            return state;
        }
    }
    return std::nullopt;
}

void SpawnGroupsConfig::restore_open_spawn_group(const OpenSpawnGroupState& state) {
    if (!state.id.empty()) {
        open_spawn_group(state.id, state.position.x, state.position.y);
        for (const auto& entry : entries_) {
            if (entry.cfg && entry.cfg->visible()) {
                return;
            }
        }
    }
    if (state.index < entries_.size()) {
        auto& entry = entries_[state.index];
        if (!entry.cfg) {
            return;
        }
        close_all();
        open_entry(entry, state.position.x, state.position.y);
    }
}

nlohmann::json SpawnGroupsConfig::to_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_) {
        if (e.json) arr.push_back(*e.json);
        else if (e.cfg) arr.push_back(e.cfg->to_json());
    }
    return arr;
}

bool SpawnGroupsConfig::is_open(const std::string& id) const {
    if (id.empty()) return false;
    for (const auto& e : entries_) {
        if (e.cfg && e.id == id && e.cfg->visible()) {
            return true;
        }
    }
    return false;
}

bool SpawnGroupsConfig::any_visible() const {
    if (is_visible()) {
        return true;
    }
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible()) return true;
    }
    return false;
}

bool SpawnGroupsConfig::is_point_inside(int x, int y) const {
    if (is_visible() && DockableCollapsible::is_point_inside(x, y)) return true;
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible() && e.cfg->is_point_inside(x, y)) return true;
    }
    return false;
}

