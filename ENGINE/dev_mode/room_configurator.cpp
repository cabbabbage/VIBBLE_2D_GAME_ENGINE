#include "room_configurator.hpp"

#include "dm_styles.hpp"
#include "FloatingDockableManager.hpp"
#include "tag_editor_widget.hpp"
#include "tag_utils.hpp"
#include "map_generation/room.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"
#include "spawn_group_list.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>
#include <utility>
#include <optional>
#include <initializer_list>
#include <set>

namespace {
constexpr int kRoomConfigPanelContentWidth = 360;
constexpr int kRoomConfigPanelMinWidth = 260;
class RoomConfigLabel : public Widget {
public:
    explicit RoomConfigLabel(std::string text) : text_(std::move(text)) {}

    void set_text(std::string text) { text_ = std::move(text); }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int ) const override {
        const DMLabelStyle& st = DMStyles::Label();
        return st.font_size + DMSpacing::small_gap() * 2;
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const DMLabelStyle& st = DMStyles::Label();
        SDL_Color color = st.color;
        TTF_Font* font = st.open_font();
        if (!font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text_.c_str(), color);
        if (!surface) {
            TTF_CloseFont(font);
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_Rect dst = rect_;
        dst.h = surface->h;
        dst.w = surface->w;
        if (texture) {
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);
    }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
};

std::string build_spawn_summary(int index, const nlohmann::json& entry) {
    std::string display = entry.value("display_name", entry.value("name", entry.value("spawn_id", std::string{"Spawn"})));
    std::string method = entry.value("position", std::string{"Unknown"});
    int min_q = entry.value("min_number", entry.value("max_number", 0));
    int max_q = entry.value("max_number", min_q);
    std::ostringstream ss;
    ss << index << ". " << display << " — " << method << " (" << min_q << "-" << max_q << ")";
    return ss.str();
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
    if (slider_max <= slider_min) {
        slider_max = slider_min + 200;
    }
    slider_max = std::min(200000, slider_max);
    return {slider_min, slider_max};
}

const nlohmann::json& empty_object() {
    static const nlohmann::json kEmpty = nlohmann::json::object();
    return kEmpty;
}

std::optional<int> read_json_int(const nlohmann::json& object, const std::string& key) {
    if (!object.is_object() || !object.contains(key)) {
        return std::nullopt;
    }
    const auto& value = object[key];
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

std::optional<int> find_dimension_value(const nlohmann::json& object,
                                       std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (!key) continue;
        if (auto value = read_json_int(object, key)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<int> read_radius_value(const nlohmann::json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    if (auto value = read_json_int(object, "radius")) {
        return std::max(0, *value);
    }
    return std::nullopt;
}

int infer_radius_from_dimensions(int w_min, int w_max, int h_min, int h_max) {
    int diameter = 0;
    diameter = std::max(diameter, std::max(w_min, w_max));
    diameter = std::max(diameter, std::max(h_min, h_max));
    if (diameter <= 0) {
        return 0;
    }
    return std::max(0, diameter / 2);
}
}

RoomConfigurator::RoomConfigurator()
    : DockableCollapsible("Room Config", true, 0, 0) {
    set_close_button_enabled(true);
    if (room_geom_options_.empty()) {
        room_geom_options_ = {"Square", "Circle"};
    }
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
    if (!docked_mode_) {
        return;
    }

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

void RoomConfigurator::load_from_json(const nlohmann::json& data) {
    if (data.is_object()) {
        loaded_json_ = data;
    } else {
        loaded_json_ = nlohmann::json::object();
    }

    spawn_groups_from_assets_ = false;
    if (loaded_json_.contains("spawn_groups") && loaded_json_["spawn_groups"].is_array()) {
        spawn_groups_from_assets_ = false;
    } else if (loaded_json_.contains("assets") && loaded_json_["assets"].is_array()) {
        spawn_groups_from_assets_ = true;
    }

    room_name_ = loaded_json_.value("name", loaded_json_.value("room_name", std::string{}));

    int fallback_w_min = room_w_min_;
    int fallback_w_max = room_w_max_;
    int fallback_h_min = room_h_min_;
    int fallback_h_max = room_h_max_;

    if (auto value = find_dimension_value(loaded_json_,
                                         {"min_width", "width_min", "minWidth", "widthMin"})) {
        room_w_min_ = *value;
    } else {
        room_w_min_ = fallback_w_min;
    }

    int width_fallback = std::max(room_w_min_, fallback_w_max);
    if (auto value = find_dimension_value(loaded_json_,
                                         {"max_width", "width_max", "maxWidth", "widthMax"})) {
        room_w_max_ = *value;
    } else {
        room_w_max_ = width_fallback;
    }

    if (auto value = find_dimension_value(loaded_json_,
                                         {"min_height", "height_min", "minHeight", "heightMin"})) {
        room_h_min_ = *value;
    } else {
        room_h_min_ = fallback_h_min;
    }

    int height_fallback = std::max(room_h_min_, fallback_h_max);
    if (auto value = find_dimension_value(loaded_json_,
                                         {"max_height", "height_max", "maxHeight", "heightMax"})) {
        room_h_max_ = *value;
    } else {
        room_h_max_ = height_fallback;
    }

    if (room_w_min_ > room_w_max_) std::swap(room_w_min_, room_w_max_);
    if (room_h_min_ > room_h_max_) std::swap(room_h_min_, room_h_max_);

    std::string geom = loaded_json_.value("geometry", room_geom_options_.empty() ? std::string{} : room_geom_options_.front());
    if (!geom.empty()) {
        auto it = std::find(room_geom_options_.begin(), room_geom_options_.end(), geom);
        if (it == room_geom_options_.end()) {
            room_geom_options_.push_back(geom);
            room_geom_ = static_cast<int>(room_geom_options_.size() - 1);
        } else {
            room_geom_ = static_cast<int>(std::distance(room_geom_options_.begin(), it));
        }
    } else {
        room_geom_ = 0;
    }

    bool geometry_is_circle = false;
    if (!geom.empty()) {
        std::string lowered = geom;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        geometry_is_circle = (lowered == "circle");
    }

    room_radius_ = 0;
    if (geometry_is_circle) {
        if (auto radius_value = read_radius_value(loaded_json_)) {
            room_radius_ = *radius_value;
        } else {
            room_radius_ = infer_radius_from_dimensions(room_w_min_, room_w_max_, room_h_min_, room_h_max_);
        }
        if (room_radius_ < 0) room_radius_ = 0;
    }

    room_is_spawn_ = loaded_json_.value("is_spawn", false);
    room_is_boss_ = loaded_json_.value("is_boss", false);
    room_inherits_assets_ = loaded_json_.value("inherits_map_assets", false);

    is_trail_context_ = false;
    if (room_) {
        const std::string& dir = room_->room_directory;
        if (dir.find("trails_data") != std::string::npos) {
            is_trail_context_ = true;
        }
    }

    edge_smoothness_ = loaded_json_.value("edge_smoothness", 2);
    if (edge_smoothness_ < 0) edge_smoothness_ = 0;
    if (edge_smoothness_ > 101) edge_smoothness_ = 101;

    if (loaded_json_.contains("curvyness")) {
        if (auto cv = read_json_int(loaded_json_, "curvyness")) {
            curvyness_ = std::max(0, *cv);
        } else {
            curvyness_ = 2;
        }
    } else {
        curvyness_ = 2;
    }

    load_tags_from_json(loaded_json_);
}

void RoomConfigurator::load_tags_from_json(const nlohmann::json& data) {
    tags_dirty_ = false;
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
            const auto& tags_section = data["tags"];
            if (tags_section.is_object()) {
                if (tags_section.contains("include")) read_array(tags_section["include"], include);
                if (tags_section.contains("tags")) read_array(tags_section["tags"], include);
                if (tags_section.contains("exclude")) read_array(tags_section["exclude"], exclude);
                if (tags_section.contains("anti_tags")) read_array(tags_section["anti_tags"], exclude);
            } else if (tags_section.is_array()) {
                read_array(tags_section, include);
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

bool RoomConfigurator::refresh_spawn_groups(const nlohmann::json& data) {
    if (!loaded_json_.is_object()) {
        loaded_json_ = nlohmann::json::object();
    }

    bool data_has_spawn_groups = data.is_object() && data.contains("spawn_groups") && data["spawn_groups"].is_array();
    bool data_has_assets = data.is_object() && data.contains("assets") && data["assets"].is_array();

    if (data_has_spawn_groups) {
        spawn_groups_from_assets_ = false;
    } else if (data_has_assets) {
        spawn_groups_from_assets_ = true;
    }

    int new_w_min = room_w_min_;
    int new_w_max = room_w_max_;
    int new_h_min = room_h_min_;
    int new_h_max = room_h_max_;

    bool have_w_min = false;
    bool have_w_max = false;
    bool have_h_min = false;
    bool have_h_max = false;

    if (auto value = find_dimension_value(data, {"min_width", "width_min", "minWidth", "widthMin"})) {
        new_w_min = *value;
        have_w_min = true;
    }
    if (auto value = find_dimension_value(data, {"max_width", "width_max", "maxWidth", "widthMax"})) {
        new_w_max = *value;
        have_w_max = true;
    }
    if (auto value = find_dimension_value(data, {"min_height", "height_min", "minHeight", "heightMin"})) {
        new_h_min = *value;
        have_h_min = true;
    }
    if (auto value = find_dimension_value(data, {"max_height", "height_max", "maxHeight", "heightMax"})) {
        new_h_max = *value;
        have_h_max = true;
    }

    if (new_w_min > new_w_max) std::swap(new_w_min, new_w_max);
    if (new_h_min > new_h_max) std::swap(new_h_min, new_h_max);

    bool dims_changed = (new_w_min != room_w_min_) || (new_w_max != room_w_max_) || (new_h_min != room_h_min_) || (new_h_max != room_h_max_);

    std::string geometry = selected_geometry();
    std::string lowered_geom;
    lowered_geom.reserve(geometry.size());
    for (char ch : geometry) {
        lowered_geom.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    bool is_circle_geom = (lowered_geom == "circle");

    int new_radius = room_radius_;
    bool radius_changed = false;
    if (is_circle_geom) {
        if (auto radius_value = read_radius_value(data)) {
            new_radius = *radius_value;
            radius_changed = (new_radius != room_radius_);
        } else if (dims_changed || room_radius_ <= 0) {
            int inferred = infer_radius_from_dimensions(new_w_min, new_w_max, new_h_min, new_h_max);
            if (inferred != room_radius_) {
                new_radius = inferred;
                radius_changed = true;
            }
        }
    }

    room_w_min_ = new_w_min;
    room_w_max_ = new_w_max;
    room_h_min_ = new_h_min;
    room_h_max_ = new_h_max;
    if (is_circle_geom && new_radius >= 0) {
        room_radius_ = new_radius;
    }

    if (is_circle_geom) {
        int diameter = std::max(0, room_radius_) * 2;
        loaded_json_["radius"] = room_radius_;
        loaded_json_["min_width"] = diameter;
        loaded_json_["width_min"] = diameter;
        loaded_json_["max_width"] = diameter;
        loaded_json_["width_max"] = diameter;
        loaded_json_["min_height"] = diameter;
        loaded_json_["height_min"] = diameter;
        loaded_json_["max_height"] = diameter;
        loaded_json_["height_max"] = diameter;
    } else {
        if (have_w_min || have_w_max || dims_changed) {
            loaded_json_["min_width"] = room_w_min_;
            loaded_json_["width_min"] = room_w_min_;
            loaded_json_["max_width"] = room_w_max_;
            loaded_json_["width_max"] = room_w_max_;
        }
        if (have_h_min || have_h_max || dims_changed) {
            loaded_json_["min_height"] = room_h_min_;
            loaded_json_["height_min"] = room_h_min_;
            loaded_json_["max_height"] = room_h_max_;
            loaded_json_["height_max"] = room_h_max_;
        }
        loaded_json_.erase("radius");
    }

    const char* target_key = spawn_groups_from_assets_ ? "assets" : "spawn_groups";
    nlohmann::json new_groups = nlohmann::json::array();
    if (data_has_spawn_groups) {
        new_groups = data["spawn_groups"];
    } else if (data_has_assets) {
        new_groups = data["assets"];
    }

    nlohmann::json& target = loaded_json_[target_key];
    bool groups_changed = !target.is_array() || target != new_groups;
    if (groups_changed) {
        target = new_groups;
        if (spawn_groups_from_assets_) {
            if (loaded_json_.contains("spawn_groups")) {
                loaded_json_.erase("spawn_groups");
            }
        } else if (loaded_json_.contains("assets")) {
            loaded_json_.erase("assets");
        }
    }
    bool dims_or_radius_changed = dims_changed || radius_changed;

    if (groups_changed) {
        rebuild_rows();
        return true;
    }
    if (dims_or_radius_changed) {
        rebuild_rows();
        return true;
    }
    return false;
}

bool RoomConfigurator::refresh_spawn_groups(Room* room) {
    const nlohmann::json& source = room ? room->assets_data() : empty_object();
    return refresh_spawn_groups(source);
}

std::string RoomConfigurator::selected_geometry() const {
    if (room_geom_options_.empty()) {
        return std::string{};
    }
    int idx = std::clamp(room_geom_, 0, static_cast<int>(room_geom_options_.size()) - 1);
    return room_geom_options_[idx];
}

bool RoomConfigurator::should_rebuild_with(const nlohmann::json& data) const {
    if (!is_visible()) {
        return true;
    }
    if (!loaded_json_.is_object()) {
        return true;
    }
    const nlohmann::json& normalized = data.is_object() ? data : empty_object();
    return loaded_json_ != normalized;
}

void RoomConfigurator::open(const nlohmann::json& data) {
    room_ = nullptr;
    if (!docked_mode_) {
        FloatingDockableManager::instance().open_floating(
            "Room Config", this, [this]() { this->close(); });
    }
    const bool was_visible = is_visible();
    if (!should_rebuild_with(data)) {
        set_visible(true);
        if (!was_visible) set_expanded(true);
        apply_bounds_if_needed();
        return;
    }
    load_from_json(data);
    rebuild_rows();
    reset_scroll();
    set_visible(true);
    if (!was_visible) set_expanded(true);
    apply_bounds_if_needed();
}

void RoomConfigurator::open(Room* room) {
    Room* previous_room = room_;
    const bool same_room = (room == previous_room);
    const nlohmann::json& source = room ? room->assets_data() : empty_object();
    room_ = room;
    if (!docked_mode_) {
        FloatingDockableManager::instance().open_floating(
            "Room Config", this, [this]() { this->close(); });
    }
    const bool was_visible = is_visible();
    if (same_room && !should_rebuild_with(source)) {
        set_visible(true);
        if (!was_visible) set_expanded(true);
        apply_bounds_if_needed();
        return;
    }
    load_from_json(source);
    rebuild_rows();
    reset_scroll();
    set_visible(true);
    if (!was_visible) set_expanded(true);
    apply_bounds_if_needed();
}

void RoomConfigurator::close() { set_visible(false); }

bool RoomConfigurator::visible() const { return is_visible(); }

bool RoomConfigurator::any_panel_visible() const { return visible(); }

void RoomConfigurator::rebuild_rows() {
    DockableCollapsible::Rows rows;
    // rows owned by SpawnGroupList are rebuilt each time via append_rows
    spawn_groups_label_.reset();
    add_group_btn_.reset();
    add_group_btn_w_.reset();
    empty_spawn_label_.reset();
    room_section_label_.reset();
    room_tags_label_.reset();
    room_tags_editor_.reset();

    room_section_label_ = std::make_unique<RoomConfigLabel>("Room Settings");
    rows.push_back({ room_section_label_.get() });

    room_name_lbl_ = std::make_unique<DMTextBox>("Room Name", room_name_);
    room_name_lbl_w_ = std::make_unique<TextBoxWidget>(room_name_lbl_.get());
    rows.push_back({ room_name_lbl_w_.get() });

    bool allow_dimension_sliders = true;
    if (!room_name_.empty()) {
        std::string lowered = room_name_;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lowered == "spawn") {
            allow_dimension_sliders = false;
        }
    }

    if (allow_dimension_sliders) {

        std::string geom_name = selected_geometry();
        std::string lowered_geom;
        lowered_geom.reserve(geom_name.size());
        for (char ch : geom_name) {
            lowered_geom.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        bool is_circle_geom = (lowered_geom == "circle");

        if (is_circle_geom) {
            room_w_label_.reset();
            room_w_slider_.reset();
            room_w_slider_w_.reset();
            room_h_label_.reset();
            room_h_slider_.reset();
            room_h_slider_w_.reset();

            int radius_value = std::max(0, room_radius_);
            auto radius_bounds = compute_slider_range(radius_value, radius_value);
            room_radius_slider_ = std::make_unique<DMSlider>("Radius", radius_bounds.first, radius_bounds.second, radius_value);
            room_radius_slider_w_ = std::make_unique<SliderWidget>(room_radius_slider_.get());
            rows.push_back({ room_radius_slider_w_.get() });
        } else {
            room_radius_slider_.reset();
            room_radius_slider_w_.reset();

            room_w_label_ = std::make_unique<RoomConfigLabel>("Width (Min/Max)");
            rows.push_back({ room_w_label_.get() });
            auto width_bounds = compute_slider_range(room_w_min_, room_w_max_);
            room_w_slider_ = std::make_unique<DMRangeSlider>(width_bounds.first, width_bounds.second, room_w_min_, room_w_max_);
            room_w_slider_w_ = std::make_unique<RangeSliderWidget>(room_w_slider_.get());
            rows.push_back({ room_w_slider_w_.get() });

            if (!is_trail_context_) {
                room_h_label_ = std::make_unique<RoomConfigLabel>("Height (Min/Max)");
                rows.push_back({ room_h_label_.get() });
                auto height_bounds = compute_slider_range(room_h_min_, room_h_max_);
                room_h_slider_ = std::make_unique<DMRangeSlider>(height_bounds.first, height_bounds.second, room_h_min_, room_h_max_);
                room_h_slider_w_ = std::make_unique<RangeSliderWidget>(room_h_slider_.get());
                rows.push_back({ room_h_slider_w_.get() });
            } else {
                room_h_slider_.reset();
                room_h_slider_w_.reset();
                room_h_label_.reset();
            }
        }
    } else {
        room_w_slider_.reset();
        room_w_slider_w_.reset();
        room_h_slider_.reset();
        room_h_slider_w_.reset();
        room_radius_slider_.reset();
        room_radius_slider_w_.reset();
    }

    int geom_index = room_geom_options_.empty() ? 0 : std::clamp(room_geom_, 0, static_cast<int>(room_geom_options_.size()) - 1);
    if (!is_trail_context_) {
        room_geom_dd_ = std::make_unique<DMDropdown>("Geometry", room_geom_options_, geom_index);
        room_geom_dd_w_ = std::make_unique<DropdownWidget>(room_geom_dd_.get());
        rows.push_back({ room_geom_dd_w_.get() });
    } else {
        room_geom_dd_.reset();
        room_geom_dd_w_.reset();
    }

    edge_smoothness_sl_ = std::make_unique<DMSlider>("Edge Smoothness", 0, 101, edge_smoothness_);
    edge_smoothness_w_ = std::make_unique<SliderWidget>(edge_smoothness_sl_.get());
    rows.push_back({ edge_smoothness_w_.get() });

    bool show_curvy = is_trail_context_;
    if (show_curvy) {
        curvyness_sl_ = std::make_unique<DMSlider>("Curvyness", 0, 16, curvyness_);
        curvyness_w_ = std::make_unique<SliderWidget>(curvyness_sl_.get());
        rows.push_back({ curvyness_w_.get() });
    } else {
        curvyness_sl_.reset();
        curvyness_w_.reset();
    }

    room_spawn_cb_.reset();
    room_spawn_cb_w_.reset();
    room_boss_cb_.reset();
    room_boss_cb_w_.reset();
    room_inherit_cb_.reset();
    room_inherit_cb_w_.reset();

    if (!is_trail_context_) {
        room_spawn_cb_ = std::make_unique<DMCheckbox>("Spawn", room_is_spawn_);
        room_spawn_cb_w_ = std::make_unique<CheckboxWidget>(room_spawn_cb_.get());
        room_boss_cb_ = std::make_unique<DMCheckbox>("Boss", room_is_boss_);
        room_boss_cb_w_ = std::make_unique<CheckboxWidget>(room_boss_cb_.get());
        room_inherit_cb_ = std::make_unique<DMCheckbox>("Inherit Map Assets", room_inherits_assets_);
        room_inherit_cb_w_ = std::make_unique<CheckboxWidget>(room_inherit_cb_.get());
        rows.push_back({ room_spawn_cb_w_.get(), room_boss_cb_w_.get(), room_inherit_cb_w_.get() });
    } else {
        room_inherit_cb_ = std::make_unique<DMCheckbox>("Inherit Map Assets", room_inherits_assets_);
        room_inherit_cb_w_ = std::make_unique<CheckboxWidget>(room_inherit_cb_.get());
        rows.push_back({ room_inherit_cb_w_.get() });
    }

    spawn_groups_label_ = std::make_unique<RoomConfigLabel>("Spawn Groups");
    rows.push_back({ spawn_groups_label_.get() });

    bool have_groups = false;
    if (!loaded_json_.is_null()) {
        const nlohmann::json* groups = nullptr;
        if (loaded_json_.contains("spawn_groups") && loaded_json_["spawn_groups"].is_array()) {
            groups = &loaded_json_["spawn_groups"];
        } else if (loaded_json_.contains("assets") && loaded_json_["assets"].is_array()) {
            groups = &loaded_json_["assets"];
        }
        if (groups) {
            if (!spawn_list_) spawn_list_ = std::make_unique<SpawnGroupList>();
            spawn_list_->load(*groups);
            SpawnGroupList::Callbacks cb{};
            cb.on_edit = [this](const std::string& id) { if (on_spawn_edit_) on_spawn_edit_(id); };
            cb.on_duplicate = [this](const std::string& id) { if (on_spawn_duplicate_) on_spawn_duplicate_(id); };
            cb.on_delete = [this](const std::string& id) { if (on_spawn_delete_) on_spawn_delete_(id); };
            cb.on_move_up = [this](const std::string& id) { if (on_spawn_move_up_) on_spawn_move_up_(id); };
            cb.on_move_down = [this](const std::string& id) { if (on_spawn_move_down_) on_spawn_move_down_(id); };
            spawn_list_->set_callbacks(std::move(cb));
            spawn_list_->append_rows(rows);
            have_groups = true;
        }
    }

    if (!have_groups) {
        empty_spawn_label_ = std::make_unique<RoomConfigLabel>("No spawn groups configured.");
        rows.push_back({ empty_spawn_label_.get() });
    }

    add_group_btn_ = std::make_unique<DMButton>("Add Group", &DMStyles::CreateButton(), 120, DMButton::height());
    if (add_group_btn_) {
        add_group_btn_w_ = std::make_unique<ButtonWidget>(add_group_btn_.get(), [this]() {
            if (on_spawn_add_) on_spawn_add_();
        });
        rows.push_back({ add_group_btn_w_.get() });
    }

    room_tags_label_ = std::make_unique<RoomConfigLabel>("Tags");
    rows.push_back({ room_tags_label_.get() });

    room_tags_editor_ = std::make_unique<TagEditorWidget>();
    room_tags_editor_->set_tags(room_tags_, room_anti_tags_);
    room_tags_editor_->set_on_changed([this](const std::vector<std::string>& tags,
                                            const std::vector<std::string>& anti_tags) {
        if (tags != room_tags_ || anti_tags != room_anti_tags_) {
            room_tags_ = tags;
            room_anti_tags_ = anti_tags;
            tags_dirty_ = true;
        }
    });
    rows.push_back({ room_tags_editor_.get() });

    set_rows(rows);
}

void RoomConfigurator::update(const Input& input, int screen_w, int screen_h) {
    if (is_visible()) {
        apply_bounds_if_needed();
        DockableCollapsible::update(input, screen_w, screen_h);
    }

    bool changed = false;
    if (tags_dirty_) {
        tags_dirty_ = false;
        changed = true;
    }

    if (room_name_lbl_) {
        std::string new_name = room_name_lbl_->value();
        if (new_name != room_name_) {
            const std::string old_name = room_name_;
            std::string final_name = new_name;
            if (on_room_renamed_) {
                try {
                    final_name = on_room_renamed_(old_name, new_name);
                } catch (...) {
                    final_name = new_name;
                }
            }
            if (final_name != new_name && room_name_lbl_) {
                room_name_lbl_->set_value(final_name);
            }
            room_name_ = std::move(final_name);
            changed = true;
        }
    }

    if (room_w_slider_) {
        int new_min = room_w_slider_->min_value();
        int new_max = room_w_slider_->max_value();
        if (new_min != room_w_min_ || new_max != room_w_max_) {
            room_w_min_ = std::min(new_min, new_max);
            room_w_max_ = std::max(new_min, new_max);
            changed = true;
        }
    }

    if (room_h_slider_) {
        int new_min = room_h_slider_->min_value();
        int new_max = room_h_slider_->max_value();
        if (new_min != room_h_min_ || new_max != room_h_max_) {
            room_h_min_ = std::min(new_min, new_max);
            room_h_max_ = std::max(new_min, new_max);
            changed = true;
        }
    }

    if (room_radius_slider_) {
        int new_radius = std::max(0, room_radius_slider_->value());
        if (new_radius != room_radius_) {
            room_radius_ = new_radius;
            changed = true;
        }
    }

    if (room_geom_dd_ && !room_geom_options_.empty()) {
        int selected = room_geom_dd_->selected();
        selected = std::clamp(selected, 0, static_cast<int>(room_geom_options_.size()) - 1);
        if (selected != room_geom_) {
            room_geom_ = selected;
            changed = true;
            rebuild_rows();
        }
    }

    if (edge_smoothness_sl_) {
        int v = edge_smoothness_sl_->value();
        v = std::clamp(v, 0, 101);
        if (v != edge_smoothness_) {
            edge_smoothness_ = v;
            changed = true;
        }
    }

    if (curvyness_sl_) {
        int v = curvyness_sl_->value();
        v = std::max(0, v);
        if (v != curvyness_) {
            curvyness_ = v;
            changed = true;
        }
    }

    bool spawn_val = room_spawn_cb_ && room_spawn_cb_->value();
    if (spawn_val != room_is_spawn_) {
        room_is_spawn_ = spawn_val;
        changed = true;
    }

    bool boss_val = room_boss_cb_ && room_boss_cb_->value();
    if (boss_val != room_is_boss_) {
        room_is_boss_ = boss_val;
        changed = true;
    }

    bool inherit_val = room_inherit_cb_ && room_inherit_cb_->value();
    if (inherit_val != room_inherits_assets_) {
        room_inherits_assets_ = inherit_val;
        changed = true;
    }

    if (room_is_spawn_ && room_is_boss_) {
        if (room_is_boss_) {
            room_is_boss_ = false;
            changed = true;
        }
        if (room_boss_cb_) room_boss_cb_->set_value(false);
    }

    if (changed) {
        if (!loaded_json_.is_object()) {
            loaded_json_ = nlohmann::json::object();
        }
        std::string geometry = selected_geometry();
        std::string lowered_geom;
        lowered_geom.reserve(geometry.size());
        for (char ch : geometry) {
            lowered_geom.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        bool is_circle_geom = (lowered_geom == "circle");
        loaded_json_["name"] = room_name_;
        if (is_circle_geom) {
            int diameter = std::max(0, room_radius_) * 2;
            room_w_min_ = room_w_max_ = diameter;
            room_h_min_ = room_h_max_ = diameter;
            loaded_json_["radius"] = room_radius_;
            loaded_json_["min_width"] = diameter;
            loaded_json_["max_width"] = diameter;
            loaded_json_["width_min"] = diameter;
            loaded_json_["width_max"] = diameter;
            loaded_json_["min_height"] = diameter;
            loaded_json_["max_height"] = diameter;
            loaded_json_["height_min"] = diameter;
            loaded_json_["height_max"] = diameter;
        } else {
            loaded_json_.erase("radius");
            loaded_json_["min_width"] = room_w_min_;
            loaded_json_["max_width"] = room_w_max_;
            loaded_json_["width_min"] = room_w_min_;
            loaded_json_["width_max"] = room_w_max_;
            loaded_json_["min_height"] = room_h_min_;
            loaded_json_["max_height"] = room_h_max_;
            loaded_json_["height_min"] = room_h_min_;
            loaded_json_["height_max"] = room_h_max_;
        }
        loaded_json_["geometry"] = selected_geometry();
        loaded_json_["is_spawn"] = room_is_spawn_;
        loaded_json_["is_boss"] = room_is_boss_;
        loaded_json_["inherits_map_assets"] = room_inherits_assets_;
        loaded_json_["edge_smoothness"] = edge_smoothness_;
        if (curvyness_sl_) {
            loaded_json_["curvyness"] = curvyness_;
        }
        write_tags_to_json(loaded_json_);

        if (room_) {
            auto& r = room_->assets_data();
            r["name"] = room_name_;
            if (is_circle_geom) {
                int diameter = std::max(0, room_radius_) * 2;
                r["radius"] = room_radius_;
                r["min_width"] = diameter;
                r["max_width"] = diameter;
                r["width_min"] = diameter;
                r["width_max"] = diameter;
                r["min_height"] = diameter;
                r["max_height"] = diameter;
                r["height_min"] = diameter;
                r["height_max"] = diameter;
            } else {
                if (r.contains("radius")) {
                    r.erase("radius");
                }
                r["min_width"] = room_w_min_;
                r["max_width"] = room_w_max_;
                r["width_min"] = room_w_min_;
                r["width_max"] = room_w_max_;
                r["min_height"] = room_h_min_;
                r["max_height"] = room_h_max_;
                r["height_min"] = room_h_min_;
                r["height_max"] = room_h_max_;
            }
            r["geometry"] = selected_geometry();
            r["is_spawn"] = room_is_spawn_;
            r["is_boss"] = room_is_boss_;
            r["inherits_map_assets"] = room_inherits_assets_;
            r["edge_smoothness"] = edge_smoothness_;
            if (curvyness_sl_) {
                r["curvyness"] = curvyness_;
            }
            write_tags_to_json(r);
            room_->save_assets_json();
        }
    }
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
    return used;
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (is_visible()) DockableCollapsible::render(r);
}

nlohmann::json RoomConfigurator::build_json() const {
    nlohmann::json result = loaded_json_.is_object() ? loaded_json_ : nlohmann::json::object();
    result["name"] = room_name_;
    std::string geometry = selected_geometry();
    std::string lowered_geom;
    lowered_geom.reserve(geometry.size());
    for (char ch : geometry) {
        lowered_geom.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    bool is_circle_geom = (lowered_geom == "circle");
    if (is_circle_geom) {
        int diameter = std::max(0, room_radius_) * 2;
        result["radius"] = room_radius_;
        result["min_width"] = diameter;
        result["max_width"] = diameter;
        result["width_min"] = diameter;
        result["width_max"] = diameter;
        result["min_height"] = diameter;
        result["max_height"] = diameter;
        result["height_min"] = diameter;
        result["height_max"] = diameter;
    } else {
        result.erase("radius");
        result["min_width"] = room_w_min_;
        result["max_width"] = room_w_max_;
        result["width_min"] = room_w_min_;
        result["width_max"] = room_w_max_;
        result["min_height"] = room_h_min_;
        result["max_height"] = room_h_max_;
        result["height_min"] = room_h_min_;
        result["height_max"] = room_h_max_;
    }
    result["geometry"] = selected_geometry();
    result["is_spawn"] = room_is_spawn_;
    result["is_boss"] = room_is_boss_;
    result["inherits_map_assets"] = room_inherits_assets_;
    result["edge_smoothness"] = edge_smoothness_;
    if (curvyness_sl_) {
        result["curvyness"] = curvyness_;
    }
    write_tags_to_json(result);
    return result;
}

bool RoomConfigurator::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    return DockableCollapsible::is_point_inside(x, y);
}

void RoomConfigurator::set_spawn_group_callbacks(std::function<void(const std::string&)> on_edit,
                                                 std::function<void(const std::string&)> on_duplicate,
                                                 std::function<void(const std::string&)> on_delete,
                                                 std::function<void(const std::string&)> on_move_up,
                                                 std::function<void(const std::string&)> on_move_down,
                                                 std::function<void()> on_add) {
    on_spawn_edit_ = std::move(on_edit);
    on_spawn_duplicate_ = std::move(on_duplicate);
    on_spawn_delete_ = std::move(on_delete);
    on_spawn_move_up_ = std::move(on_move_up);
    on_spawn_move_down_ = std::move(on_move_down);
    on_spawn_add_ = std::move(on_add);
}
