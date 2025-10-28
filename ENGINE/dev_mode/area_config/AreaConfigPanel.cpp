#include "AreaConfigPanel.hpp"

#include <nlohmann/json.hpp>

#include "dev_mode/widgets.hpp"
#include "dev_mode/dm_styles.hpp"
#include "map_generation/room.hpp"

AreaConfigPanel::AreaConfigPanel()
    : DockableCollapsible("Area Config", false) {
    set_show_header(true);
    set_close_button_enabled(true);
    set_scroll_enabled(false);
    set_floating_content_width(360);
}

AreaConfigPanel::~AreaConfigPanel() = default;

void AreaConfigPanel::open(Room* room, const std::string& area_name) {
    room_ = room;
    area_name_ = area_name;
    reload_values();
    set_visible(true);
    set_expanded(true);
}

void AreaConfigPanel::close() {
    set_visible(false);
}

void AreaConfigPanel::set_bounds(const SDL_Rect& r) {
    set_rect(r);
}

void AreaConfigPanel::build() {
    DockableCollapsible::Rows rows;

    if (!name_box_) name_box_ = std::make_unique<DMTextBox>("Area Name", area_name_);
    if (!name_widget_) name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get(), true);
    rows.push_back({ name_widget_.get() });

    if (!scale_checkbox_) scale_checkbox_ = std::make_unique<DMCheckbox>("Scale to room", last_scale_value_);
    if (!scale_widget_) scale_widget_ = std::make_unique<CheckboxWidget>(scale_checkbox_.get());
    rows.push_back({ scale_widget_.get() });

    if (!edit_btn_) edit_btn_ = std::make_unique<DMButton>("Edit Shape", &DMStyles::CreateButton(), 160, DMButton::height());
    if (!edit_btn_widget_) edit_btn_widget_ = std::make_unique<ButtonWidget>(edit_btn_.get(), [this]() {
        if (on_edit_shape_) on_edit_shape_(area_name_);
    });
    rows.push_back({ edit_btn_widget_.get() });

    set_rows(rows);
}

void AreaConfigPanel::update(const Input& input, int screen_w, int screen_h) {
    (void)input; (void)screen_w; (void)screen_h;
    // Apply rename when box not editing
    try_rename_if_needed();

    // Apply scale flag toggle
    const bool current = scale_checkbox_ ? scale_checkbox_->value() : last_scale_value_;
    if (current != last_scale_value_) {
        last_scale_value_ = current;
        persist_scale_flag(current);
        if (on_changed_) on_changed_();
    }
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool AreaConfigPanel::handle_event(const SDL_Event& e) {
    bool used = DockableCollapsible::handle_event(e);
    if (used) return true;
    return false;
}

void AreaConfigPanel::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
}

bool AreaConfigPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void AreaConfigPanel::try_rename_if_needed() {
    if (!room_ || !name_box_) return;
    if (name_box_->is_editing()) return;
    const std::string desired = name_box_->value();
    if (desired.empty() || desired == area_name_) return;

    // Validate uniqueness and apply
    if (room_->rename_area(area_name_, desired)) {
        area_name_ = desired;
        if (on_changed_) on_changed_();
        room_->save_assets_json();
        // Refresh our UI values after rename
        reload_values();
        build();
    } else {
        // Revert
        name_box_->set_value(area_name_);
    }
}

void AreaConfigPanel::persist_scale_flag(bool value) {
    if (!room_) return;
    try {
        nlohmann::json& root = room_->assets_data();
        if (root.contains("areas") && root["areas"].is_array()) {
            for (auto& entry : root["areas"]) {
                if (!entry.is_object()) continue;
                if (entry.value("name", std::string{}) == area_name_) {
                    if (value) {
                        entry["scale_to_room"] = true;
                    } else {
                        if (entry.contains("scale_to_room")) entry.erase("scale_to_room");
                    }
                    break;
                }
            }
        }
        room_->save_assets_json();
    } catch (...) {
    }
}

void AreaConfigPanel::reload_values() {
    last_scale_value_ = false;
    if (!room_) return;
    try {
        const nlohmann::json& root = room_->assets_data();
        if (root.contains("areas") && root["areas"].is_array()) {
            for (const auto& entry : root["areas"]) {
                if (!entry.is_object()) continue;
                if (entry.value("name", std::string{}) == area_name_) {
                    last_scale_value_ = entry.value("scale_to_room", false);
                    break;
                }
            }
        }
    } catch (...) {
    }
    if (name_box_) name_box_->set_value(area_name_);
    if (scale_checkbox_) scale_checkbox_->set_value(last_scale_value_);
}

