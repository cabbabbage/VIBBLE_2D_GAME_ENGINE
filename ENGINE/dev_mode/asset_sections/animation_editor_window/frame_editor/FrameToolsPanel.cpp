#include "FrameToolsPanel.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "dev_mode/dm_styles.hpp"

FrameToolsPanel::FrameToolsPanel()
    : DockableCollapsible("Tools", true /*floatable*/, 32, 32) {
    set_show_header(true);
    // Build initial widgets for movement mode
    smooth_btn_ = std::make_unique<DMButton>("Smooth", &DMStyles::AccentButton(), 120, DMButton::height());
    show_anim_checkbox_ = std::make_unique<DMCheckbox>("Show Animation", true);
    dx_box_ = std::make_unique<DMTextBox>("Total dX", "0");
    dy_box_ = std::make_unique<DMTextBox>("Total dY", "0");

    smooth_btn_w_ = std::make_unique<ButtonWidget>(smooth_btn_.get());
    show_anim_w_ = std::make_unique<CheckboxWidget>(show_anim_checkbox_.get());
    dx_w_ = std::make_unique<TextBoxWidget>(dx_box_.get(), false);
    dy_w_ = std::make_unique<TextBoxWidget>(dy_box_.get(), false);

    last_dx_text_ = dx_box_->value();
    last_dy_text_ = dy_box_->value();
    last_checkbox_value_ = show_anim_checkbox_->value();

    rebuild_rows();
}

void FrameToolsPanel::set_mode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    rebuild_rows();
}

void FrameToolsPanel::set_callbacks(std::function<void()> on_smooth,
                                    std::function<void(bool)> on_toggle_show_animation,
                                    std::function<void(int,int)> on_totals_changed) {
    on_smooth_ = std::move(on_smooth);
    on_toggle_show_animation_ = std::move(on_toggle_show_animation);
    on_totals_changed_ = std::move(on_totals_changed);
}

void FrameToolsPanel::set_totals(int dx, int dy, bool avoid_overwrite_if_editing) {
    if (!dx_box_ || !dy_box_) return;
    const bool editing = dx_box_->is_editing() || dy_box_->is_editing();
    if (avoid_overwrite_if_editing && editing) {
        return;
    }
    const std::string dxs = std::to_string(dx);
    const std::string dys = std::to_string(dy);
    if (dx_box_->value() != dxs) dx_box_->set_value(dxs);
    if (dy_box_->value() != dys) dy_box_->set_value(dys);
    last_dx_text_ = dx_box_->value();
    last_dy_text_ = dy_box_->value();
}

void FrameToolsPanel::set_show_animation(bool show) {
    if (show_anim_checkbox_) {
        show_anim_checkbox_->set_value(show);
        last_checkbox_value_ = show;
    }
}

void FrameToolsPanel::set_work_area_bounds(const SDL_Rect& bounds) {
    set_work_area(bounds);
}

bool FrameToolsPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;

    bool consumed = DockableCollapsible::handle_event(e);

    // Smooth button click detection on mouse up inside rect
    if (smooth_btn_) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{e.button.x, e.button.y};
            if (SDL_PointInRect(&p, &smooth_btn_->rect())) {
                if (on_smooth_) on_smooth_();
                consumed = true;
            }
        }
    }

    // Checkbox toggling detection
    if (show_anim_checkbox_) {
        bool current = show_anim_checkbox_->value();
        if (current != last_checkbox_value_) {
            last_checkbox_value_ = current;
            if (on_toggle_show_animation_) on_toggle_show_animation_(current);
            consumed = true;
        }
    }

    // Totals edit: parse and call on change for integers
    auto parse_int = [](const std::string& s, int& out) -> bool {
        try {
            size_t idx = 0;
            int v = std::stoi(s, &idx);
            if (idx == s.size()) { out = v; return true; }
        } catch (...) {}
        return false;
    };
    if (dx_box_ && dy_box_) {
        const std::string now_dx = dx_box_->value();
        const std::string now_dy = dy_box_->value();
        if (now_dx != last_dx_text_ || now_dy != last_dy_text_) {
            int dx = 0, dy = 0;
            bool okx = parse_int(now_dx, dx);
            bool oky = parse_int(now_dy, dy);
            last_dx_text_ = now_dx;
            last_dy_text_ = now_dy;
            if (okx && oky && on_totals_changed_) {
                on_totals_changed_(dx, dy);
                consumed = true;
            }
        }
    }

    return consumed;
}

void FrameToolsPanel::rebuild_rows() {
    Rows rows;
    switch (mode_) {
        case Mode::Movement: {
            rows.push_back({ smooth_btn_w_.get() });
            rows.push_back({ show_anim_w_.get() });
            rows.push_back({ dx_w_.get(), dy_w_.get() });
            break;
        }
        case Mode::Children:
        case Mode::Attacking: {
            rows.clear(); // empty for now
            break;
        }
    }
    set_rows(rows);
}

