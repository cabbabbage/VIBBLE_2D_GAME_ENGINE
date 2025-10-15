#pragma once

#include "../DockableCollapsible.hpp"
#include <algorithm>
#include <memory>
#include <vector>
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
        s_ray_strength_.reset();
        if (!info_) return;
        if (info_->generate_rays) {
            const int strength = std::clamp(info_->ray_strength, 0, 100);
            s_ray_strength_ = std::make_unique<DMSlider>("Ray Strength", 0, 100, strength);
            s_ray_strength_->set_defer_commit_until_unfocus(true);
        } else {
            info_->set_ray_strength(0);
        }

        for (const auto& ls : info_->light_sources) {
            Row r;
            r.light = ls;
            r.lbl = std::make_unique<DMButton>("Light Source", &DMStyles::HeaderButton(), 180, DMButton::height());
            r.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::ListButton(), 120, DMButton::height());
            r.s_intensity = std::make_unique<DMSlider>("Light Intensity", 0, 255, ls.intensity);
            r.s_radius    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, ls.radius);
            r.s_falloff   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, ls.fall_off);
            r.s_flicker   = std::make_unique<DMSlider>("Flicker", 0, 20, ls.flicker);
            r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, ls.offset_x);
            r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, ls.offset_y);
            r.s_color_r   = std::make_unique<DMSlider>("Color R", 0, 255, ls.color.r);
            r.s_color_g   = std::make_unique<DMSlider>("Color G", 0, 255, ls.color.g);
            r.s_color_b   = std::make_unique<DMSlider>("Color B", 0, 255, ls.color.b);
            configure_row_sliders(r);
            rows_.push_back(std::move(r));
        }
        b_add_ = std::make_unique<DMButton>("Add New Light Source", &DMStyles::CreateButton(), 220, DMButton::height());
        if (!apply_btn_) {
            apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 200, DMButton::height());
        }
    }

    void layout_custom_content(int /*screen_w*/, int /*screen_h*/) const override {
        int x = rect_.x + DMSpacing::panel_padding();
        int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
        int maxw = rect_.w - 2 * DMSpacing::panel_padding();

        auto place = [&](auto& widget, int h) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{ x, y - scroll_, maxw, h });
            y += h + DMSpacing::item_gap();
};

        if (s_ray_strength_) {
            place(s_ray_strength_, DMSlider::height());
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (r.lbl) {
                int lbl_x = rect_.x + DMSpacing::panel_padding() + (maxw - 180) / 2;
                r.lbl->set_rect(SDL_Rect{ lbl_x, y - scroll_, 180, DMButton::height() });
            }
            if (r.b_delete)
                r.b_delete->set_rect(SDL_Rect{ x + maxw - 120, y - scroll_, 120, DMButton::height() });
            y += DMButton::height() + DMSpacing::item_gap();
            place(r.s_intensity, DMSlider::height());
            place(r.s_radius,    DMSlider::height());
            place(r.s_falloff,   DMSlider::height());
            place(r.s_flicker,   DMSlider::height());
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
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        if (!info_ || !expanded_) return used;
        bool changed = false;
        bool regenerate_lighting = false;
        bool reset_scaling_profile = false;
        bool purge_light_cache = false;
        if (s_ray_strength_) {
            if (s_ray_strength_->handle_event(e)) {
                used = true;
                const int new_strength = std::clamp(s_ray_strength_->value(), 0, 100);
                if (info_ && info_->ray_strength != new_strength) {
                    changed = true;
                    regenerate_lighting = true;
                }
            }
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (r.lbl && r.lbl->handle_event(e)) used = true;
            if (r.b_delete && r.b_delete->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    rows_.erase(rows_.begin() + i);
                    changed = true;
                    regenerate_lighting = true;
                    reset_scaling_profile = true;
                    purge_light_cache = true;
                    used = true;
                    break;
                }
            }
            auto handle_slider = [&](std::unique_ptr<DMSlider>& slider,
                                     auto get_value,
                                     auto set_value,
                                     bool affects_texture) {
                if (!slider) return;
                const int previous_value = get_value();
                const bool slider_used = slider->handle_event(e);
                const int committed_value = slider->value();
                if (committed_value != previous_value) {
                    set_value(committed_value);
                    changed = true;
                    reset_scaling_profile = true;
                    used = true;
                    if (affects_texture) {
                        regenerate_lighting = true;
                    }
                } else if (slider_used) {
                    used = true;
                }
            };

            handle_slider(r.s_intensity,
                          [&]() { return r.light.intensity; },
                          [&](int v) { r.light.intensity = v; },
                          true);
            handle_slider(r.s_radius,
                          [&]() { return r.light.radius; },
                          [&](int v) { r.light.radius = v; },
                          true);
            handle_slider(r.s_falloff,
                          [&]() { return r.light.fall_off; },
                          [&](int v) { r.light.fall_off = v; },
                          true);
            handle_slider(r.s_flicker,
                          [&]() { return r.light.flicker; },
                          [&](int v) { r.light.flicker = v; },
                          true);
            handle_slider(r.s_offset_x,
                          [&]() { return r.light.offset_x; },
                          [&](int v) { r.light.offset_x = v; },
                          false);
            handle_slider(r.s_offset_y,
                          [&]() { return r.light.offset_y; },
                          [&](int v) { r.light.offset_y = v; },
                          false);
            handle_slider(r.s_color_r,
                          [&]() { return static_cast<int>(r.light.color.r); },
                          [&](int v) { r.light.color.r = static_cast<Uint8>(v); },
                          true);
            handle_slider(r.s_color_g,
                          [&]() { return static_cast<int>(r.light.color.g); },
                          [&](int v) { r.light.color.g = static_cast<Uint8>(v); },
                          true);
            handle_slider(r.s_color_b,
                          [&]() { return static_cast<int>(r.light.color.b); },
                          [&](int v) { r.light.color.b = static_cast<Uint8>(v); },
                          true);
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
                r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, r.light.offset_x);
                r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, r.light.offset_y);
                r.s_color_r   = std::make_unique<DMSlider>("Color R", 0, 255, r.light.color.r);
                r.s_color_g   = std::make_unique<DMSlider>("Color G", 0, 255, r.light.color.g);
                r.s_color_b   = std::make_unique<DMSlider>("Color B", 0, 255, r.light.color.b);
                configure_row_sliders(r);
                rows_.push_back(std::move(r));
                changed = true;
                regenerate_lighting = true;
                reset_scaling_profile = true;
                purge_light_cache = true;
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
                if (ui_ && reset_scaling_profile) {
                    ui_->notify_light_sources_modified(purge_light_cache);
                }
                (void)info_->update_info_json();
                if (regenerate_lighting && ui_) {
                    SDL_Renderer* r = ui_->get_last_renderer();
                    if (r) LightingLoader::generate_textures(*info_, r);
                }
            }
        }
        return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
        if (s_ray_strength_) s_ray_strength_->render(r);
        for (const auto& rrow : rows_) {
            if (rrow.lbl)      rrow.lbl->render(r);
            if (rrow.b_delete) rrow.b_delete->render(r);
            if (rrow.s_intensity) rrow.s_intensity->render(r);
            if (rrow.s_radius)    rrow.s_radius->render(r);
            if (rrow.s_falloff)   rrow.s_falloff->render(r);
            if (rrow.s_flicker)   rrow.s_flicker->render(r);
            if (rrow.s_offset_x)  rrow.s_offset_x->render(r);
            if (rrow.s_offset_y)  rrow.s_offset_y->render(r);
            if (rrow.s_color_r)   rrow.s_color_r->render(r);
            if (rrow.s_color_g)   rrow.s_color_g->render(r);
            if (rrow.s_color_b)   rrow.s_color_b->render(r);
        }
        if (b_add_) b_add_->render(r);
        if (apply_btn_) apply_btn_->render(r);
    }

private:
    struct Row {
        LightSource light;
        std::unique_ptr<DMButton> lbl;
        std::unique_ptr<DMButton> b_delete;
        std::unique_ptr<DMSlider> s_intensity;
        std::unique_ptr<DMSlider> s_radius;
        std::unique_ptr<DMSlider> s_falloff;
        std::unique_ptr<DMSlider> s_flicker;
        std::unique_ptr<DMSlider> s_offset_x;
        std::unique_ptr<DMSlider> s_offset_y;
        std::unique_ptr<DMSlider> s_color_r;
        std::unique_ptr<DMSlider> s_color_g;
        std::unique_ptr<DMSlider> s_color_b;
};

    void configure_row_sliders(Row& r) {
        auto configure_regen_slider = [](std::unique_ptr<DMSlider>& slider) {
            if (slider) slider->set_defer_commit_until_unfocus(true);
        };
        configure_regen_slider(r.s_intensity);
        configure_regen_slider(r.s_radius);
        configure_regen_slider(r.s_falloff);
        configure_regen_slider(r.s_flicker);
        configure_regen_slider(r.s_color_r);
        configure_regen_slider(r.s_color_g);
        configure_regen_slider(r.s_color_b);
        if (r.s_offset_x) r.s_offset_x->set_defer_commit_until_unfocus(false);
        if (r.s_offset_y) r.s_offset_y->set_defer_commit_until_unfocus(false);
    }

    void commit_to_info() {
        if (!info_) return;
        std::vector<LightSource> lights;
        for (const auto& r : rows_) lights.push_back(r.light);
        if (info_) {
            int strength = 0;
            if (s_ray_strength_) {
                strength = std::clamp(s_ray_strength_->value(), 0, 100);
            }
            info_->set_ray_strength(strength);
        }
        LightSource shading_light{};
        if (info_ && !info_->orbital_light_sources.empty()) {
            shading_light = info_->orbital_light_sources.front();
        }
        int shading_factor = info_ ? info_->shading_factor : 100;
        bool is_shaded = info_ ? info_->is_shaded : false;
        info_->set_lighting(is_shaded, shading_light, shading_factor, lights);
    }

    std::unique_ptr<DMSlider> s_ray_strength_;

    std::vector<Row> rows_;
    std::unique_ptr<DMButton> b_add_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr;

protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "lighting"; }
};

