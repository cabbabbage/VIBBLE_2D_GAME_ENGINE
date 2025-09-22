#include "assets_config.hpp"
#include "asset_config_ui.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"

namespace {
constexpr int kStandaloneWidth = 1920;
constexpr int kStandaloneHeight = 1080;
}

AssetsConfig::AssetsConfig()
    : DockableCollapsible("Assets", true, 32, 32) {
    set_expanded(true);
    set_visible(false);
    set_cell_width(120);
}

void AssetsConfig::open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close) {
    on_close_ = std::move(on_close);
    // make copy for standalone editing
    temp_assets_ = assets;
    load(temp_assets_, [](){}, {});
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
    set_expanded(true);
    Input dummy;
    update(dummy, kStandaloneWidth, kStandaloneHeight);
}

void AssetsConfig::close() { set_visible(false); }

bool AssetsConfig::visible() const { return is_visible(); }

void AssetsConfig::set_position(int x, int y) { DockableCollapsible::set_position(x, y); }

void AssetsConfig::load(nlohmann::json& assets,
                        std::function<void()> on_change,
                        std::function<void(const nlohmann::json&, const AssetConfigUI::ChangeSummary&)> on_entry_change) {
    entries_.clear();
    assets_json_ = &assets;
    on_change_ = std::move(on_change);
    on_entry_change_ = std::move(on_entry_change);
    if (!assets.is_array()) return;
    for (auto& it : assets) {
        Entry e;
        e.id = it.value("spawn_id", std::string{});
        if (e.id.empty()) {
            if (it.contains("name") && it["name"].is_string()) e.id = it["name"].get<std::string>();
            else if (it.contains("tag") && it["tag"].is_string()) e.id = "#" + it["tag"].get<std::string>();
        }
        e.json = &it;
        e.cfg = std::make_unique<AssetConfigUI>();
        e.cfg->load(it);
        e.btn = std::make_unique<DMButton>(e.id, &DMStyles::HeaderButton(), 100, DMButton::height());
        auto* cfg_ptr = e.cfg.get();
        e.btn_w = std::make_unique<ButtonWidget>(e.btn.get(), [this, cfg_ptr]() {
            if (cfg_ptr) {
                cfg_ptr->set_position(anchor_x_, anchor_y_);
                cfg_ptr->open_panel();
            }
        });
        entries_.push_back(std::move(e));
    }
}

void AssetsConfig::append_rows(DockableCollapsible::Rows& rows) {
    for (auto& e : entries_) {
        rows.push_back({ e.btn_w.get() });
    }
}

void AssetsConfig::set_anchor(int x, int y) {
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

void AssetsConfig::update(const Input& input, int screen_w, int screen_h) {
    if (is_visible()) {
        DockableCollapsible::update(input, screen_w, screen_h);
    }
    for (auto& e : entries_) {
        if (e.cfg) e.cfg->update(input);
    }
}

bool AssetsConfig::handle_event(const SDL_Event& ev) {
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

void AssetsConfig::render(SDL_Renderer* r) const {
    if (is_visible()) DockableCollapsible::render(r);
    for (const auto& e : entries_) {
        if (e.cfg) e.cfg->render(r);
    }
}

void AssetsConfig::open_asset_config(const std::string& id, int x, int y) {
    close_all_asset_configs();
    for (auto& e : entries_) {
        if (e.id == id) {
            e.cfg->set_position(x, y);
            e.cfg->open_panel();
            break;
        }
    }
}

void AssetsConfig::close_all_asset_configs() {
    for (auto& e : entries_) {
        if (e.cfg) e.cfg->close();
    }
}

std::optional<AssetsConfig::OpenConfigState> AssetsConfig::capture_open_config() const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!e.cfg) continue;
        if (e.cfg->visible()) {
            AssetsConfig::OpenConfigState state;
            state.id = e.id;
            state.position = e.cfg->position();
            state.index = i;
            return state;
        }
    }
    return std::nullopt;
}

void AssetsConfig::restore_open_config(const OpenConfigState& state) {
    if (!state.id.empty()) {
        open_asset_config(state.id, state.position.x, state.position.y);
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
        close_all_asset_configs();
        entry.cfg->set_position(state.position.x, state.position.y);
        entry.cfg->open_panel();
    }
}

nlohmann::json AssetsConfig::to_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_) {
        if (e.json) arr.push_back(*e.json);
        else if (e.cfg) arr.push_back(e.cfg->to_json());
    }
    return arr;
}

bool AssetsConfig::any_visible() const {
    if (is_visible()) {
        return true;
    }
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible()) return true;
    }
    return false;
}

bool AssetsConfig::is_point_inside(int x, int y) const {
    if (is_visible() && DockableCollapsible::is_point_inside(x, y)) return true;
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible() && e.cfg->is_point_inside(x, y)) return true;
    }
    return false;
}
