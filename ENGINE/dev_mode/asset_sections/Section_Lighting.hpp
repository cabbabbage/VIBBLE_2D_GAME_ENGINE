#pragma once

#include "../DockableCollapsible.hpp"
#include <algorithm>
#include <memory>
#include <vector>
#include "asset/asset_info.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include "color_range_widget.hpp"

class AssetInfoUI;

class Section_Lighting : public DockableCollapsible {
public:
    Section_Lighting() : DockableCollapsible("Lighting", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Lighting() override = default;

    void build() override {
        rows_.clear();
        if (!info_) return;

        for (const auto& ls : info_->light_sources) {
            Row r;
            r.light = ls;
            r.lbl = std::make_unique<DMButton>("Light Source", &DMStyles::HeaderButton(), 180, DMButton::height());
            r.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 120, DMButton::height());
            r.s_intensity = std::make_unique<DMSlider>("Light Intensity", 0, 255, ls.intensity);
            r.s_radius    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, ls.radius);
            r.s_falloff   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, ls.fall_off);
            r.s_flicker   = std::make_unique<DMSlider>("Flicker", 0, 20, ls.flicker);
            r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, ls.offset_x);
            r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, ls.offset_y);
            r.c_front          = std::make_unique<DMCheckbox>("Render Texture In Front", ls.in_front);
            r.c_behind         = std::make_unique<DMCheckbox>("Render Texture Behind", ls.behind);
            r.c_dark_mask      = std::make_unique<DMCheckbox>("Render To Dark Mask", ls.render_to_dark_mask);
            r.color_widget = std::make_unique<DMColorRangeWidget>("Light Color");
            {
                DMColorRangeWidget::RangedColor rc;
                rc.r.min = rc.r.max = static_cast<int>(ls.color.r);
                rc.g.min = rc.g.max = static_cast<int>(ls.color.g);
                rc.b.min = rc.b.max = static_cast<int>(ls.color.b);
                rc.a.min = rc.a.max = static_cast<int>(ls.color.a);
                r.color_widget->set_value(rc);
            }
            configure_row_sliders(r);
            rows_.push_back(std::move(r));
        }
        b_add_ = std::make_unique<DMButton>("Add New Light Source", &DMStyles::CreateButton(), 220, DMButton::height());
        if (!apply_btn_) {
            apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 200, DMButton::height());
        }
    }

    void layout_custom_content(int , int ) const override {
        int x = rect_.x + DMSpacing::panel_padding();
        int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
        int maxw = rect_.w - 2 * DMSpacing::panel_padding();

        auto place = [&](auto& widget, int h) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{ x, y - scroll_, maxw, h });
            y += h + DMSpacing::item_gap();
};

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
            if (r.c_front) {
                r.c_front->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMCheckbox::height() });
                y += DMCheckbox::height() + DMSpacing::item_gap();
            }
            if (r.c_behind) {
                r.c_behind->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMCheckbox::height() });
                y += DMCheckbox::height() + DMSpacing::item_gap();
            }
            if (r.c_dark_mask) {
                r.c_dark_mask->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMCheckbox::height() });
                y += DMCheckbox::height() + DMSpacing::item_gap();
            }
            if (r.color_widget) {
                int ch = r.color_widget->height_for_width(maxw);
                r.color_widget->set_rect(SDL_Rect{ x, y - scroll_, maxw, ch });
                y += ch + DMSpacing::item_gap();
            }
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
            auto commit_change = [&]() {
                changed = true; regenerate_lighting = true; reset_scaling_profile = true; purge_light_cache = true; used = true;
            };

            if (r.c_front && r.c_front->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    r.light.in_front = r.c_front->value();
                    commit_change();
                }
            }

            if (r.c_behind && r.c_behind->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    r.light.behind = r.c_behind->value();
                    commit_change();
                }
            }
            if (r.c_dark_mask && r.c_dark_mask->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    r.light.render_to_dark_mask = r.c_dark_mask->value();
                    commit_change();
                }
            }
            if (r.color_widget && r.color_widget->handle_event(e)) {
                used = true;
            }
            if (r.color_widget && r.color_widget->handle_overlay_event(e)) {
                used = true;
                const auto& v = r.color_widget->value();
                SDL_Color new_c{ static_cast<Uint8>(std::clamp(v.r.min, 0, 255)),
                                 static_cast<Uint8>(std::clamp(v.g.min, 0, 255)),
                                 static_cast<Uint8>(std::clamp(v.b.min, 0, 255)),
                                 static_cast<Uint8>(std::clamp(v.a.min, 0, 255)) };
                if (new_c.r != r.light.color.r || new_c.g != r.light.color.g || new_c.b != r.light.color.b || new_c.a != r.light.color.a) {
                    r.light.color = new_c;
                    changed = true;
                    regenerate_lighting = true;
                    purge_light_cache = true;
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
        }
        if (b_add_ && b_add_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                Row r;
                r.light = LightSource{};
                r.lbl = std::make_unique<DMButton>("Light Source", &DMStyles::HeaderButton(), 180, DMButton::height());
                r.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 120, DMButton::height());
                r.s_intensity = std::make_unique<DMSlider>("Light Intensity", 0, 255, r.light.intensity);
                r.s_radius    = std::make_unique<DMSlider>("Radius (px)", 0, 2000, r.light.radius);
                r.s_falloff   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, r.light.fall_off);
                r.s_flicker   = std::make_unique<DMSlider>("Flicker", 0, 20, r.light.flicker);
                r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, r.light.offset_x);
                r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, r.light.offset_y);
                r.c_front          = std::make_unique<DMCheckbox>("Render Texture In Front", r.light.in_front);
                r.c_behind         = std::make_unique<DMCheckbox>("Render Texture Behind", r.light.behind);
                r.c_dark_mask      = std::make_unique<DMCheckbox>("Render To Dark Mask", r.light.render_to_dark_mask);
                r.color_widget = std::make_unique<DMColorRangeWidget>("Light Color");
                {
                    DMColorRangeWidget::RangedColor rc;
                    rc.r.min = rc.r.max = static_cast<int>(r.light.color.r);
                    rc.g.min = rc.g.max = static_cast<int>(r.light.color.g);
                    rc.b.min = rc.b.max = static_cast<int>(r.light.color.b);
                    rc.a.min = rc.a.max = static_cast<int>(r.light.color.a);
                    r.color_widget->set_value(rc);
                }
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
                (void)info_->commit_manifest();
                (void)regenerate_lighting;
            }
        }
        return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
        for (const auto& rrow : rows_) {
            if (rrow.lbl)      rrow.lbl->render(r);
            if (rrow.b_delete) rrow.b_delete->render(r);
            if (rrow.s_intensity) rrow.s_intensity->render(r);
            if (rrow.s_radius)    rrow.s_radius->render(r);
            if (rrow.s_falloff)   rrow.s_falloff->render(r);
            if (rrow.s_flicker)   rrow.s_flicker->render(r);
            if (rrow.s_offset_x)  rrow.s_offset_x->render(r);
            if (rrow.s_offset_y)  rrow.s_offset_y->render(r);
            if (rrow.c_front)  rrow.c_front->render(r);
            if (rrow.c_behind) rrow.c_behind->render(r);
            if (rrow.c_dark_mask) rrow.c_dark_mask->render(r);
            if (rrow.color_widget) rrow.color_widget->render(r);
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
        std::unique_ptr<DMCheckbox> c_front;
        std::unique_ptr<DMCheckbox> c_behind;
        std::unique_ptr<DMCheckbox> c_dark_mask;
        std::unique_ptr<DMColorRangeWidget> color_widget;
};

    void configure_row_sliders(Row& r) {
        auto configure_regen_slider = [](std::unique_ptr<DMSlider>& slider) {
            if (slider) slider->set_defer_commit_until_unfocus(true);
};
        configure_regen_slider(r.s_intensity);
        configure_regen_slider(r.s_radius);
        configure_regen_slider(r.s_falloff);
        configure_regen_slider(r.s_flicker);
        if (r.s_offset_x) r.s_offset_x->set_defer_commit_until_unfocus(false);
        if (r.s_offset_y) r.s_offset_y->set_defer_commit_until_unfocus(false);
    }

    void commit_to_info() {
        if (!info_) return;
        std::vector<LightSource> lights;
        for (const auto& r : rows_) lights.push_back(r.light);
        info_->set_lighting(lights);
    }

    std::vector<Row> rows_;
    std::unique_ptr<DMButton> b_add_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr;

protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "lighting"; }
public:
    void sync_from_info() {
        if (!info_) return;
        const size_t n = info_->light_sources.size();
        if (rows_.size() != n) {
            build();
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            auto& r = rows_[i];
            const auto& src = info_->light_sources[i];
            r.light = src;
            if (r.s_intensity) r.s_intensity->set_value(src.intensity);
            if (r.s_radius)    r.s_radius->set_value(src.radius);
            if (r.s_falloff)   r.s_falloff->set_value(src.fall_off);
            if (r.s_flicker)   r.s_flicker->set_value(src.flicker);
            if (r.s_offset_x)  r.s_offset_x->set_value(src.offset_x);
            if (r.s_offset_y)  r.s_offset_y->set_value(src.offset_y);
            if (r.c_front)           r.c_front->set_value(src.in_front);
            if (r.c_behind)          r.c_behind->set_value(src.behind);
            if (r.c_dark_mask)       r.c_dark_mask->set_value(src.render_to_dark_mask);
            if (r.color_widget) {
                DMColorRangeWidget::RangedColor rc;
                rc.r.min = rc.r.max = static_cast<int>(src.color.r);
                rc.g.min = rc.g.max = static_cast<int>(src.color.g);
                rc.b.min = rc.b.max = static_cast<int>(src.color.b);
                rc.a.min = rc.a.max = static_cast<int>(src.color.a);
                r.color_widget->set_value(rc);
            }
        }
    }
    void update(const Input& input, int screen_w, int screen_h) override {
        DockableCollapsible::update(input, screen_w, screen_h);
        for (auto& r : rows_) {
            if (r.color_widget) {
                r.color_widget->update_overlay(input, screen_w, screen_h);
            }
        }
    }
    void render(SDL_Renderer* r) const override {
        DockableCollapsible::render(r);
        for (const auto& row : rows_) {
            if (row.color_widget) {
                row.color_widget->render_overlay(r);
            }
        }
    }
};
