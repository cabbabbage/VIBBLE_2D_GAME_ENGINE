#include "spawn_group_list.hpp"

#include <algorithm>
#include <sstream>

#include "FloatingDockableManager.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <SDL_ttf.h>

namespace {
constexpr int kStandaloneWidth = 1920;
constexpr int kStandaloneHeight = 1080;
constexpr int kSpawnGroupsMaxHeight = 560;

class SimpleLabel : public Widget {
public:
    explicit SimpleLabel(std::string text) : text_(std::move(text)) {}
    void set_text(std::string t) { text_ = std::move(t); }
    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return DMStyles::Label().font_size + DMSpacing::small_gap() * 2; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* r) const override {
        const DMLabelStyle& st = DMStyles::Label();
        TTF_Font* font = st.open_font();
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), st.color);
        if (!surf) { TTF_CloseFont(font); return; }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ rect_.x, rect_.y, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
        TTF_CloseFont(font);
    }
    bool wants_full_row() const override { return true; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
};

std::string build_summary_for(const nlohmann::json& entry, int display_index) {
    const std::string display = entry.value("display_name", entry.value("name", entry.value("spawn_id", std::string{"Spawn"})));
    std::string method = entry.value("position", std::string{"Unknown"});
    int min_q = entry.value("min_number", entry.value("max_number", 0));
    int max_q = entry.value("max_number", min_q);
    std::ostringstream ss;
    ss << display_index << ". " << display << " - " << method << " (" << min_q << "-" << max_q << ")";
    return ss.str();
}

nlohmann::json normalize_spawn_assets(const nlohmann::json& assets) {
    if (assets.is_array()) {
        return assets;
    }
    return nlohmann::json::array();
}
} // namespace

SpawnGroupList::SpawnGroupList(bool floatable)
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
    DockableCollapsible::set_on_close([this]() { close_all(); });
}

SpawnGroupList::~SpawnGroupList() = default;

bool SpawnGroupList::should_rebuild_with(const nlohmann::json& normalized_assets) const {
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

void SpawnGroupList::rebuild_list_rows(const nlohmann::json& groups) {
    rows_.clear();
    snapshot_ = groups;
    if (!groups.is_array()) {
        return;
    }
    int display_index = 1;
    for (const auto& entry : groups) {
        if (!entry.is_object()) { ++display_index; continue; }
        std::string spawn_id = entry.value("spawn_id", std::string{});
        if (spawn_id.empty()) { ++display_index; continue; }

        auto row = std::make_unique<RowWidgets>();
        row->id = spawn_id;
        row->label = std::make_unique<SimpleLabel>(build_summary_for(entry, display_index));

        if (intrinsic_callbacks_.on_edit || callbacks_.on_edit) {
            row->btn_edit = std::make_unique<DMButton>("✎", &DMStyles::HeaderButton(), 36, DMButton::height());
            row->w_edit = std::make_unique<ButtonWidget>(row->btn_edit.get(), [this, spawn_id]() {
                if (intrinsic_callbacks_.on_edit) intrinsic_callbacks_.on_edit(spawn_id);
                if (callbacks_.on_edit) callbacks_.on_edit(spawn_id);
            });
        }

        if (intrinsic_callbacks_.on_move_up || callbacks_.on_move_up) {
            row->btn_up = std::make_unique<DMButton>("▲", &DMStyles::ListButton(), 32, DMButton::height());
            row->w_up = std::make_unique<ButtonWidget>(row->btn_up.get(), [this, spawn_id]() {
                if (intrinsic_callbacks_.on_move_up) intrinsic_callbacks_.on_move_up(spawn_id);
                if (callbacks_.on_move_up) callbacks_.on_move_up(spawn_id);
            });
        }

        if (intrinsic_callbacks_.on_move_down || callbacks_.on_move_down) {
            row->btn_down = std::make_unique<DMButton>("▼", &DMStyles::ListButton(), 32, DMButton::height());
            row->w_down = std::make_unique<ButtonWidget>(row->btn_down.get(), [this, spawn_id]() {
                if (intrinsic_callbacks_.on_move_down) intrinsic_callbacks_.on_move_down(spawn_id);
                if (callbacks_.on_move_down) callbacks_.on_move_down(spawn_id);
            });
        }

        if (intrinsic_callbacks_.on_duplicate || callbacks_.on_duplicate) {
            row->btn_dup = std::make_unique<DMButton>("Duplicate", &DMStyles::HeaderButton(), 96, DMButton::height());
            row->w_dup = std::make_unique<ButtonWidget>(row->btn_dup.get(), [this, spawn_id]() {
                if (intrinsic_callbacks_.on_duplicate) intrinsic_callbacks_.on_duplicate(spawn_id);
                if (callbacks_.on_duplicate) callbacks_.on_duplicate(spawn_id);
            });
        }

        if (intrinsic_callbacks_.on_delete || callbacks_.on_delete) {
            row->btn_del = std::make_unique<DMButton>("🗑", &DMStyles::DeleteButton(), 36, DMButton::height());
            row->w_del = std::make_unique<ButtonWidget>(row->btn_del.get(), [this, spawn_id]() {
                if (intrinsic_callbacks_.on_delete) intrinsic_callbacks_.on_delete(spawn_id);
                if (callbacks_.on_delete) callbacks_.on_delete(spawn_id);
            });
        }

        rows_.push_back(std::move(row));
        ++display_index;
    }
}

void SpawnGroupList::rebuild_panel_rows() {
    Rows rows;
    append_rows(rows);
    if (b_done_w_) {
        rows.push_back({ b_done_w_.get() });
    }
    set_rows(rows);
}

void SpawnGroupList::open_entry(Entry& entry, int x, int y) {
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

void SpawnGroupList::open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close) {
    if (!floatable_mode_) {
        return;
    }
    on_close_ = std::move(on_close);
    FloatingDockableManager::instance().open_floating(
        "Spawn Groups", this, [this]() { this->set_visible(false); });

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
    intrinsic_callbacks_.on_edit = [this](const std::string& id) { this->request_open_spawn_group(id, anchor_x_, anchor_y_); };
    intrinsic_callbacks_.on_duplicate = {};
    intrinsic_callbacks_.on_delete = {};
    intrinsic_callbacks_.on_move_up = {};
    intrinsic_callbacks_.on_move_down = {};

    load(temp_assets_, [](){});
    rebuild_list_rows(temp_assets_);
    if (!b_done_) {
        b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 80, DMButton::height());
        b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() {
            if (on_close_) on_close_(to_json());
            close();
        });
    }
    rebuild_panel_rows();
    set_visible(true);
    if (!was_visible) set_expanded(true);
    Input dummy;
    update(dummy, screen_w_, screen_h_);
}

void SpawnGroupList::close() { DockableCollapsible::set_visible(false); }

bool SpawnGroupList::visible() const { return is_visible(); }

void SpawnGroupList::set_position(int x, int y) { DockableCollapsible::set_position(x, y); }

void SpawnGroupList::set_screen_dimensions(int width, int height) {
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

void SpawnGroupList::load(nlohmann::json& assets,
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

    intrinsic_callbacks_.on_edit = [this](const std::string& id) { this->request_open_spawn_group(id, anchor_x_, anchor_y_); };
    intrinsic_callbacks_.on_duplicate = {};
    intrinsic_callbacks_.on_delete = {};
    intrinsic_callbacks_.on_move_up = {};
    intrinsic_callbacks_.on_move_down = {};

    if (entries_loaded_ && !source_changed && !content_changed) {
        if (configure_entry_) {
            for (auto& entry : entries_) {
                if (entry.cfg && entry.json) {
                    configure_entry_(*entry.cfg, *entry.json);
                }
            }
        }
        rebuild_list_rows(loaded_snapshot_);
        rebuild_panel_rows();
        return;
    }

    entries_.clear();
    if (!assets.is_array()) {
        loaded_snapshot_ = std::move(normalized);
        entries_loaded_ = true;
        rebuild_list_rows(loaded_snapshot_);
        rebuild_panel_rows();
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
        entries_.push_back(std::move(e));
    }
    loaded_snapshot_ = std::move(normalized);
    entries_loaded_ = true;
    rebuild_list_rows(loaded_snapshot_);
    rebuild_panel_rows();
}

void SpawnGroupList::load(const nlohmann::json& groups) {
    intrinsic_callbacks_ = {};
    rebuild_list_rows(groups);
}

void SpawnGroupList::append_rows(Rows& rows) {
    for (auto& e : rows_) {
        DockableCollapsible::Row row;
        if (e->label) row.push_back(e->label.get());
        if (e->w_edit) row.push_back(e->w_edit.get());
        if (e->w_up)   row.push_back(e->w_up.get());
        if (e->w_down) row.push_back(e->w_down.get());
        if (e->w_dup)  row.push_back(e->w_dup.get());
        if (e->w_del)  row.push_back(e->w_del.get());
        if (!row.empty()) rows.push_back(std::move(row));
    }
}

void SpawnGroupList::set_callbacks(Callbacks cb) {
    callbacks_ = std::move(cb);
    rebuild_list_rows(snapshot_);
    if (floatable_mode_) {
        rebuild_panel_rows();
    }
}

void SpawnGroupList::set_anchor(int x, int y) {
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

void SpawnGroupList::open_spawn_group(const std::string& id, int x, int y) {
    close_all();
    for (auto& e : entries_) {
        if (e.id == id) {
            open_entry(e, x, y);
            break;
        }
    }
}

void SpawnGroupList::request_open_spawn_group(const std::string& id, int x, int y) {
    pending_open_ = PendingOpenRequest{id, x, y};
}

void SpawnGroupList::close_all() {
    for (auto& e : entries_) {
        if (e.cfg) e.cfg->close();
    }
}

bool SpawnGroupList::is_open(const std::string& id) const {
    if (id.empty()) return false;
    for (const auto& e : entries_) {
        if (e.cfg && e.id == id && e.cfg->visible()) {
            return true;
        }
    }
    return false;
}

std::optional<SpawnGroupList::OpenSpawnGroupState> SpawnGroupList::capture_open_spawn_group() const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!e.cfg) continue;
        if (e.cfg->visible()) {
            SpawnGroupList::OpenSpawnGroupState state;
            state.id = e.id;
            state.position = e.cfg->position();
            state.index = i;
            return state;
        }
    }
    return std::nullopt;
}

void SpawnGroupList::restore_open_spawn_group(const OpenSpawnGroupState& state) {
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

nlohmann::json SpawnGroupList::to_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_) {
        if (e.json) arr.push_back(*e.json);
        else if (e.cfg) arr.push_back(e.cfg->to_json());
    }
    return arr;
}

bool SpawnGroupList::any_visible() const {
    if (is_visible()) {
        return true;
    }
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible()) return true;
    }
    return false;
}

bool SpawnGroupList::is_point_inside(int x, int y) const {
    if (is_visible() && DockableCollapsible::is_point_inside(x, y)) return true;
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible() && e.cfg->is_point_inside(x, y)) return true;
    }
    return false;
}

void SpawnGroupList::update(const Input& input, int screen_w, int screen_h) {
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

bool SpawnGroupList::handle_event(const SDL_Event& ev) {
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

void SpawnGroupList::render(SDL_Renderer* r) const {
    if (is_visible()) DockableCollapsible::render(r);
    for (const auto& e : entries_) {
        if (e.cfg) e.cfg->render(r);
    }
}

