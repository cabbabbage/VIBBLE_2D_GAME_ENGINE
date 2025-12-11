#pragma once

#include "../DockableCollapsible.hpp"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
#include "asset/asset_info.hpp"
#include "asset_info_methods/lighting_loader.hpp"
#include "color_range_widget.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include "../dm_icons.hpp"
#include "../draw_utils.hpp"

class AssetInfoUI;

class Section_Lighting : public DockableCollapsible {
public:
    Section_Lighting() : DockableCollapsible("Lighting", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Lighting() override = default;
    void set_highlighted_light(std::optional<std::size_t> index);
    void expand_light_row(std::size_t index);

    void build() override {
        rows_.clear();
        highlighted_row_index_ = -1;

        DockableCollapsible::Rows rows;
        if (!info_) {
            if (!empty_state_widget_) {
                empty_state_widget_ = std::make_unique<ReadOnlyTextBoxWidget>( "", "No asset selected. Select an asset from the library or scene to view and edit its information.");
            }
            rows.push_back({ empty_state_widget_.get() });
            set_rows(rows);
            b_add_.reset();
            apply_btn_.reset();
            return;
        }

        set_rows(rows);

        for (const auto& ls : info_->light_sources) {
            rows_.push_back(create_row_from_light(ls, false));
        }
        refresh_row_headers();
        refresh_highlight_state();
        b_add_ = std::make_unique<DMButton>("Add New Light Source", &DMStyles::CreateButton(), 220, DMButton::height());
        if (!apply_btn_) {
            apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 200, DMButton::height());
        }
    }

    void layout_custom_content(int , int ) const override {
        if (!info_) {
            return;
        }

        int x = rect_.x + DMSpacing::panel_padding();
        int y = rect_.y + DMSpacing::panel_padding() + DMButton::height() + DMSpacing::header_gap();
        int maxw = rect_.w - 2 * DMSpacing::panel_padding();

        auto place = [&](auto& widget, int h) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{ x, y - scroll_, maxw, h });
            y += h + DMSpacing::item_gap();
};
        auto hide = [&](auto& widget) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{ 0, 0, 0, 0 });
};

        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            const int row_top = y;

            const int btn_w = 120;
            const int gap = DMSpacing::item_gap();
            int right_cursor = x + maxw;

            if (r.b_delete) {
                right_cursor -= btn_w;
                r.b_delete->set_rect(SDL_Rect{ right_cursor, y - scroll_, btn_w, DMButton::height() });
                right_cursor -= gap;
            }

            if (r.b_duplicate) {
                right_cursor -= btn_w;
                r.b_duplicate->set_rect(SDL_Rect{ right_cursor, y - scroll_, btn_w, DMButton::height() });
                right_cursor -= gap;
            }

            if (r.lbl) {
                const int label_x = x;
                const int label_w = std::max(0, right_cursor - label_x);
                r.lbl->set_rect(SDL_Rect{ label_x, y - scroll_, label_w, DMButton::height() });
            }
            y += DMButton::height() + DMSpacing::item_gap();
            if (r.expanded) {
                place(r.s_intensity, DMSlider::height());
                place(r.s_radius,    DMSlider::height());
                place(r.s_falloff,   DMSlider::height());
                place(r.s_flicker_speed, DMSlider::height());
                place(r.s_flicker_smoothness, DMSlider::height());
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
                if (r.c_asset_alpha_mask) {
                    r.c_asset_alpha_mask->set_rect(SDL_Rect{ x, y - scroll_, maxw, DMCheckbox::height() });
                    y += DMCheckbox::height() + DMSpacing::item_gap();
                }
                if (r.color_widget) {
                    int ch = r.color_widget->height_for_width(maxw);
                    r.color_widget->set_rect(SDL_Rect{ x, y - scroll_, maxw, ch });
                    y += ch + DMSpacing::item_gap();
                }
            } else {
                hide(r.s_intensity);
                hide(r.s_radius);
                hide(r.s_falloff);
                hide(r.s_flicker_speed);
                hide(r.s_flicker_smoothness);
                hide(r.s_offset_x);
                hide(r.s_offset_y);
                hide(r.c_front);
                hide(r.c_behind);
                hide(r.c_dark_mask);
                hide(r.c_asset_alpha_mask);
                hide(r.color_widget);
            }
            const int row_bottom = y - DMSpacing::item_gap();
            int row_height = std::max(DMButton::height(), row_bottom - row_top);
            r.container_rect = SDL_Rect{ x, row_top - scroll_, maxw, row_height };
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
        bool reset_scaling_profile = false;
        bool rebuild_required = false;
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto& r = rows_[i];
            if (r.lbl && r.lbl->handle_event(e)) {
                used = true;
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    set_row_expanded(r, !r.expanded, i);
                }
            }
            if (r.b_delete && r.b_delete->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    rows_.erase(rows_.begin() + static_cast<long long>(i));
                    changed = true;
                    reset_scaling_profile = true;
                    rebuild_required = true;
                    schedule_full_asset_light_rebuild();
                    used = true;
                    refresh_row_headers();
                    refresh_highlight_state();
                    break;
                }
            }
            if (r.b_duplicate && r.b_duplicate->handle_event(e)) {
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    Row nr = create_row_from_light(r.light, true);
                    rows_.insert(rows_.begin() + static_cast<long long>(i) + 1, std::move(nr));
                    shift_pending_rebuild_indices(static_cast<std::size_t>(i + 1));
                    changed = true;
                    reset_scaling_profile = true;
                    rebuild_required = true;
                    const std::size_t new_index = static_cast<std::size_t>(i + 1);
                    schedule_light_rebuild(new_index);
                    used = true;
                    refresh_row_headers();
                    refresh_highlight_state();
                    break;
                }
            }
            if (!r.expanded) {
                continue;
            }
            auto commit_change = [&](bool requires_rebuild) {
                changed = true;
                reset_scaling_profile = true;
                rebuild_required = rebuild_required || requires_rebuild;
                used = true;
};

            if (r.c_front && r.c_front->handle_event(e)) {
                r.light.in_front = r.c_front->value();
                commit_change(false);
            }

            if (r.c_behind && r.c_behind->handle_event(e)) {
                r.light.behind = r.c_behind->value();
                commit_change(false);
            }
            if (r.c_dark_mask && r.c_dark_mask->handle_event(e)) {
                r.light.render_to_dark_mask = r.c_dark_mask->value();
                commit_change(false);
            }
            if (r.c_asset_alpha_mask && r.c_asset_alpha_mask->handle_event(e)) {
                r.light.render_front_and_back_to_asset_alpha_mask = r.c_asset_alpha_mask->value();
                commit_change(false);
            }
            if (r.color_widget && r.color_widget->handle_event(e)) {
                used = true;
            }
            if (r.color_widget && r.color_widget->handle_overlay_event(e)) {
                used = true;
            }
            auto handle_slider = [&](std::unique_ptr<DMSlider>& slider,
                                     auto get_value,
                                     auto set_value,
                                     bool requires_rebuild) {
                if (!slider) return;
                const int previous_value = get_value();
                const bool slider_used = slider->handle_event(e);
                const int committed_value = slider->value();
                if (committed_value != previous_value) {
                    set_value(committed_value);
                    changed = true;
                    reset_scaling_profile = true;
                    rebuild_required = rebuild_required || requires_rebuild;
                    used = true;
                    if (requires_rebuild) {
                        schedule_light_rebuild(i);
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
            handle_slider(r.s_flicker_speed,
                          [&]() { return r.light.flicker_speed; },
                          [&](int v) { r.light.flicker_speed = v; },
                          false);
            handle_slider(r.s_flicker_smoothness,
                          [&]() { return r.light.flicker_smoothness; },
                          [&](int v) { r.light.flicker_smoothness = v; },
                          false);
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
                LightSource new_light{};
                new_light.in_front = true;
                new_light.render_to_dark_mask = true;
                rows_.push_back(create_row_from_light(new_light, true, false));
                shift_pending_rebuild_indices(rows_.empty() ? 0 : rows_.size() - 1);
                changed = true;
                reset_scaling_profile = true;
                rebuild_required = true;
                if (!rows_.empty()) {
                    schedule_light_rebuild(rows_.size() - 1);
                }
                used = true;
                refresh_row_headers();
                refresh_highlight_state();
            }
        }
        if (apply_btn_ && apply_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (ui_) ui_->request_apply_section(AssetInfoSectionId::Lighting);
            }
            return true;
        }
        if (changed) {
            apply_light_change(reset_scaling_profile, rebuild_required);
        }
        return used || changed;
    }

    void render_content(SDL_Renderer* r) const override {
        if (!info_) {
            return;
        }

        for (const auto& rrow : rows_) {
            if (rrow.highlighted && rrow.container_rect.w > 0 && rrow.container_rect.h > 0) {
                SDL_Rect highlight_rect = rrow.container_rect;
                const int inset = 2;
                highlight_rect.x += inset;
                highlight_rect.y += inset;
                highlight_rect.w = std::max(0, highlight_rect.w - inset * 2);
                highlight_rect.h = std::max(0, highlight_rect.h - inset * 2);
                const SDL_Color fill = dm_draw::LightenColor(DMStyles::PanelBG(), 0.08f);
                dm_draw::DrawBeveledRect( r, highlight_rect, DMStyles::CornerRadius(), DMStyles::BevelDepth(), fill, DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
            }
            if (rrow.lbl)      rrow.lbl->render(r);
            if (rrow.b_duplicate) rrow.b_duplicate->render(r);
            if (rrow.b_delete) rrow.b_delete->render(r);
            if (!rrow.expanded) continue;
            if (rrow.s_intensity) rrow.s_intensity->render(r);
            if (rrow.s_radius)    rrow.s_radius->render(r);
            if (rrow.s_falloff)   rrow.s_falloff->render(r);
            if (rrow.s_flicker_speed)   rrow.s_flicker_speed->render(r);
            if (rrow.s_flicker_smoothness)   rrow.s_flicker_smoothness->render(r);
            if (rrow.s_offset_x)  rrow.s_offset_x->render(r);
            if (rrow.s_offset_y)  rrow.s_offset_y->render(r);
            if (rrow.c_front)  rrow.c_front->render(r);
            if (rrow.c_behind) rrow.c_behind->render(r);
            if (rrow.c_dark_mask) rrow.c_dark_mask->render(r);
            if (rrow.c_asset_alpha_mask) rrow.c_asset_alpha_mask->render(r);
            if (rrow.color_widget) rrow.color_widget->render(r);
        }
        if (b_add_) b_add_->render(r);
        if (apply_btn_) apply_btn_->render(r);
    }

private:
    struct Row {
        LightSource light;
        bool expanded = false;
        bool highlighted = false;
        mutable SDL_Rect container_rect{0, 0, 0, 0};
        std::unique_ptr<DMButton> lbl;
        std::unique_ptr<DMButton> b_duplicate;
        std::unique_ptr<DMButton> b_delete;
        std::unique_ptr<DMSlider> s_intensity;
        std::unique_ptr<DMSlider> s_radius;
        std::unique_ptr<DMSlider> s_falloff;
        std::unique_ptr<DMSlider> s_flicker_speed;
        std::unique_ptr<DMSlider> s_flicker_smoothness;
        std::unique_ptr<DMSlider> s_offset_x;
        std::unique_ptr<DMSlider> s_offset_y;
        std::unique_ptr<DMCheckbox> c_front;
        std::unique_ptr<DMCheckbox> c_behind;
        std::unique_ptr<DMCheckbox> c_dark_mask;
        std::unique_ptr<DMCheckbox> c_asset_alpha_mask;
        std::unique_ptr<DMColorRangeWidget> color_widget;
};

    Row create_row_from_light(const LightSource& ls, bool expanded_default, bool include_duplicate_button = true) {
        Row r;
        r.light = ls;
        r.expanded = expanded_default;
        r.lbl = std::make_unique<DMButton>("Light Source", &DMStyles::HeaderButton(), 180, DMButton::height());
        if (include_duplicate_button) {
            r.b_duplicate = std::make_unique<DMButton>("Duplicate", &DMStyles::AccentButton(), 120, DMButton::height());
        }
        r.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 120, DMButton::height());
        r.s_intensity = std::make_unique<DMSlider>("Light Intensity", 0, 255, ls.intensity);
        r.s_radius    = std::make_unique<DMSlider>("Radius (px)", 0, 4000, ls.radius);
        r.s_falloff   = std::make_unique<DMSlider>("Falloff (%)", 0, 100, ls.fall_off);
        r.s_flicker_speed = std::make_unique<DMSlider>("Flicker Speed", 0, 100, ls.flicker_speed);
        r.s_flicker_smoothness =
            std::make_unique<DMSlider>("Flicker Smoothness", 0, 100, ls.flicker_smoothness);
        r.s_offset_x  = std::make_unique<DMSlider>("Offset X", -2000, 2000, ls.offset_x);
        r.s_offset_y  = std::make_unique<DMSlider>("Offset Y", -2000, 2000, ls.offset_y);
        r.c_front          = std::make_unique<DMCheckbox>("Render Texture In Front", ls.in_front);
        r.c_behind         = std::make_unique<DMCheckbox>("Render Texture Behind", ls.behind);
        r.c_dark_mask      = std::make_unique<DMCheckbox>("Render To Dark Mask", ls.render_to_dark_mask);
        r.c_asset_alpha_mask = std::make_unique<DMCheckbox>( "Render Front/Back To Asset Alpha Mask", ls.render_front_and_back_to_asset_alpha_mask);
        r.color_widget = std::make_unique<DMColorRangeWidget>("Light Color");
        {
            DMColorRangeWidget::RangedColor rc;
            rc.r.min = rc.r.max = static_cast<int>(ls.color.r);
            rc.g.min = rc.g.max = static_cast<int>(ls.color.g);
            rc.b.min = rc.b.max = static_cast<int>(ls.color.b);
            rc.a.min = rc.a.max = static_cast<int>(ls.color.a);
            r.color_widget->set_value(rc);
        }
        configure_color_widget(r);
        configure_row_sliders(r);
        return r;
    }

    void refresh_row_headers() {
        for (size_t i = 0; i < rows_.size(); ++i) {
            update_row_header(rows_[i], i);
        }
    }

    void update_row_header(Row& row, size_t index) const {
        if (!row.lbl) {
            return;
        }
        std::string label = "Light " + std::to_string(index + 1) + " ";
        label += row.expanded ? std::string(DMIcons::CollapseExpanded()) : std::string(DMIcons::CollapseCollapsed());
        row.lbl->set_text(label);
    }

    void refresh_highlight_state() {
        if (highlighted_row_index_ < 0 || highlighted_row_index_ >= static_cast<int>(rows_.size())) {
            highlighted_row_index_ = -1;
        }
        for (size_t i = 0; i < rows_.size(); ++i) {
            rows_[i].highlighted = (static_cast<int>(i) == highlighted_row_index_);
        }
    }

    void set_row_expanded(Row& row, bool expanded, size_t index) {
        if (row.expanded == expanded) return;
        row.expanded = expanded;
        if (!row.expanded && row.color_widget && row.color_widget->overlay_visible()) {
            row.color_widget->close_overlay();
        }
        update_row_header(row, index);
    }

    void configure_row_sliders(Row& r) {
        auto configure_rebuild_slider = [](std::unique_ptr<DMSlider>& slider) {
            if (slider) slider->set_defer_commit_until_unfocus(true);
};
        auto configure_normal_slider = [](std::unique_ptr<DMSlider>& slider) {
            if (slider) slider->set_defer_commit_until_unfocus(false);
};

        configure_rebuild_slider(r.s_intensity);
        configure_rebuild_slider(r.s_radius);
        configure_rebuild_slider(r.s_falloff);

        configure_normal_slider(r.s_flicker_speed);
        configure_normal_slider(r.s_flicker_smoothness);
        configure_normal_slider(r.s_offset_x);
        configure_normal_slider(r.s_offset_y);
    }

    void commit_to_info() {
        if (!info_) return;
        std::vector<LightSource> lights;
        for (const auto& r : rows_) lights.push_back(r.light);
        info_->set_lighting(lights);
    }

    void configure_color_widget(Row& r) {
        if (!r.color_widget) {
            return;
        }
        DMColorRangeWidget* widget = r.color_widget.get();
        widget->set_on_value_changed([this, widget](const DMColorRangeWidget::RangedColor& value) {
            this->handle_color_widget_changed(widget, value);
        });
        widget->set_on_sample_requested(
            [this](const DMColorRangeWidget::RangedColor& current,
                   std::function<void(SDL_Color)> on_sample,
                   std::function<void()> on_cancel) {
                if (ui_) {
                    ui_->begin_color_sampling(current, std::move(on_sample), std::move(on_cancel));
                } else if (on_cancel) {
                    on_cancel();
                }
            });
    }

    void handle_color_widget_changed(DMColorRangeWidget* widget,
                                     const DMColorRangeWidget::RangedColor& value) {
        if (!widget) {
            return;
        }
        auto row_it = std::find_if(rows_.begin(), rows_.end(),
                                   [widget](const Row& r) { return r.color_widget.get() == widget; });
        if (row_it == rows_.end()) {
            return;
        }
        SDL_Color new_c{
            static_cast<Uint8>(std::clamp(value.r.min, 0, 255)), static_cast<Uint8>(std::clamp(value.g.min, 0, 255)), static_cast<Uint8>(std::clamp(value.b.min, 0, 255)), static_cast<Uint8>(std::clamp(value.a.min, 0, 255))};
        if (new_c.r == row_it->light.color.r && new_c.g == row_it->light.color.g &&
            new_c.b == row_it->light.color.b && new_c.a == row_it->light.color.a) {
            return;
        }
        row_it->light.color = new_c;
        const std::size_t row_index = static_cast<std::size_t>(row_it - rows_.begin());
        schedule_light_rebuild(row_index);
        apply_light_change(true, true);
    }

    void apply_light_change(bool reset_scaling_profile, bool rebuild_required) {
        commit_to_info();
        if (info_) {
            const bool purge_light_cache = rebuild_required;
            if (ui_ && reset_scaling_profile) {
                ui_->notify_light_sources_modified(purge_light_cache);
                ui_->mark_target_asset_composite_dirty();
            }
            (void)info_->commit_manifest();
            if (rebuild_required) {
                finalize_pending_light_rebuilds();
            } else {
                pending_light_rebuild_indices_.clear();
                pending_full_asset_light_rebuild_ = false;
            }
        }
    }

    void schedule_light_rebuild(std::size_t index) {
        if (pending_full_asset_light_rebuild_ || index >= rows_.size()) {
            return;
        }
        const auto existing = std::find(pending_light_rebuild_indices_.begin(), pending_light_rebuild_indices_.end(), index);
        if (existing == pending_light_rebuild_indices_.end()) {
            pending_light_rebuild_indices_.push_back(index);
        }
    }

    void schedule_full_asset_light_rebuild() {
        if (pending_full_asset_light_rebuild_) {
            return;
        }
        pending_full_asset_light_rebuild_ = true;
        pending_light_rebuild_indices_.clear();
    }

    void shift_pending_rebuild_indices(std::size_t inserted_at) {
        if (pending_light_rebuild_indices_.empty()) {
            return;
        }
        bool shifted = false;
        for (auto& index : pending_light_rebuild_indices_) {
            if (index >= inserted_at) {
                ++index;
                shifted = true;
            }
        }
        if (!shifted) {
            return;
        }
        std::sort(pending_light_rebuild_indices_.begin(), pending_light_rebuild_indices_.end());
        pending_light_rebuild_indices_.erase( std::unique(pending_light_rebuild_indices_.begin(), pending_light_rebuild_indices_.end()), pending_light_rebuild_indices_.end());
    }

    void finalize_pending_light_rebuilds() {
        if (!info_ || !ui_) {
            pending_light_rebuild_indices_.clear();
            pending_full_asset_light_rebuild_ = false;
            return;
        }
        if (pending_full_asset_light_rebuild_) {
            ui_->mark_lighting_asset_for_rebuild();
        } else {
            for (std::size_t index : pending_light_rebuild_indices_) {
                ui_->mark_light_for_rebuild(index);
            }
        }
        pending_light_rebuild_indices_.clear();
        pending_full_asset_light_rebuild_ = false;
    }

    std::vector<Row> rows_;
    int highlighted_row_index_ = -1;
    std::vector<std::size_t> pending_light_rebuild_indices_;
    bool pending_full_asset_light_rebuild_ = false;
    std::unique_ptr<DMButton> b_add_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr;
    std::unique_ptr<ReadOnlyTextBoxWidget> empty_state_widget_;

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
            if (r.s_flicker_speed)        r.s_flicker_speed->set_value(src.flicker_speed);
            if (r.s_flicker_smoothness)   r.s_flicker_smoothness->set_value(src.flicker_smoothness);
            if (r.s_offset_x)  r.s_offset_x->set_value(src.offset_x);
            if (r.s_offset_y)  r.s_offset_y->set_value(src.offset_y);
            if (r.c_front)           r.c_front->set_value(src.in_front);
            if (r.c_behind)          r.c_behind->set_value(src.behind);
            if (r.c_dark_mask)       r.c_dark_mask->set_value(src.render_to_dark_mask);
            if (r.c_asset_alpha_mask)
                r.c_asset_alpha_mask->set_value(src.render_front_and_back_to_asset_alpha_mask);
            if (r.color_widget) {
                DMColorRangeWidget::RangedColor rc;
                rc.r.min = rc.r.max = static_cast<int>(src.color.r);
                rc.g.min = rc.g.max = static_cast<int>(src.color.g);
                rc.b.min = rc.b.max = static_cast<int>(src.color.b);
                rc.a.min = rc.a.max = static_cast<int>(src.color.a);
                r.color_widget->set_value(rc);
                configure_color_widget(r);
            }
        }
        refresh_row_headers();
        refresh_highlight_state();
    }
    void update_light_offsets(std::size_t index, int offset_x, int offset_y) {
        if (index >= rows_.size()) {
            return;
        }
        auto& row = rows_[index];
        row.light.offset_x = offset_x;
        row.light.offset_y = offset_y;
        if (row.s_offset_x) {
            row.s_offset_x->set_value(offset_x);
        }
        if (row.s_offset_y) {
            row.s_offset_y->set_value(offset_y);
        }
    }
    void update(const Input& input, int screen_w, int screen_h) override {
        DockableCollapsible::update(input, screen_w, screen_h);
        for (auto& r : rows_) {
            if (r.expanded && r.color_widget) {
                r.color_widget->update_overlay(input, screen_w, screen_h);
            }
        }
    }
    void render(SDL_Renderer* r) const override {
        DockableCollapsible::render(r);
    }

    void render_overlays(SDL_Renderer* r) const {
        if (!r) {
            return;
        }
        for (const auto& row : rows_) {
            if (row.expanded && row.color_widget) {
                row.color_widget->render_overlay(r);
            }
        }
    }
};

inline void Section_Lighting::set_highlighted_light(std::optional<std::size_t> index) {
    int new_index = -1;
    if (index && *index < rows_.size()) {
        new_index = static_cast<int>(*index);
    }
    if (new_index == highlighted_row_index_) {
        return;
    }
    highlighted_row_index_ = new_index;
    refresh_highlight_state();
}

inline void Section_Lighting::expand_light_row(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    set_row_expanded(rows_[index], true, index);
}
