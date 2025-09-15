#include "assets_config.hpp"
#include "asset_config.hpp"
#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"

AssetsConfig::AssetsConfig() {}

void AssetsConfig::open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close) {
    on_close_ = std::move(on_close);
    load(assets);
    panel_ = std::make_unique<DockableCollapsible>("Assets", true, 32, 32);
    panel_->set_expanded(true);
    panel_->set_visible(true);
    b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 80, DMButton::height());
    b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() {
        if (on_close_) on_close_(to_json());
        close();
    });
    DockableCollapsible::Rows rows;
    append_rows(rows);
    rows.push_back({ b_done_w_.get() });
    panel_->set_cell_width(120);
    panel_->set_rows(rows);
    Input dummy; panel_->update(dummy, 1920, 1080);
}

void AssetsConfig::close() { if (panel_) panel_->set_visible(false); }

bool AssetsConfig::visible() const { return panel_ && panel_->is_visible(); }

void AssetsConfig::set_position(int x, int y) {
    if (panel_) panel_->set_position(x, y);
}

void AssetsConfig::load(const nlohmann::json& assets) {
    entries_.clear();
    if (!assets.is_array()) return;
    for (auto& it : assets) {
        Entry e;
        if (it.contains("name") && it["name"].is_string()) e.id = it["name"].get<std::string>();
        else if (it.contains("tag") && it["tag"].is_string()) e.id = "#" + it["tag"].get<std::string>();
        e.cfg = std::make_unique<AssetConfig>();
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
    anchor_x_ = x; anchor_y_ = y;
}

void AssetsConfig::update(const Input& input) {
    if (panel_ && panel_->is_visible()) panel_->update(input, 1920, 1080);
    for (auto& e : entries_) {
        if (e.cfg) e.cfg->update(input);
    }
}

bool AssetsConfig::handle_event(const SDL_Event& ev) {
    bool used = false;
    if (panel_ && panel_->is_visible()) used |= panel_->handle_event(ev);
    for (auto& e : entries_) {
        if (e.cfg && e.cfg->handle_event(ev)) used = true;
    }
    return used;
}

void AssetsConfig::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
    for (const auto& e : entries_) {
        if (e.cfg) e.cfg->render(r);
    }
}

void AssetsConfig::open_asset_config(const std::string& id, int x, int y) {
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

nlohmann::json AssetsConfig::to_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_) {
        if (e.cfg) arr.push_back(e.cfg->to_json());
    }
    return arr;
}

bool AssetsConfig::any_visible() const {
    for (const auto& e : entries_) {
        if (e.cfg && e.cfg->visible()) return true;
    }
    return false;
}
