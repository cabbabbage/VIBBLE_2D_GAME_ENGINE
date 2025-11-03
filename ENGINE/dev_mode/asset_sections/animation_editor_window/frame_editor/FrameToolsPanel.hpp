#pragma once

#include <SDL.h>
#include <functional>
#include <memory>
#include <string>

#include "dev_mode/DockableCollapsible.hpp"
#include "dev_mode/widgets.hpp"

class FrameToolsPanel : public DockableCollapsible {
public:
    enum class Mode { Movement = 0, Children = 1, Attacking = 2 };

    FrameToolsPanel();

    void set_mode(Mode mode);
    Mode mode() const { return mode_; }

    // Movement mode widgets wiring
    void set_callbacks(std::function<void()> on_smooth,
                       std::function<void(bool)> on_toggle_show_animation,
                       std::function<void(int,int)> on_totals_changed);

    // Keep UI in sync with editor state
    void set_totals(int dx, int dy, bool avoid_overwrite_if_editing);
    void set_show_animation(bool show);

    // Allow clamp/move inside FrameEditor work area
    void set_work_area_bounds(const SDL_Rect& bounds);

    // Relay events and render via base
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override { DockableCollapsible::render(r); }

private:
    void rebuild_rows();

private:
    Mode mode_ = Mode::Movement;
    std::unique_ptr<DMButton> smooth_btn_;
    std::unique_ptr<DMCheckbox> show_anim_checkbox_;
    std::unique_ptr<DMTextBox> dx_box_;
    std::unique_ptr<DMTextBox> dy_box_;
    std::unique_ptr<ButtonWidget> smooth_btn_w_;
    std::unique_ptr<CheckboxWidget> show_anim_w_;
    std::unique_ptr<TextBoxWidget> dx_w_;
    std::unique_ptr<TextBoxWidget> dy_w_;

    std::function<void()> on_smooth_{};
    std::function<void(bool)> on_toggle_show_animation_{};
    std::function<void(int,int)> on_totals_changed_{};

    // Track last-known values to detect edits
    std::string last_dx_text_{};
    std::string last_dy_text_{};
    bool last_checkbox_value_ = true;
};

