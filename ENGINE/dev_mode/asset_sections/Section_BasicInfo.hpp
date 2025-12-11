#pragma once

#include "../DockableCollapsible.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_types.hpp"
#include "render/warped_screen_grid.hpp"
#include "render/render.hpp"
#include "widgets.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include "dm_styles.hpp"
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
    void render_world_overlay(SDL_Renderer* r, const WarpedScreenGrid& cam, const Asset* target, float reference_screen_height) const;

  private:
    static int find_index(const std::vector<std::string>& opts, const std::string& value);

    std::unique_ptr<DMDropdown>  dd_type_;
    std::unique_ptr<DMSlider>    s_scale_pct_;
    std::unique_ptr<DMSlider>    s_zindex_;
    std::unique_ptr<DMCheckbox>  c_flipable_;
    std::unique_ptr<DMCheckbox>  c_apply_distance_scaling_;
    std::unique_ptr<DMCheckbox>  c_apply_vertical_scaling_;
    std::unique_ptr<DMCheckbox>  c_tillable_;
    std::unique_ptr<DMButton>    apply_btn_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::vector<std::string> type_options_;
    AssetInfoUI* ui_ = nullptr;
    void on_scale_slider_value_changed(int new_value);

  protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "basic"; }
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
    if (!info_) {
        auto placeholder = std::make_unique<ReadOnlyTextBoxWidget>( "", "No asset selected. Select an asset from the library or scene to view and edit its information.");
        rows.push_back({ placeholder.get() });
        widgets_.push_back(std::move(placeholder));
        set_rows(rows);
        return;
    }

    type_options_ = asset_types::all_as_strings();

    const bool is_area_asset = (asset_types::canonicalize(info_->type) == std::string(asset_types::area));
    const bool is_tiled_asset = info_->tillable;
    if (is_area_asset) {
        type_options_.clear();
        type_options_.emplace_back(std::string(asset_types::area));
    } else {

        type_options_.erase(std::remove(type_options_.begin(), type_options_.end(), std::string(asset_types::area)), type_options_.end());
    }
    dd_type_ = std::make_unique<DMDropdown>("Type", type_options_, find_index(type_options_, info_->type));
    int pct = std::max(0, static_cast<int>(std::lround(info_->scale_factor * 100.0f)));
    s_scale_pct_ = std::make_unique<DMSlider>("Scale (%)", 1, 400, pct);
    s_scale_pct_->set_on_value_changed([this](int value) { this->on_scale_slider_value_changed(value); });
    s_zindex_    = std::make_unique<DMSlider>("Z Index Offset", -1000, 1000, info_->z_threshold);
    if (!is_tiled_asset) {
        c_flipable_  = std::make_unique<DMCheckbox>("Flipable (can invert)", info_->flipable);
        c_apply_distance_scaling_ = std::make_unique<DMCheckbox>("Apply distance scaling", info_->apply_distance_scaling);
        c_apply_vertical_scaling_ = std::make_unique<DMCheckbox>("Apply vertical scaling", info_->apply_vertical_scaling);
    } else {
        c_flipable_.reset();
        c_apply_distance_scaling_.reset();
        c_apply_vertical_scaling_.reset();
    }
    c_tillable_ = std::make_unique<DMCheckbox>("Tileable (grid tiles)", info_->tillable);

    auto w_type = std::make_unique<DropdownWidget>(dd_type_.get());
    rows.push_back({ w_type.get() });
    widgets_.push_back(std::move(w_type));

    auto w_scale = std::make_unique<SliderWidget>(s_scale_pct_.get());
    rows.push_back({ w_scale.get() });
    widgets_.push_back(std::move(w_scale));

    auto w_z = std::make_unique<SliderWidget>(s_zindex_.get());
    rows.push_back({ w_z.get() });
    widgets_.push_back(std::move(w_z));

    if (c_flipable_) {
        auto w_flip = std::make_unique<CheckboxWidget>(c_flipable_.get());
        rows.push_back({ w_flip.get() });
        widgets_.push_back(std::move(w_flip));
    }

    if (c_apply_distance_scaling_) {
        auto w_distance = std::make_unique<CheckboxWidget>(c_apply_distance_scaling_.get());
        rows.push_back({ w_distance.get() });
        widgets_.push_back(std::move(w_distance));
    }

    if (c_apply_vertical_scaling_) {
        auto w_vertical = std::make_unique<CheckboxWidget>(c_apply_vertical_scaling_.get());
        rows.push_back({ w_vertical.get() });
        widgets_.push_back(std::move(w_vertical));
    }

    auto w_tillable = std::make_unique<CheckboxWidget>(c_tillable_.get());
    rows.push_back({ w_tillable.get() });
    widgets_.push_back(std::move(w_tillable));

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

    if (!used) {
        if (dd_type_ && dd_type_->handle_event(e)) used = true;
        if (s_scale_pct_ && s_scale_pct_->handle_event(e)) used = true;
        if (s_zindex_ && s_zindex_->handle_event(e)) used = true;
        if (c_flipable_ && c_flipable_->handle_event(e)) used = true;
    if (c_apply_distance_scaling_ && c_apply_distance_scaling_->handle_event(e)) used = true;
        if (c_apply_vertical_scaling_ && c_apply_vertical_scaling_->handle_event(e)) used = true;
        if (c_tillable_ && c_tillable_->handle_event(e)) used = true;
    }

    bool changed = false;
    bool rebuild_needed = false;
    bool z_changed = false;
    bool tile_changed = false;
    bool render_settings_changed = false;
    bool type_changed = false;
    if (dd_type_ && !type_options_.empty()) {
        int idx = std::clamp(dd_type_->selected(), 0, static_cast<int>(type_options_.size()) - 1);
        std::string selected = asset_types::canonicalize(type_options_[idx]);
        std::string current = asset_types::canonicalize(info_->type);
        const bool is_area_asset = (current == std::string(asset_types::area));
        const bool selecting_area = (selected == std::string(asset_types::area));

        if (!(is_area_asset && !selecting_area) && !(!is_area_asset && selecting_area) && current != selected) {
            info_->set_asset_type(selected);
            changed = true;
            render_settings_changed = true;
            type_changed = true;
        }
    }

    if (s_zindex_ && info_->z_threshold != s_zindex_->value()) {
        info_->set_z_threshold(s_zindex_->value());
        changed = true;
        z_changed = true;
    }

    if (c_flipable_ && info_->flipable != c_flipable_->value()) {
        info_->set_flipable(c_flipable_->value());
        changed = true;
        render_settings_changed = true;
    }

    if (c_apply_distance_scaling_ && info_->apply_distance_scaling != c_apply_distance_scaling_->value()) {
        info_->set_apply_distance_scaling(c_apply_distance_scaling_->value());
        changed = true;
        render_settings_changed = true;
    }

    if (c_apply_vertical_scaling_ && info_->apply_vertical_scaling != c_apply_vertical_scaling_->value()) {
        info_->set_apply_vertical_scaling(c_apply_vertical_scaling_->value());
        changed = true;
        render_settings_changed = true;
    }

    if (c_tillable_ && info_->tillable != c_tillable_->value()) {
        info_->set_tillable(c_tillable_->value());
        changed = true;
        tile_changed = true;
        rebuild_needed = true;
    }

    if (changed) {
        (void)info_->commit_manifest();
        if (ui_) {
            if (z_changed) ui_->sync_target_z_threshold();
            if (tile_changed) ui_->sync_target_tiling_state();
            if (render_settings_changed) ui_->sync_target_basic_render_settings(type_changed);
        }
    }
    if (rebuild_needed) {
        build();
    }
    return used || changed;
}

inline void Section_BasicInfo::render_world_overlay(SDL_Renderer* r,
                                                    const WarpedScreenGrid& cam,
                                                    const Asset* target,
                                                    float reference_screen_height) const {
    if (!is_expanded() || !target || !target->info) return;
    Assets* assets = ui_ ? ui_->assets() : nullptr;

    SDL_Texture* tex = target->get_current_frame();
    int fw = target->cached_w;
    int fh = target->cached_h;
    if ((fw == 0 || fh == 0) && tex) {
        SDL_QueryTexture(tex, nullptr, nullptr, &fw, &fh);
    }
    if (fw == 0 || fh == 0) {
        if (target->info) {
            fw = target->info->original_canvas_width;
            fh = target->info->original_canvas_height;
        }
    }
    if (fw == 0 || fh == 0) return;

    float scale = cam.get_scale();
    if (scale <= 0.0f) return;
    float inv_scale = 1.0f / scale;
    const float base_scale = (target->info && std::isfinite(target->info->scale_factor) && target->info->scale_factor >= 0.0f) ? target->info->scale_factor : 1.0f;
    float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
    float base_sh = static_cast<float>(fh) * base_scale * inv_scale;
    if (base_sw <= 0.0f || base_sh <= 0.0f) return;

    const auto effects = cam.compute_render_effects(
        SDL_Point{target->pos.x, target->pos.y},
        base_sh,
        reference_screen_height <= 0.0f ? 1.0f : reference_screen_height,
        WarpedScreenGrid::RenderSmoothingKey(target));

    float scaled_sw = base_sw * effects.distance_scale;
    float scaled_sh = base_sh * effects.distance_scale;
    float final_visible_h = scaled_sh * effects.vertical_scale;

    int sw = std::max(1, static_cast<int>(std::round(scaled_sw)));
    int sh = std::max(1, static_cast<int>(std::round(final_visible_h)));
    if (sw <= 0 || sh <= 0) return;

        SDL_Point world_point{ target->pos.x, target->pos.y };
        float center_x = effects.screen_position.x;
        if (assets) {

            if (!(assets->player == target)) {

            }
        }
        const int   left     = static_cast<int>(std::lround(center_x - static_cast<float>(sw) * 0.5f));
        const int   top      = static_cast<int>(std::lround(effects.screen_position.y)) - sh;
    SDL_Rect bounds{ left, top, sw, sh };

    int z_world_y = target->pos.y + target->info->z_threshold;
    SDL_FPoint z_screen_f = cam.map_to_screen(SDL_Point{target->pos.x, z_world_y});
    const int z_line_y = static_cast<int>(std::lround(z_screen_f.y));

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_Color accent = DMStyles::DeleteButton().hover_bg;
    SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 200);
    SDL_RenderDrawLine(r, bounds.x, z_line_y, bounds.x + bounds.w, z_line_y);
}

inline void Section_BasicInfo::on_scale_slider_value_changed(int new_value) {
    if (!info_) return;
    info_->set_scale_percentage(static_cast<float>(new_value));
    (void)info_->commit_manifest();
    if (ui_) {
        ui_->refresh_target_asset_scale();
    }
}
