#include "map_assets_panel.hpp"

#include "FloatingDockableManager.hpp"
#include "assets_config.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <SDL.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

SDL_Color lighten(SDL_Color color, int delta) {
    auto clamp = [](int v) { return static_cast<Uint8>(std::clamp(v, 0, 255)); };
    return SDL_Color{clamp(color.r + delta), clamp(color.g + delta), clamp(color.b + delta), color.a};
}

class SimpleLabel : public Widget {
public:
    explicit SimpleLabel(std::string text) : text_(std::move(text)) {}

    void set_text(const std::string& text) { text_ = text; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int /*w*/) const override {
        const DMLabelStyle& st = DMStyles::Label();
        return st.font_size + DMSpacing::small_gap() * 2;
    }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const DMLabelStyle& st = DMStyles::Label();
        TTF_Font* font = st.open_font();
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), st.color);
        if (!surf) {
            TTF_CloseFont(font);
            return;
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        const int padding = DMSpacing::small_gap();
        SDL_Rect bg{rect_.x, rect_.y, rect_.w, height_for_width(rect_.w)};
        bg.w = std::max(bg.w, surf->w + padding * 2);
        SDL_Color base = DMStyles::PanelBG();
        SDL_Color accent = lighten(base, 18);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, 220);
        SDL_RenderFillRect(renderer, &bg);
        SDL_Color border = DMStyles::Border();
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &bg);
        if (tex) {
            SDL_Rect dst{rect_.x + padding, rect_.y + (bg.h - surf->h) / 2, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
        TTF_CloseFont(font);
    }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
};

class DividerWidget : public Widget {
public:
    DividerWidget() = default;

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return std::max(2, DMSpacing::small_gap()); }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        SDL_Color border = DMStyles::Border();
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        int y = rect_.y + rect_.h / 2;
        SDL_RenderDrawLine(renderer, rect_.x, y, rect_.x + rect_.w, y);
    }

private:
    SDL_Rect rect_{0, 0, 0, 0};
};

constexpr int kAnchorOffset = 16;
}

using nlohmann::json;

MapAssetsPanel::MapAssetsPanel(int x, int y)
    : DockableCollapsible("Map Assets", true, x, y) {
    set_expanded(true);
    set_visible(false);
    set_padding(DMSpacing::panel_padding());
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::item_gap());
    set_cell_width(260);
    map_assets_cfg_ = std::make_unique<AssetsConfig>();
    boundary_cfg_ = std::make_unique<AssetsConfig>();
    inherits_checkbox_ = std::make_unique<DMCheckbox>("Inherit Map Assets", false);
    inherits_widget_ = std::make_unique<CheckboxWidget>(inherits_checkbox_.get());
    save_button_ = std::make_unique<DMButton>("Save", &DMStyles::CreateButton(), 100, DMButton::height());
    save_button_widget_ = std::make_unique<ButtonWidget>(save_button_.get(), [this]() { perform_save(); });
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::HeaderButton(), 100, DMButton::height());
    reload_button_widget_ = std::make_unique<ButtonWidget>(reload_button_.get(), [this]() { reload_from_disk(); });
    close_button_ = std::make_unique<DMButton>("Close", &DMStyles::HeaderButton(), 100, DMButton::height());
    close_button_widget_ = std::make_unique<ButtonWidget>(close_button_.get(), [this]() { this->close(); });
    map_label_ = std::make_unique<SimpleLabel>("Map-wide Spawn Groups");
    boundary_label_ = std::make_unique<SimpleLabel>("Boundary Spawn Groups");
    map_divider_ = std::make_unique<DividerWidget>();
    footer_divider_ = std::make_unique<DividerWidget>();
}

MapAssetsPanel::~MapAssetsPanel() = default;

void MapAssetsPanel::set_map_info(json* map_info, const std::string& map_path) {
    map_info_ = map_info;
    map_path_ = map_path;
    ensure_configs_loaded();
    rebuild_rows();
}

void MapAssetsPanel::set_on_save(SaveCallback cb) {
    on_save_ = std::move(cb);
}

void MapAssetsPanel::open() {
    if (!map_info_) return;
    rebuild_rows();
    FloatingDockableManager::instance().open_floating("Map Assets Config", this, [this]() { this->close(); });
    set_visible(true);
    set_expanded(true);
}

void MapAssetsPanel::close() {
    set_visible(false);
    if (map_assets_cfg_) map_assets_cfg_->close_all_asset_configs();
    if (boundary_cfg_) boundary_cfg_->close_all_asset_configs();
}

void MapAssetsPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_visible()) return;
    DockableCollapsible::update(input, screen_w, screen_h);
    SDL_Rect r = rect();
    int anchor_x = r.x + r.w + kAnchorOffset;
    int anchor_y = r.y;
    if (map_assets_cfg_) map_assets_cfg_->set_anchor(anchor_x, anchor_y);
    if (boundary_cfg_) boundary_cfg_->set_anchor(anchor_x, anchor_y);
    if (map_assets_cfg_) map_assets_cfg_->update(input, screen_w, screen_h);
    if (boundary_cfg_) boundary_cfg_->update(input, screen_w, screen_h);
}

bool MapAssetsPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;
    bool used = DockableCollapsible::handle_event(e);
    if (inherits_widget_ && inherits_widget_->handle_event(e)) {
        handle_inherits_checkbox_change();
        used = true;
    }
    if (map_assets_cfg_ && map_assets_cfg_->handle_event(e)) used = true;
    if (boundary_cfg_ && boundary_cfg_->handle_event(e)) used = true;
    return used;
}

void MapAssetsPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
    if (map_assets_cfg_) map_assets_cfg_->render(renderer);
    if (boundary_cfg_) boundary_cfg_->render(renderer);
}

bool MapAssetsPanel::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    if (DockableCollapsible::is_point_inside(x, y)) return true;
    if (map_assets_cfg_ && map_assets_cfg_->is_point_inside(x, y)) return true;
    if (boundary_cfg_ && boundary_cfg_->is_point_inside(x, y)) return true;
    return false;
}

void MapAssetsPanel::rebuild_rows() {
    DockableCollapsible::Rows rows;
    if (!map_info_) {
        set_rows(rows);
        return;
    }

    ensure_configs_loaded();
    refresh_checkbox_from_json();

    if (map_label_) rows.push_back({ map_label_.get() });
    if (map_assets_cfg_) map_assets_cfg_->append_rows(rows);

    if (map_divider_) rows.push_back({ map_divider_.get() });

    if (boundary_label_) rows.push_back({ boundary_label_.get() });
    if (inherits_widget_) rows.push_back({ inherits_widget_.get() });
    if (boundary_cfg_) boundary_cfg_->append_rows(rows);

    if (footer_divider_) rows.push_back({ footer_divider_.get() });

    DockableCollapsible::Row actions;
    if (save_button_widget_) actions.push_back(save_button_widget_.get());
    if (reload_button_widget_) actions.push_back(reload_button_widget_.get());
    if (close_button_widget_) actions.push_back(close_button_widget_.get());
    if (!actions.empty()) rows.push_back(actions);

    set_rows(rows);
    mark_clean();
}

void MapAssetsPanel::ensure_configs_loaded() {
    if (!map_info_) return;
    if (!map_assets_cfg_) map_assets_cfg_ = std::make_unique<AssetsConfig>();
    if (!boundary_cfg_) boundary_cfg_ = std::make_unique<AssetsConfig>();

    auto on_change = [this]() { mark_dirty(); };
    json& assets_root = ensure_map_assets();
    bool created_assets_default = ensure_at_least_one_spawn_group(assets_root);
    json& assets_array = ensure_spawn_groups(assets_root);
    map_assets_cfg_->load(assets_array, on_change);

    json& boundary_root = ensure_map_boundary();
    bool created_boundary_default = ensure_at_least_one_spawn_group(boundary_root);
    json& boundary_array = ensure_spawn_groups(boundary_root);
    boundary_cfg_->load(boundary_array, on_change);

    if (created_assets_default || created_boundary_default) {
        mark_dirty();
    }
}

json& MapAssetsPanel::ensure_map_assets() {
    if (!map_info_->contains("map_assets_data") || !(*map_info_)["map_assets_data"].is_object()) {
        (*map_info_)["map_assets_data"] = json::object();
    }
    return (*map_info_)["map_assets_data"];
}

json& MapAssetsPanel::ensure_map_boundary() {
    if (!map_info_->contains("map_boundary_data") || !(*map_info_)["map_boundary_data"].is_object()) {
        (*map_info_)["map_boundary_data"] = json::object();
    }
    json& boundary = (*map_info_)["map_boundary_data"];
    if (!boundary.contains("inherits_map_assets")) {
        boundary["inherits_map_assets"] = false;
    }
    return boundary;
}

json& MapAssetsPanel::ensure_spawn_groups(json& root) {
    if (root.contains("spawn_groups") && root["spawn_groups"].is_array()) {
        return root["spawn_groups"];
    }
    if (root.contains("assets") && root["assets"].is_array()) {
        root["spawn_groups"] = root["assets"];
        root.erase("assets");
        return root["spawn_groups"];
    }
    root["spawn_groups"] = json::array();
    return root["spawn_groups"];
}

bool MapAssetsPanel::ensure_at_least_one_spawn_group(json& root) {
    // Ensure the array exists (and migrate legacy key if present)
    json& arr = ensure_spawn_groups(root);
    if (!arr.is_array() || !arr.empty()) return false;

    // Heuristic: boundary root has the inherits flag
    const bool is_boundary = root.contains("inherits_map_assets");

    json entry = json::object();
    entry["display_name"] = is_boundary ? "batch_map_boundary" : "batch_map_assets";
    entry["position"] = "Random";
    entry["min_number"] = 1;
    entry["max_number"] = 1;
    entry["check_overlap"] = false;
    entry["enforce_spacing"] = false;
    entry["candidates"] = json::array();

    arr.push_back(std::move(entry));
    return true;
}

void MapAssetsPanel::refresh_checkbox_from_json() {
    if (!inherits_checkbox_) return;
    json& boundary = ensure_map_boundary();
    bool value = boundary.value("inherits_map_assets", false);
    inherits_checkbox_->set_value(value);
}

void MapAssetsPanel::handle_inherits_checkbox_change() {
    if (!map_info_ || !inherits_checkbox_) return;
    json& boundary = ensure_map_boundary();
    bool value = inherits_checkbox_->value();
    boundary["inherits_map_assets"] = value;
    mark_dirty();
}

void MapAssetsPanel::mark_dirty() {
    if (dirty_) return;
    dirty_ = true;
    if (save_button_) save_button_->set_text("Save*");
}

void MapAssetsPanel::mark_clean() {
    dirty_ = false;
    if (save_button_) save_button_->set_text("Save");
}

bool MapAssetsPanel::perform_save() {
    bool ok = false;
    if (on_save_) {
        ok = on_save_();
    } else {
        ok = save_to_disk();
    }
    if (ok) {
        mark_clean();
    }
    return ok;
}

bool MapAssetsPanel::save_to_disk() const {
    if (!map_info_) return false;
    if (map_path_.empty()) return false;
    std::ofstream out(map_path_ + "/map_info.json");
    if (!out) {
        std::cerr << "[MapAssetsPanel] Failed to open map_info.json for writing\n";
        return false;
    }
    try {
        out << map_info_->dump(2);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapAssetsPanel] Failed to serialize map_info.json: " << ex.what() << "\n";
        return false;
    }
}

bool MapAssetsPanel::reload_from_disk() {
    if (!map_info_ || map_path_.empty()) return false;
    std::ifstream in(map_path_ + "/map_info.json");
    if (!in) {
        std::cerr << "[MapAssetsPanel] Failed to open map_info.json for reload\n";
        return false;
    }
    try {
        json fresh;
        in >> fresh;
        if (!fresh.is_object()) {
            std::cerr << "[MapAssetsPanel] map_info.json reload produced non-object\n";
            return false;
        }
        *map_info_ = std::move(fresh);
        ensure_configs_loaded();
        rebuild_rows();
        mark_clean();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[MapAssetsPanel] Failed to reload map_info.json: " << ex.what() << "\n";
        return false;
    }
}

