#include "map_grid_panel.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"

namespace {
constexpr int kMinSpacing = 8;
constexpr int kMaxSpacing = 2048;
constexpr int kMinJitter = 0;
constexpr int kMaxJitter = 1024;
}

MapGridPanel::MapGridPanel(int x, int y)
    : DockableCollapsible("Map Grid", true, x, y) {
    set_expanded(true);
    build_ui();
    rebuild_rows();
    sync_from_json();
}

MapGridPanel::~MapGridPanel() = default;

void MapGridPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save, RegenCallback on_regen) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    on_regen_ = std::move(on_regen);
    if (map_info_) {
        ensure_map_grid_settings(*map_info_);
    }
    sync_from_json();
    apply_settings(false);
}

void MapGridPanel::open() {
    set_visible(true);
    set_expanded(true);
}

void MapGridPanel::close() {
    set_visible(false);
}

void MapGridPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool MapGridPanel::is_visible() const {
    return DockableCollapsible::is_visible();
}

void MapGridPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    if (spacing_slider_) {
        const int spacing_value = spacing_slider_->value();
        if (spacing_value != last_spacing_value_) {
            last_spacing_value_ = spacing_value;
            handle_spacing_changed();
        }
    }
    if (jitter_slider_) {
        const int jitter_value = jitter_slider_->value();
        if (jitter_value != last_jitter_value_) {
            last_jitter_value_ = jitter_value;
            handle_jitter_changed();
        }
    }
}

bool MapGridPanel::handle_event(const SDL_Event& e) {
    return DockableCollapsible::handle_event(e);
}

void MapGridPanel::render(SDL_Renderer* renderer) const {
    DockableCollapsible::render(renderer);
}

bool MapGridPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapGridPanel::build_ui() {
    settings_ = MapGridSettings::defaults();

    spacing_slider_ = std::make_unique<DMSlider>("Grid Spacing (px)", kMinSpacing, kMaxSpacing, settings_.spacing);

    jitter_slider_ = std::make_unique<DMSlider>("Grid Jitter (px)", kMinJitter, kMaxJitter, settings_.jitter);

    regen_button_ = std::make_unique<DMButton>("Regenerate Grid Spawns", &DMStyles::AccentButton(), 220, DMButton::height());
}

void MapGridPanel::rebuild_rows() {
    widget_wrappers_.clear();
    DockableCollapsible::Rows rows;

    auto add_widget = [this](std::unique_ptr<Widget> widget) {
        Widget* raw = widget.get();
        widget_wrappers_.push_back(std::move(widget));
        return raw;
    };

    if (spacing_slider_) {
        rows.push_back({add_widget(std::make_unique<SliderWidget>(spacing_slider_.get()))});
    }
    if (jitter_slider_) {
        rows.push_back({add_widget(std::make_unique<SliderWidget>(jitter_slider_.get()))});
    }
    if (regen_button_) {
        rows.push_back({add_widget(std::make_unique<ButtonWidget>(regen_button_.get(), [this]() { trigger_regen(); }))});
    }

    set_rows(rows);
}

void MapGridPanel::sync_from_json() {
    settings_ = MapGridSettings::defaults();
    if (map_info_ && map_info_->is_object()) {
        auto it = map_info_->find("map_grid_settings");
        if (it != map_info_->end() && it->is_object()) {
            settings_ = MapGridSettings::from_json(&(*it));
        }
    }
    settings_.clamp();
    if (spacing_slider_) {
        spacing_slider_->set_value(settings_.spacing);
    }
    if (jitter_slider_) {
        jitter_slider_->set_value(settings_.jitter);
    }
    last_spacing_value_ = settings_.spacing;
    last_jitter_value_ = settings_.jitter;
}

void MapGridPanel::apply_settings(bool trigger_save) {
    if (spacing_slider_) {
        settings_.spacing = std::clamp(spacing_slider_->value(), kMinSpacing, kMaxSpacing);
    }
    if (jitter_slider_) {
        settings_.jitter = std::clamp(jitter_slider_->value(), kMinJitter, kMaxJitter);
    }
    settings_.clamp();
    if (spacing_slider_ && spacing_slider_->value() != settings_.spacing) {
        spacing_slider_->set_value(settings_.spacing);
    }
    if (jitter_slider_ && jitter_slider_->value() != settings_.jitter) {
        jitter_slider_->set_value(settings_.jitter);
    }
    last_spacing_value_ = settings_.spacing;
    last_jitter_value_ = settings_.jitter;

    if (map_info_ && map_info_->is_object()) {
        nlohmann::json& section = (*map_info_)["map_grid_settings"];
        settings_.apply_to_json(section);
        if (trigger_save && on_save_) {
            on_save_();
        }
    }
}

void MapGridPanel::handle_spacing_changed() {
    apply_settings(true);
}

void MapGridPanel::handle_jitter_changed() {
    apply_settings(true);
}

void MapGridPanel::trigger_regen() {
    apply_settings(true);
    if (on_regen_) {
        on_regen_();
    }
}
