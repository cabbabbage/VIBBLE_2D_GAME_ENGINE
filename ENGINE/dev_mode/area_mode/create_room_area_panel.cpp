#include "area_mode/create_room_area_panel.hpp"

#include "dev_mode/DockableCollapsible.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "utils/input.hpp"

CreateRoomAreaPanel::CreateRoomAreaPanel() = default;
CreateRoomAreaPanel::~CreateRoomAreaPanel() = default;

void CreateRoomAreaPanel::ensure_panel() {
    if (panel_) return;
    panel_ = std::make_unique<DockableCollapsible>("Create new room area:", true, 0, 0);
    panel_->set_padding(12);
    label_btn_ = std::make_unique<DMButton>("Create new room area:", &DMStyles::HeaderButton(), 260, DMButton::height());
    trigger_btn_ = std::make_unique<DMButton>("trigger", &DMStyles::CreateButton(), 120, DMButton::height());
    spawn_btn_ = std::make_unique<DMButton>("spawn", &DMStyles::CreateButton(), 120, DMButton::height());
    trigger_widget_ = std::make_unique<ButtonWidget>(trigger_btn_.get(), [this]() {
        if (on_create_) on_create_(std::string("trigger"));
        close();
    });
    spawn_widget_ = std::make_unique<ButtonWidget>(spawn_btn_.get(), [this]() {
        if (on_create_) on_create_(std::string("spawn"));
        close();
    });
    rebuild_rows();
}

void CreateRoomAreaPanel::rebuild_rows() {
    if (!panel_) return;
    DockableCollapsible::Rows rows;
    // Just the two action buttons (label is the header title)
    rows.push_back({ trigger_widget_.get(), spawn_widget_.get() });
    panel_->set_rows(rows);
}

void CreateRoomAreaPanel::open_at(int screen_x, int screen_y) {
    ensure_panel();
    if (!panel_) return;
    const int w = 270;
    const int h = DMButton::height() + DMSpacing::panel_padding() * 2 + DMSpacing::item_gap();
    // Position above the click (drop up)
    int x = screen_x - w / 2;
    int y = screen_y - h - 8;
    panel_->set_rect(SDL_Rect{x, y, w, h});
    panel_->open();
}

void CreateRoomAreaPanel::close() {
    if (panel_) panel_->close();
}

bool CreateRoomAreaPanel::visible() const {
    return panel_ && panel_->is_visible();
}

void CreateRoomAreaPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!panel_) return;
    panel_->update(input, screen_w, screen_h);
}

bool CreateRoomAreaPanel::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    return panel_->handle_event(e);
}

void CreateRoomAreaPanel::render(SDL_Renderer* r) const {
    if (!panel_ || !panel_->is_visible()) return;
    panel_->render(r);
}

bool CreateRoomAreaPanel::is_point_inside(int x, int y) const {
    if (!panel_) return false;
    return panel_->is_point_inside(x, y);
}

DockableCollapsible* CreateRoomAreaPanel::panel() {
    return panel_.get();
}

