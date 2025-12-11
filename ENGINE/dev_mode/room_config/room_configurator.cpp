#include "room_configurator.hpp"

#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "map_generation/room.hpp"
#include "../spawn_group_config/SpawnGroupConfig.hpp"
#include "../spawn_group_config/spawn_group_utils.hpp"
#include "tag_editor_widget.hpp"
#include "tag_utils.hpp"
#include "utils/input.hpp"
#include "utils/map_grid_settings.hpp"
#include "widgets.hpp"
#include "font_cache.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <utility>
#include <limits>

namespace {
constexpr int kRoomConfigPanelMinWidth = 260;
constexpr bool kTrailsAllowIndependentDimensions = true;
constexpr int kMinimumRadius = 100;
constexpr int kRadiusSliderInitialMax = 2000;
constexpr int kRadiusSliderExpansionMargin = 64;
constexpr int kRadiusSliderExpansionFactor = 2;
constexpr int kRadiusSliderHardCap = 20000;

const nlohmann::json& empty_object() {
    static const nlohmann::json kEmpty = nlohmann::json::object();
    return kEmpty;
}

std::string lowercase_copy(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::optional<int> read_json_int(const nlohmann::json& obj, const std::string& key) {
    if (!obj.is_object() || !obj.contains(key)) {
        return std::nullopt;
    }
    const auto& value = obj[key];
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_number_float()) {
        return static_cast<int>(std::lround(value.get<double>()));
    }
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
        }
    }
    return std::nullopt;
}

std::optional<int> read_radius_value(const nlohmann::json& obj) {
    if (!obj.is_object()) return std::nullopt;
    if (auto value = read_json_int(obj, "radius")) {
        return std::max(0, *value);
    }
    return std::nullopt;
}

int infer_radius_from_dimensions(int w_min, int w_max, int h_min, int h_max) {
    int diameter = 0;
    diameter = std::max(diameter, std::max(w_min, w_max));
    diameter = std::max(diameter, std::max(h_min, h_max));
    if (diameter <= 0) return 0;
    return std::max(0, diameter / 2);
}

std::string trim_copy_room_config(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string{};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<int> parse_int_from_text(const std::string& text) {
    std::string trimmed = trim_copy_room_config(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    try {
        size_t consumed = 0;
        long long value = std::stoll(trimmed, &consumed, 10);
        if (consumed != trimmed.size()) {
            return std::nullopt;
        }
        if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
            value > static_cast<long long>(std::numeric_limits<int>::max())) {
            return std::nullopt;
        }
        return static_cast<int>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> read_text_box_value(DMTextBox* box) {
    if (!box) {
        return std::nullopt;
    }
    return parse_int_from_text(box->value());
}

void sync_text_box_with_value(DMTextBox* box, int value) {
    if (!box || box->is_editing()) {
        return;
    }
    std::string desired = std::to_string(value);
    if (box->value() != desired) {
        box->set_value(desired);
    }
}

bool append_unique(std::vector<std::string>& options, const std::string& value) {
    if (value.empty()) return false;
    if (std::find(options.begin(), options.end(), value) != options.end()) {
        return false;
    }
    options.push_back(value);
    return true;
}

struct SpawnCallbackGuard {
    bool& flag;
    explicit SpawnCallbackGuard(bool& f) : flag(f) { flag = true; }
    ~SpawnCallbackGuard() { flag = false; }
};

}

struct RoomConfigurator::State {
    std::string name;
    std::string geometry;
    int width_min = 1500;
    int width_max = 10000;
    int height_min = 1500;
    int height_max = 10000;
    int radius_min = 100;
    int radius_max = 100;
    int edge_smoothness = 2;
    int curvyness = 2;
    bool is_spawn = false;
    bool is_boss = false;
    bool inherits_assets = false;

    bool geometry_is_circle() const { return lowercase_copy(geometry) == "circle"; }

    bool ensure_valid(bool allow_height, bool enforce_dimensions = true) {
        bool mutated = false;
        if (!geometry_is_circle()) {
            if (enforce_dimensions) {
                if (width_min > width_max) {
                    std::swap(width_min, width_max);
                    mutated = true;
                }
                if (allow_height) {
                    if (height_min > height_max) {
                        std::swap(height_min, height_max);
                        mutated = true;
                    }
                } else {
                    if (height_min != width_min) {
                        height_min = width_min;
                        mutated = true;
                    }
                    if (height_max != width_max) {
                        height_max = width_max;
                        mutated = true;
                    }
                }
                int new_width_min = std::max(0, width_min);
                if (new_width_min != width_min) {
                    width_min = new_width_min;
                    mutated = true;
                }
                int new_width_max = std::max(width_min, width_max);
                if (new_width_max != width_max) {
                    width_max = new_width_max;
                    mutated = true;
                }
                int new_height_min = std::max(0, height_min);
                if (new_height_min != height_min) {
                    height_min = new_height_min;
                    mutated = true;
                }
                int new_height_max = std::max(height_min, height_max);
                if (new_height_max != height_max) {
                    height_max = new_height_max;
                    mutated = true;
                }
            }
        }
        int new_edge = std::clamp(edge_smoothness, 0, 101);
        if (new_edge != edge_smoothness) {
            edge_smoothness = new_edge;
            mutated = true;
        }
        int new_curvy = std::max(0, curvyness);
        if (new_curvy != curvyness) {
            curvyness = new_curvy;
            mutated = true;
        }
        if (geometry_is_circle() && enforce_dimensions) {
            int new_radius_min = std::max(0, radius_min);
            int new_radius_max = std::max(0, radius_max);
            if (new_radius_min < kMinimumRadius) {
                new_radius_min = kMinimumRadius;
            }
            if (new_radius_max < kMinimumRadius) {
                new_radius_max = kMinimumRadius;
            }
            if (new_radius_max < new_radius_min) {
                new_radius_max = new_radius_min;
            }
            if (new_radius_min != radius_min) {
                radius_min = new_radius_min;
                mutated = true;
            }
            if (new_radius_max != radius_max) {
                radius_max = new_radius_max;
                mutated = true;
            }
            {
                int min_diameter = radius_min > 0 ? radius_min * 2 : 0;
                int max_diameter = radius_max > 0 ? radius_max * 2 : min_diameter;
                if (width_min != min_diameter) {
                    width_min = min_diameter;
                    mutated = true;
                }
                if (width_max != max_diameter) {
                    width_max = max_diameter;
                    mutated = true;
                }
                if (height_min != min_diameter) {
                    height_min = min_diameter;
                    mutated = true;
                }
                if (height_max != max_diameter) {
                    height_max = max_diameter;
                    mutated = true;
                }
            }
        }
        if (is_spawn && is_boss) {
            is_boss = false;
            mutated = true;
        }
        return mutated;
    }

    void load_from_json(const nlohmann::json& data,
                        const std::vector<std::string>& geometry_options,
                        bool allow_height) {
        const nlohmann::json& src = data.is_object() ? data : empty_object();
        name = src.value("name", src.value("room_name", std::string{}));
        geometry = src.value("geometry", geometry_options.empty() ? std::string{} : geometry_options.front());

        if (auto value = read_json_int(src, "min_width")) {
            width_min = *value;
        }
        if (auto value = read_json_int(src, "max_width")) {
            width_max = *value;
        }
        if (allow_height) {
            if (auto value = read_json_int(src, "min_height")) {
                height_min = *value;
            }
            if (auto value = read_json_int(src, "max_height")) {
                height_max = *value;
            }
        }

        radius_min = 0;
        radius_max = 0;
        if (auto value = read_json_int(src, "min_radius")) {
            radius_min = std::max(0, *value);
        }
        if (auto value = read_json_int(src, "max_radius")) {
            radius_max = std::max(0, *value);
        }
        if (geometry_is_circle()) {
            if (radius_min <= 0 && radius_max <= 0) {
                if (auto single = read_radius_value(src)) {
                    radius_min = std::max(0, *single);
                    radius_max = std::max(radius_min, *single);
                }
            }
            if (radius_min <= 0 && width_min > 0) {
                radius_min = std::max(radius_min, width_min / 2);
            }
            if (radius_min <= 0 && height_min > 0) {
                radius_min = std::max(radius_min, height_min / 2);
            }
            if (radius_max <= 0 && width_max > 0) {
                radius_max = std::max(radius_max, width_max / 2);
            }
            if (radius_max <= 0 && height_max > 0) {
                radius_max = std::max(radius_max, height_max / 2);
            }
            if (radius_min <= 0 && radius_max > 0) {
                radius_min = radius_max;
            }
            if (radius_max <= 0 && radius_min > 0) {
                radius_max = radius_min;
            }
            if (radius_min <= 0 && radius_max <= 0) {
                int inferred = infer_radius_from_dimensions(width_min, width_max, height_min, height_max);
                radius_min = radius_max = inferred;
            }
        } else {
            if (radius_max < radius_min) {
                radius_max = radius_min;
            }
        }

        is_spawn = src.value("is_spawn", false);
        is_boss = src.value("is_boss", false);
        inherits_assets = src.value("inherits_map_assets", false);
        edge_smoothness = src.value("edge_smoothness", 2);
        if (src.contains("curvyness")) {
            if (auto cv = read_json_int(src, "curvyness")) {
                curvyness = std::max(0, *cv);
            }
        }

        ensure_valid(allow_height);
    }

    void apply_to_json(nlohmann::json& dest, bool allow_height) const {
        if (!dest.is_object()) dest = nlohmann::json::object();
        dest["name"] = name;
        dest["geometry"] = geometry;
        dest["is_spawn"] = is_spawn;
        dest["is_boss"] = is_boss;
        dest["inherits_map_assets"] = inherits_assets;
        dest["edge_smoothness"] = edge_smoothness;
        if (allow_height) {
            dest["curvyness"] = curvyness;
        } else {
            dest.erase("curvyness");
        }

        if (geometry_is_circle()) {
            int min_r = std::max(0, radius_min);
            int max_r = std::max(min_r, radius_max);
            int min_diameter = min_r * 2;
            int max_diameter = max_r * 2;
            dest["radius"] = max_r;
            dest["min_radius"] = min_r;
            dest["max_radius"] = max_r;
            dest["min_width"] = min_diameter;
            dest["max_width"] = max_diameter;
            dest["min_height"] = min_diameter;
            dest["max_height"] = max_diameter;
        } else {
            dest.erase("radius");
            dest.erase("min_radius");
            dest.erase("max_radius");
            dest["min_width"] = width_min;
            dest["max_width"] = width_max;
            dest["min_height"] = allow_height ? height_min : width_min;
            dest["max_height"] = allow_height ? height_max : width_max;
        }
    }
};

RoomConfigurator::RoomConfigurator() {
    geometry_options_ = {"Square", "Circle"};
    state_ = std::make_unique<State>();
    default_container_ = std::make_unique<SlidingWindowContainer>();
    container_ = default_container_.get();
    configure_container(*container_);
}

RoomConfigurator::~RoomConfigurator() {
    if (container_ && container_ != default_container_.get()) {
        clear_container_callbacks(*container_);
    }
}

void RoomConfigurator::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
    for (auto& cfg : spawn_group_configs_) {
        if (cfg) {
            cfg->set_manifest_store(manifest_store_);
        }
    }
}

void RoomConfigurator::set_bounds(const SDL_Rect& bounds) {
    bounds_override_ = bounds;
    has_bounds_override_ = bounds.w > 0 && bounds.h > 0;
    SDL_Rect applied = bounds;
    if (has_bounds_override_) {
        applied.w = std::max(0, applied.w);
        applied.h = std::max(0, applied.h);
        const int padding = DMSpacing::panel_padding();
        int min_panel_w = kRoomConfigPanelMinWidth + padding * 2;
        if (applied.w > 0) {
            applied.w = std::max(min_panel_w, applied.w);
        }
        if (container_) {
            container_->set_panel_bounds_override(applied);
        }
    } else {
        if (container_) {
            container_->clear_panel_bounds_override();
        }
    }
    if (!has_bounds_override_) {
        applied = work_area_;
    }
    ensure_base_panels();
    if (geometry_panel_) geometry_panel_->set_work_area(applied);
    if (tags_panel_) tags_panel_->set_work_area(applied);
    if (types_panel_) types_panel_->set_work_area(applied);
    request_container_layout();
}

void RoomConfigurator::set_work_area(const SDL_Rect& bounds) {
    work_area_ = bounds;
    ensure_base_panels();
    if (geometry_panel_) geometry_panel_->set_work_area(bounds);
    if (tags_panel_) tags_panel_->set_work_area(bounds);
    if (types_panel_) types_panel_->set_work_area(bounds);
    request_container_layout();
}

void RoomConfigurator::set_show_header(bool show) {
    show_header_ = show;
    if (container_) {
        container_->set_header_visible(show_header_);
    }
}

void RoomConfigurator::set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

void RoomConfigurator::set_header_visibility_controller(std::function<void(bool)> cb) {
    header_visibility_controller_ = std::move(cb);
    if (container_) {
        container_->set_header_visibility_controller(header_visibility_controller_);
    }
}

void RoomConfigurator::set_blocks_editor_interactions(bool block) {
    if (blocks_editor_interactions_ == block) {
        return;
    }
    blocks_editor_interactions_ = block;
    if (container_) {
        container_->set_blocks_editor_interactions(blocks_editor_interactions_);
    }
    if (default_container_ && default_container_.get() != container_) {
        default_container_->set_blocks_editor_interactions(blocks_editor_interactions_);
    }
}

void RoomConfigurator::reset_scroll() {
    if (container_) {
        container_->reset_scroll();
    }
}

void RoomConfigurator::attach_container(SlidingWindowContainer* container) {
    if (container == container_) {
        return;
    }
    if (!container) {
        detach_container();
        return;
    }
    if (container_ && container_ != default_container_.get()) {
        clear_container_callbacks(*container_);
    }
    container_ = container;
    configure_container(*container_);
    if (has_bounds_override_) {
        set_bounds(bounds_override_);
    } else {
        request_container_layout();
    }
}

void RoomConfigurator::detach_container() {
    SlidingWindowContainer* previous = container_;
    if (previous && previous != default_container_.get()) {
        clear_container_callbacks(*previous);
    }
    if (!default_container_) {
        default_container_ = std::make_unique<SlidingWindowContainer>();
    }
    container_ = default_container_.get();
    if (container_) {
        configure_container(*container_);
        if (has_bounds_override_) {
            set_bounds(bounds_override_);
        } else {
            request_container_layout();
        }
    }
}

SlidingWindowContainer* RoomConfigurator::container() { return container_; }

const SlidingWindowContainer* RoomConfigurator::container() const { return container_; }

void RoomConfigurator::configure_container(SlidingWindowContainer& container) {
    container.set_header_text_provider([this]() { return this->current_header_text(); });
    container.set_on_close([this]() { handle_container_closed(); });
    container.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        return this->layout_content(ctx);
    });
    container.set_render_function([this](SDL_Renderer* renderer) {
        for (size_t i = 0; i < ordered_base_panels_.size(); ++i) {
            auto* panel = ordered_base_panels_[i];
            if (panel && panel->is_visible()) {
                SDL_Rect bounds = (i < ordered_panel_bounds_.size()) ? ordered_panel_bounds_[i] : panel->rect();
                panel->render_embedded(renderer, bounds, last_screen_w_, last_screen_h_);
            }
        }
        for (size_t i = 0; i < spawn_group_configs_.size(); ++i) {
            const auto& cfg = spawn_group_configs_[i];
            if (cfg && cfg->is_visible()) {
                SDL_Rect bounds = (i < spawn_config_bounds_.size()) ? spawn_config_bounds_[i] : cfg->rect();
                cfg->render_embedded(renderer, bounds, last_screen_w_, last_screen_h_);
            }
        }
        if (add_spawn_widget_) {
            add_spawn_widget_->render(renderer);
        }
    });
    container.set_event_function([this](const SDL_Event& e) {
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            this->close();
            return true;
        }
        if (handle_panel_focus_event(e)) {
            return true;
        }
        if (focused_panel_ && focused_panel_->is_visible()) {
            if (focused_panel_->handle_event(e)) {
                request_container_layout();
                auto it = base_panel_keys_.find(focused_panel_);
                if (it != base_panel_keys_.end()) {
                    set_base_panel_expanded(it->second, focused_panel_->is_expanded());
                }
                return true;
            }
        }
        if (add_spawn_widget_ && add_spawn_widget_->handle_event(e)) {
            return true;
        }
        return false;
    });
    container.set_update_function([this](const Input& input, int screen_w, int screen_h) {
        for (auto* panel : ordered_base_panels_) {
            if (panel) panel->update(input, screen_w, screen_h);
        }
        for (auto& cfg : spawn_group_configs_) {
            if (cfg) cfg->update(input, screen_w, screen_h);
        }
    });
    container.set_blocks_editor_interactions(blocks_editor_interactions_);
    container.set_scrollbar_visible(true);
    container.set_content_clip_enabled(false);
    container.set_header_visible(show_header_);
    container.set_header_visibility_controller(header_visibility_controller_);
    if (!has_bounds_override_) {
        container.clear_panel_bounds_override();
    }
}

void RoomConfigurator::clear_container_callbacks(SlidingWindowContainer& container) {
    container.set_header_text_provider({});
    container.set_on_close({});
    container.set_layout_function({});
    container.set_render_function({});
    container.set_event_function({});
    container.set_update_function({});
    container.set_header_visibility_controller({});
    container.set_blocks_editor_interactions(false);
    container.clear_panel_bounds_override();
}

bool RoomConfigurator::add_spawn_group_direct() {
    nlohmann::json& root = live_room_json();
    nlohmann::json& groups = devmode::spawn::ensure_spawn_groups_array(root);

    nlohmann::json new_group = nlohmann::json::object();
    devmode::spawn::ensure_spawn_group_entry_defaults(new_group, "New Spawn");
    groups.push_back(new_group);
    renumber_spawn_group_priorities(groups);
    devmode::spawn::sanitize_perimeter_spawn_groups(groups);

    if (room_) {
        refresh_spawn_groups(room_);
    } else if (external_room_json_) {
        refresh_spawn_groups(*external_room_json_);
    } else {
        bool changed = apply_room_data(root);
        if (changed) {
            rebuild_rows();
        } else {
            request_rebuild();
        }
    }
    persist_spawn_group_changes();
    return true;
}

void RoomConfigurator::renumber_spawn_group_priorities(nlohmann::json& groups) const {
    if (!groups.is_array()) return;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!groups[i].is_object()) continue;
        groups[i]["priority"] = static_cast<int>(i);
    }
}

SDL_Rect RoomConfigurator::clamp_to_work_area(const SDL_Rect& bounds) const {
    if (work_area_.w <= 0 || work_area_.h <= 0) {
        return bounds;
    }
    SDL_Rect result = bounds;
    result.w = std::max(1, std::min(result.w, work_area_.w));
    result.h = std::max(1, std::min(result.h, work_area_.h));
    int min_x = work_area_.x;
    int max_x = work_area_.x + work_area_.w - result.w;
    int min_y = work_area_.y;
    int max_y = work_area_.y + work_area_.h - result.h;
    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;
    result.x = std::clamp(result.x, min_x, max_x);
    result.y = std::clamp(result.y, min_y, max_y);
    return result;
}

void RoomConfigurator::ensure_base_panels() {
    auto ensure_panel = [&](std::unique_ptr<DockableCollapsible>& panel,
                            const std::string& key,
                            const std::string& title) {
        const bool created = !panel;
        if (!panel) {
            panel = std::make_unique<DockableCollapsible>(title, false);
            panel->set_floatable(false);
            panel->set_show_header(true);
            panel->set_close_button_enabled(false);
            panel->set_scroll_enabled(false);
            panel->set_row_gap(DMSpacing::item_gap());
            panel->set_col_gap(DMSpacing::item_gap());
            panel->set_padding(DMSpacing::panel_padding());
            panel->reset_scroll();
            panel->set_visible(true);
            panel->force_pointer_ready();
            panel->set_embedded_focus_state(false);
            panel->set_embedded_interaction_enabled(false);
        }
        base_panel_keys_[panel.get()] = key;
        if (created && !base_panel_expanded_state_.count(key)) {
            base_panel_expanded_state_[key] = false;
        }
        bool expanded = base_panel_expanded(key);
        if (panel->is_expanded() != expanded) {
            panel->set_expanded(expanded);
        }
};

    const std::string geometry_title = is_trail_context_ ? "Trail Geometry" : "Room Geometry";
    const std::string tags_title = is_trail_context_ ? "Trail Tags" : "Room Tags";
    const std::string types_title = is_trail_context_ ? "Trail Types" : "Room Types";

    ensure_panel(geometry_panel_, "geometry", geometry_title);
    ensure_panel(tags_panel_, "tags", tags_title);
    ensure_panel(types_panel_, "types", types_title);
}

void RoomConfigurator::refresh_base_panel_rows() {
    ordered_base_panels_.clear();
    ordered_panel_bounds_.clear();
    spawn_config_bounds_.clear();
    add_spawn_bounds_ = SDL_Rect{0,0,0,0};

    if (geometry_panel_) {
        DockableCollapsible::Rows rows;
        if (name_widget_) rows.push_back({name_widget_.get()});
        if (geometry_widget_) rows.push_back({geometry_widget_.get()});
        if (radius_widget_) {
            rows.push_back({radius_widget_.get()});
        }
        if (width_min_widget_ || width_max_widget_) {
            DockableCollapsible::Row row;
            if (width_min_widget_) row.push_back(width_min_widget_.get());
            if (width_max_widget_) row.push_back(width_max_widget_.get());
            if (!row.empty()) rows.push_back(std::move(row));
        }
        if (height_min_widget_ || height_max_widget_) {
            DockableCollapsible::Row row;
            if (height_min_widget_) row.push_back(height_min_widget_.get());
            if (height_max_widget_) row.push_back(height_max_widget_.get());
            if (!row.empty()) rows.push_back(std::move(row));
        }
        if (edge_widget_) rows.push_back({edge_widget_.get()});
        if (curvy_widget_) rows.push_back({curvy_widget_.get()});
        geometry_panel_->set_rows(rows);
        geometry_panel_->set_visible(!rows.empty());
        if (!rows.empty()) {
            ordered_base_panels_.push_back(geometry_panel_.get());
        }
    }

    if (tags_panel_ && tag_editor_) {
        DockableCollapsible::Rows rows;
        rows.push_back({tag_editor_.get()});
        tags_panel_->set_rows(rows);
        tags_panel_->set_visible(!rows.empty());
        if (!rows.empty()) {
            ordered_base_panels_.push_back(tags_panel_.get());
        }
    }

    if (types_panel_) {
        DockableCollapsible::Rows rows;
        DockableCollapsible::Row toggles;
        if (spawn_widget_) toggles.push_back(spawn_widget_.get());
        if (boss_widget_) toggles.push_back(boss_widget_.get());
        if (inherit_widget_) toggles.push_back(inherit_widget_.get());
        if (!toggles.empty()) {
            rows.push_back(std::move(toggles));
        }
        types_panel_->set_rows(rows);
        types_panel_->set_visible(!rows.empty());
        if (!rows.empty()) {
            ordered_base_panels_.push_back(types_panel_.get());
        }
    }
    apply_panel_focus_states();
}

void RoomConfigurator::request_container_layout() {
    if (container_) {
        container_->request_layout();
    }
}

void RoomConfigurator::prune_collapsible_caches() {
    std::unordered_set<const DockableCollapsible*> active;
    active.reserve(ordered_base_panels_.size() + spawn_group_configs_.size());
    for (auto* panel : ordered_base_panels_) {
        if (panel) active.insert(panel);
    }
    for (const auto& cfg : spawn_group_configs_) {
        if (cfg) active.insert(cfg.get());
    }

    for (auto it = collapsible_height_cache_.begin(); it != collapsible_height_cache_.end();) {
        if (active.find(it->first) == active.end()) {
            it = collapsible_height_cache_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = base_panel_keys_.begin(); it != base_panel_keys_.end();) {
        if (active.find(it->first) == active.end()) {
            it = base_panel_keys_.erase(it);
        } else {
            ++it;
        }
    }
}

int RoomConfigurator::cached_collapsible_height(const DockableCollapsible* panel) const {
    if (!panel) return 0;
    auto it = collapsible_height_cache_.find(panel);
    if (it != collapsible_height_cache_.end() && it->second > 0) {
        return it->second;
    }
    int h = panel->height();
    if (h <= 0) {
        h = DMButton::height() + 2 * DMSpacing::panel_padding();
    }
    return h;
}

void RoomConfigurator::update_collapsible_height_cache(const DockableCollapsible* panel, int new_height) {
    if (!panel) return;
    int clamped = std::max(new_height, DMButton::height());
    auto it = collapsible_height_cache_.find(panel);
    if (it != collapsible_height_cache_.end() && it->second == clamped) {
        return;
    }
    collapsible_height_cache_[panel] = clamped;
    request_container_layout();
}

void RoomConfigurator::forget_collapsible(const DockableCollapsible* panel) {
    if (!panel) return;
    collapsible_height_cache_.erase(panel);
    base_panel_keys_.erase(panel);
}

bool RoomConfigurator::base_panel_expanded(const std::string& key) const {
    auto it = base_panel_expanded_state_.find(key);
    if (it != base_panel_expanded_state_.end()) {
        return it->second;
    }
    return false;
}

void RoomConfigurator::set_base_panel_expanded(const std::string& key, bool expanded) {
    base_panel_expanded_state_[key] = expanded;
}

void RoomConfigurator::persist_spawn_group_changes() {
    if (room_) {
        room_->save_assets_json();
    } else if (on_external_spawn_change_) {
        on_external_spawn_change_();
    }
}

void RoomConfigurator::handle_spawn_groups_mutated() {
    SpawnCallbackGuard guard(spawn_callbacks_active_);
    request_rebuild();
    persist_spawn_group_changes();
}

void RoomConfigurator::handle_spawn_group_entry_changed(const nlohmann::json& entry,
                                                        const SpawnGroupConfig::ChangeSummary& summary) {
    SpawnCallbackGuard guard(spawn_callbacks_active_);
    if (on_external_spawn_entry_change_) {
        on_external_spawn_entry_change_(entry, summary);
    }
}

void RoomConfigurator::apply_panel_focus_states() {
    auto panel_is_active = [this](DockableCollapsible* candidate) -> bool {
        if (!candidate) {
            return false;
        }
        for (auto* panel : ordered_base_panels_) {
            if (panel == candidate) {
                return true;
            }
        }
        for (const auto& cfg : spawn_group_configs_) {
            if (cfg && cfg.get() == candidate) {
                return true;
            }
        }
        return false;
};

    if (focused_panel_ && !panel_is_active(focused_panel_)) {
        focused_panel_ = nullptr;
    }

    auto apply_state = [&](DockableCollapsible* panel) {
        if (!panel) {
            return;
        }
        const bool focused = (panel == focused_panel_);
        panel->set_embedded_focus_state(focused);
        panel->set_embedded_interaction_enabled(focused);
};

    for (auto* panel : ordered_base_panels_) {
        apply_state(panel);
    }
    for (const auto& cfg : spawn_group_configs_) {
        apply_state(cfg.get());
    }
}

void RoomConfigurator::focus_panel(DockableCollapsible* panel) {
    auto is_active = [this](DockableCollapsible* candidate) -> bool {
        if (!candidate) {
            return false;
        }
        for (auto* base : ordered_base_panels_) {
            if (base == candidate) {
                return true;
            }
        }
        for (const auto& cfg : spawn_group_configs_) {
            if (cfg && cfg.get() == candidate) {
                return true;
            }
        }
        return false;
};

    DockableCollapsible* resolved = (panel && is_active(panel)) ? panel : nullptr;
    DockableCollapsible* previous = focused_panel_;
    focused_panel_ = resolved;
    apply_panel_focus_states();
    if (focused_panel_) {
        focused_panel_->force_pointer_ready();
        if (!focused_panel_->is_expanded()) {
            focused_panel_->set_expanded(true);
        }
    }
    if (previous != focused_panel_) {
        request_container_layout();
    }
}

void RoomConfigurator::clear_panel_focus() {
    focus_panel(nullptr);
}

DockableCollapsible* RoomConfigurator::panel_at_point(SDL_Point p) const {
    for (size_t i = 0; i < ordered_base_panels_.size(); ++i) {
        auto* panel = ordered_base_panels_[i];
        if (!panel || !panel->is_visible()) {
            continue;
        }
        SDL_Rect bounds = (i < ordered_panel_bounds_.size()) ? ordered_panel_bounds_[i] : panel->rect();
        if (bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }
        if (SDL_PointInRect(&p, &bounds)) {
            return panel;
        }
    }
    for (size_t i = 0; i < spawn_group_configs_.size(); ++i) {
        const auto& cfg = spawn_group_configs_[i];
        if (!cfg || !cfg->is_visible()) {
            continue;
        }
        SDL_Rect bounds = (i < spawn_config_bounds_.size()) ? spawn_config_bounds_[i] : cfg->rect();
        if (bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }
        if (SDL_PointInRect(&p, &bounds)) {
            return cfg.get();
        }
    }
    return nullptr;
}

bool RoomConfigurator::handle_panel_focus_event(const SDL_Event& e) {
    if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    SDL_Point pointer{e.button.x, e.button.y};
    DockableCollapsible* target = panel_at_point(pointer);
    if (!target || target == focused_panel_) {
        return false;
    }
    focus_panel(target);
    return true;
}

int RoomConfigurator::layout_content(const SlidingWindowContainer::LayoutContext& ctx) const {
    int y = ctx.content_top;
    ordered_panel_bounds_.resize(ordered_base_panels_.size());
    const int embed_screen_h = last_screen_h_ > 0 ? last_screen_h_ : std::max(1, ctx.content_width);
    size_t panel_index = 0;
    for (auto* panel : ordered_base_panels_) {
        if (!panel || !panel->is_visible()) {
            if (panel_index < ordered_panel_bounds_.size()) {
                ordered_panel_bounds_[panel_index] = SDL_Rect{0,0,0,0};
            }
            ++panel_index;
            continue;
        }
        int panel_height = panel->embedded_height(ctx.content_width, embed_screen_h);
        SDL_Rect rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, panel_height};
        panel->set_rect(rect);
        if (panel_index < ordered_panel_bounds_.size()) {
            ordered_panel_bounds_[panel_index] = rect;
        }
        y += panel_height + ctx.gap;
        ++panel_index;
    }

    bool any_spawn_visible = false;
    spawn_config_bounds_.resize(spawn_group_configs_.size());
    for (size_t i = 0; i < spawn_group_configs_.size(); ++i) {
        const auto& cfg = spawn_group_configs_[i];
        if (!cfg || !cfg->is_visible()) {
            spawn_config_bounds_[i] = SDL_Rect{0,0,0,0};
            continue;
        }
        if (!any_spawn_visible && y > ctx.content_top) {
            y += ctx.gap;
        }
        int cfg_height = cfg->embedded_height(ctx.content_width, embed_screen_h);
        SDL_Rect rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, cfg_height};
        cfg->set_rect(rect);
        spawn_config_bounds_[i] = rect;
        y += cfg_height + ctx.gap;
        any_spawn_visible = true;
    }

    if (add_spawn_widget_) {
        if (y > ctx.content_top) {
            y += ctx.gap;
        }
        SDL_Rect rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, DMButton::height()};
        add_spawn_widget_->set_rect(rect);
        add_spawn_bounds_ = rect;
        y += rect.h;
    }

    return y + ctx.gap;
}

void RoomConfigurator::handle_container_closed() {
    for (auto& config : spawn_group_configs_) {
        if (config) {
            config->close();
            config->set_visible(false);
            config->close_embedded_search();
        }
    }
    for (auto* panel : ordered_base_panels_) {
        if (panel) {
            panel->set_visible(false);
        }
    }
    clear_panel_focus();
    external_room_json_ = nullptr;
    on_external_spawn_change_ = {};
    on_external_spawn_entry_change_ = {};
    external_configure_entry_ = {};
    if (on_close_) on_close_();
}

bool RoomConfigurator::apply_room_data(const nlohmann::json& data) {
    const nlohmann::json& normalized = data.is_object() ? data : empty_object();

    nlohmann::json normalized_copy = normalized;
    if (!normalized_copy.contains("spawn_groups") || !normalized_copy["spawn_groups"].is_array()) {
        normalized_copy["spawn_groups"] = nlohmann::json::array();
    }

    const nlohmann::json new_spawn_array = normalized_copy["spawn_groups"];
    const nlohmann::json current_spawn_array =
        (loaded_json_.contains("spawn_groups") && loaded_json_["spawn_groups"].is_array()) ? loaded_json_["spawn_groups"] : nlohmann::json::array();

    bool spawn_changed = (new_spawn_array != current_spawn_array);

    const bool allow_height = !is_trail_context_ && kTrailsAllowIndependentDimensions;

    State new_state = state_ ? *state_ : State{};
    new_state.load_from_json(normalized_copy, geometry_options_, allow_height);

    bool geometry_added = append_unique(geometry_options_, new_state.geometry);

    bool dims_changed = false;
    if (!state_) {
        dims_changed = true;
    } else {
        dims_changed =
            new_state.name != state_->name ||
            new_state.geometry != state_->geometry ||
            new_state.width_min != state_->width_min ||
            new_state.width_max != state_->width_max ||
            new_state.height_min != state_->height_min ||
            new_state.height_max != state_->height_max ||
            new_state.radius_min != state_->radius_min ||
            new_state.radius_max != state_->radius_max ||
            new_state.edge_smoothness != state_->edge_smoothness ||
            new_state.curvyness != state_->curvyness ||
            new_state.is_spawn != state_->is_spawn ||
            new_state.is_boss != state_->is_boss ||
            new_state.inherits_assets != state_->inherits_assets;
    }

    std::vector<std::string> include;
    std::vector<std::string> exclude;
    auto capture_tags = [&](const std::vector<std::string>& src, std::vector<std::string>& dst) {
        dst = src;
        std::sort(dst.begin(), dst.end());
};

    std::vector<std::string> prev_include = room_tags_;
    std::vector<std::string> prev_exclude = room_anti_tags_;
    capture_tags(prev_include, prev_include);
    capture_tags(prev_exclude, prev_exclude);

    load_tags_from_json(normalized_copy);
    capture_tags(room_tags_, include);
    capture_tags(room_anti_tags_, exclude);
    bool tags_changed = (include != prev_include) || (exclude != prev_exclude);

    if (!spawn_changed && !dims_changed && !geometry_added && !tags_changed) {
        return false;
    }

    loaded_json_ = std::move(normalized_copy);
    if (!state_) state_ = std::make_unique<State>();
    *state_ = std::move(new_state);
    tags_dirty_ = false;
    return true;
}

void RoomConfigurator::open(const nlohmann::json& room_data) {
    const bool was_visible = container_ && container_->is_visible();

    if (!was_visible) {
        reset_expanded_state_pending_ = true;
    }

    room_ = nullptr;
    external_room_json_ = nullptr;
    on_external_spawn_change_ = {};
    on_external_spawn_entry_change_ = {};
    external_configure_entry_ = {};
    is_trail_context_ = false;
    bool changed = apply_room_data(room_data);
    if (changed || !was_visible) {
        rebuild_rows();
        if (!was_visible) {
            reset_scroll();
        }
    }
    if (container_) {
        container_->open();
    }
}

void RoomConfigurator::open(nlohmann::json& room_data,
                            std::function<void()> on_change,
                            std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change,
                            SpawnGroupConfig::ConfigureEntryCallback configure_entry) {
    const bool was_visible = container_ && container_->is_visible();

    if (!was_visible) {
        reset_expanded_state_pending_ = true;
    }

    room_ = nullptr;
    external_room_json_ = &room_data;
    on_external_spawn_change_ = std::move(on_change);
    on_external_spawn_entry_change_ = std::move(on_entry_change);
    external_configure_entry_ = std::move(configure_entry);
    is_trail_context_ = false;
    bool changed = apply_room_data(room_data);
    if (changed || !was_visible) {
        rebuild_rows();
        if (!was_visible) {
            reset_scroll();
        }
    }
    if (container_) {
        container_->open();
    }
}

void RoomConfigurator::open(Room* room) {
    const bool was_visible = container_ && container_->is_visible();

    if (!was_visible) {
        reset_expanded_state_pending_ = true;
    }

    Room* previous = room_;
    room_ = room;
    external_room_json_ = nullptr;
    on_external_spawn_change_ = {};
    on_external_spawn_entry_change_ = {};
    external_configure_entry_ = {};
    is_trail_context_ = false;
    if (room_) {
        const std::string& dir = room_->room_directory;
        if (dir.find("trails_data") != std::string::npos) {
            is_trail_context_ = true;
        }
    }

    const nlohmann::json& source = room ? room->assets_data() : empty_object();
    bool changed = (room != previous) || apply_room_data(source);
    if (changed || !was_visible) {
        rebuild_rows();
        if (!was_visible) {
            reset_scroll();
        }
    }
    if (container_) {
        container_->open();
    }
}

bool RoomConfigurator::refresh_spawn_groups(const nlohmann::json& room_data) {
    bool changed = apply_room_data(room_data);
    if (changed) {
        if (spawn_callbacks_active_) {
            deferred_rebuild_ = true;
        } else {
            rebuild_rows();
        }
    }
    return changed;
}

bool RoomConfigurator::refresh_spawn_groups(nlohmann::json& room_data) {
    external_room_json_ = &room_data;
    bool changed = apply_room_data(room_data);
    if (changed) {
        if (spawn_callbacks_active_) {
            deferred_rebuild_ = true;
        } else {
            rebuild_rows();
        }
    }
    return changed;
}

bool RoomConfigurator::refresh_spawn_groups(Room* room) {
    const nlohmann::json& src = room ? room->assets_data() : empty_object();
    return refresh_spawn_groups(src);
}

void RoomConfigurator::notify_spawn_groups_mutated() {
    handle_spawn_groups_mutated();
}

void RoomConfigurator::close() {
    clear_panel_focus();
    if (!container_ || !container_->is_visible()) {
        for (auto& config : spawn_group_configs_) {
            if (config) config->set_visible(false);
        }
        external_room_json_ = nullptr;
        on_external_spawn_change_ = {};
        on_external_spawn_entry_change_ = {};
        external_configure_entry_ = {};
        return;
    }
    container_->close();
}

bool RoomConfigurator::visible() const { return container_ && container_->is_visible(); }

bool RoomConfigurator::any_panel_visible() const { return visible(); }

bool RoomConfigurator::is_locked() const {
    for (const auto& cfg : spawn_group_configs_) {
        if (cfg && cfg->isLocked()) {
            return true;
        }
    }
    return false;
}

std::string RoomConfigurator::selected_geometry() const {
    if (!state_) return geometry_options_.empty() ? std::string{} : geometry_options_.front();
    if (geometry_options_.empty()) return state_->geometry;
    auto it = std::find(geometry_options_.begin(), geometry_options_.end(), state_->geometry);
    if (it != geometry_options_.end()) return *it;
    return geometry_options_.front();
}

void RoomConfigurator::rebuild_spawn_rows(bool force_collapse_sections) {
    add_spawn_button_.reset();
    add_spawn_widget_.reset();

    auto previous_configs = std::move(spawn_group_configs_);
    auto previous_ids = std::move(spawn_group_config_ids_);
    spawn_group_configs_.clear();
    spawn_group_config_ids_.clear();

    auto take_config = [&](const std::string& id) -> std::unique_ptr<SpawnGroupConfig> {
        if (!id.empty()) {
            for (size_t i = 0; i < previous_configs.size(); ++i) {
                if (!previous_configs[i]) continue;
                if (i < previous_ids.size() && previous_ids[i] == id) {
                    auto cfg = std::move(previous_configs[i]);
                    previous_configs[i].reset();
                    return cfg;
                }
            }
        }
        for (auto& cfg : previous_configs) {
            if (cfg) {
                auto result = std::move(cfg);
                cfg.reset();
                return result;
            }
        }
        return nullptr;
};

    auto bind_spawn_entry = [&](nlohmann::json& entry,
                                nlohmann::json& group_array,
                                SpawnGroupConfig::ConfigureEntryCallback configure_entry,
                                std::function<void()> on_change,
                                std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change) {
        bool have_id_field = entry.is_object() && entry.contains("spawn_id");
        std::string id = have_id_field ? entry.value("spawn_id", std::string{}) : std::string{};
        auto config = take_config(id);
        const bool created_new = !config;
        if (!config) {
            config = std::make_unique<SpawnGroupConfig>();
        }

        if (manifest_store_) {
            config->set_manifest_store(manifest_store_);
        }

        int default_resolution = MapGridSettings::defaults().resolution;
        if (room_) {
            default_resolution = room_->map_grid_settings().resolution;
        } else if (external_room_json_) {
            const auto it = external_room_json_->find("map_grid_settings");
            if (it != external_room_json_->end()) {
                MapGridSettings settings = MapGridSettings::from_json(&*it);
                default_resolution = settings.resolution;
            }
        }
        config->set_default_resolution(default_resolution);

        config->set_embedded_mode(true);
        config->set_show_header(true);
        config->set_close_button_enabled(false);
        config->set_scroll_enabled(false);
        config->set_row_gap(DMSpacing::item_gap());
        config->set_col_gap(DMSpacing::item_gap());
        config->set_padding(DMSpacing::panel_padding());
        config->set_header_button_style(&DMStyles::AccentButton());
        config->set_header_highlight_color(DMStyles::AccentButton().bg);
        config->force_pointer_ready();
        config->set_embedded_focus_state(false);
        config->set_embedded_interaction_enabled(false);
        if (created_new) {
            config->set_expanded(false);
        }
        if (force_collapse_sections) {
            config->set_expanded(false);
        }

        config->set_screen_dimensions(last_screen_w_, last_screen_h_);

        SpawnGroupConfig::Callbacks callbacks{};
        callbacks.on_regenerate = [this](const std::string& value) {
            SpawnCallbackGuard guard(this->spawn_callbacks_active_);
            if (on_spawn_regenerate_) on_spawn_regenerate_(value);
};
        callbacks.on_delete = [this](const std::string& value) {
            SpawnCallbackGuard guard(this->spawn_callbacks_active_);
            if (on_spawn_delete_) on_spawn_delete_(value);
            if (room_) {
                this->refresh_spawn_groups(room_);
            } else if (external_room_json_) {
                this->refresh_spawn_groups(*external_room_json_);
            } else {
                this->request_rebuild();
            }
            this->persist_spawn_group_changes();
};
        callbacks.on_reorder = [this, groups = &group_array](const std::string& value, size_t index) {
            SpawnCallbackGuard guard(this->spawn_callbacks_active_);
            if (on_spawn_reorder_) on_spawn_reorder_(value, index);
            if (!groups || !groups->is_array() || groups->empty()) {
                return;
            }

            auto& arr = *groups;
            size_t current_index = arr.size();
            for (size_t i = 0; i < arr.size(); ++i) {
                const auto& element = arr[i];
                if (!element.is_object()) continue;
                if (element.contains("spawn_id") && element["spawn_id"].is_string() &&
                    element["spawn_id"].get<std::string>() == value) {
                    current_index = i;
                    break;
                }
            }
            if (current_index >= arr.size()) {
                return;
            }

            size_t target_index = index;
            if (!arr.empty()) {
                const size_t max_index = arr.size() - 1;
                if (target_index > max_index) {
                    target_index = max_index;
                }
            } else {
                target_index = 0;
            }

            if (current_index != target_index) {
                nlohmann::json moved = std::move(arr[current_index]);
                const auto erase_pos = arr.begin() + static_cast<nlohmann::json::difference_type>(current_index);
                arr.erase(erase_pos);
                size_t insert_index = target_index;
                if (insert_index > arr.size()) {
                    insert_index = arr.size();
                }
                const auto insert_pos = arr.begin() + static_cast<nlohmann::json::difference_type>(insert_index);
                arr.insert(insert_pos, std::move(moved));
            }

            renumber_spawn_group_priorities(arr);
};
        config->set_callbacks(std::move(callbacks));

        SpawnGroupConfig::EntryCallbacks entry_callbacks{};
        nlohmann::json* entry_ptr = &entry;
        auto request_regenerate = [this, entry_ptr, id]() {
            std::string target = id;
            if (target.empty() && entry_ptr && entry_ptr->is_object()) {
                target = entry_ptr->value("spawn_id", std::string{});
            }
            if (target.empty()) return;
            if (on_spawn_regenerate_) on_spawn_regenerate_(target);
};
        entry_callbacks.on_method_changed = [request_regenerate](const std::string&) { request_regenerate(); };
        entry_callbacks.on_quantity_changed = [request_regenerate](int, int) { request_regenerate(); };
        entry_callbacks.on_candidates_changed = [request_regenerate](const nlohmann::json&) { request_regenerate(); };

        SpawnGroupConfig::ConfigureEntryCallback final_configure_entry = std::move(configure_entry);

        auto title_from = [](const nlohmann::json& e) -> std::string {
            if (e.is_object()) {
                if (e.contains("display_name") && e["display_name"].is_string()) {
                    std::string t = e["display_name"].get<std::string>();
                    if (!t.empty()) return t;
                }
                if (e.contains("spawn_id") && e["spawn_id"].is_string()) {
                    std::string id = e["spawn_id"].get<std::string>();
                    if (!id.empty()) return id;
                }
            }
            return std::string{"Spawn Group"};
};
        config->set_title(title_from(entry));

        auto wrapped_entry_change = [this, cfg=config.get(), on_entry_change = std::move(on_entry_change), title_from](
                                        const nlohmann::json& updated,
                                        const SpawnGroupConfig::ChangeSummary& summary) {

            if (cfg) cfg->set_title(title_from(updated));
            this->handle_spawn_group_entry_changed(updated, summary);
            if (on_entry_change) on_entry_change(updated, summary);
};
        auto wrapped_on_change = [this, extra = std::move(on_change)]() {
            this->handle_spawn_groups_mutated();
            if (extra) extra();
};
        config->bind_entry(entry, std::move(wrapped_on_change), std::move(wrapped_entry_change), std::move(entry_callbacks), std::move(final_configure_entry));

        config->set_on_layout_changed([this, cfg_ptr = config.get()]() {
            this->update_collapsible_height_cache(cfg_ptr, cfg_ptr->height());
            this->request_container_layout();
        });

        spawn_group_config_ids_.push_back(id);
        spawn_group_configs_.push_back(std::move(config));
};

    bool have_groups = false;
    if (room_) {
        auto& root = live_room_json();
        nlohmann::json& groups = devmode::spawn::ensure_spawn_groups_array(root);

        auto configure_entry = [this](SpawnGroupConfig::EntryController& entry, const nlohmann::json&) {
            entry.set_area_names_provider([this]() {
                std::vector<std::string> names;
                if (!room_) return names;
                auto& data = room_->assets_data();
                if (data.contains("areas") && data["areas"].is_array()) {
                    for (const auto& entry : data["areas"]) {
                        if (entry.is_object() && entry.contains("name") && entry["name"].is_string()) {
                            names.push_back(entry["name"].get<std::string>());
                        }
                    }
                }
                return names;
            });
            if (room_) {
                std::string label = room_->room_name.empty() ? std::string("Room") : room_->room_name;
                entry.set_ownership_label(label, SDL_Color{255, 224, 96, 255});
            }

};

        for (auto& entry : groups) {
            have_groups = true;
            std::function<void()> on_change;
            std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change;
            bind_spawn_entry(entry, groups, configure_entry, std::move(on_change), std::move(on_entry_change));
        }
    } else if (external_room_json_) {
        auto& root = live_room_json();
        nlohmann::json& groups = devmode::spawn::ensure_spawn_groups_array(root);

        for (auto& entry : groups) {
            have_groups = true;
            std::function<void()> on_change;
            std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change;
            bind_spawn_entry(entry, groups, external_configure_entry_, std::move(on_change), std::move(on_entry_change));
        }
    } else {
        nlohmann::json& root = loaded_json_;
        if (!root.is_object()) {
            root = nlohmann::json::object();
        }
        nlohmann::json& groups = devmode::spawn::ensure_spawn_groups_array(root);
        for (auto& entry : groups) {
            have_groups = true;
            std::function<void()> on_change;
            std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change;
            bind_spawn_entry(entry, groups, {}, std::move(on_change), std::move(on_entry_change));
        }
    }
    (void)have_groups;
    request_container_layout();
    apply_panel_focus_states();
}

void RoomConfigurator::rebuild_rows() {
    if (!state_) {
        state_ = std::make_unique<State>();
    }

    int previous_scroll = container_ ? container_->scroll_value() : 0;

    if (rebuild_in_progress_) {
        pending_rebuild_ = true;
        return;
    }

    for (;;) {
        rebuild_in_progress_ = true;
        do {
            pending_rebuild_ = false;
            rebuild_rows_internal();
        } while (pending_rebuild_);
        rebuild_in_progress_ = false;
        if (!pending_rebuild_) {
            break;
        }

        static int guard_counter = 0;
        if (++guard_counter > 8) {
            deferred_rebuild_ = true;
            guard_counter = 0;
            break;
        }
    }

    if (container_) {
        container_->set_scroll_value(previous_scroll);
    }
}

void RoomConfigurator::rebuild_rows_internal() {
    if (!state_) {
        state_ = std::make_unique<State>();
    }

    bool force_collapse_sections = reset_expanded_state_pending_;
    if (reset_expanded_state_pending_) {
        base_panel_expanded_state_.clear();
        collapsible_height_cache_.clear();
        base_panel_keys_.clear();
    }
    reset_expanded_state_pending_ = false;

    ensure_base_panels();
    ordered_base_panels_.clear();

    name_box_ = std::make_unique<DMTextBox>(is_trail_context_ ? "Trail Name" : "Room Name", state_->name);
    name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get());

    bool allow_geometry_choice = !is_trail_context_;
    const bool allow_height = !is_trail_context_ && kTrailsAllowIndependentDimensions;
    if (allow_geometry_choice) {
        auto geom_it = std::find(geometry_options_.begin(), geometry_options_.end(), state_->geometry);
        int geom_index = 0;
        if (geom_it != geometry_options_.end()) {
            geom_index = static_cast<int>(std::distance(geometry_options_.begin(), geom_it));
        }
        geometry_dropdown_ = std::make_unique<DMDropdown>("", geometry_options_, geom_index);
        geometry_widget_ = std::make_unique<DropdownWidget>(geometry_dropdown_.get());
    } else {
        geometry_dropdown_.reset();
        geometry_widget_.reset();
    }

    if (state_->geometry_is_circle()) {
        width_min_box_.reset();
        width_min_widget_.reset();
        width_max_box_.reset();
        width_max_widget_.reset();
        height_min_box_.reset();
        height_min_widget_.reset();
        height_max_box_.reset();
        height_max_widget_.reset();
        initialize_radius_slider(false);
    } else {
        radius_slider_.reset();
        radius_widget_.reset();
        radius_slider_max_range_ = 0;

        width_min_box_ = std::make_unique<DMTextBox>("Min Width", std::to_string(state_->width_min));
        width_min_widget_ = std::make_unique<TextBoxWidget>(width_min_box_.get());
        width_max_box_ = std::make_unique<DMTextBox>("Max Width", std::to_string(state_->width_max));
        width_max_widget_ = std::make_unique<TextBoxWidget>(width_max_box_.get());

        if (allow_height) {
            height_min_box_ = std::make_unique<DMTextBox>("Min Height", std::to_string(state_->height_min));
            height_min_widget_ = std::make_unique<TextBoxWidget>(height_min_box_.get());
            height_max_box_ = std::make_unique<DMTextBox>("Max Height", std::to_string(state_->height_max));
            height_max_widget_ = std::make_unique<TextBoxWidget>(height_max_box_.get());
        } else {
            height_min_box_.reset();
            height_min_widget_.reset();
            height_max_box_.reset();
            height_max_widget_.reset();
        }
    }

    if (!is_trail_context_) {
        edge_slider_ = std::make_unique<DMSlider>("Edge Smoothness", 0, 101, state_->edge_smoothness);
        edge_widget_ = std::make_unique<SliderWidget>(edge_slider_.get());
    } else {
        edge_slider_.reset();
        edge_widget_.reset();
    }

    if (is_trail_context_) {
        curvy_slider_ = std::make_unique<DMSlider>("Curvyness", 0, 16, state_->curvyness);
        curvy_widget_ = std::make_unique<SliderWidget>(curvy_slider_.get());
    } else {
        curvy_slider_.reset();
        curvy_widget_.reset();
    }

    if (!is_trail_context_) {
        spawn_checkbox_ = std::make_unique<DMCheckbox>("Spawn", state_->is_spawn);
        spawn_widget_ = std::make_unique<CheckboxWidget>(spawn_checkbox_.get());
    } else {
        spawn_checkbox_.reset();
        spawn_widget_.reset();
    }

    if (!is_trail_context_) {
        boss_checkbox_ = std::make_unique<DMCheckbox>("Boss", state_->is_boss);
        boss_widget_ = std::make_unique<CheckboxWidget>(boss_checkbox_.get());
    } else {
        boss_checkbox_.reset();
        boss_widget_.reset();
    }

    inherit_checkbox_ = std::make_unique<DMCheckbox>("Inherit Map Assets", state_->inherits_assets);
    inherit_widget_ = std::make_unique<CheckboxWidget>(inherit_checkbox_.get());

    tag_editor_ = std::make_unique<TagEditorWidget>();
    tag_editor_->set_tags(room_tags_, room_anti_tags_);
    tag_editor_->set_on_changed([this](const std::vector<std::string>& include,
                                       const std::vector<std::string>& exclude) {
        if (include != room_tags_ || exclude != room_anti_tags_) {
            room_tags_ = include;
            room_anti_tags_ = exclude;
            tags_dirty_ = true;
            this->request_container_layout();
        }
    });

    refresh_base_panel_rows();

    if (force_collapse_sections) {
        for (auto* panel : ordered_base_panels_) {
            if (panel) {
                panel->force_pointer_ready();
            }
        }
    }

    rebuild_spawn_rows(force_collapse_sections);

    add_spawn_button_ = std::make_unique<DMButton>("Add Spawn Group", &DMStyles::CreateButton(), 0, DMButton::height());
    add_spawn_widget_ = std::make_unique<ButtonWidget>(add_spawn_button_.get(), [this]() {
        if (on_spawn_add_) {
            on_spawn_add_();
        } else {
            this->add_spawn_group_direct();
        }
    });

    prune_collapsible_caches();
    request_container_layout();
}

void RoomConfigurator::update(const Input& input, int screen_w, int screen_h) {
    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;
    if (deferred_rebuild_ && !rebuild_in_progress_ && !spawn_callbacks_active_) {
        deferred_rebuild_ = false;
        rebuild_rows();
    }
    ensure_base_panels();
    const bool panel_visible = container_ && container_->is_visible();
    SDL_Rect panel_work_area = work_area_;
    if (panel_work_area.w <= 0 || panel_work_area.h <= 0) {
        panel_work_area = SDL_Rect{0, 0, screen_w, screen_h};
    }
    for (auto* panel : ordered_base_panels_) {
        if (!panel) continue;
        panel->set_visible(panel_visible);
        if (panel_work_area.w > 0 && panel_work_area.h > 0) {
            panel->set_work_area(panel_work_area);
        }
    }
    for (auto& config : spawn_group_configs_) {
        if (!config) continue;
        config->set_visible(panel_visible);
        config->set_screen_dimensions(screen_w, screen_h);
    }

    if (container_) {
        container_->update(input, screen_w, screen_h);
    }

    for (auto* panel : ordered_base_panels_) {
        if (!panel) continue;
        update_collapsible_height_cache(panel, panel->height());
        auto it = base_panel_keys_.find(panel);
        if (it != base_panel_keys_.end()) {
            set_base_panel_expanded(it->second, panel->is_expanded());
        }
    }
    for (auto& config : spawn_group_configs_) {
        if (!config) continue;
        update_collapsible_height_cache(config.get(), config->height());
    }

    if (!state_) return;

    bool needs_rebuild = sync_state_from_widgets();
    if (needs_rebuild) {
        rebuild_rows();
    } else if (deferred_rebuild_) {
        deferred_rebuild_ = false;
        rebuild_rows();
    }
}

void RoomConfigurator::prepare_for_event(int screen_w, int screen_h) {
    int use_w = screen_w > 0 ? screen_w : (last_screen_w_ > 0 ? last_screen_w_ : 0);
    int use_h = screen_h > 0 ? screen_h : (last_screen_h_ > 0 ? last_screen_h_ : 0);
    if (use_w <= 0 || use_h <= 0) {
        return;
    }
    ensure_base_panels();
    last_screen_w_ = use_w;
    last_screen_h_ = use_h;
    if (container_) {
        container_->prepare_layout(use_w, use_h);
    }
    const bool panel_visible = container_ && container_->is_visible();
    SDL_Rect panel_work_area = work_area_;
    if (panel_work_area.w <= 0 || panel_work_area.h <= 0) {
        panel_work_area = SDL_Rect{0, 0, use_w, use_h};
    }
    for (auto* panel : ordered_base_panels_) {
        if (panel) {
            panel->set_visible(panel_visible);
            if (panel_work_area.w > 0 && panel_work_area.h > 0) {
                panel->set_work_area(panel_work_area);
            }
        }
    }
    for (auto& config : spawn_group_configs_) {
        if (config) {
            config->set_visible(panel_visible);
            config->set_screen_dimensions(use_w, use_h);
        }
    }
}

int RoomConfigurator::compute_radius_slider_initial_range() const {
    int base = std::max(kRadiusSliderInitialMax, kMinimumRadius);
    if (!state_) {
        return base;
    }
    int dimensions = std::max(state_->width_max, state_->height_max);
    int derived_radius = dimensions > 0 ? (dimensions + 1) / 2 : 0;
    base = std::max(base, derived_radius);
    base = std::max(base, state_->radius_max);
    base = std::min(base + kRadiusSliderExpansionMargin, kRadiusSliderHardCap);
    return std::max(base, kMinimumRadius);
}

void RoomConfigurator::initialize_radius_slider(bool request_layout) {
    if (!state_) {
        radius_slider_.reset();
        radius_widget_.reset();
        radius_slider_max_range_ = 0;
        return;
    }
    int range = compute_radius_slider_initial_range();
    radius_slider_max_range_ = range;
    radius_slider_ = std::make_unique<DMRangeSlider>(kMinimumRadius, range, state_->radius_min, state_->radius_max);
    radius_slider_->set_defer_commit_until_unfocus(true);
    radius_widget_ = std::make_unique<RangeSliderWidget>(radius_slider_.get());
    if (request_layout) {
        refresh_base_panel_rows();
        request_container_layout();
    }
}

void RoomConfigurator::expand_radius_slider_range_if_needed() {
    if (!radius_slider_ || !state_) {
        return;
    }
    if (radius_slider_max_range_ >= kRadiusSliderHardCap) {
        return;
    }
    if (state_->radius_max + kRadiusSliderExpansionMargin < radius_slider_max_range_) {
        return;
    }
    int desired = std::max(radius_slider_max_range_ * kRadiusSliderExpansionFactor, state_->radius_max + kRadiusSliderExpansionMargin);
    desired = std::min(desired, kRadiusSliderHardCap);
    if (desired <= radius_slider_max_range_) {
        return;
    }
    radius_slider_max_range_ = desired;
    radius_slider_ = std::make_unique<DMRangeSlider>(kMinimumRadius, radius_slider_max_range_, state_->radius_min, state_->radius_max);
    radius_slider_->set_defer_commit_until_unfocus(true);
    radius_widget_ = std::make_unique<RangeSliderWidget>(radius_slider_.get());
    refresh_base_panel_rows();
    request_container_layout();
}

bool RoomConfigurator::sync_state_from_widgets() {
    if (!state_) return false;

    bool changed = false;
    bool rebuild_required = false;
    bool tags_changed = false;
    const bool allow_height = !is_trail_context_ && kTrailsAllowIndependentDimensions;

    if (tags_dirty_) {
        changed = true;
        tags_dirty_ = false;
        tags_changed = true;
    }

    if (name_box_ && !name_box_->is_editing()) {
        std::string new_name = name_box_->value();
        if (new_name != state_->name) {
            std::string final_name = new_name;
            if (on_room_renamed_) {
                try {
                    final_name = on_room_renamed_(state_->name, new_name);
                } catch (...) {
                    final_name = new_name;
                }
            }
            if (final_name != new_name && name_box_) {
                name_box_->set_value(final_name);
            }
            state_->name = std::move(final_name);
            changed = true;
        }
    }

    if (geometry_dropdown_) {
        int idx = std::clamp(geometry_dropdown_->selected(), 0, static_cast<int>(geometry_options_.size()) - 1);
        std::string selected = geometry_options_.empty() ? std::string{} : geometry_options_[idx];
        if (selected != state_->geometry) {
            state_->geometry = selected;
            if (state_->geometry_is_circle()) {
                int inferred_min = state_->radius_min;
                int inferred_max = state_->radius_max;
                if (!radius_slider_) {
                    int min_diameter = std::max(state_->width_min, state_->height_min);
                    int max_diameter = std::max(state_->width_max, state_->height_max);
                    if (min_diameter > 0) inferred_min = std::max(inferred_min, min_diameter / 2);
                    if (max_diameter > 0) inferred_max = std::max(inferred_max, max_diameter / 2);
                }
                if (inferred_min <= 0 && inferred_max <= 0) {
                    inferred_min = inferred_max = infer_radius_from_dimensions( state_->width_min, state_->width_max, state_->height_min, state_->height_max);
                }
                state_->radius_min = inferred_min;
                state_->radius_max = std::max(inferred_min, inferred_max);
            } else {
                int min_diameter = std::max(0, state_->radius_min) * 2;
                int max_radius = std::max(state_->radius_min, state_->radius_max);
                int max_diameter = std::max(min_diameter, std::max(0, max_radius) * 2);
                state_->width_min = state_->height_min = std::max(1, min_diameter);
                state_->width_max = state_->height_max = std::max(state_->width_min, max_diameter);
            }
            rebuild_required = true;
            changed = true;
        }
    }

    if (width_min_box_ && !width_min_box_->is_editing()) {
        if (auto parsed = read_text_box_value(width_min_box_.get())) {
            if (*parsed != state_->width_min) {
                state_->width_min = *parsed;
                changed = true;
            }
        }
    }

    if (width_max_box_ && !width_max_box_->is_editing()) {
        if (auto parsed = read_text_box_value(width_max_box_.get())) {
            if (*parsed != state_->width_max) {
                state_->width_max = *parsed;
                changed = true;
            }
        }
    }

    if (height_min_box_ && !height_min_box_->is_editing()) {
        if (auto parsed = read_text_box_value(height_min_box_.get())) {
            if (*parsed != state_->height_min) {
                state_->height_min = *parsed;
                changed = true;
            }
        }
    }

    if (height_max_box_ && !height_max_box_->is_editing()) {
        if (auto parsed = read_text_box_value(height_max_box_.get())) {
            if (*parsed != state_->height_max) {
                state_->height_max = *parsed;
                changed = true;
            }
        }
    }

    if (radius_slider_) {
        int slider_min = radius_slider_->min_value();
        int slider_max = radius_slider_->max_value();
        if (slider_min != state_->radius_min || slider_max != state_->radius_max) {
            state_->radius_min = slider_min;
            state_->radius_max = slider_max;
            changed = true;
        }
        expand_radius_slider_range_if_needed();
    }

    if (edge_slider_) {
        int v = std::clamp(edge_slider_->value(), 0, 101);
        if (v != state_->edge_smoothness) {
            state_->edge_smoothness = v;
            changed = true;
        }
    }

    if (curvy_slider_) {
        int v = std::max(0, curvy_slider_->value());
        if (v != state_->curvyness) {
            state_->curvyness = v;
            changed = true;
        }
    }

    if (spawn_checkbox_) {
        bool value = spawn_checkbox_->value();
        if (value != state_->is_spawn) {
            state_->is_spawn = value;
            changed = true;
        }
    }

    if (boss_checkbox_) {
        bool value = boss_checkbox_->value();
        if (value != state_->is_boss) {
            state_->is_boss = value;
            changed = true;
        }
    }

    if (inherit_checkbox_) {
        bool value = inherit_checkbox_->value();
        if (value != state_->inherits_assets) {
            state_->inherits_assets = value;
            changed = true;
        }
    }

    const bool editing_size_box =
        (width_min_box_ && width_min_box_->is_editing()) || (width_max_box_ && width_max_box_->is_editing()) || (height_min_box_ && height_min_box_->is_editing()) || (height_max_box_ && height_max_box_->is_editing());

    if (state_->ensure_valid(allow_height, !editing_size_box)) {
        changed = true;
    }

    if (state_->is_spawn && state_->is_boss) {
        state_->is_boss = false;
        if (boss_checkbox_) boss_checkbox_->set_value(false);
    }

    sync_text_box_with_value(width_min_box_.get(), state_->width_min);
    sync_text_box_with_value(width_max_box_.get(), state_->width_max);
    sync_text_box_with_value(height_min_box_.get(), state_->height_min);
    sync_text_box_with_value(height_max_box_.get(), state_->height_max);
    if (radius_slider_) {
        bool skip_slider_sync =
            radius_slider_->defer_commit_until_unfocus() && radius_slider_->has_pending_values();
        if (!skip_slider_sync) {
            radius_slider_->set_min_value(state_->radius_min);
            radius_slider_->set_max_value(state_->radius_max);
        }
    }

    if (changed) {
        state_->apply_to_json(loaded_json_, allow_height);
        write_tags_to_json(loaded_json_);
        if (room_) {
            auto& root = live_room_json();
            state_->apply_to_json(root, allow_height);
            write_tags_to_json(root);
            room_->save_assets_json();
            if (tags_changed) {
                tag_utils::notify_tags_changed();
            }
        } else if (external_room_json_) {
            auto& root = live_room_json();
            state_->apply_to_json(root, allow_height);
            write_tags_to_json(root);
        }
        if (on_external_spawn_change_) {
            on_external_spawn_change_();
        }
    }

    return rebuild_required;
}

bool RoomConfigurator::handle_event(const SDL_Event& e) {
    if (!container_ || !container_->is_visible()) return false;
    if (last_screen_w_ > 0 && last_screen_h_ > 0) {
        prepare_for_event(last_screen_w_, last_screen_h_);
    }
    return container_->handle_event(e);
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (!container_ || !container_->is_visible()) return;
    container_->render(r, last_screen_w_, last_screen_h_);
    DMDropdown::render_active_options(r);
}

const SDL_Rect& RoomConfigurator::panel_rect() const {
    if (!container_) {
        static SDL_Rect empty{0, 0, 0, 0};
        return empty;
    }
    return container_->panel_rect();
}

std::string RoomConfigurator::current_header_text() const {
    if (state_ && !state_->name.empty()) {
        if (is_trail_context_) {
            return std::string{"Trail: "} + state_->name;
        }
        return std::string{"Room: "} + state_->name;
    }
    return is_trail_context_ ? std::string{"Trail Config"} : std::string{"Room Config"};
}

const nlohmann::json& RoomConfigurator::live_room_json() const {
    if (room_) {
        return room_->assets_data();
    }
    if (external_room_json_) {
        return *external_room_json_;
    }
    return loaded_json_;
}

nlohmann::json& RoomConfigurator::live_room_json() {
    if (room_) {
        return room_->assets_data();
    }
    if (external_room_json_) {
        return *external_room_json_;
    }
    if (!loaded_json_.is_object()) {
        loaded_json_ = nlohmann::json::object();
    }
    return loaded_json_;
}

nlohmann::json RoomConfigurator::build_json() const {
    nlohmann::json result = loaded_json_.is_object() ? loaded_json_ : nlohmann::json::object();
    if (state_) {
        State copy = *state_;
    const bool allow_height = !is_trail_context_ && kTrailsAllowIndependentDimensions;
        copy.ensure_valid(allow_height);
        copy.apply_to_json(result, allow_height);
    }
    return result;
}

bool RoomConfigurator::is_point_inside(int x, int y) const {
    return container_ && container_->is_point_inside(x, y);
}

void RoomConfigurator::load_tags_from_json(const nlohmann::json& data) {
    std::set<std::string> include;
    std::set<std::string> exclude;

    auto read_array = [&](const nlohmann::json& arr, std::set<std::string>& dest) {
        if (!arr.is_array()) return;
        for (const auto& entry : arr) {
            if (!entry.is_string()) continue;
            std::string normalized = tag_utils::normalize(entry.get<std::string>());
            if (!normalized.empty()) dest.insert(std::move(normalized));
        }
};

    if (data.is_object()) {
        if (data.contains("tags")) {
            const auto& section = data["tags"];
            if (section.is_object()) {
                if (section.contains("include")) read_array(section["include"], include);
                if (section.contains("tags")) read_array(section["tags"], include);
                if (section.contains("exclude")) read_array(section["exclude"], exclude);
                if (section.contains("anti_tags")) read_array(section["anti_tags"], exclude);
            } else if (section.is_array()) {
                read_array(section, include);
            }
        }
        if (data.contains("anti_tags")) {
            read_array(data["anti_tags"], exclude);
        }
    }

    room_tags_.assign(include.begin(), include.end());
    room_anti_tags_.assign(exclude.begin(), exclude.end());
}

void RoomConfigurator::write_tags_to_json(nlohmann::json& object) const {
    if (!object.is_object()) {
        object = nlohmann::json::object();
    }
    if (room_tags_.empty() && room_anti_tags_.empty()) {
        object.erase("tags");
        object.erase("anti_tags");
        return;
    }

    nlohmann::json section = nlohmann::json::object();
    if (!room_tags_.empty()) {
        section["include"] = room_tags_;
    }
    if (!room_anti_tags_.empty()) {
        section["exclude"] = room_anti_tags_;
    }
    object["tags"] = std::move(section);
    object.erase("anti_tags");
}

void RoomConfigurator::set_spawn_group_callbacks(std::function<void(const std::string&)> on_edit,
                                                 std::function<void(const std::string&)> on_delete,
                                                 std::function<void(const std::string&, size_t)> on_reorder,
                                                 std::function<void()> on_add,
                                                 std::function<void(const std::string&)> on_regenerate) {
    on_spawn_edit_ = std::move(on_edit);
    on_spawn_delete_ = std::move(on_delete);
    on_spawn_reorder_ = std::move(on_reorder);
    on_spawn_add_ = std::move(on_add);
    on_spawn_regenerate_ = std::move(on_regenerate);
}

bool RoomConfigurator::focus_spawn_group(const std::string& spawn_id) {
    if (spawn_id.empty()) {
        return false;
    }
    ensure_base_panels();
    if (!container_) {
        return false;
    }

    SpawnGroupConfig* target = nullptr;
    for (size_t i = 0; i < spawn_group_config_ids_.size(); ++i) {
        if (spawn_group_config_ids_[i] == spawn_id && i < spawn_group_configs_.size() && spawn_group_configs_[i]) {
            target = spawn_group_configs_[i].get();
            break;
        }
    }
    if (!target) {
        return false;
    }

    focus_panel(target);
    target->request_open_spawn_group(spawn_id, 0, 0);

    prepare_for_event(last_screen_w_, last_screen_h_);
    container_->prepare_layout(last_screen_w_, last_screen_h_);

    SDL_Rect view = container_->scroll_region();
    if (view.h <= 0) {
        return true;
    }

    SDL_Rect rect = target->rect();
    int current_scroll = container_->scroll_value();
    int new_scroll = current_scroll;

    const int actual_top = rect.y + current_scroll;
    int actual_bottom = rect.y + rect.h + current_scroll;
    if (rect.h <= 0) {
        actual_bottom = actual_top + cached_collapsible_height(target);
    }

    if (rect.y < view.y) {
        new_scroll = std::max(0, actual_top - view.y);
    } else if (rect.y + rect.h > view.y + view.h) {
        new_scroll = std::max(0, actual_bottom - (view.y + view.h));
    }

    if (new_scroll != current_scroll) {
        container_->set_scroll_value(new_scroll);
        container_->prepare_layout(last_screen_w_, last_screen_h_);
    }

    return true;
}

void RoomConfigurator::set_spawn_area_open_callback(
    std::function<void(const std::string&, const std::string&)> cb,
    std::string stack_key) {
    on_spawn_area_open_ = std::move(cb);
    spawn_area_stack_key_ = std::move(stack_key);
    for (auto& config : spawn_group_configs_) {
        if (config) {
            config->refresh_row_configuration();
        }
    }
}

void RoomConfigurator::request_rebuild() {
    deferred_rebuild_ = true;
}

