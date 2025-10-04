#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <SDL.h>

class DockableCollapsible;
class DMDropdown;
class DropdownWidget;
class DMButton;
class ButtonWidget;
class Input;

// Floating editor for a selected room area: change type and delete.
class EditRoomAreaPanel {
public:
    using ChangeTypeCallback = std::function<void(const std::string&)>;
    using ChangeNameCallback = std::function<void(const std::string&)>;
    using DeleteCallback     = std::function<void()>;

    EditRoomAreaPanel();
    ~EditRoomAreaPanel();

    void set_area_types(const std::vector<std::string>& types);
    void set_selected_type(const std::string& type_value);
    void set_selected_name(const std::string& name_value);

    void set_on_change_type(ChangeTypeCallback cb) { on_change_type_ = std::move(cb); }
    void set_on_change_name(ChangeNameCallback cb) { on_change_name_ = std::move(cb); }
    void set_on_delete(DeleteCallback cb) { on_delete_ = std::move(cb); }

    void open(int screen_x, int screen_y); // place panel
    void close();
    bool visible() const;

    void update(const Input& input, int screen_w, int screen_h);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_point_inside(int x, int y) const;

private:
    void ensure_panel();
    void rebuild_rows();
    void maybe_emit_change();

private:
    std::unique_ptr<DockableCollapsible> panel_;
    std::unique_ptr<DMDropdown> type_dd_;
    std::unique_ptr<DropdownWidget> type_widget_;
    std::unique_ptr<class DMTextBox> name_tb_;
    std::unique_ptr<class TextBoxWidget> name_widget_;
    std::unique_ptr<DMButton> delete_btn_;
    std::unique_ptr<ButtonWidget> delete_widget_;
    std::vector<std::string> types_;
    int last_selected_index_ = -1;
    ChangeTypeCallback on_change_type_;
    ChangeNameCallback on_change_name_;
    DeleteCallback on_delete_;
};
