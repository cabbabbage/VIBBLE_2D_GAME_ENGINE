#pragma once

#include "../DockableCollapsible.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "render/camera.hpp"
#include "widgets.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

class AssetInfoUI;

class Section_BasicInfo : public DockableCollapsible {
  public:
    Section_BasicInfo();
    ~Section_BasicInfo() override = default;

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }

    void build() override;
    void layout() override { DockableCollapsible::layout(); }
    bool handle_event(const SDL_Event& e) override;
    void render_content(SDL_Renderer* r) const override {}
    void render_world_overlay(SDL_Renderer* r,
                              const camera& cam,
                              const Asset* target,
                              float reference_screen_height) const;

  private:
    static int find_index(const std::vector<std::string>& opts, const std::string& value);

    std::unique_ptr<DMDropdown>  dd_type_;
    std::unique_ptr<DMSlider>    s_scale_pct_;
    std::unique_ptr<DMSlider>    s_zindex_;
    std::unique_ptr<DMCheckbox>  c_flipable_;
    std::unique_ptr<DMButton>    apply_btn_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::vector<std::string> type_options_;
    AssetInfoUI* ui_ = nullptr; // non-owning
};

inline Section_BasicInfo::Section_BasicInfo()
    : DockableCollapsible("Basic Info", false) {}

inline int Section_BasicInfo::find_index(const std::vector<std::string>& opts, const std::string& value) {
    std::string canonical = asset_types::canonicalize(value);
    auto it = std::find(opts.begin(), opts.end(), canonical);
    if (it != opts.end()) {
        return static_cast<int>(std::distance(opts.begin(), it));
    }
    auto fallback = std::find(opts.begin(), opts.end(), std::string(asset_types::object));
    if (fallback != opts.end()) {
        return static_cast<int>(std::distance(opts.begin(), fallback));
    }
    return 0;
}

inline void Section_BasicInfo::build() {
    widgets_.clear();
    DockableCollapsible::Rows rows;
    if (!info_) { set_rows(rows); return; }

    type_options_ = asset_types::all_as_strings();
    dd_type_ = std::make_unique<DMDropdown>("Type", type_options_, find_index(type_options_, info_->type));
    int pct = std::max(0, static_cast<int>(std::lround(info_->scale_factor * 100.0f)));
    s_scale_pct_ = std::make_unique<DMSlider>("Scale (%)", 1, 400, pct);
    s_zindex_    = std::make_unique<DMSlider>("Z Index Offset", -1000, 1000, info_->z_threshold);
    c_flipable_  = std::make_unique<DMCheckbox>("Flipable (can invert)", info_->flipable);

    auto w_type = std::make_unique<DropdownWidget>(dd_type_.get());
    rows.push_back({ w_type.get() });
    widgets_.push_back(std::move(w_type));

    auto w_scale = std::make_unique<SliderWidget>(s_scale_pct_.get());
    rows.push_back({ w_scale.get() });
    widgets_.push_back(std::move(w_scale));

    auto w_z = std::make_unique<SliderWidget>(s_zindex_.get());
    rows.push_back({ w_z.get() });
    widgets_.push_back(std::move(w_z));

    auto w_flip = std::make_unique<CheckboxWidget>(c_flipable_.get());
    rows.push_back({ w_flip.get() });
    widgets_.push_back(std::move(w_flip));

    if (!apply_btn_) {
        apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 180, DMButton::height());
    }
    auto w_apply = std::make_unique<ButtonWidget>(apply_btn_.get(), [this]() {
        if (ui_) ui_->request_apply_section(AssetInfoSectionId::BasicInfo);
    });
    rows.push_back({ w_apply.get() });
    widgets_.push_back(std::move(w_apply));

    set_rows(rows);
}

inline bool Section_BasicInfo::handle_event(const SDL_Event& e) {
    bool used = DockableCollapsible::handle_event(e);
    if (!info_) return used;

    bool changed = false;
    bool scale_changed = false;
    bool z_changed = false;
    if (dd_type_ && !type_options_.empty()) {
        int idx = std::clamp(dd_type_->selected(), 0, static_cast<int>(type_options_.size()) - 1);
        const std::string& selected = type_options_[idx];
        if (info_->type != selected) {
            info_->set_asset_type(selected);
            changed = true;
        }
    }

    int pct = std::max(0, static_cast<int>(std::lround(info_->scale_factor * 100.0f)));
    if (s_scale_pct_ && pct != s_scale_pct_->value()) {
        info_->set_scale_percentage(static_cast<float>(s_scale_pct_->value()));
        changed = true;
        scale_changed = true;
    }

    if (s_zindex_ && info_->z_threshold != s_zindex_->value()) {
        info_->set_z_threshold(s_zindex_->value());
        changed = true;
        z_changed = true;
    }

    if (c_flipable_ && info_->flipable != c_flipable_->value()) {
        info_->set_flipable(c_flipable_->value());
        changed = true;
    }

    if (changed) {
        (void)info_->update_info_json();
        if (ui_) {
            if (scale_changed) ui_->refresh_target_asset_scale();
            if (z_changed) ui_->sync_target_z_threshold();
        }
    }
    return used || changed;
}

inline void Section_BasicInfo::render_world_overlay(SDL_Renderer* r,
                                                    const camera& cam,
                                                    const Asset* target,
                                                    float reference_screen_height) const {
    if (!is_expanded() || !target || !target->info) return;

    SDL_Texture* tex = target->get_final_texture();
    int fw = target->cached_w;
    int fh = target->cached_h;
    if ((fw == 0 || fh == 0) && tex) {
        SDL_QueryTexture(tex, nullptr, nullptr, &fw, &fh);
    }
    if (fw == 0 || fh == 0) {
        if (target->info) {
            fw = static_cast<int>(std::round(target->info->original_canvas_width * target->info->scale_factor));
            fh = static_cast<int>(std::round(target->info->original_canvas_height * target->info->scale_factor));
        }
    }
    if (fw == 0 || fh == 0) return;

    float scale = cam.get_scale();
    if (scale <= 0.0f) return;
    float inv_scale = 1.0f / scale;
    float base_sw = static_cast<float>(fw) * inv_scale;
    float base_sh = static_cast<float>(fh) * inv_scale;
    if (base_sw <= 0.0f || base_sh <= 0.0f) return;

    const auto effects = cam.compute_render_effects(
        SDL_Point{target->pos.x, target->pos.y}, base_sh, reference_screen_height <= 0.0f ? 1.0f : reference_screen_height);

    float scaled_sw = base_sw * effects.distance_scale;
    float scaled_sh = base_sh * effects.distance_scale;
    float final_visible_h = scaled_sh * effects.vertical_scale;

    int sw = std::max(1, static_cast<int>(std::round(scaled_sw)));
    int sh = std::max(1, static_cast<int>(std::round(final_visible_h)));
    if (sw <= 0 || sh <= 0) return;

    const SDL_Point& base = effects.screen_position;
    SDL_Rect bounds{ base.x - sw / 2, base.y - sh, sw, sh };

    int z_world_y = target->pos.y + target->info->z_threshold;
    SDL_Point z_screen = cam.map_to_screen(SDL_Point{target->pos.x, z_world_y});

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 0, 0, 200);
    SDL_RenderDrawLine(r, bounds.x, z_screen.y, bounds.x + bounds.w, z_screen.y);
}

