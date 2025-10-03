#pragma once

#include "../DockableCollapsible.hpp"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>
#include "asset/asset_info.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include "dev_mode/asset_info_sections.hpp"

class AssetInfoUI;

class Section_Lighting : public DockableCollapsible {
public:
    Section_Lighting() : DockableCollapsible("Lighting", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Lighting() override = default;

    void build() override {
        rows_.clear();
        shading_factor_ = 100;
        if (!info_) return;
        shading_factor_ = std::clamp(info_->shading_factor, 1, 200);
        c_is_shaded_ = std::make_unique<DMCheckbox>("Has Shading", info_->is_shaded);
        shading_label_ = std::make_unique<DMButton>("Shading Source", &DMStyles::HeaderButton(), 150, DMButton::height());
        if (!info_->orbital_light_sources.empty()) {
            shading_light_ = info_->orbital_light_sources[0];
        } else {
            shading_light_ = LightSource{};
        }
        s_sh_intensity_ = std::make_unique<DMSlider>("Light Intensity", 0, 255, shading_light_.intensity);
        s_sh_radius_    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, shading_light_.radius);
        s_sh_x_radius_  = std::make_unique<DMSlider>("X Orbit Radius (px)", 0, 2000, shading_light_.x_radius);
        s_sh_y_radius_  = std::make_unique<DMSlider>("Y Orbit Radius (px)", 0, 2000, shading_light_.y_radius);
        s_sh_offset_x_  = std::make_unique<DMSlider>("X Offset (px)", -2000, 2000, shading_light_.offset_x);
        s_sh_offset_y_  = std::make_unique<DMSlider>("Y Offset (px)", -2000, 2000, shading_light_.offset_y);
        s_sh_falloff_   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, shading_light_.fall_off);
        s_sh_factor_    = std::make_unique<DMSlider>("Factor", 1, 200, shading_factor_);

        for (const auto& ls : info_->light_sources) {
            Row r;
            r.light = ls;
            r.lbl = std::make_unique<DMButton>("Light Source", &DMStyles::HeaderButton(), 180, DMButton::height());
            r.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::ListButton(), 120, DMButton::height());
            r.s_intensity = std::make_unique<DMSlider>("Light Intensity", 0, 255, ls.intensity);
            r.s_radius    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, ls.radius);
            r.s_falloff   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, ls.fall_off);
            r.s_flicker   = std::make_unique<DMSlider>("Flicker", 0, 20, ls.flicker);
            r.s_flare     = std::make_unique<DMSlider>("Flare (px)", 0, 100, ls.flare);
            r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, ls.offset_x);
            r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, ls.offset_y);
            r.s_color_r   = std::make_unique<DMSlider>("Color R", 0, 255, ls.color.r);
            r.s_color_g   = std::make_unique<DMSlider>("Color G", 0, 255, ls.color.g);
            r.s_color_b   = std::make_unique<DMSlider>("Color B", 0, 255, ls.color.b);
            rows_.push_back(std::move(r));
        }
        b_add_ = std::make_unique<DMButton>("Add New Light Source", &DMStyles::CreateButton(), 220, DMButton::height());
        if (!apply_btn_) {
            apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 200, DMButton::height());
        }
    }

    void layout() override {
        int x = rect_.x + DMSpacing::panel_padding();
        int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
        int maxw = rect_.w - 2 * DMSpacing::panel_padding();

        auto place = [&](auto& widget, int h) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{ x, y - scroll_, maxw, h });
            y += h + DMSpacing::item_gap();
};

        if (c_is_shaded_) {
            place(c_is_shaded_, DMCheckbox::height());
        }
        if (c_is_shaded_ && c_is_shaded_->value()) {
            int shade_start = y;
            if (shading_label_) {
                int lbl_w = shading_label_->rect().w;
                int lbl_x = rect_.x + DMSpacing::panel_padding() + (maxw - lbl_w) / 2;
                shading_label_->set_rect(SDL_Rect{ lbl_x, y - scroll_, lbl_w, DMButton::height() });
                y += DMButton::height() + DMSpacing::item_gap();
            }
            place(s_sh_intensity_, DMSlider::height());
            place(s_sh_radius_,    DMSlider::height());
            place(s_sh_x_radius_,  DMSlider::height());
            place(s_sh_y_radius_,  DMSlider::height());
            place(s_sh_offset_x_,  DMSlider::height());
            place(s_sh_offset_y_,  DMSlider::height());
            place(s_sh_falloff_,   DMSlider::height());
            place(s_sh_factor_,    DMSlider::height());
            shading_rect_ = SDL_Rect{ x - 4, shade_start - scroll_ - 4, maxw + 8, (y - shade_start) + 8 };
        }

        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (!r.lbl)
                r.lbl = std::make_unique<DMButton>("Light Source " + std::to_string(i + 1), &DMStyles::HeaderButton(), 180, DMButton::height());
            int lbl_x = rect_.x + DMSpacing::panel_padding() + (maxw - 180) / 2;
            r.lbl->set_rect(SDL_Rect{ lbl_x, y - scroll_, 180, DMButton::height() });
            if (r.b_delete)
                r.b_delete->set_rect(SDL_Rect{ x + maxw - 120, y - scroll_, 120, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
            place(r.s_intensity, DMSlider::height());
            place(r.s_radius,    DMSlider::height());
            place(r.s_falloff,   DMSlider::height());
            place(r.s_flicker,   DMSlider::height());
            place(r.s_flare,     DMSlider::height());
            place(r.s_offset_x,  DMSlider::height());
            place(r.s_offset_y,  DMSlider::height());
            place(r.s_color_r,   DMSlider::height());
            place(r.s_color_g,   DMSlider::height());
            place(r.s_color_b,   DMSlider::height());
        }
        if (b_add_) {
            b_add_->set_rect(SDL_Rect{ x, y - scroll_, std::min(260, maxw), DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
        }
        if (apply_btn_) {
            apply_btn_->set_rect(SDL_Rect{ x, y - scroll_, std::min(260, maxw), DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
        }
        content_height_ = std::max(0, y - (rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap()));
        DockableCollapsible::layout();
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        if (!info_ || !expanded_) return used;
        bool changed = false;
        if (c_is_shaded_ && c_is_shaded_->handle_event(e)) {
            changed = true;
        }
        if (c_is_shaded_ && c_is_shaded_->value()) {
            if (s_sh_intensity_ && s_sh_intensity_->handle_event(e)) { shading_light_.intensity = s_sh_intensity_->value(); changed = true; }
            if (s_sh_radius_    && s_sh_radius_->handle_event(e))    { shading_light_.radius = s_sh_radius_->value(); changed = true; }
            if (s_sh_x_radius_  && s_sh_x_radius_->handle_event(e))  { shading_light_.x_radius = s_sh_x_radius_->value(); changed = true; }
            if (s_sh_y_radius_  && s_sh_y_radius_->handle_event(e))  { shading_light_.y_radius = s_sh_y_radius_->value(); changed = true; }
            if (s_sh_offset_x_  && s_sh_offset_x_->handle_event(e))  { shading_light_.offset_x = s_sh_offset_x_->value(); changed = true; }
            if (s_sh_offset_y_  && s_sh_offset_y_->handle_event(e))  { shading_light_.offset_y = s_sh_offset_y_->value(); changed = true; }
            if (s_sh_falloff_   && s_sh_falloff_->handle_event(e))   { shading_light_.fall_off = s_sh_falloff_->value(); changed = true; }
            if (s_sh_factor_    && s_sh_factor_->handle_event(e))    {
                int new_factor = std::clamp(s_sh_factor_->value(), 1, 200);
                if (new_factor != shading_factor_) {
                    const double prev = std::max(1, shading_factor_);
                    const double ratio = static_cast<double>(new_factor) / prev;
                    auto scale_clamped = [&](int value, int min_v, int max_v) {
                        double scaled = std::round(static_cast<double>(value) * ratio);
                        return static_cast<int>(std::clamp(scaled, static_cast<double>(min_v), static_cast<double>(max_v)));
};
                    shading_light_.x_radius = scale_clamped(shading_light_.x_radius, 0, 2000);
                    shading_light_.y_radius = scale_clamped(shading_light_.y_radius, 0, 2000);
                    shading_light_.offset_x = scale_clamped(shading_light_.offset_x, -2000, 2000);
                    shading_light_.offset_y = scale_clamped(shading_light_.offset_y, -2000, 2000);
                    if (s_sh_x_radius_)  s_sh_x_radius_->set_value(shading_light_.x_radius);
                    if (s_sh_y_radius_)  s_sh_y_radius_->set_value(shading_light_.y_radius);
                    if (s_sh_offset_x_)  s_sh_offset_x_->set_value(shading_light_.offset_x);
                    if (s_sh_offset_y_)  s_sh_offset_y_->set_value(shading_light_.offset_y);
                }
                shading_factor_ = new_factor;
                changed = true;
            }
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (r.lbl && r.lbl->handle_event(e)) used = true;
            if (r.b_delete && r.b_delete->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    rows_.erase(rows_.begin() + i);
                    changed = true;
                    used = true;
                    break;
                }
            }
            if (r.s_intensity && r.s_intensity->handle_event(e)) { r.light.intensity = r.s_intensity->value(); changed = true; }
            if (r.s_radius    && r.s_radius->handle_event(e))    { r.light.radius    = r.s_radius->value();    changed = true; }
            if (r.s_falloff   && r.s_falloff->handle_event(e))   { r.light.fall_off  = r.s_falloff->value();   changed = true; }
            if (r.s_flicker   && r.s_flicker->handle_event(e))   { r.light.flicker   = r.s_flicker->value();   changed = true; }
            if (r.s_flare     && r.s_flare->handle_event(e))     { r.light.flare     = r.s_flare->value();     changed = true; }
            if (r.s_offset_x  && r.s_offset_x->handle_event(e))  { r.light.offset_x  = r.s_offset_x->value();  changed = true; }
            if (r.s_offset_y  && r.s_offset_y->handle_event(e))  { r.light.offset_y  = r.s_offset_y->value();  changed = true; }
            if (r.s_color_r   && r.s_color_r->handle_event(e))   { r.light.color.r   = (Uint8)r.s_color_r->value(); changed = true; }
            if (r.s_color_g   && r.s_color_g->handle_event(e))   { r.light.color.g   = (Uint8)r.s_color_g->value(); changed = true; }
            if (r.s_color_b   && r.s_color_b->handle_event(e))   { r.light.color.b   = (Uint8)r.s_color_b->value(); changed = true; }
        }
        if (b_add_ && b_add_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                Row r;
                r.light = LightSource{};
                r.lbl = std::make_unique<DMButton>("Light Source", &DMStyles::HeaderButton(), 180, DMButton::height());
                r.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::ListButton(), 120, DMButton::height());
                r.s_intensity = std::make_unique<DMSlider>("Light Intensity", 0, 255, r.light.intensity);
                r.s_radius    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, r.light.radius);
                r.s_falloff   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, r.light.fall_off);
                r.s_flicker   = std::make_unique<DMSlider>("Flicker", 0, 20, r.light.flicker);
                r.s_flare     = std::make_unique<DMSlider>("Flare (px)", 0, 100, r.light.flare);
                r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, r.light.offset_x);
                r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, r.light.offset_y);
                r.s_color_r   = std::make_unique<DMSlider>("Color R", 0, 255, r.light.color.r);
                r.s_color_g   = std::make_unique<DMSlider>("Color G", 0, 255, r.light.color.g);
                r.s_color_b   = std::make_unique<DMSlider>("Color B", 0, 255, r.light.color.b);
                rows_.push_back(std::move(r));
                changed = true;
                used = true;
            }
        }
        if (apply_btn_ && apply_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (ui_) ui_->request_apply_section(AssetInfoSectionId::Lighting);
            }
            return true;
        }
        if (changed) {
            commit_to_info();
            if (info_) {
                (void)info_->update_info_json();
                if (ui_) {
                    SDL_Renderer* r = ui_->get_last_renderer();
                    if (r) LightingLoader::generate_textures(*info_, r);
                }
            }
        }
        return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
        if (c_is_shaded_) c_is_shaded_->render(r);
        if (c_is_shaded_ && c_is_shaded_->value()) {
            if (shading_label_) shading_label_->render(r);
            if (s_sh_intensity_) s_sh_intensity_->render(r);
            if (s_sh_radius_)    s_sh_radius_->render(r);
            if (s_sh_x_radius_)  s_sh_x_radius_->render(r);
            if (s_sh_y_radius_)  s_sh_y_radius_->render(r);
            if (s_sh_offset_x_)  s_sh_offset_x_->render(r);
            if (s_sh_offset_y_)  s_sh_offset_y_->render(r);
            if (s_sh_falloff_)   s_sh_falloff_->render(r);
            if (s_sh_factor_)    s_sh_factor_->render(r);
            SDL_Color bc = DMStyles::Border();
            SDL_SetRenderDrawColor(r, bc.r, bc.g, bc.b, bc.a);
            SDL_RenderDrawRect(r, &shading_rect_);
        }
        for (const auto& rrow : rows_) {
            if (rrow.lbl)      rrow.lbl->render(r);
            if (rrow.b_delete) rrow.b_delete->render(r);
            if (rrow.s_intensity) rrow.s_intensity->render(r);
            if (rrow.s_radius)    rrow.s_radius->render(r);
            if (rrow.s_falloff)   rrow.s_falloff->render(r);
            if (rrow.s_flicker)   rrow.s_flicker->render(r);
            if (rrow.s_flare)     rrow.s_flare->render(r);
            if (rrow.s_offset_x)  rrow.s_offset_x->render(r);
            if (rrow.s_offset_y)  rrow.s_offset_y->render(r);
            if (rrow.s_color_r)   rrow.s_color_r->render(r);
            if (rrow.s_color_g)   rrow.s_color_g->render(r);
            if (rrow.s_color_b)   rrow.s_color_b->render(r);
        }
        if (b_add_) b_add_->render(r);
        if (apply_btn_) apply_btn_->render(r);
    }

    bool shading_enabled() const { return c_is_shaded_ && c_is_shaded_->value(); }
    const LightSource& shading_light() const { return shading_light_; }

private:
    struct Row {
        LightSource light;
        std::unique_ptr<DMButton> lbl;
        std::unique_ptr<DMButton> b_delete;
        std::unique_ptr<DMSlider> s_intensity;
        std::unique_ptr<DMSlider> s_radius;
        std::unique_ptr<DMSlider> s_falloff;
        std::unique_ptr<DMSlider> s_flicker;
        std::unique_ptr<DMSlider> s_flare;
        std::unique_ptr<DMSlider> s_offset_x;
        std::unique_ptr<DMSlider> s_offset_y;
        std::unique_ptr<DMSlider> s_color_r;
        std::unique_ptr<DMSlider> s_color_g;
        std::unique_ptr<DMSlider> s_color_b;
};

    void commit_to_info() {
        if (!info_) return;
        std::vector<LightSource> lights;
        for (const auto& r : rows_) lights.push_back(r.light);
        info_->set_lighting(c_is_shaded_ ? c_is_shaded_->value() : false, shading_light_, shading_factor_, lights);
    }

    LightSource shading_light_{};
    int shading_factor_ = 100;
    std::unique_ptr<DMButton> shading_label_;
    SDL_Rect shading_rect_{0,0,0,0};
    std::unique_ptr<DMCheckbox> c_is_shaded_;
    std::unique_ptr<DMSlider> s_sh_intensity_;
    std::unique_ptr<DMSlider> s_sh_radius_;
    std::unique_ptr<DMSlider> s_sh_x_radius_;
    std::unique_ptr<DMSlider> s_sh_y_radius_;
    std::unique_ptr<DMSlider> s_sh_offset_x_;
    std::unique_ptr<DMSlider> s_sh_offset_y_;
    std::unique_ptr<DMSlider> s_sh_falloff_;
    std::unique_ptr<DMSlider> s_sh_factor_;

    std::vector<Row> rows_;
    std::unique_ptr<DMButton> b_add_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr;
};

