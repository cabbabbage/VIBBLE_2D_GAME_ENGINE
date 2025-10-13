#include "room_configurator.hpp"

#include "dm_styles.hpp"
#include "DockableCollapsible.hpp"
#include "map_generation/room.hpp"
#include "../spawn_group_config/SpawnGroupConfig.hpp"
#include "../spawn_group_config/spawn_group_utils.hpp"
#include "tag_editor_widget.hpp"
#include "tag_utils.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"
#include "font_cache.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace {
constexpr int kRoomConfigPanelContentWidth = 360;
constexpr int kRoomConfigPanelMinWidth = 260;

class RoomConfiguratorSectionLabel : public Widget {
public:
    explicit RoomConfiguratorSectionLabel(std::string text, bool subtle = false)
        : text_(std::move(text)), subtle_(subtle) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override {
        return DMCheckbox::height();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const DMLabelStyle& st = DMStyles::Label();
        SDL_Color color = subtle_ ? SDL_Color{static_cast<Uint8>(st.color.r / 2),
                                             static_cast<Uint8>(st.color.g / 2), static_cast<Uint8>(st.color.b / 2), st.color.a} : st.color;
        DMLabelStyle style{st.font_path, st.font_size, color};
        DrawLabelText(renderer, text_, rect_.x, rect_.y, style);
    }

private:
    std::string text_;
    bool subtle_ = false;
    SDL_Rect rect_{0, 0, 0, 0};
};

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

std::pair<int, int> compute_slider_range(int min_value, int max_value) {
    int lo = std::min(min_value, max_value);
    int hi = std::max(min_value, max_value);
    lo = std::max(0, lo);
    hi = std::max(lo + 1, hi);
    int span = std::max(hi - lo, 200);
    int padding = std::max(100, span / 2);
    int slider_min = std::max(0, lo - padding);
    int slider_max = hi + padding;
    if (slider_max <= slider_min) slider_max = slider_min + 200;
    slider_max = std::min(200000, slider_max);
    return {slider_min, slider_max};
}

bool append_unique(std::vector<std::string>& options, const std::string& value) {
    if (value.empty()) return false;
    if (std::find(options.begin(), options.end(), value) != options.end()) {
        return false;
    }
    options.push_back(value);
    return true;
}

}

struct RoomConfigurator::State {
    std::string name;
    std::string geometry;
    int width_min = 1500;
    int width_max = 10000;
    int height_min = 1500;
    int height_max = 10000;
    int radius = 0;
    int edge_smoothness = 2;
    int curvyness = 2;
    bool is_spawn = false;
    bool is_boss = false;
    bool inherits_assets = false;

    bool geometry_is_circle() const { return lowercase_copy(geometry) == "circle"; }

    void ensure_valid(bool allow_height) {
        if (width_min > width_max) std::swap(width_min, width_max);
        if (allow_height) {
            if (height_min > height_max) std::swap(height_min, height_max);
        } else {
            height_min = width_min;
            height_max = width_max;
        }
        width_min = std::max(0, width_min);
        width_max = std::max(width_min + 1, width_max);
        height_min = std::max(0, height_min);
        height_max = std::max(height_min + 1, height_max);
        radius = std::max(0, radius);
        edge_smoothness = std::clamp(edge_smoothness, 0, 101);
        curvyness = std::max(0, curvyness);
        if (geometry_is_circle()) {
            int diameter = std::max(width_max, height_max);
            radius = std::max(radius, diameter / 2);
            width_min = width_max = height_min = height_max = radius * 2;
        }
        if (is_spawn && is_boss) {

            is_boss = false;
        }
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

        radius = 0;
        if (geometry_is_circle()) {
            if (auto value = read_radius_value(src)) {
                radius = *value;
            } else {
                radius = infer_radius_from_dimensions(width_min, width_max, height_min, height_max);
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
            int diameter = std::max(0, radius) * 2;
            dest["radius"] = radius;
            dest["min_width"] = diameter;
            dest["max_width"] = diameter;
            dest["min_height"] = diameter;
            dest["max_height"] = diameter;
        } else {
            dest.erase("radius");
            dest["min_width"] = width_min;
            dest["max_width"] = width_max;
            dest["min_height"] = allow_height ? height_min : width_min;
            dest["max_height"] = allow_height ? height_max : width_max;
        }
    }
};

RoomConfigurator::RoomConfigurator() {
    geometry_options_ = {"Square", "Circle"};
    row_gap_ = DMSpacing::item_gap();
    col_gap_ = DMSpacing::item_gap();
    cell_width_ = kRoomConfigPanelContentWidth;
    container_.set_header_text_provider([this]() {
        if (state_ && !state_->name.empty()) {
            return std::string{"Room: "} + state_->name;
        }
        return std::string{"Room Config"};
    });
    container_.set_on_close([this]() { handle_container_closed(); });
    container_.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        return this->layout_content(ctx);
    });
    container_.set_render_function([this](SDL_Renderer* renderer) {
        if (basic_panel_) basic_panel_->render(renderer);
        for (const auto& cfg : spawn_group_configs_) {
            if (cfg) cfg->render(renderer);
        }
    });
    container_.set_event_function([this](const SDL_Event& e) {
        // Give child dockables first shot at events so clicks don't get swallowed by the container
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            this->close();
            return true;
        }
        bool used = false;
        if (basic_panel_ && basic_panel_->is_visible()) {
            if (basic_panel_->handle_event(e)) used = true;
        }
        for (auto& cfg : spawn_group_configs_) {
            if (cfg && cfg->is_visible()) {
                if (cfg->handle_event(e)) used = true;
            }
        }
        return used;
    });
    container_.set_update_function([this](const Input& input, int screen_w, int screen_h) {
        if (basic_panel_) basic_panel_->update(input, screen_w, screen_h);
        for (auto& cfg : spawn_group_configs_) {
            if (cfg) cfg->update(input, screen_w, screen_h);
        }
    });
    container_.set_blocks_editor_interactions(true);
    container_.set_scrollbar_visible(false);
    container_.set_header_visible(show_header_);
    state_ = std::make_unique<State>();
}

RoomConfigurator::~RoomConfigurator() = default;

void RoomConfigurator::set_bounds(const SDL_Rect& bounds) {
    bounds_override_ = bounds;
    has_bounds_override_ = bounds.w > 0 && bounds.h > 0;
    if (has_bounds_override_) {
        SDL_Rect clamped = bounds;
        clamped.w = std::max(0, clamped.w);
        clamped.h = std::max(0, clamped.h);
        container_.set_panel_bounds_override(clamped);
        const int padding = DMSpacing::panel_padding();
        int available = std::max(kRoomConfigPanelMinWidth, clamped.w - padding * 2);
        cell_width_ = std::max(kRoomConfigPanelMinWidth, available);
    } else {
        container_.clear_panel_bounds_override();
        cell_width_ = kRoomConfigPanelContentWidth;
    }
}

void RoomConfigurator::set_work_area(const SDL_Rect& bounds) {
    work_area_ = bounds;
    // No-op for bounds override; container default layout handles snug sizing
}

void RoomConfigurator::set_show_header(bool show) {
    show_header_ = show;
    container_.set_header_visible(show_header_);
}

void RoomConfigurator::set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

void RoomConfigurator::set_header_visibility_controller(std::function<void(bool)> cb) {
    container_.set_header_visibility_controller(std::move(cb));
}

void RoomConfigurator::reset_scroll() { container_.reset_scroll(); }

bool RoomConfigurator::add_spawn_group_direct() {
    nlohmann::json& root = live_room_json();
    nlohmann::json& groups = devmode::spawn::ensure_spawn_groups_array(root);

    nlohmann::json new_group = nlohmann::json::object();
    devmode::spawn::ensure_spawn_group_entry_defaults(new_group, "New Spawn");
    groups.push_back(new_group);
    renumber_spawn_group_priorities(groups);
    devmode::spawn::sanitize_perimeter_spawn_groups(groups);

    if (room_) {
        room_->save_assets_json();
        refresh_spawn_groups(room_);
    } else if (external_room_json_) {
        refresh_spawn_groups(*external_room_json_);
        if (on_external_spawn_change_) on_external_spawn_change_();
    } else {
        bool changed = apply_room_data(root);
        if (changed) {
            rebuild_rows();
        } else {
            request_rebuild();
        }
    }
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

RoomConfigurator::Rows RoomConfigurator::compute_layout_rows() const {
    Rows layout_rows;
    layout_rows.reserve(rows_.size());
    for (const auto& row : rows_) {
        Row current;
        bool inserted_any = false;
        for (Widget* w : row) {
            if (w && w->wants_full_row()) {
                if (!current.empty()) {
                    layout_rows.push_back(current);
                    current.clear();
                }
                layout_rows.push_back({w});
                inserted_any = true;
            } else {
                current.push_back(w);
                if (w) inserted_any = true;
            }
        }
        if (!current.empty()) {
            layout_rows.push_back(std::move(current));
        } else if (!inserted_any) {
            layout_rows.emplace_back();
        }
    }
    return layout_rows;
}

int RoomConfigurator::layout_content(const SlidingWindowContainer::LayoutContext& ctx) const {
    int y = ctx.content_top;
    // Position the basic info panel first
    if (basic_panel_ && basic_panel_->is_visible()) {
        basic_panel_->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, 0});
        y += basic_panel_->height() + ctx.gap;
    }
    // Then position each spawn group panel in order
    for (const auto& cfg : spawn_group_configs_) {
        if (!cfg || !cfg->is_visible()) continue;
        cfg->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, 0});
        y += cfg->height() + ctx.gap;
    }
    return y + ctx.gap;
}

void RoomConfigurator::set_rows(Rows rows) { rows_ = std::move(rows); }

void RoomConfigurator::handle_container_closed() {
    for (auto& config : spawn_group_configs_) {
        if (config) {
            config->close();
            config->set_visible(false);
            config->close_embedded_search();
        }
    }
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

    State new_state = state_ ? *state_ : State{};
    new_state.load_from_json(normalized_copy, geometry_options_, !is_trail_context_);

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
            new_state.radius != state_->radius ||
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
    room_ = nullptr;
    external_room_json_ = nullptr;
    on_external_spawn_change_ = {};
    on_external_spawn_entry_change_ = {};
    external_configure_entry_ = {};
    is_trail_context_ = false;
    bool changed = apply_room_data(room_data);
    if (changed) {
        rebuild_rows();
        reset_scroll();
    }
    container_.open();
}

void RoomConfigurator::open(nlohmann::json& room_data,
                            std::function<void()> on_change,
                            std::function<void(const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&)> on_entry_change,
                            SpawnGroupConfig::ConfigureEntryCallback configure_entry) {
    room_ = nullptr;
    external_room_json_ = &room_data;
    on_external_spawn_change_ = std::move(on_change);
    on_external_spawn_entry_change_ = std::move(on_entry_change);
    external_configure_entry_ = std::move(configure_entry);
    is_trail_context_ = false;
    bool changed = apply_room_data(room_data);
    if (changed) {
        rebuild_rows();
        reset_scroll();
    }
    container_.open();
}

void RoomConfigurator::open(Room* room) {
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
    if (changed) {
        rebuild_rows();
        reset_scroll();
    }
    container_.open();
}

bool RoomConfigurator::refresh_spawn_groups(const nlohmann::json& room_data) {
    bool changed = apply_room_data(room_data);
    if (changed) {
        rebuild_rows();
    }
    return changed;
}

bool RoomConfigurator::refresh_spawn_groups(nlohmann::json& room_data) {
    external_room_json_ = &room_data;
    bool changed = apply_room_data(room_data);
    if (changed) {
        rebuild_rows();
    }
    return changed;
}

bool RoomConfigurator::refresh_spawn_groups(Room* room) {
    const nlohmann::json& src = room ? room->assets_data() : empty_object();
    return refresh_spawn_groups(src);
}

void RoomConfigurator::close() {
    if (!container_.is_visible()) {
        for (auto& config : spawn_group_configs_) {
            if (config) config->set_visible(false);
        }
        external_room_json_ = nullptr;
        on_external_spawn_change_ = {};
        on_external_spawn_entry_change_ = {};
        external_configure_entry_ = {};
        return;
    }
    container_.close();
}

bool RoomConfigurator::visible() const { return container_.is_visible(); }

bool RoomConfigurator::any_panel_visible() const { return visible(); }

bool RoomConfigurator::is_locked() const {
    if (basic_panel_ && basic_panel_->isLocked()) {
        return true;
    }
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

void RoomConfigurator::rebuild_spawn_rows(Rows& rows) {
    // In the new layout, spawn groups are individual dockable panels
    spawn_label_.reset();
    empty_spawn_label_.reset();
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

        // Panels are dockable collapsibles anchored inside the container
        config->set_embedded_mode(true);
        config->set_show_header(true);
        config->set_close_button_enabled(false);
        config->set_scroll_enabled(false);
        config->force_pointer_ready();
        if (created_new) {
            config->set_expanded(false);
        }

        config->set_screen_dimensions(last_screen_w_, last_screen_h_);

        SpawnGroupConfig::Callbacks callbacks{};
        callbacks.on_regenerate = [this](const std::string& value) {
            if (on_spawn_regenerate_) on_spawn_regenerate_(value);
        };
        callbacks.on_duplicate = [this](const std::string& value) {
            if (on_spawn_duplicate_) on_spawn_duplicate_(value);
            this->request_rebuild();
            if (room_) this->refresh_spawn_groups(room_);
            else if (external_room_json_) this->refresh_spawn_groups(*external_room_json_);
        };
        callbacks.on_delete = [this](const std::string& value) {
            if (on_spawn_delete_) on_spawn_delete_(value);
            this->request_rebuild();
            if (room_) this->refresh_spawn_groups(room_);
            else if (external_room_json_) this->refresh_spawn_groups(*external_room_json_);
        };
        callbacks.on_reorder = [this, groups = &group_array](const std::string& value, size_t index) {
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

        SpawnGroupConfig::ConfigureEntryCallback final_configure_entry;
        if (configure_entry) {
            final_configure_entry = [this, configure_entry = std::move(configure_entry)](
                                        SpawnGroupConfig::EntryController& entry, const nlohmann::json& cfg_entry) {
                configure_entry(entry, cfg_entry);
                entry.set_open_area_handler(on_spawn_area_open_, spawn_area_stack_key_);
            };
        } else if (on_spawn_area_open_ || !spawn_area_stack_key_.empty()) {
            final_configure_entry = [this](SpawnGroupConfig::EntryController& entry, const nlohmann::json&) {
                entry.set_open_area_handler(on_spawn_area_open_, spawn_area_stack_key_);
            };
        }

        // Derive a header title from the spawn entry
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

        // Bind and build panel first with callbacks suppressed to avoid
        // re-entrant rebuild loops during initial construction.
        auto wrapped_entry_change = [this, cfg=config.get(), on_entry_change = std::move(on_entry_change), title_from](
                                        const nlohmann::json& updated,
                                        const SpawnGroupConfig::ChangeSummary& summary) {
            // Update title when name changes
            if (cfg) cfg->set_title(title_from(updated));
            if (on_entry_change) on_entry_change(updated, summary);
        };
        config->bind_entry(entry,
                           std::move(on_change),
                           std::move(wrapped_entry_change),
                           std::move(entry_callbacks),
                           std::move(final_configure_entry));
        // When the panel layout changes (collapse/expand), request a relayout
        config->set_on_layout_changed([this]() { this->request_rebuild(); });

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
            auto on_change = [this]() {
                if (room_) room_->save_assets_json();
};
            auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&) {
                if (room_) room_->save_assets_json();
                this->request_rebuild();
};
            bind_spawn_entry(entry, groups, configure_entry, std::move(on_change), std::move(on_entry_change));
        }
    } else if (external_room_json_) {
        auto& root = live_room_json();
        nlohmann::json& groups = devmode::spawn::ensure_spawn_groups_array(root);

        for (auto& entry : groups) {
            have_groups = true;
            auto on_change = [this]() {
                if (!external_room_json_) return;
                if (on_external_spawn_change_) on_external_spawn_change_();
};
            auto on_entry_change = [this](const nlohmann::json& updated, const SpawnGroupConfig::ChangeSummary& summary) {
                if (!external_room_json_) return;
                if (on_external_spawn_entry_change_) on_external_spawn_entry_change_(updated, summary);
                if (on_external_spawn_change_) on_external_spawn_change_();
                this->request_rebuild();
};
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
            auto on_change = [this]() { this->request_rebuild(); };
            auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&) { this->request_rebuild(); };
            bind_spawn_entry(entry, groups, {}, std::move(on_change), std::move(on_entry_change));
        }
    }
    (void)have_groups; // Panels are handled above; basic panel handles add button
}

void RoomConfigurator::rebuild_rows() {
    if (!state_) {
        state_ = std::make_unique<State>();
    }

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
        // Failsafe: if rebuilds keep getting requested, schedule a deferred
        // rebuild on the next tick and bail to avoid freezing.
        static int guard_counter = 0;
        if (++guard_counter > 8) {
            deferred_rebuild_ = true;
            guard_counter = 0;
            break;
        }
    }
}

void RoomConfigurator::rebuild_rows_internal() {
    if (!state_) {
        state_ = std::make_unique<State>();
    }

    Rows rows;
    room_section_label_ = std::make_unique<RoomConfiguratorSectionLabel>("Room Properties");
    rows.push_back({room_section_label_.get()});

    name_box_ = std::make_unique<DMTextBox>("Room Name", state_->name);
    name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get());
    rows.push_back({name_widget_.get()});

    bool allow_geometry_choice = !is_trail_context_;
    if (allow_geometry_choice) {
        geometry_label_ = std::make_unique<RoomConfiguratorSectionLabel>("Geometry", true);
        rows.push_back({geometry_label_.get()});
        auto geom_it = std::find(geometry_options_.begin(), geometry_options_.end(), state_->geometry);
        int geom_index = 0;
        if (geom_it != geometry_options_.end()) {
            geom_index = static_cast<int>(std::distance(geometry_options_.begin(), geom_it));
        }
        geometry_dropdown_ = std::make_unique<DMDropdown>("", geometry_options_, geom_index);
        geometry_widget_ = std::make_unique<DropdownWidget>(geometry_dropdown_.get());
        rows.push_back({geometry_widget_.get()});
    } else {
        geometry_label_.reset();
        geometry_dropdown_.reset();
        geometry_widget_.reset();
    }

    dimensions_label_ = std::make_unique<RoomConfiguratorSectionLabel>("Dimensions", true);
    rows.push_back({dimensions_label_.get()});

    if (state_->geometry_is_circle()) {
        radius_slider_ = std::make_unique<DMSlider>("Radius", 0, 200000, state_->radius);
        radius_slider_->set_defer_commit_until_unfocus(true);
        radius_widget_ = std::make_unique<SliderWidget>(radius_slider_.get());
        rows.push_back({radius_widget_.get()});
        width_slider_.reset();
        width_widget_.reset();
        height_slider_.reset();
        height_widget_.reset();
    } else {
        radius_slider_.reset();
        radius_widget_.reset();

        auto width_range = compute_slider_range(state_->width_min, state_->width_max);
        width_slider_ = std::make_unique<DMRangeSlider>(width_range.first, width_range.second, state_->width_min, state_->width_max);
        width_slider_->set_defer_commit_until_unfocus(true);
        width_widget_ = std::make_unique<RangeSliderWidget>(width_slider_.get());
        rows.push_back({width_widget_.get()});

        if (!is_trail_context_) {
            auto height_range = compute_slider_range(state_->height_min, state_->height_max);
            height_slider_ = std::make_unique<DMRangeSlider>(height_range.first, height_range.second, state_->height_min, state_->height_max);
            height_slider_->set_defer_commit_until_unfocus(true);
            height_widget_ = std::make_unique<RangeSliderWidget>(height_slider_.get());
            rows.push_back({height_widget_.get()});
        } else {
            height_slider_.reset();
            height_widget_.reset();
        }
    }

    edge_slider_ = std::make_unique<DMSlider>("Edge Smoothness", 0, 101, state_->edge_smoothness);
    edge_slider_->set_defer_commit_until_unfocus(true);
    edge_widget_ = std::make_unique<SliderWidget>(edge_slider_.get());
    rows.push_back({edge_widget_.get()});

    if (is_trail_context_) {
        curvy_slider_ = std::make_unique<DMSlider>("Curvyness", 0, 16, state_->curvyness);
        curvy_slider_->set_defer_commit_until_unfocus(true);
        curvy_widget_ = std::make_unique<SliderWidget>(curvy_slider_.get());
        rows.push_back({curvy_widget_.get()});
    } else {
        curvy_slider_.reset();
        curvy_widget_.reset();
    }

    toggles_label_ = std::make_unique<RoomConfiguratorSectionLabel>("Flags", true);
    rows.push_back({toggles_label_.get()});

    spawn_checkbox_ = std::make_unique<DMCheckbox>("Spawn", state_->is_spawn);
    spawn_widget_ = std::make_unique<CheckboxWidget>(spawn_checkbox_.get());

    if (!is_trail_context_) {
        boss_checkbox_ = std::make_unique<DMCheckbox>("Boss", state_->is_boss);
        boss_widget_ = std::make_unique<CheckboxWidget>(boss_checkbox_.get());
    } else {
        boss_checkbox_.reset();
        boss_widget_.reset();
    }

    inherit_checkbox_ = std::make_unique<DMCheckbox>("Inherit Map Assets", state_->inherits_assets);
    inherit_widget_ = std::make_unique<CheckboxWidget>(inherit_checkbox_.get());

    if (boss_widget_) {
        rows.push_back({spawn_widget_.get(), boss_widget_.get(), inherit_widget_.get()});
    } else {
        rows.push_back({spawn_widget_.get(), inherit_widget_.get()});
    }

    // Build/refresh spawn group panels (as separate dockable panels)
    rebuild_spawn_rows(rows);

    tags_label_ = std::make_unique<RoomConfiguratorSectionLabel>("Tags", true);
    rows.push_back({tags_label_.get()});

    tag_editor_ = std::make_unique<TagEditorWidget>();
    tag_editor_->set_tags(room_tags_, room_anti_tags_);
    tag_editor_->set_on_changed([this](const std::vector<std::string>& include,
                                       const std::vector<std::string>& exclude) {
        if (include != room_tags_ || exclude != room_anti_tags_) {
            room_tags_ = include;
            room_anti_tags_ = exclude;
            tags_dirty_ = true;
        }
    });
    rows.push_back({tag_editor_.get()});

    add_spawn_button_ = std::make_unique<DMButton>("Add Spawn Group", &DMStyles::CreateButton(), 0, DMButton::height());
    add_spawn_widget_ = std::make_unique<ButtonWidget>(add_spawn_button_.get(), [this]() {
        if (on_spawn_add_) {
            on_spawn_add_();
        } else {
            this->add_spawn_group_direct();
        }
    });
    rows.push_back({add_spawn_widget_.get()});

    set_rows(rows);

    // Ensure a dockable basic info panel exists and uses these rows
    if (!basic_panel_) {
        basic_panel_ = std::make_unique<DockableCollapsible>("Basic Room Info", false);
        basic_panel_->set_show_header(true);
        basic_panel_->set_close_button_enabled(false);
        basic_panel_->set_scroll_enabled(false);
        basic_panel_->set_expanded(false);
    } else {
        basic_panel_->set_scroll_enabled(false);
    }
    basic_panel_->set_rows(rows_);
    basic_panel_->force_pointer_ready();
}

void RoomConfigurator::update(const Input& input, int screen_w, int screen_h) {
    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;
    const bool panel_visible = container_.is_visible();
    if (basic_panel_) {
        basic_panel_->set_visible(panel_visible);
    }
    for (auto& config : spawn_group_configs_) {
        if (!config) continue;
        config->set_visible(panel_visible);
        config->set_screen_dimensions(screen_w, screen_h);
    }

    container_.update(input, screen_w, screen_h);

    if (!state_) return;

    bool needs_rebuild = sync_state_from_widgets();
    if (needs_rebuild) {
        rebuild_rows();
    } else if (deferred_rebuild_) {
        deferred_rebuild_ = false;
        rebuild_rows();
    }
}

bool RoomConfigurator::sync_state_from_widgets() {
    if (!state_) return false;

    bool changed = false;
    bool rebuild_required = false;

    if (tags_dirty_) {
        changed = true;
        tags_dirty_ = false;
    }

    if (name_box_) {
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
                if (!radius_slider_) {
                    state_->radius = infer_radius_from_dimensions(state_->width_min, state_->width_max, state_->height_min, state_->height_max);
                }
            } else if (radius_slider_) {
                int diameter = std::max(0, state_->radius) * 2;
                state_->width_min = state_->width_max = std::max(1, diameter);
                state_->height_min = state_->height_max = std::max(1, diameter);
                state_->radius = 0;
            }
            rebuild_required = true;
            changed = true;
        }
    }

    if (width_slider_) {
        int new_min = width_slider_->min_value();
        int new_max = width_slider_->max_value();
        if (new_min != state_->width_min || new_max != state_->width_max) {
            state_->width_min = std::min(new_min, new_max);
            state_->width_max = std::max(new_min, new_max);
            changed = true;
        }
    }

    if (height_slider_) {
        int new_min = height_slider_->min_value();
        int new_max = height_slider_->max_value();
        if (new_min != state_->height_min || new_max != state_->height_max) {
            state_->height_min = std::min(new_min, new_max);
            state_->height_max = std::max(new_min, new_max);
            changed = true;
        }
    }

    if (radius_slider_) {
        int value = std::max(0, radius_slider_->value());
        if (value != state_->radius) {
            state_->radius = value;
            changed = true;
        }
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

    state_->ensure_valid(!is_trail_context_);

    if (state_->is_spawn && state_->is_boss) {
        state_->is_boss = false;
        if (boss_checkbox_) boss_checkbox_->set_value(false);
    }

    if (changed) {
        state_->apply_to_json(loaded_json_, !is_trail_context_);
        write_tags_to_json(loaded_json_);
        if (room_) {
            auto& root = live_room_json();
            state_->apply_to_json(root, !is_trail_context_);
            write_tags_to_json(root);
            room_->save_assets_json();
        } else if (external_room_json_) {
            auto& root = live_room_json();
            state_->apply_to_json(root, !is_trail_context_);
            write_tags_to_json(root);
        }
    }

    return rebuild_required;
}

bool RoomConfigurator::handle_event(const SDL_Event& e) {
    if (!container_.is_visible()) return false;
    return container_.handle_event(e);
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (!container_.is_visible()) return;
    container_.render(r, last_screen_w_, last_screen_h_);
    DMDropdown::render_active_options(r);
}

const SDL_Rect& RoomConfigurator::panel_rect() const { return container_.panel_rect(); }

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
        copy.ensure_valid(!is_trail_context_);
        copy.apply_to_json(result, !is_trail_context_);
    }
    return result;
}

bool RoomConfigurator::is_point_inside(int x, int y) const { return container_.is_point_inside(x, y); }

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
                                                 std::function<void(const std::string&)> on_duplicate,
                                                 std::function<void(const std::string&)> on_delete,
                                                 std::function<void(const std::string&, size_t)> on_reorder,
                                                 std::function<void()> on_add,
                                                 std::function<void(const std::string&)> on_regenerate) {
    on_spawn_edit_ = std::move(on_edit);
    on_spawn_duplicate_ = std::move(on_duplicate);
    on_spawn_delete_ = std::move(on_delete);
    on_spawn_reorder_ = std::move(on_reorder);
    on_spawn_add_ = std::move(on_add);
    on_spawn_regenerate_ = std::move(on_regenerate);
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

