#include "room_configurator.hpp"
#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "room/room.hpp"
#include "widgets.hpp"

RoomConfigurator::RoomConfigurator() {
    room_geom_options_ = {"Square", "Circle"};
    panel_ = std::make_unique<DockableCollapsible>("Room", true, 32, 32);
    panel_->set_expanded(true);
    panel_->set_visible(false);
}

RoomConfigurator::~RoomConfigurator() = default;

void RoomConfigurator::set_position(int x, int y) {
    if (panel_) panel_->set_position(x, y);
}

void RoomConfigurator::open(const nlohmann::json& data) {
    if (data.is_object()) {
        room_name_ = data.value("name", std::string{});
        room_w_min_ = data.value("min_width", data.value("width_min", 0));
        room_w_max_ = data.value("max_width", data.value("width_max", 0));
        room_h_min_ = data.value("min_height", data.value("height_min", 0));
        room_h_max_ = data.value("max_height", data.value("height_max", 0));
        std::string geom = data.value("geometry", room_geom_options_.front());
        for (size_t i=0;i<room_geom_options_.size();++i) if (room_geom_options_[i]==geom) room_geom_ = int(i);
        room_is_spawn_ = data.value("is_spawn", false);
        room_is_boss_ = data.value("is_boss", false);
    }
    rebuild_rows();
    if (panel_) {
        panel_->set_visible(true);
        Input dummy; panel_->update(dummy, 1920, 1080);
    }
}

void RoomConfigurator::open(Room* room) {
    room_ = room;
    nlohmann::json j;
    if (room) {
        j = room->assets_data();
    }
    open(j);
}

void RoomConfigurator::close() {
    if (panel_) panel_->set_visible(false);
}

bool RoomConfigurator::visible() const { return panel_ && panel_->is_visible(); }

bool RoomConfigurator::any_panel_visible() const {
    return visible();
}

void RoomConfigurator::rebuild_rows() {
    if (!panel_) return;
    DockableCollapsible::Rows rows;
    room_name_lbl_ = std::make_unique<DMTextBox>("Room", room_name_);
    room_name_lbl_w_ = std::make_unique<TextBoxWidget>(room_name_lbl_.get());
    room_w_slider_ = std::make_unique<DMRangeSlider>(0, 1000, room_w_min_, room_w_max_);
    room_w_slider_w_ = std::make_unique<RangeSliderWidget>(room_w_slider_.get());
    room_h_slider_ = std::make_unique<DMRangeSlider>(0, 1000, room_h_min_, room_h_max_);
    room_h_slider_w_ = std::make_unique<RangeSliderWidget>(room_h_slider_.get());
    room_geom_dd_ = std::make_unique<DMDropdown>("Geometry", room_geom_options_, room_geom_);
    room_geom_dd_w_ = std::make_unique<DropdownWidget>(room_geom_dd_.get());
    room_spawn_cb_ = std::make_unique<DMCheckbox>("Spawn", room_is_spawn_);
    room_spawn_cb_w_ = std::make_unique<CheckboxWidget>(room_spawn_cb_.get());
    room_boss_cb_ = std::make_unique<DMCheckbox>("Boss", room_is_boss_);
    room_boss_cb_w_ = std::make_unique<CheckboxWidget>(room_boss_cb_.get());
    rows.push_back({ room_name_lbl_w_.get() });
    rows.push_back({ room_w_slider_w_.get() });
    rows.push_back({ room_h_slider_w_.get() });
    rows.push_back({ room_geom_dd_w_.get() });
    rows.push_back({ room_spawn_cb_w_.get(), room_boss_cb_w_.get() });
    panel_->set_cell_width(120);
    panel_->set_rows(rows);
}

void RoomConfigurator::update(const Input& input) {
    if (panel_ && panel_->is_visible()) {
        panel_->update(input, 1920, 1080);
    }
    if (room_w_slider_) { room_w_min_ = room_w_slider_->min_value(); room_w_max_ = room_w_slider_->max_value(); }
    if (room_h_slider_) { room_h_min_ = room_h_slider_->min_value(); room_h_max_ = room_h_slider_->max_value(); }
    if (room_geom_dd_) room_geom_ = room_geom_dd_->selected();
    room_is_spawn_ = room_spawn_cb_ && room_spawn_cb_->value();
    room_is_boss_ = room_boss_cb_ && room_boss_cb_->value();
    if (room_is_spawn_ && room_is_boss_) {
        room_is_boss_ = false;
        if (room_boss_cb_) room_boss_cb_->set_value(false);
    }
    if (room_) {
        auto& r = room_->assets_data();
        r["min_width"] = room_w_min_;
        r["max_width"] = room_w_max_;
        r["min_height"] = room_h_min_;
        r["max_height"] = room_h_max_;
        r["geometry"] = room_geom_options_[room_geom_];
        r["is_spawn"] = room_is_spawn_;
        r["is_boss"] = room_is_boss_;
        room_->save_assets_json();
    }
}

bool RoomConfigurator::handle_event(const SDL_Event& e) {
    bool used = false;
    if (panel_ && panel_->is_visible()) used |= panel_->handle_event(e);
    return used;
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
}

nlohmann::json RoomConfigurator::build_json() const {
    nlohmann::json r;
    r["min_width"] = room_w_min_;
    r["max_width"] = room_w_max_;
    r["min_height"] = room_h_min_;
    r["max_height"] = room_h_max_;
    r["geometry"] = room_geom_options_[room_geom_];
    r["is_spawn"] = room_is_spawn_;
    r["is_boss"] = room_is_boss_;
    r["name"] = room_name_;
    return r;
}

bool RoomConfigurator::is_point_inside(int x, int y) const {
    if (panel_ && panel_->is_visible()) {
        SDL_Point p{ x, y };
        if (SDL_PointInRect(&p, &panel_->rect())) return true;
    }
    return false;
}
