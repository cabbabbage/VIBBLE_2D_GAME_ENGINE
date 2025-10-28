#pragma once

#include <SDL.h>
#include <memory>
#include <string>
#include <functional>

#include "dev_mode/DockableCollapsible.hpp"

class Room;
class DMTextBox;
class DMCheckbox;
class DMButton;

// Minimal Area Config panel for room-scoped Areas
// - Rename Area
// - Toggle scale-to-room
// - Edit Shape button (invokes callback)
class AreaConfigPanel : public DockableCollapsible {
public:
    AreaConfigPanel();
    ~AreaConfigPanel() override;

    void open(Room* room, const std::string& area_name);
    void close();
    bool visible() const { return is_visible(); }

    void set_on_edit_shape(std::function<void(const std::string&)> cb) { on_edit_shape_ = std::move(cb); }
    void set_on_changed(std::function<void()> cb) { on_changed_ = std::move(cb); }

    // Optional explicit bounds (right-docked suggested)
    void set_bounds(const SDL_Rect& r);

    // DockableCollapsible
    void build() override;
    void update(const class Input& input, int screen_w, int screen_h) override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;

    bool is_point_inside(int x, int y) const override;

private:
    void persist_scale_flag(bool value);
    void try_rename_if_needed();
    void reload_values();

private:
    Room* room_ = nullptr;
    std::string area_name_;
    bool last_scale_value_ = false;

    std::unique_ptr<DMTextBox> name_box_;
    std::unique_ptr<class TextBoxWidget> name_widget_;
    std::unique_ptr<DMCheckbox> scale_checkbox_;
    std::unique_ptr<class CheckboxWidget> scale_widget_;
    std::unique_ptr<DMButton> edit_btn_;
    std::unique_ptr<class ButtonWidget> edit_btn_widget_;

    std::function<void(const std::string&)> on_edit_shape_{};
    std::function<void()> on_changed_{};
};

