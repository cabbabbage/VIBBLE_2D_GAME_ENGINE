#include "map_grid_panel.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "util/grid.hpp"

namespace {
constexpr int kMinResolution = 0;
constexpr int kMaxResolution = vibble::grid::kMaxResolution;
constexpr int kMinChunkResolution = 0;
constexpr int kMaxChunkResolution = vibble::grid::kMaxResolution;
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
    if (resolution_slider_) {
        const int resolution_value = resolution_slider_->value();
        if (resolution_value != last_resolution_value_) {
            last_resolution_value_ = resolution_value;
            handle_resolution_changed();
        }
    }
    if (chunk_slider_) {
        const int chunk_value = chunk_slider_->value();
        if (chunk_value != last_chunk_value_) {
            last_chunk_value_ = chunk_value;
            handle_chunk_changed();
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

    resolution_slider_ = std::make_unique<DMSlider>("Grid Resolution (2^r px)", kMinResolution, kMaxResolution, settings_.resolution);

    chunk_slider_ = std::make_unique<DMSlider>("Chunk Resolution (2^r px)", kMinChunkResolution, kMaxChunkResolution, settings_.r_chunk);

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

    if (resolution_slider_) {
        rows.push_back({add_widget(std::make_unique<SliderWidget>(resolution_slider_.get()))});
    }
    if (chunk_slider_) {
        rows.push_back({add_widget(std::make_unique<SliderWidget>(chunk_slider_.get()))});
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
    if (resolution_slider_) {
        resolution_slider_->set_value(settings_.resolution);
    }
    if (chunk_slider_) {
        chunk_slider_->set_value(settings_.r_chunk);
    }
    if (jitter_slider_) {
        jitter_slider_->set_value(settings_.jitter);
    }
    last_resolution_value_ = settings_.resolution;
    last_jitter_value_ = settings_.jitter;
    last_chunk_value_ = settings_.r_chunk;
}

void MapGridPanel::apply_settings(bool trigger_save) {
    if (resolution_slider_) {
        settings_.resolution = std::clamp(resolution_slider_->value(), kMinResolution, kMaxResolution);
    }
    if (chunk_slider_) {
        settings_.r_chunk = std::clamp(chunk_slider_->value(), kMinChunkResolution, kMaxChunkResolution);
    }
    if (jitter_slider_) {
        const int jitter_cap = std::min(kMaxJitter, std::max(kMinJitter, settings_.spacing() / 2));
        settings_.jitter = std::clamp(jitter_slider_->value(), kMinJitter, jitter_cap);
    }
    settings_.clamp();
    if (resolution_slider_ && resolution_slider_->value() != settings_.resolution) {
        resolution_slider_->set_value(settings_.resolution);
    }
    if (chunk_slider_ && chunk_slider_->value() != settings_.r_chunk) {
        chunk_slider_->set_value(settings_.r_chunk);
    }
    if (jitter_slider_) {
        const int jitter_cap = std::min(kMaxJitter, std::max(kMinJitter, settings_.spacing() / 2));
        const int corrected = std::clamp(jitter_slider_->value(), kMinJitter, jitter_cap);
        if (corrected != settings_.jitter) {
            jitter_slider_->set_value(settings_.jitter);
        }
    }
    last_resolution_value_ = settings_.resolution;
    last_jitter_value_ = settings_.jitter;
    last_chunk_value_ = settings_.r_chunk;

    if (map_info_ && map_info_->is_object()) {
        nlohmann::json& section = (*map_info_)["map_grid_settings"];
        settings_.apply_to_json(section);
        if (trigger_save && on_save_) {
            on_save_();
        }
    }
}

void MapGridPanel::handle_resolution_changed() {
    apply_settings(true);
}

void MapGridPanel::handle_jitter_changed() {
    apply_settings(true);
}

void MapGridPanel::handle_chunk_changed() {
    trigger_regen();
}

void MapGridPanel::trigger_regen() {
    apply_settings(true);
    if (on_regen_) {
        on_regen_();
    }
}
