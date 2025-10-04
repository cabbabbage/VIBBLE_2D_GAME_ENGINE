#pragma once

#include <memory>
#include <string>
#include <functional>
#include <SDL.h>

class DockableCollapsible;
class DMButton;
class ButtonWidget;
class Input;

// A small floating panel offering creation of a new room area.
// Shows: "Create new room area:" with options for Trigger and Spawn.
class CreateRoomAreaPanel {
public:
    using CreateCallback = std::function<void(const std::string& type)>;

    CreateRoomAreaPanel();
    ~CreateRoomAreaPanel();

    void set_on_create(CreateCallback cb) { on_create_ = std::move(cb); }

    void open_at(int screen_x, int screen_y);
    void close();
    bool visible() const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_point_inside(int x, int y) const;

    // Used for stacking/focus management
    DockableCollapsible* panel();

private:
    void ensure_panel();
    void rebuild_rows();

private:
    std::unique_ptr<DockableCollapsible> panel_;
    std::unique_ptr<DMButton> label_btn_;
    std::unique_ptr<DMButton> trigger_btn_;
    std::unique_ptr<DMButton> spawn_btn_;
    std::unique_ptr<ButtonWidget> trigger_widget_;
    std::unique_ptr<ButtonWidget> spawn_widget_;
    CreateCallback on_create_;
};

