#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dev_mode/DockableCollapsible.hpp"
#include "dev_mode/widgets.hpp"

namespace animation_editor {

class FrameToolsPanel : public DockableCollapsible {
public:
    enum class Mode { Movement = 0, Children = 1, AttackGeometry = 2, HitGeometry = 3 };

    FrameToolsPanel();

    void set_mode(Mode mode);
    Mode mode() const { return mode_; }

    // Movement mode widgets wiring
    void set_callbacks(std::function<void(bool)> on_toggle_smooth,
                       std::function<void(bool)> on_toggle_show_animation,
                       std::function<void(int,int)> on_totals_changed);
    void set_children_callbacks(std::function<void(int)> on_child_selected,
                                std::function<void()> on_apply_to_next,
                                std::function<void(bool)> on_visible_changed);

    // Keep UI in sync with editor state
    void set_totals(int dx, int dy, bool avoid_overwrite_if_editing);
    void set_show_animation(bool show);
    void set_children_state(const std::vector<std::string>& options,
                            int selected_index,
                            bool visible,
                            bool enabled);

    // Allow clamp/move inside FrameEditor work area
    void set_work_area_bounds(const SDL_Rect& bounds);

    // Relay events and render via base
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override { DockableCollapsible::render(r); }

private:
    void rebuild_rows();

private:
    Mode mode_ = Mode::Movement;
    std::unique_ptr<DMCheckbox> smooth_checkbox_;
    std::unique_ptr<DMCheckbox> show_anim_checkbox_;
    std::unique_ptr<DMTextBox> dx_box_;
    std::unique_ptr<DMTextBox> dy_box_;
    std::unique_ptr<CheckboxWidget> smooth_widget_;
    std::unique_ptr<CheckboxWidget> show_anim_w_;
    std::unique_ptr<TextBoxWidget> dx_w_;
    std::unique_ptr<TextBoxWidget> dy_w_;
    std::unique_ptr<DMDropdown> child_dropdown_;
    std::unique_ptr<DropdownWidget> child_dropdown_widget_;
    std::unique_ptr<DMButton> child_apply_button_;
    std::unique_ptr<ButtonWidget> child_apply_widget_;
    std::unique_ptr<DMCheckbox> child_visible_checkbox_;
    std::unique_ptr<CheckboxWidget> child_visible_widget_;

    std::function<void(bool)> on_toggle_smooth_{};
    std::function<void(bool)> on_toggle_show_animation_{};
    std::function<void(int,int)> on_totals_changed_{};
    std::function<void(int)> on_child_selected_{};
    std::function<void()> on_child_apply_to_next_{};
    std::function<void(bool)> on_child_visible_{};

    // Track last-known values to detect edits
    std::string last_dx_text_{};
    std::string last_dy_text_{};
    bool last_smooth_value_ = false;
    bool last_checkbox_value_ = true;
    std::vector<std::string> child_options_;
    int child_selected_index_ = -1;
    bool child_visible_state_ = true;
    bool children_controls_enabled_ = false;
    int child_dropdown_last_index_ = -1;
};

}
