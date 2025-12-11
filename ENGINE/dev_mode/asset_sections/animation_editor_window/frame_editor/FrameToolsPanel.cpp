#include "FrameToolsPanel.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "dev_mode/dm_styles.hpp"

namespace animation_editor {
namespace {

bool is_children_mode(FrameToolsPanel::Mode mode) {
    return mode == FrameToolsPanel::Mode::StaticChildren ||
           mode == FrameToolsPanel::Mode::AsyncChildren;
}
}

FrameToolsPanel::FrameToolsPanel()
    : DockableCollapsible("Tools", true , 32, 32) {
    set_show_header(true);

    smooth_checkbox_ = std::make_unique<DMCheckbox>("Smooth", false);
    curve_checkbox_  = std::make_unique<DMCheckbox>("Curve", false);
    show_anim_checkbox_ = std::make_unique<DMCheckbox>("Show Animation", true);
    dx_box_ = std::make_unique<DMTextBox>("Total dX", "0");
    dy_box_ = std::make_unique<DMTextBox>("Total dY", "0");

    smooth_widget_ = std::make_unique<CheckboxWidget>(smooth_checkbox_.get());
    curve_widget_  = std::make_unique<CheckboxWidget>(curve_checkbox_.get());
    show_anim_w_ = std::make_unique<CheckboxWidget>(show_anim_checkbox_.get());
    dx_w_ = std::make_unique<TextBoxWidget>(dx_box_.get(), false);
    dy_w_ = std::make_unique<TextBoxWidget>(dy_box_.get(), false);

    child_dropdown_ = std::make_unique<DMDropdown>("Child", std::vector<std::string>{}, 0);
    child_dropdown_widget_ = std::make_unique<DropdownWidget>(child_dropdown_.get());
    child_mode_dropdown_ = std::make_unique<DMDropdown>("Mode", std::vector<std::string>{"Static", "Async"}, 0);
    child_mode_widget_ = std::make_unique<DropdownWidget>(child_mode_dropdown_.get());
    child_apply_button_ = std::make_unique<DMButton>("Apply current frame settings to next", &DMStyles::AccentButton(), 240, DMButton::height());
    child_apply_widget_ = std::make_unique<ButtonWidget>(child_apply_button_.get(), [this]() {
        if (children_controls_enabled_ && has_child_options_ && on_child_apply_to_next_) {
            on_child_apply_to_next_();
        }
    });
    child_name_box_ = std::make_unique<DMTextBox>("Child Asset", "");
    child_name_widget_ = std::make_unique<TextBoxWidget>(child_name_box_.get(), false);
    child_add_button_ = std::make_unique<DMButton>("Add / Rename", &DMStyles::AccentButton(), 160, DMButton::height());
    child_add_widget_ = std::make_unique<ButtonWidget>(child_add_button_.get(), [this]() {
        if (on_child_add_or_rename_) {
            on_child_add_or_rename_(child_name_box_ ? child_name_box_->value() : std::string{});
        }
    });
    child_remove_button_ = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(), 120, DMButton::height());
    child_remove_widget_ = std::make_unique<ButtonWidget>(child_remove_button_.get(), [this]() {
        if (on_child_remove_) {
            on_child_remove_();
        }
    });
    child_visible_checkbox_ = std::make_unique<DMCheckbox>("Visible", true);
    child_visible_widget_ = std::make_unique<CheckboxWidget>(child_visible_checkbox_.get());

    last_dx_text_ = dx_box_->value();
    last_dy_text_ = dy_box_->value();
    last_checkbox_value_ = show_anim_checkbox_->value();
    last_curve_value_ = curve_checkbox_->value();
    child_mode_last_index_ = child_mode_dropdown_->selected();
    last_child_name_text_ = child_name_box_->value();

    rebuild_rows();
}

void FrameToolsPanel::set_mode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    rebuild_rows();
}

void FrameToolsPanel::set_callbacks(std::function<void(bool)> on_toggle_smooth,
                                    std::function<void(bool)> on_toggle_curve,
                                    std::function<void(bool)> on_toggle_show_animation,
                                    std::function<void(int,int)> on_totals_changed) {
    on_toggle_smooth_ = std::move(on_toggle_smooth);
    on_toggle_curve_ = std::move(on_toggle_curve);
    on_toggle_show_animation_ = std::move(on_toggle_show_animation);
    on_totals_changed_ = std::move(on_totals_changed);
}

void FrameToolsPanel::set_children_callbacks(std::function<void(int)> on_child_selected,
                                             std::function<void()> on_apply_to_next,
                                             std::function<void(bool)> on_visible_changed,
                                             std::function<void(int)> on_mode_changed,
                                             std::function<void(const std::string&)> on_add_or_rename,
                                             std::function<void()> on_remove_child) {
    on_child_selected_ = std::move(on_child_selected);
    on_child_apply_to_next_ = std::move(on_apply_to_next);
    on_child_visible_ = std::move(on_visible_changed);
    on_child_mode_changed_ = std::move(on_mode_changed);
    on_child_add_or_rename_ = std::move(on_add_or_rename);
    on_child_remove_ = std::move(on_remove_child);
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

void FrameToolsPanel::set_children_state(const std::vector<std::string>& options,
                                         int selected_index,
                                         bool visible,
                                         bool enabled,
                                         int mode_index,
                                         const std::string& current_name) {
    child_options_ = options;
    has_child_options_ = !child_options_.empty();
    children_controls_enabled_ = enabled;
    std::vector<std::string> dropdown_options = has_child_options_ ? child_options_
                                                             : std::vector<std::string>{"(no children configured)"};
    int clamped_index = 0;
    if (has_child_options_) {
        clamped_index = std::clamp(selected_index, 0, static_cast<int>(child_options_.size()) - 1);
    }
    child_selected_index_ = (children_controls_enabled_ && has_child_options_) ? clamped_index : -1;
    child_dropdown_last_index_ = child_selected_index_;
    child_visible_state_ = (children_controls_enabled_ && has_child_options_) ? visible : false;
    child_mode_last_index_ = std::clamp(mode_index, 0, 1);

    child_dropdown_ = std::make_unique<DMDropdown>("Child", dropdown_options, clamped_index);
    child_dropdown_widget_ = std::make_unique<DropdownWidget>(child_dropdown_.get());
    child_mode_dropdown_ = std::make_unique<DMDropdown>("Mode", std::vector<std::string>{"Static", "Async"}, child_mode_last_index_);
    child_mode_widget_ = std::make_unique<DropdownWidget>(child_mode_dropdown_.get());
    if (child_visible_checkbox_) {
        child_visible_checkbox_->set_value(child_visible_state_);
    }
    if (child_apply_button_) {
        const DMButtonStyle* style = (children_controls_enabled_ && has_child_options_) ? &DMStyles::AccentButton() : &DMStyles::HeaderButton();
        child_apply_button_->set_style(style);
    }
    if (is_children_mode(mode_)) {
        rebuild_rows();
    }

    if (child_name_box_ && !child_name_box_->is_editing()) {
        const bool should_override = !current_name.empty() || child_name_box_->value().empty();
        if (should_override) {
            child_name_box_->set_value(current_name);
            last_child_name_text_ = child_name_box_->value();
        }
    }
    const bool has_name = child_name_box_ && !child_name_box_->value().empty();
    const DMButtonStyle* add_style = has_name ? &DMStyles::AccentButton() : &DMStyles::HeaderButton();
    if (child_add_button_) {
        child_add_button_->set_style(add_style);
    }
    const bool can_remove = has_child_options_;
    if (child_remove_button_) {
        child_remove_button_->set_style(can_remove ? &DMStyles::DeleteButton() : &DMStyles::HeaderButton());
    }
}

void FrameToolsPanel::set_work_area_bounds(const SDL_Rect& bounds) {
    set_work_area(bounds);
}

bool FrameToolsPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;

    bool consumed = DockableCollapsible::handle_event(e);

    if (mode_ == Mode::Movement) {

        if (smooth_checkbox_) {
            bool current = smooth_checkbox_->value();
            if (current != last_smooth_value_) {
                last_smooth_value_ = current;
                if (on_toggle_smooth_) on_toggle_smooth_(current);

                rebuild_rows();
                consumed = true;
            }
        }

        if (curve_checkbox_) {
            bool current = curve_checkbox_->value();
            if (current != last_curve_value_) {
                last_curve_value_ = current;
                if (on_toggle_curve_) on_toggle_curve_(current);
                consumed = true;
            }
        }

        if (show_anim_checkbox_) {
            bool current = show_anim_checkbox_->value();
            if (current != last_checkbox_value_) {
                last_checkbox_value_ = current;
                if (on_toggle_show_animation_) on_toggle_show_animation_(current);
                consumed = true;
            }
        }

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
    } else if (is_children_mode(mode_)) {
        if (child_name_box_) {
            const std::string now = child_name_box_->value();
            if (now != last_child_name_text_) {
                last_child_name_text_ = now;
                const bool has_name = !now.empty();
                if (child_add_button_) {
                    child_add_button_->set_style(has_name ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
                }
                consumed = true;
            }
        }
        if (children_controls_enabled_ && has_child_options_ && child_dropdown_) {
            int current = child_dropdown_->selected();
            if (current != child_dropdown_last_index_) {
                child_dropdown_last_index_ = current;
                if (on_child_selected_) {
                    on_child_selected_(current);
                }
                consumed = true;
            }
        }
        if (children_controls_enabled_ && has_child_options_ && child_mode_dropdown_) {
            int current = child_mode_dropdown_->selected();
            if (current != child_mode_last_index_) {
                child_mode_last_index_ = current;
                if (on_child_mode_changed_) {
                    on_child_mode_changed_(current);
                }
                consumed = true;
            }
        }
        if (children_controls_enabled_ && has_child_options_ && child_visible_checkbox_) {
            bool current = child_visible_checkbox_->value();
            if (current != child_visible_state_) {
                child_visible_state_ = current;
                if (on_child_visible_) {
                    on_child_visible_(current);
                }
                consumed = true;
            }
        } else if (!children_controls_enabled_ && child_visible_checkbox_) {
            child_visible_checkbox_->set_value(child_visible_state_);
        }

        if (child_add_widget_ && child_add_widget_->handle_event(e)) {
            consumed = true;
        }
        if (child_remove_widget_ && child_remove_widget_->handle_event(e)) {
            consumed = true;
        }
    }

    return consumed;
}

void FrameToolsPanel::rebuild_rows() {
    Rows rows;
    switch (mode_) {
        case Mode::Movement: {
            rows.push_back({ smooth_widget_.get() });
            if (smooth_checkbox_ && smooth_checkbox_->value()) {
                rows.push_back({ curve_widget_.get() });
            }
            rows.push_back({ show_anim_w_.get() });
            rows.push_back({ dx_w_.get(), dy_w_.get() });
            break;
        }
        case Mode::StaticChildren:
        case Mode::AsyncChildren: {
            rows.push_back({ child_dropdown_widget_.get() });
            rows.push_back({ child_mode_widget_.get(), child_visible_widget_.get() });
            rows.push_back({ child_apply_widget_.get() });
            rows.push_back({ child_name_widget_.get() });
            rows.push_back({ child_add_widget_.get(), child_remove_widget_.get() });
            break;
        }
        case Mode::AttackGeometry:
        case Mode::HitGeometry: {
            rows.clear();
            break;
        }
    }
    set_rows(rows);
}

}
