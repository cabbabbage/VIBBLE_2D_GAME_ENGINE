#pragma once

#include "../DockableCollapsible.hpp"
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "asset/asset_info.hpp"
#include "dev_mode/spawn_groups_config.hpp"
#include "dev_mode/asset_info_sections.hpp"

class AssetInfoUI;

class Section_ChildAssets : public DockableCollapsible {
public:
    Section_ChildAssets()
    : DockableCollapsible("Child Assets", false) {}

    void set_open_area_editor_callback(std::function<void(const std::string&)> cb) { open_area_editor_ = std::move(cb); }
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }

    void build() override {
        rows_.clear();
        rebuild_area_names();
        rebuild_rows_from_info();
        b_add_ = std::make_unique<DMButton>("Add Child Region", &DMStyles::CreateButton(), 220, DMButton::height());
        if (!apply_btn_) {
            apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 180, DMButton::height());
        }
    }

    void layout() override {
        int x = rect_.x + DMSpacing::panel_padding();
        int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
        int maxw = rect_.w - 2 * DMSpacing::panel_padding();

        if (info_) {
            std::vector<std::string> latest = collect_area_names();
            if (latest != area_names_) {
                area_names_ = std::move(latest);

                for (auto& r : rows_) {
                    int idx = r.dd_area ? r.dd_area->selected() : 0;
                    std::string sel = (idx >= 0 && idx < (int)r.options.size()) ? r.options[idx] : std::string{};
                    r.options = area_names_with_none();
                    r.dd_area = std::make_unique<DMDropdown>("Area", r.options, find_index(r.options, sel));
                }
            }
        }

        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (!r.lbl_) r.lbl_ = std::make_unique<DMButton>("Region " + std::to_string(i + 1), &DMStyles::HeaderButton(), 180, DMButton::height());
            r.lbl_->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();

            if (!r.dd_area) {
                r.options = area_names_with_none();
                r.dd_area = std::make_unique<DMDropdown>("Area", r.options, find_index(r.options, r.area_name));
            }
            r.dd_area->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMDropdown::height() });
            y += DMDropdown::height() + DMSpacing::item_gap();

            if (!r.s_z) r.s_z = std::make_unique<DMSlider>("Z Offset", -5000, 5000, r.z_offset);
            r.s_z->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMSlider::height() });
            y += DMSlider::height() + DMSpacing::item_gap();

            ensure_spawn_config(r);
            configure_spawn_config(r);
            const int spawn_top = y;
            layout_spawn_config(r, x, y, maxw);
            if (!r.spawn_rows.empty()) {
                const SDL_Point anchor = spawn_groups_anchor_at(spawn_top - scroll_);
                if (r.spawn_cfg) {
                    r.spawn_cfg->set_anchor(anchor.x, anchor.y);
                }
            }

            if (!r.b_edit_area) r.b_edit_area = std::make_unique<DMButton>("Edit Area", &DMStyles::ListButton(), 140, DMButton::height());
            r.b_edit_area->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();

            if (!r.b_delete)    r.b_delete    = std::make_unique<DMButton>("Delete", &DMStyles::ListButton(), 120, DMButton::height());
            r.b_delete->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
        }

        if (b_add_) {
            b_add_->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
        }
        if (apply_btn_) {
            apply_btn_->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
        }

        content_height_ = std::max(0, y - (rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap()));
        DockableCollapsible::layout();
    }

    void update(const Input& input, int screen_w, int screen_h) override {
        DockableCollapsible::update(input, screen_w, screen_h);
        for (auto& row : rows_) {
            if (row.spawn_cfg) {
                row.spawn_cfg->update(input, screen_w, screen_h);
            }
        }

        if (pending_open_area_ && open_area_editor_) {
            pending_open_area_ = false;
            std::string nm = pending_area_name_;
            pending_area_name_.clear();
            open_area_editor_(nm);
        }
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        bool spawn_used = false;
        for (auto& row : rows_) {
            if (row.spawn_cfg && row.spawn_cfg->handle_event(e)) {
                spawn_used = true;
            }
        }
        if (!info_ || !expanded_) return used || spawn_used;

        bool changed = false;

        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (r.lbl_ && r.lbl_->handle_event(e)) used = true;
            if (r.dd_area && r.dd_area->handle_event(e)) { r.area_name = safe_get_option(r.options, r.dd_area->selected()); changed = true; used = true; }
            if (r.s_z && r.s_z->handle_event(e)) { r.z_offset = r.s_z->value(); changed = true; used = true; }
            for (auto& widget_row : r.spawn_rows) {
                for (Widget* w : widget_row) {
                    if (w && w->handle_event(e)) {
                        used = true;
                    }
                }
            }
            if (r.b_edit_area && r.b_edit_area->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    std::string nm = r.area_name;
                    if (nm == "(none)") nm.clear();
                    if (nm.empty()) nm = std::string("child_area_") + std::to_string(i+1);

                    pending_area_name_ = nm;
                    pending_open_area_ = true;
                    used = true;
                }
            }
            if (r.b_delete && r.b_delete->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    if (r.spawn_cfg) {
                        r.spawn_cfg->close_all();
                    }
                    rows_.erase(rows_.begin() + i);
                    changed = true; used = true;
                    break;
                }
            }
        }

        if (b_add_ && b_add_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                Row r;
                r.options = area_names_with_none();
                r.area_name = "";
                r.z_offset = 0;
                r.dd_area = std::make_unique<DMDropdown>("Area", r.options, 0);
                r.s_z = std::make_unique<DMSlider>("Z Offset", -5000, 5000, 0);
                ensure_spawn_config(r);
                rows_.push_back(std::move(r));
                changed = true; used = true;
            }
        }

        if (apply_btn_ && apply_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (ui_) ui_->request_apply_section(AssetInfoSectionId::ChildAssets);
            }
            return true;
        }

        if (changed) {
            commit_to_info();
            (void)info_->update_info_json();
        }
        return used || changed || spawn_used;
    }

    void render_content(SDL_Renderer* r) const override {
        for (const auto& row : rows_) {
            if (row.lbl_)       row.lbl_->render(r);
            if (row.dd_area)    row.dd_area->render(r);
            if (row.s_z)        row.s_z->render(r);
            for (const auto& widget_row : row.spawn_rows) {
                for (Widget* w : widget_row) {
                    if (w) w->render(r);
                }
            }
            if (row.b_edit_area)row.b_edit_area->render(r);
            if (row.b_delete)   row.b_delete->render(r);
        }
        if (b_add_)        b_add_->render(r);
        if (apply_btn_)    apply_btn_->render(r);
    }

    void render(SDL_Renderer* r) const override {
        if (!is_visible()) {
            return;
        }
        DockableCollapsible::render(r);
        for (const auto& row : rows_) {
            if (row.spawn_cfg) {
                row.spawn_cfg->render(r);
            }
        }
    }

private:
    struct Row {

        std::string area_name;
        int z_offset = 0;
        std::string json_path;
        nlohmann::json assets = nlohmann::json::array();

        std::unique_ptr<DMButton>   lbl_;
        std::unique_ptr<DMDropdown> dd_area;
        std::unique_ptr<DMSlider>   s_z;
        std::unique_ptr<DMButton>   b_edit_area;
        std::unique_ptr<DMButton>   b_delete;
        std::unique_ptr<SpawnGroupsConfig> spawn_cfg;
        DockableCollapsible::Rows spawn_rows;

        std::vector<std::string> options;
};

private:
    SDL_Point spawn_groups_anchor_at(int screen_y) const {
        constexpr int kPanelContentWidth = 360;
        const int panel_w = 2 * DMSpacing::panel_padding() + kPanelContentWidth;
        const int gap = DMSpacing::section_gap();
        int x = rect_.x - panel_w - gap;
        if (x < 0) {
            x = rect_.x + rect_.w + gap;
        }
        int y = std::max(0, screen_y);
        return SDL_Point{ x, y };
    }

    void rebuild_rows_from_info() {
        rows_.clear();
        if (!info_) return;
        auto base_dir = parent_dir(info_->info_json_path());
        for (const auto& c : info_->children) {
            Row r;
            r.area_name = c.area_name;
            r.z_offset = c.z_offset;

            r.json_path = make_relative(base_dir, c.json_path);
            if (c.inline_assets.is_array()) {
                r.assets = c.inline_assets;
            }
            r.options = area_names_with_none();
            r.dd_area = std::make_unique<DMDropdown>("Area", r.options, find_index(r.options, r.area_name));
            r.s_z = std::make_unique<DMSlider>("Z Offset", -5000, 5000, r.z_offset);

            rows_.push_back(std::move(r));
        }
    }

    void ensure_spawn_config(Row& r) {
        if (!r.spawn_cfg) {
            r.spawn_cfg = std::make_unique<SpawnGroupsConfig>(false);
            r.spawn_cfg->set_visible(false);
            r.spawn_cfg->set_scroll_enabled(true);
        }
    }

    void configure_spawn_config(Row& r) {
        if (!r.spawn_cfg) return;
        r.spawn_cfg->load(r.assets, [this]() {
            commit_to_info();
            if (info_) (void)info_->update_info_json();
        });
        r.spawn_rows.clear();
        r.spawn_cfg->append_rows(r.spawn_rows);
    }

    void layout_spawn_config(Row& r, int x, int& y, int maxw) {
        const int gap = DMSpacing::item_gap();
        int curr_y = y;
        for (auto& widget_row : r.spawn_rows) {
            if (widget_row.empty()) continue;
            int row_height = 0;
            for (Widget* w : widget_row) {
                if (!w) continue;
                row_height = std::max(row_height, w->height_for_width(maxw));
            }
            if (row_height <= 0) {
                row_height = DMButton::height();
            }
            int remaining = maxw;
            int col_x = x;
            const int cols = static_cast<int>(widget_row.size());
            for (int c = 0; c < cols; ++c) {
                Widget* w = widget_row[c];
                if (!w) continue;
                const int remaining_cols = cols - c;
                int width = remaining;
                if (remaining_cols > 1) {
                    width = std::max(40, (remaining - gap * (remaining_cols - 1)) / remaining_cols);
                }
                SDL_Rect wr{ col_x, curr_y - scroll_, width, row_height };
                w->set_rect(wr);
                col_x += width + gap;
                remaining = std::max(0, maxw - (col_x - x));
            }
            curr_y += row_height + gap;
        }
        y = curr_y;
    }

    void commit_to_info() {
        if (!info_) return;
        std::vector<ChildInfo> out;
        out.reserve(rows_.size());
        auto base_dir = parent_dir(info_->info_json_path());
        for (auto& r : rows_) {
            ChildInfo ci;
            ci.area_name = r.area_name;
            ci.z_offset  = r.z_offset;

            ci.inline_assets = r.assets.is_array() ? r.assets : nlohmann::json::array();

            ci.json_path = r.json_path.empty() ? std::string{} : join_path(base_dir, r.json_path);
            out.push_back(std::move(ci));
        }
        info_->set_children(out);
    }

    void rebuild_area_names() { area_names_ = collect_area_names(); }
    std::vector<std::string> collect_area_names() const {
        std::vector<std::string> names;
        names.reserve(info_ ? info_->areas.size() : 0);
        if (info_) {
            for (const auto& na : info_->areas) names.push_back(na.name);
        }
        return names;
    }
    std::vector<std::string> area_names_with_none() const {
        auto v = collect_area_names();
        v.insert(v.begin(), std::string("(none)"));
        return v;
    }
    static int find_index(const std::vector<std::string>& v, const std::string& s) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i] == s) return (int)i;

        return 0;
    }
    static std::string safe_get_option(const std::vector<std::string>& v, int idx) {
        if (idx < 0 || idx >= (int)v.size()) return std::string{};
        return (v[idx] == "(none)") ? std::string{} : v[idx];
    }

    static std::string parent_dir(const std::string& p) {
        auto pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? std::string{} : p.substr(0, pos);
    }
    static std::string make_relative(const std::string& base, const std::string& full) {
        if (base.empty() || full.empty()) return full;
        if (full.rfind(base, 0) == 0) {
            size_t cut = base.size();
            if (full.size() > cut && (full[cut] == '/' || full[cut] == '\\')) ++cut;
            return full.substr(cut);
        }
        return full;
    }
    static std::string join_path(const std::string& base, const std::string& rel) {
        if (base.empty()) return rel;
        if (rel.empty()) return base;
        if (rel.front() == '/' || rel.front() == '\\') return base + rel;
        char sep = (
#ifdef _WIN32
            '\\'
#else
            '/'
#endif
        );
        if (!base.empty() && (base.back() == '/' || base.back() == '\\'))
            return base + rel;
        return base + sep + rel;
    }

private:
    std::vector<Row> rows_;
    std::vector<std::string> area_names_;
    std::unique_ptr<DMButton> b_add_;
    std::unique_ptr<DMButton> apply_btn_;
    std::function<void(const std::string&)> open_area_editor_;
    AssetInfoUI* ui_ = nullptr;

    bool pending_open_area_ = false;
    std::string pending_area_name_;
};
