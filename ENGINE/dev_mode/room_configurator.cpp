#include "room_configurator.hpp"
#include "assets_config.hpp"
#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"

RoomConfigurator::RoomConfigurator() {
    room_geom_options_ = {"Rectangle", "Circle"};
    panel_ = std::make_unique<DockableCollapsible>("Room", true, 32, 32);
    panel_->set_expanded(true);
    panel_->set_visible(false);
    assets_cfg_ = std::make_unique<AssetsConfig>();
}

RoomConfigurator::~RoomConfigurator() = default;

void RoomConfigurator::set_position(int x, int y) {
    if (panel_) panel_->set_position(x, y);
}

void RoomConfigurator::open(const nlohmann::json& data) {
    nlohmann::json assets = nlohmann::json::array();
    if (data.is_object()) {
        if (data.contains("room")) {
            const auto& r = data["room"];
            room_w_min_ = r.value("width_min", 0);
            room_w_max_ = r.value("width_max", 0);
            room_h_min_ = r.value("height_min", 0);
            room_h_max_ = r.value("height_max", 0);
            std::string geom = r.value("geometry", room_geom_options_.front());
            for (size_t i=0;i<room_geom_options_.size();++i) if (room_geom_options_[i]==geom) room_geom_ = int(i);
            room_is_spawn_ = r.value("is_spawn", false);
            room_is_boss_ = r.value("is_boss", false);
        }
        if (data.contains("assets")) assets = data["assets"];
    }
    if (assets_cfg_) assets_cfg_->load(assets);
    rebuild_rows();
    if (panel_) {
        panel_->set_visible(true);
        Input dummy; panel_->update(dummy, 1920, 1080);
    }
}

void RoomConfigurator::close() {
    if (panel_) panel_->set_visible(false);
}

bool RoomConfigurator::visible() const { return panel_ && panel_->is_visible(); }

bool RoomConfigurator::any_panel_visible() const {
    return visible() || (assets_cfg_ && assets_cfg_->any_visible());
}

void RoomConfigurator::rebuild_rows() {
    if (!panel_) return;
    DockableCollapsible::Rows rows;
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
    rows.push_back({ room_w_slider_w_.get(), room_h_slider_w_.get() });
    rows.push_back({ room_geom_dd_w_.get() });
    rows.push_back({ room_spawn_cb_w_.get(), room_boss_cb_w_.get() });
    if (assets_cfg_) assets_cfg_->append_rows(rows);
    panel_->set_cell_width(120);
    panel_->set_rows(rows);
}

void RoomConfigurator::update(const Input& input) {
    if (panel_ && panel_->is_visible()) {
        panel_->update(input, 1920, 1080);
        if (assets_cfg_) assets_cfg_->set_anchor(panel_->rect().x + panel_->rect().w + 10, panel_->rect().y);
    }
    if (assets_cfg_) assets_cfg_->update(input);
    if (room_w_slider_) { room_w_min_ = room_w_slider_->min_value(); room_w_max_ = room_w_slider_->max_value(); }
    if (room_h_slider_) { room_h_min_ = room_h_slider_->min_value(); room_h_max_ = room_h_slider_->max_value(); }
    if (room_geom_dd_) room_geom_ = room_geom_dd_->selected();
    room_is_spawn_ = room_spawn_cb_ && room_spawn_cb_->value();
    room_is_boss_ = room_boss_cb_ && room_boss_cb_->value();
    if (room_is_spawn_ && room_is_boss_) {
        room_is_boss_ = false;
        if (room_boss_cb_) room_boss_cb_->set_value(false);
    }
}

bool RoomConfigurator::handle_event(const SDL_Event& e) {
    bool used = false;
    if (panel_ && panel_->is_visible()) used |= panel_->handle_event(e);
    if (assets_cfg_ && assets_cfg_->handle_event(e)) used = true;
    return used;
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
    if (assets_cfg_) assets_cfg_->render(r);
}

nlohmann::json RoomConfigurator::build_json() const {
    nlohmann::json root;
    if (assets_cfg_) root["assets"] = assets_cfg_->to_json();
    nlohmann::json r;
    r["width_min"] = room_w_min_;
    r["width_max"] = room_w_max_;
    r["height_min"] = room_h_min_;
    r["height_max"] = room_h_max_;
    r["geometry"] = room_geom_options_[room_geom_];
    r["is_spawn"] = room_is_spawn_;
    r["is_boss"] = room_is_boss_;
    root["room"] = r;
    return root;
}

void RoomConfigurator::open_asset_config(const std::string& id, int x, int y) {
    if (assets_cfg_) assets_cfg_->open_asset_config(id, x, y);
}

void RoomConfigurator::close_asset_configs() {
    if (assets_cfg_) assets_cfg_->close_all_asset_configs();
}
