#include "area_mode/edit_room_area_panel.hpp"

#include "dev_mode/DockableCollapsible.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "utils/input.hpp"

EditRoomAreaPanel::EditRoomAreaPanel() = default;
EditRoomAreaPanel::~EditRoomAreaPanel() = default;

void EditRoomAreaPanel::ensure_panel() {
    if (panel_) return;
    panel_ = std::make_unique<DockableCollapsible>("Selected Area", true, 48, 96);
    panel_->set_padding(12);
    panel_->set_close_button_enabled(true);
}

void EditRoomAreaPanel::set_area_types(const std::vector<std::string>& types) {
    types_ = types;
    if (!type_dd_) {
        type_dd_ = std::make_unique<DMDropdown>("Type", types_, 0);
        type_widget_ = std::make_unique<DropdownWidget>(type_dd_.get());
    }
    rebuild_rows();
}

void EditRoomAreaPanel::set_selected_type(const std::string& type_value) {
    if (!type_dd_) return;
    int idx = 0;
    for (size_t i = 0; i < types_.size(); ++i) {
        if (types_[i] == type_value) { idx = static_cast<int>(i); break; }
    }
    // DMDropdown does not expose setter; re-create to set index
    type_dd_ = std::make_unique<DMDropdown>("Type", types_, idx);
    type_widget_ = std::make_unique<DropdownWidget>(type_dd_.get());
    last_selected_index_ = idx;
    rebuild_rows();
}

void EditRoomAreaPanel::set_selected_name(const std::string& name_value) {
    if (!panel_) ensure_panel();
    if (!name_tb_) {
        name_tb_ = std::make_unique<DMTextBox>("Name", name_value);
    } else {
        name_tb_->set_value(name_value);
    }
    rebuild_rows();
}

void EditRoomAreaPanel::rebuild_rows() {
    ensure_panel();
    if (!panel_) return;
    if (!name_tb_) {
        name_tb_ = std::make_unique<DMTextBox>("Name", "");
    }
    if (!delete_btn_) {
        delete_btn_ = std::make_unique<DMButton>("Delete this area", &DMStyles::DeleteButton(), 180, DMButton::height());
        delete_widget_ = std::make_unique<ButtonWidget>(delete_btn_.get(), [this]() {
            if (on_delete_) on_delete_();
            if (panel_) panel_->close();
        });
    }
    DockableCollapsible::Rows rows;
    if (!name_widget_) name_widget_ = std::make_unique<TextBoxWidget>(name_tb_.get(), true);
    rows.push_back(std::vector<Widget*>{ type_widget_.get() });
    rows.push_back(std::vector<Widget*>{ name_widget_.get() });
    rows.push_back(std::vector<Widget*>{ delete_widget_.get() });
    panel_->set_rows(rows);
}

void EditRoomAreaPanel::maybe_emit_change() {
    if (!type_dd_) return;
    int sel = type_dd_->selected();
    if (sel != last_selected_index_) {
        last_selected_index_ = sel;
        if (sel >= 0 && sel < static_cast<int>(types_.size()) && on_change_type_) {
            on_change_type_(types_[sel]);
        }
    }
}

void EditRoomAreaPanel::open(int screen_x, int screen_y) {
    ensure_panel();
    if (!panel_) return;
    const int w = 300;
    const int h = 140;
    int x = std::max(0, screen_x);
    int y = std::max(0, screen_y);
    panel_->set_rect(SDL_Rect{ x, y, w, h });
    panel_->open();
}

void EditRoomAreaPanel::close() {
    if (panel_) panel_->close();
}

bool EditRoomAreaPanel::visible() const {
    return panel_ && panel_->is_visible();
}

void EditRoomAreaPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!panel_) return;
    panel_->update(input, screen_w, screen_h);
    maybe_emit_change();
    if (name_tb_ && on_change_name_) {
        on_change_name_(name_tb_->value());
    }
}

bool EditRoomAreaPanel::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    return panel_->handle_event(e);
}

void EditRoomAreaPanel::render(SDL_Renderer* r) const {
    if (!panel_ || !panel_->is_visible()) return;
    panel_->render(r);
}

bool EditRoomAreaPanel::is_point_inside(int x, int y) const {
    if (!panel_) return false;
    return panel_->is_point_inside(x, y);
}
