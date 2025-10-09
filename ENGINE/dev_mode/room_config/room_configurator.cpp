#include "room_configurator.hpp"

#include "FloatingDockableManager.hpp"
#include "dm_styles.hpp"
#include "map_generation/room.hpp"
#include "spawn_group_list.hpp"
#include "tag_editor_widget.hpp"
#include "tag_utils.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <SDL_ttf.h>

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
                                             static_cast<Uint8>(st.color.g / 2),
                                             static_cast<Uint8>(st.color.b / 2),
                                             st.color.a}
                                  : st.color;
        TTF_Font* font = TTF_OpenFont(st.font_path.c_str(), st.font_size);
        if (!font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text_.c_str(), color);
        if (!surface) {
            TTF_CloseFont(font);
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);
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

std::optional<int> find_dimension_value(const nlohmann::json& obj,
                                       std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (!key) continue;
        if (auto value = read_json_int(obj, key)) {
            return value;
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

} // namespace

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
            // prefer spawn flag if both enabled
            is_boss = false;
        }
    }

    void load_from_json(const nlohmann::json& data,
                        const std::vector<std::string>& geometry_options,
                        bool allow_height) {
        const nlohmann::json& src = data.is_object() ? data : empty_object();
        name = src.value("name", src.value("room_name", std::string{}));
        geometry = src.value("geometry", geometry_options.empty() ? std::string{} : geometry_options.front());

        if (auto value = find_dimension_value(src, {"min_width", "width_min", "minWidth", "widthMin"})) {
            width_min = *value;
        }
        if (auto value = find_dimension_value(src, {"max_width", "width_max", "maxWidth", "widthMax"})) {
            width_max = *value;
        }
        if (allow_height) {
            if (auto value = find_dimension_value(src, {"min_height", "height_min", "minHeight", "heightMin"})) {
                height_min = *value;
            }
            if (auto value = find_dimension_value(src, {"max_height", "height_max", "maxHeight", "heightMax"})) {
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
            dest["width_min"] = diameter;
            dest["width_max"] = diameter;
            dest["min_height"] = diameter;
            dest["max_height"] = diameter;
            dest["height_min"] = diameter;
            dest["height_max"] = diameter;
        } else {
            dest.erase("radius");
            dest["min_width"] = width_min;
            dest["max_width"] = width_max;
            dest["width_min"] = width_min;
            dest["width_max"] = width_max;
            dest["min_height"] = allow_height ? height_min : width_min;
            dest["max_height"] = allow_height ? height_max : width_max;
            dest["height_min"] = allow_height ? height_min : width_min;
            dest["height_max"] = allow_height ? height_max : width_max;
        }
    }
};

RoomConfigurator::RoomConfigurator()
    : DockableCollapsible("Room Config", true, 0, 0) {
    geometry_options_ = {"Square", "Circle"};
    set_close_button_enabled(true);
    set_title("Room Config");
    set_expanded(true);
    set_visible(false);
    set_show_header(true);
    set_scroll_enabled(true);
    set_padding(DMSpacing::panel_padding());
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::item_gap());
    set_cell_width(kRoomConfigPanelContentWidth);
    set_available_height_override(kMaxFloatingHeight);
    set_work_area(SDL_Rect{0, 0, 0, 0});
    floating_position_ = position();
    state_ = std::make_unique<State>();
}

RoomConfigurator::~RoomConfigurator() = default;

void RoomConfigurator::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    const bool want_docked = bounds_.w > 0 && bounds_.h > 0;
    if (want_docked != docked_mode_) {
        if (want_docked) {
            floating_position_ = has_custom_position_ ? position() : preferred_position_;
            FloatingDockableManager::instance().notify_panel_closed(this);
            set_floatable(false);
            set_close_button_enabled(false);
            set_scroll_enabled(true);
            has_custom_position_ = false;
        } else {
            set_floatable(true);
            set_close_button_enabled(true);
            preferred_position_ = floating_position_;
        }
        docked_mode_ = want_docked;
    } else if (!want_docked) {
        preferred_position_ = floating_position_;
    }
    applied_bounds_ = SDL_Rect{-1, -1, 0, 0};
    apply_bounds_if_needed();
}

void RoomConfigurator::apply_bounds_if_needed() {
    if (!docked_mode_ || bounds_.w <= 0 || bounds_.h <= 0) {
        if (applied_bounds_.x != bounds_.x || applied_bounds_.y != bounds_.y ||
            applied_bounds_.w != bounds_.w || applied_bounds_.h != bounds_.h) {
            set_available_height_override(kMaxFloatingHeight);
            set_visible_height(kMaxFloatingHeight);
            applied_bounds_ = bounds_;
            if (!has_custom_position_) {
                set_position(preferred_position_.x, preferred_position_.y);
                floating_position_ = preferred_position_;
            }
        }
        return;
    }

    if (applied_bounds_.x == bounds_.x && applied_bounds_.y == bounds_.y &&
        applied_bounds_.w == bounds_.w && applied_bounds_.h == bounds_.h) {
        return;
    }

    const int pad = DMSpacing::panel_padding();
    const int available = std::max(0, bounds_.h - 2 * pad);
    const int available_width = std::max(0, bounds_.w - 2 * pad);
    int cell_width = kRoomConfigPanelContentWidth;
    if (available_width > 0) {
        cell_width = std::min(kRoomConfigPanelContentWidth, available_width);
        if (available_width >= kRoomConfigPanelMinWidth) {
            cell_width = std::max(kRoomConfigPanelMinWidth, cell_width);
        }
    }

    set_cell_width(cell_width);
    int override_h = available > 0 ? std::min(available, kMaxFloatingHeight) : kMaxFloatingHeight;
    set_available_height_override(override_h);
    set_visible_height(available > 0 ? available : override_h);
    DockableCollapsible::set_rect(bounds_);
    applied_bounds_ = bounds_;
}

void RoomConfigurator::undock_from_sidebar(const SDL_Point& grab_point) {
    if (!docked_mode_) return;

    const int offset_x = grab_point.x - bounds_.x;
    const int offset_y = grab_point.y - bounds_.y;
    const int max_header_width = kRoomConfigPanelContentWidth;
    const int clamped_offset_x = std::max(0, std::min(offset_x, max_header_width - 1));
    int new_x = grab_point.x - clamped_offset_x;
    int new_y = grab_point.y - std::max(0, std::min(offset_y, DMButton::height() - 1));

    docked_mode_ = false;
    bounds_ = SDL_Rect{0, 0, 0, 0};
    applied_bounds_ = SDL_Rect{-1, -1, 0, 0};
    set_floatable(true);
    set_close_button_enabled(true);
    set_available_height_override(kMaxFloatingHeight);

    if (!has_custom_position_) {
        preferred_position_ = floating_position_;
    }

    floating_position_ = SDL_Point{new_x, new_y};
    preferred_position_ = floating_position_;
    has_custom_position_ = true;
    set_position(new_x, new_y);
    SDL_Point clamped = position();
    floating_position_ = clamped;
    preferred_position_ = clamped;
    FloatingDockableManager::instance().open_floating(
        "Room Config", this, [this]() { this->close(); });
}

bool RoomConfigurator::apply_room_data(const nlohmann::json& data) {
    const nlohmann::json& normalized = data.is_object() ? data : empty_object();

    bool new_spawn_from_assets = false;
    const nlohmann::json* new_spawn_array = nullptr;
    if (normalized.contains("spawn_groups") && normalized["spawn_groups"].is_array()) {
        new_spawn_array = &normalized["spawn_groups"];
        new_spawn_from_assets = false;
    } else if (normalized.contains("assets") && normalized["assets"].is_array()) {
        new_spawn_array = &normalized["assets"];
        new_spawn_from_assets = true;
    }

    const nlohmann::json* current_spawn_array = nullptr;
    if (spawn_groups_from_assets_) {
        if (loaded_json_.contains("assets") && loaded_json_["assets"].is_array()) {
            current_spawn_array = &loaded_json_["assets"];
        }
    } else {
        if (loaded_json_.contains("spawn_groups") && loaded_json_["spawn_groups"].is_array()) {
            current_spawn_array = &loaded_json_["spawn_groups"];
        }
    }

    bool spawn_changed = false;
    if (new_spawn_from_assets != spawn_groups_from_assets_) {
        spawn_changed = true;
    } else if (new_spawn_array || current_spawn_array) {
        if (!new_spawn_array || !current_spawn_array || *new_spawn_array != *current_spawn_array) {
            spawn_changed = true;
        }
    }

    State new_state = state_ ? *state_ : State{};
    new_state.load_from_json(normalized, geometry_options_, !is_trail_context_);

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

    load_tags_from_json(normalized);
    capture_tags(room_tags_, include);
    capture_tags(room_anti_tags_, exclude);
    bool tags_changed = (include != prev_include) || (exclude != prev_exclude);

    if (!spawn_changed && !dims_changed && !geometry_added && !tags_changed) {
        return false;
    }

    loaded_json_ = normalized;
    spawn_groups_from_assets_ = new_spawn_from_assets;
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
    if (!docked_mode_) {
        FloatingDockableManager::instance().open_floating(
            "Room Config", this, [this]() { this->close(); });
    }
    bool changed = apply_room_data(room_data);
    if (changed) {
        rebuild_rows();
        reset_scroll();
    }
    set_visible(true);
    set_expanded(true);
    apply_bounds_if_needed();
}

void RoomConfigurator::open(nlohmann::json& room_data,
                            std::function<void()> on_change,
                            std::function<void(const nlohmann::json&, const SpawnGroupList::ChangeSummary&)> on_entry_change,
                            SpawnGroupList::ConfigureEntryCallback configure_entry) {
    room_ = nullptr;
    external_room_json_ = &room_data;
    on_external_spawn_change_ = std::move(on_change);
    on_external_spawn_entry_change_ = std::move(on_entry_change);
    external_configure_entry_ = std::move(configure_entry);
    is_trail_context_ = false;
    if (!docked_mode_) {
        FloatingDockableManager::instance().open_floating(
            "Room Config", this, [this]() { this->close(); });
    }
    bool changed = apply_room_data(room_data);
    if (changed) {
        rebuild_rows();
        reset_scroll();
    }
    set_visible(true);
    set_expanded(true);
    apply_bounds_if_needed();
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
    if (!docked_mode_) {
        FloatingDockableManager::instance().open_floating(
            "Room Config", this, [this]() { this->close(); });
    }

    const nlohmann::json& source = room ? room->assets_data() : empty_object();
    bool changed = (room != previous) || apply_room_data(source);
    if (changed) {
        rebuild_rows();
        reset_scroll();
    }
    set_visible(true);
    set_expanded(true);
    apply_bounds_if_needed();
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
    set_visible(false);
    external_room_json_ = nullptr;
    on_external_spawn_change_ = {};
    on_external_spawn_entry_change_ = {};
    external_configure_entry_ = {};
}

bool RoomConfigurator::visible() const { return is_visible(); }

bool RoomConfigurator::any_panel_visible() const { return visible(); }

std::string RoomConfigurator::selected_geometry() const {
    if (!state_) return geometry_options_.empty() ? std::string{} : geometry_options_.front();
    if (geometry_options_.empty()) return state_->geometry;
    auto it = std::find(geometry_options_.begin(), geometry_options_.end(), state_->geometry);
    if (it != geometry_options_.end()) return *it;
    return geometry_options_.front();
}

void RoomConfigurator::ensure_spawn_list() {
    if (!spawn_list_) {
        spawn_list_ = std::make_unique<SpawnGroupList>();
        spawn_list_->set_embedded_mode(true);
    }
}

void RoomConfigurator::rebuild_spawn_rows(Rows& rows) {
    ensure_spawn_list();
    spawn_label_ = std::make_unique<RoomConfiguratorSectionLabel>("Spawn Groups");
    rows.push_back({spawn_label_.get()});

    empty_spawn_label_.reset();

    bool have_groups = false;
    if (room_) {
        auto& root = live_room_json();
        const char* key = spawn_groups_from_assets_ ? "assets" : "spawn_groups";
        if (!root.contains(key) || !root[key].is_array()) {
            root[key] = nlohmann::json::array();
        }
        nlohmann::json& groups = root[key];

        auto on_change = [this]() {
            if (room_) {
                room_->save_assets_json();
                this->refresh_spawn_groups(room_);
            }
        };

        auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupList::ChangeSummary&) {
            if (room_) {
                room_->save_assets_json();
                this->refresh_spawn_groups(room_);
            }
        };

        SpawnGroupList::ConfigureEntryCallback configure_entry = [this](SpawnGroupList::RowController& row, const nlohmann::json&) {
            row.set_area_names_provider([this]() {
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
                row.set_ownership_label(label, SDL_Color{255, 224, 96, 255});
            }
        };

        auto expanded = spawn_list_->expanded_groups();
        spawn_list_->load(groups, on_change, on_entry_change, std::move(configure_entry));
        spawn_list_->set_on_layout_changed([this]() { this->rebuild_rows(); });
        spawn_list_->restore_expanded_groups(expanded);
        SpawnGroupList::Callbacks cb{};
        cb.on_regenerate = [this](const std::string& id) {
            if (on_spawn_regenerate_) on_spawn_regenerate_(id);
        };
        cb.on_duplicate = [this](const std::string& id) {
            if (on_spawn_duplicate_) on_spawn_duplicate_(id);
        };
        cb.on_delete = [this](const std::string& id) {
            if (on_spawn_delete_) on_spawn_delete_(id);
        };
        cb.on_move_up = [this](const std::string& id) {
            if (on_spawn_move_up_) on_spawn_move_up_(id);
        };
        cb.on_move_down = [this](const std::string& id) {
            if (on_spawn_move_down_) on_spawn_move_down_(id);
        };
        cb.on_add = [this]() {
            if (on_spawn_add_) on_spawn_add_();
        };
        spawn_list_->set_callbacks(std::move(cb));
        spawn_list_->append_rows(rows);
        have_groups = true;
    } else if (external_room_json_) {
        auto& root = live_room_json();
        const char* key = spawn_groups_from_assets_ ? "assets" : "spawn_groups";
        if (!root.contains(key) || !root[key].is_array()) {
            root[key] = nlohmann::json::array();
        }
        nlohmann::json& groups = root[key];

        auto on_change = [this]() {
            if (external_room_json_) {
                this->refresh_spawn_groups(*external_room_json_);
                if (on_external_spawn_change_) on_external_spawn_change_();
            }
        };

        auto on_entry_change = [this](const nlohmann::json& entry, const SpawnGroupList::ChangeSummary& summary) {
            if (external_room_json_) {
                this->refresh_spawn_groups(*external_room_json_);
                if (on_external_spawn_entry_change_) {
                    on_external_spawn_entry_change_(entry, summary);
                }
                if (on_external_spawn_change_) {
                    on_external_spawn_change_();
                }
            }
        };

        auto expanded = spawn_list_->expanded_groups();
        spawn_list_->load(groups, std::move(on_change), std::move(on_entry_change), external_configure_entry_);
        spawn_list_->set_on_layout_changed([this]() { this->rebuild_rows(); });
        spawn_list_->restore_expanded_groups(expanded);
        SpawnGroupList::Callbacks cb{};
        cb.on_regenerate = [this](const std::string& id) {
            if (on_spawn_regenerate_) on_spawn_regenerate_(id);
        };
        cb.on_duplicate = [this](const std::string& id) {
            if (on_spawn_duplicate_) on_spawn_duplicate_(id);
        };
        cb.on_delete = [this](const std::string& id) {
            if (on_spawn_delete_) on_spawn_delete_(id);
        };
        cb.on_move_up = [this](const std::string& id) {
            if (on_spawn_move_up_) on_spawn_move_up_(id);
        };
        cb.on_move_down = [this](const std::string& id) {
            if (on_spawn_move_down_) on_spawn_move_down_(id);
        };
        cb.on_add = [this]() {
            if (on_spawn_add_) on_spawn_add_();
        };
        spawn_list_->set_callbacks(std::move(cb));
        spawn_list_->append_rows(rows);
        have_groups = true;
    } else {
        const nlohmann::json* groups = nullptr;
        const char* primary_key = spawn_groups_from_assets_ ? "assets" : "spawn_groups";
        if (loaded_json_.contains(primary_key) && loaded_json_[primary_key].is_array()) {
            groups = &loaded_json_[primary_key];
        } else {
            const char* fallback = spawn_groups_from_assets_ ? "spawn_groups" : "assets";
            if (loaded_json_.contains(fallback) && loaded_json_[fallback].is_array()) {
                groups = &loaded_json_[fallback];
            }
        }
        if (groups) {
            auto expanded = spawn_list_->expanded_groups();
            spawn_list_->load(*groups);
            spawn_list_->set_on_layout_changed([this]() { this->rebuild_rows(); });
            spawn_list_->restore_expanded_groups(expanded);
            SpawnGroupList::Callbacks cb{};
            cb.on_regenerate = [this](const std::string& id) {
                if (on_spawn_regenerate_) on_spawn_regenerate_(id);
            };
            cb.on_duplicate = [this](const std::string& id) {
                if (on_spawn_duplicate_) on_spawn_duplicate_(id);
            };
            cb.on_delete = [this](const std::string& id) {
                if (on_spawn_delete_) on_spawn_delete_(id);
            };
            cb.on_move_up = [this](const std::string& id) {
                if (on_spawn_move_up_) on_spawn_move_up_(id);
            };
            cb.on_move_down = [this](const std::string& id) {
                if (on_spawn_move_down_) on_spawn_move_down_(id);
            };
            cb.on_add = [this]() {
                if (on_spawn_add_) on_spawn_add_();
            };
            spawn_list_->set_callbacks(std::move(cb));
            spawn_list_->append_rows(rows);
            have_groups = true;
        }
    }

    if (!have_groups) {
        empty_spawn_label_ = std::make_unique<RoomConfiguratorSectionLabel>("No spawn groups configured.", true);
        rows.push_back({empty_spawn_label_.get()});
    }
}

void RoomConfigurator::rebuild_rows() {
    if (!state_) {
        state_ = std::make_unique<State>();
    }

    DockableCollapsible::Rows rows;
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
        width_slider_ = std::make_unique<DMRangeSlider>(width_range.first, width_range.second,
                                                        state_->width_min, state_->width_max);
        width_slider_->set_defer_commit_until_unfocus(true);
        width_widget_ = std::make_unique<RangeSliderWidget>(width_slider_.get());
        rows.push_back({width_widget_.get()});

        if (!is_trail_context_) {
            auto height_range = compute_slider_range(state_->height_min, state_->height_max);
            height_slider_ = std::make_unique<DMRangeSlider>(height_range.first, height_range.second,
                                                             state_->height_min, state_->height_max);
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

    set_rows(rows);
}

void RoomConfigurator::update(const Input& input, int screen_w, int screen_h) {
    const bool panel_visible = is_visible();
    apply_bounds_if_needed();
    DockableCollapsible::update(input, screen_w, screen_h);

    if (spawn_list_) {
        spawn_list_->set_visible(panel_visible);
        spawn_list_->set_screen_dimensions(screen_w, screen_h);
        SDL_Rect panel_rect = rect();
        SDL_Point anchor{panel_rect.x + panel_rect.w + DMSpacing::item_gap(), panel_rect.y};
        spawn_list_->set_anchor(anchor.x, anchor.y);
        spawn_list_->update(input, screen_w, screen_h);
    }

    if (!state_) return;

    bool needs_rebuild = sync_state_from_widgets();
    if (needs_rebuild) {
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
        int idx = std::clamp(geometry_dropdown_->selected(), 0,
                              static_cast<int>(geometry_options_.size()) - 1);
        std::string selected = geometry_options_.empty() ? std::string{} : geometry_options_[idx];
        if (selected != state_->geometry) {
            state_->geometry = selected;
            if (state_->geometry_is_circle()) {
                if (!radius_slider_) {
                    state_->radius = infer_radius_from_dimensions(state_->width_min, state_->width_max,
                                                                  state_->height_min, state_->height_max);
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
    bool used = false;
    if (is_visible()) {
        apply_bounds_if_needed();
        if (docked_mode_ && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{e.button.x, e.button.y};
            if (SDL_PointInRect(&p, &header_rect_) || SDL_PointInRect(&p, &handle_rect_)) {
                undock_from_sidebar(p);
            }
        }
        SDL_Point before = position();
        used |= DockableCollapsible::handle_event(e);
        SDL_Point after = position();
        if (after.x != before.x || after.y != before.y) {
            has_custom_position_ = true;
            if (!docked_mode_) {
                floating_position_ = after;
            }
        }
    }
    if (spawn_list_ && spawn_list_->handle_event(e)) {
        used = true;
    }
    return used;
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (is_visible()) {
        DockableCollapsible::render(r);
    }
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
        copy.ensure_valid(!is_trail_context_);
        copy.apply_to_json(result, !is_trail_context_);
    }
    return result;
}

bool RoomConfigurator::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    return DockableCollapsible::is_point_inside(x, y);
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
                                                 std::function<void(const std::string&)> on_duplicate,
                                                 std::function<void(const std::string&)> on_delete,
                                                 std::function<void(const std::string&)> on_move_up,
                                                 std::function<void(const std::string&)> on_move_down,
                                                 std::function<void()> on_add,
                                                 std::function<void(const std::string&)> on_regenerate) {
    on_spawn_edit_ = std::move(on_edit);
    on_spawn_duplicate_ = std::move(on_duplicate);
    on_spawn_delete_ = std::move(on_delete);
    on_spawn_move_up_ = std::move(on_move_up);
    on_spawn_move_down_ = std::move(on_move_down);
    on_spawn_add_ = std::move(on_add);
    on_spawn_regenerate_ = std::move(on_regenerate);
}

