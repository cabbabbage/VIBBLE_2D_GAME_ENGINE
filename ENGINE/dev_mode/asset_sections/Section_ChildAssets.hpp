#pragma once

#include "CollapsibleSection.hpp"
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "asset/asset_info.hpp"
#include "assets_config.hpp"

class Section_ChildAssets : public CollapsibleSection {
public:
    Section_ChildAssets()
    : CollapsibleSection("Child Assets") {}

    void set_open_area_editor_callback(std::function<void(const std::string&)> cb) { open_area_editor_ = std::move(cb); }

    void build() override {
        rows_.clear();
        rebuild_area_names();
        rebuild_rows_from_info();
        b_add_ = std::make_unique<DMButton>("Add Child Region", &DMStyles::CreateButton(), 220, DMButton::height());
    }

    void layout() override {
        CollapsibleSection::layout();
        int x = rect_.x + 12;
        int y = rect_.y + DMButton::height() + 8;
        int maxw = rect_.w - 24;

        // Detect if areas changed; rebuild dropdowns if needed
        if (info_) {
            std::vector<std::string> latest = collect_area_names();
            if (latest != area_names_) {
                area_names_ = std::move(latest);
                // rebuild dropdowns with new options
                for (auto& r : rows_) {
                    int idx = r.dd_area ? r.dd_area->selected() : 0;
                    std::string sel = (idx >= 0 && idx < (int)r.options.size()) ? r.options[idx] : std::string{};
                    r.options = area_names_with_none();
                    r.dd_area = std::make_unique<DMDropdown>("Area", r.options, find_index(r.options, sel));
                }
            }
        }

        int used = 0;
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            // Row label
            if (!r.lbl_) r.lbl_ = std::make_unique<DMButton>("Region " + std::to_string(i + 1), &DMStyles::HeaderButton(), 180, DMButton::height());
            r.lbl_->set_rect(SDL_Rect{ x, y + used, 180, DMButton::height() });
            used += DMButton::height() + 4;

            // Area dropdown
            if (!r.dd_area) {
                r.options = area_names_with_none();
                r.dd_area = std::make_unique<DMDropdown>("Area", r.options, find_index(r.options, r.area_name));
            }
            r.dd_area->set_rect(SDL_Rect{ x, y + used, std::min(300, maxw), DMDropdown::height() });

            // Z offset slider
            if (!r.s_z) r.s_z = std::make_unique<DMSlider>("Z Offset", -5000, 5000, r.z_offset);
            int slider_w = std::min(300, maxw);
            r.s_z->set_rect(SDL_Rect{ x + std::min(320, maxw), y + used, slider_w, DMSlider::height() });
            used += std::max(DMDropdown::height(), DMSlider::height()) + 6;

            // Configure assets button
            if (!r.b_assets) r.b_assets = std::make_unique<DMButton>("Configure Assets", &DMStyles::ListButton(), 160, DMButton::height());
            r.b_assets->set_rect(SDL_Rect{ x, y + used, 160, DMButton::height() });
            used += DMButton::height() + 6;

            // Buttons: Edit Area, Delete
            if (!r.b_edit_area) r.b_edit_area = std::make_unique<DMButton>("Edit Area", &DMStyles::ListButton(), 140, DMButton::height());
            if (!r.b_delete)    r.b_delete    = std::make_unique<DMButton>("Delete", &DMStyles::ListButton(), 120, DMButton::height());
            r.b_edit_area->set_rect(SDL_Rect{ x, y + used, 160, DMButton::height() });
            r.b_delete->set_rect(SDL_Rect{ x + 170, y + used, 120, DMButton::height() });
            used += DMButton::height() + 10;
        }

        // Footer actions
        if (b_add_) {
            b_add_->set_rect(SDL_Rect{ x, y + used, std::min(260, maxw), DMButton::height() });
            used += DMButton::height() + 8;
        }

        content_height_ = std::max(0, used);
    }

    void update(const Input& input) override {
        CollapsibleSection::update(input);
        if (assets_cfg_.visible()) assets_cfg_.update(input);
    }

    bool handle_event(const SDL_Event& e) override {
        if (assets_cfg_.visible()) {
            return assets_cfg_.handle_event(e);
        }
        bool used = CollapsibleSection::handle_event(e);
        if (!info_ || !expanded_) return used;

        bool changed = false;

        // Per-row interactions
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (r.lbl_ && r.lbl_->handle_event(e)) used = true;
            if (r.dd_area && r.dd_area->handle_event(e)) { r.area_name = safe_get_option(r.options, r.dd_area->selected()); changed = true; used = true; }
            if (r.s_z && r.s_z->handle_event(e)) { r.z_offset = r.s_z->value(); changed = true; used = true; }
            if (r.b_assets && r.b_assets->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    size_t idx = i;
                    assets_cfg_.open(r.assets, [this, idx](const nlohmann::json& j){
                        if (idx < rows_.size()) {
                            rows_[idx].assets = j;
                            commit_to_info();
                            if (info_) (void)info_->update_info_json();
                        }
                    });
                    assets_cfg_.set_position(rect_.x - 260, rect_.y);
                    used = true;
                }
            }
            if (r.b_edit_area && r.b_edit_area->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    std::string nm = r.area_name;
                    if (nm.empty()) nm = std::string("child_area_") + std::to_string(i+1);
                    if (open_area_editor_) open_area_editor_(nm);
                    used = true;
                }
            }
            if (r.b_delete && r.b_delete->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    rows_.erase(rows_.begin() + i);
                    changed = true; used = true;
                    break; // indices invalidated, rebuild next frame
                }
            }
        }

        // Footer buttons
        if (b_add_ && b_add_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                Row r;
                r.options = area_names_with_none();
                r.area_name = "";
                r.z_offset = 0;
                r.dd_area = std::make_unique<DMDropdown>("Area", r.options, 0);
                r.s_z = std::make_unique<DMSlider>("Z Offset", -5000, 5000, 0);
                rows_.push_back(std::move(r));
                changed = true; used = true;
            }
        }

        if (changed) {
            commit_to_info();
            (void)info_->update_info_json();
        }
        return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
        for (const auto& row : rows_) {
            if (row.lbl_)       row.lbl_->render(r);
            if (row.dd_area)    row.dd_area->render(r);
            if (row.s_z)        row.s_z->render(r);
            if (row.b_assets)   row.b_assets->render(r);
            if (row.b_edit_area)row.b_edit_area->render(r);
            if (row.b_delete)   row.b_delete->render(r);
        }
        if (b_add_)        b_add_->render(r);
        if (assets_cfg_.visible()) assets_cfg_.render(r);
    }

private:
    struct Row {
        // Data snapshot
        std::string area_name;
        int z_offset = 0;
        std::string json_path; // relative
        nlohmann::json assets = nlohmann::json::array();
        // Controls
        std::unique_ptr<DMButton>   lbl_;
        std::unique_ptr<DMDropdown> dd_area;
        std::unique_ptr<DMSlider>   s_z;
        std::unique_ptr<DMButton>   b_assets;
        std::unique_ptr<DMButton>   b_edit_area;
        std::unique_ptr<DMButton>   b_delete;
        // Choices
        std::vector<std::string> options;
    };

private:
    void rebuild_rows_from_info() {
        rows_.clear();
        if (!info_) return;
        auto base_dir = parent_dir(info_->info_json_path());
        for (const auto& c : info_->children) {
            Row r;
            r.area_name = c.area_name;
            r.z_offset = c.z_offset;
            // json_path from loader is absolute; convert to relative if under asset dir
            r.json_path = make_relative(base_dir, c.json_path);
            if (c.inline_assets.is_array()) {
                r.assets = c.inline_assets;
            }
            r.options = area_names_with_none();
            r.dd_area = std::make_unique<DMDropdown>("Area", r.options, find_index(r.options, r.area_name));
            r.s_z = std::make_unique<DMSlider>("Z Offset", -5000, 5000, r.z_offset);
            // Text boxes and buttons are built in layout (lazy)
            rows_.push_back(std::move(r));
        }
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
            // Inline assets
            ci.inline_assets = r.assets.is_array() ? r.assets : nlohmann::json::array();
            // json_path (store absolute internally for consistency; set_children will re-relativize)
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
        // map empty string to index 0 ("(none)")
        return 0;
    }
    static std::string safe_get_option(const std::vector<std::string>& v, int idx) {
        if (idx < 0 || idx >= (int)v.size()) return std::string{};
        return (v[idx] == "(none)") ? std::string{} : v[idx];
    }

    // path helpers
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
        if (rel.front() == '/' || rel.front() == '\\') return base + rel; // naive
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
    AssetsConfig assets_cfg_;
    std::function<void(const std::string&)> open_area_editor_;
};
