#include "room_configurator.hpp"

#include "dm_styles.hpp"
#include "room/room.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <optional>
#include <initializer_list>

namespace {
class RoomConfigLabel : public Widget {
public:
    explicit RoomConfigLabel(std::string text) : text_(std::move(text)) {}

    void set_text(std::string text) { text_ = std::move(text); }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int /*w*/) const override {
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
} // namespace

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
    set_cell_width(260);
}

RoomConfigurator::~RoomConfigurator() = default;

void RoomConfigurator::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    const bool docked = bounds_.w > 0 && bounds_.h > 0;
    if (using_docked_bounds_ != docked) {
        using_docked_bounds_ = docked;
        floatable_ = !docked;
        set_show_header(show_header());
    }
    applied_bounds_ = SDL_Rect{-1, -1, 0, 0};
    apply_bounds_if_needed();
}

void RoomConfigurator::apply_bounds_if_needed() {
    if (!using_docked_bounds_) {
        if (bounds_.w > 0 && bounds_.h > 0) {
            set_work_area(bounds_);
        } else {
            set_work_area(SDL_Rect{0, 0, 0, 0});
        }
        set_available_height_override(-1);
        applied_bounds_ = bounds_;
        return;
    }

    if (bounds_.w <= 0 || bounds_.h <= 0) {
        set_available_height_override(-1);
        set_work_area(SDL_Rect{0, 0, 0, 0});
        applied_bounds_ = SDL_Rect{-1, -1, 0, 0};
        return;
    }
    if (applied_bounds_.x == bounds_.x && applied_bounds_.y == bounds_.y &&
        applied_bounds_.w == bounds_.w && applied_bounds_.h == bounds_.h) {
        return;
    }

    const int pad = DMSpacing::panel_padding();
    const int available = std::max(0, bounds_.h - 2 * pad);
    const int cell_width = std::max(180, bounds_.w - 2 * pad);
    set_available_height_override(available > 0 ? available : -1);
    set_cell_width(cell_width);
    set_work_area(bounds_);
    DockableCollapsible::set_rect(bounds_);
    applied_bounds_ = bounds_;
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

    room_is_spawn_ = loaded_json_.value("is_spawn", false);
    room_is_boss_ = loaded_json_.value("is_boss", false);
    room_inherits_assets_ = loaded_json_.value("inherits_map_assets", false);
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

    bool dims_changed = (new_w_min != room_w_min_) || (new_w_max != room_w_max_) ||
                        (new_h_min != room_h_min_) || (new_h_max != room_h_max_);

    room_w_min_ = new_w_min;
    room_w_max_ = new_w_max;
    room_h_min_ = new_h_min;
    room_h_max_ = new_h_max;

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
    if (groups_changed) {
        rebuild_rows();
        return true;
    }
    if (dims_changed) {
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
    spawn_rows_.clear();
    spawn_groups_label_.reset();
    add_group_btn_.reset();
    add_group_btn_w_.reset();
    empty_spawn_label_.reset();
    room_section_label_.reset();

    room_section_label_ = std::make_unique<RoomConfigLabel>("Room Settings");
    rows.push_back({ room_section_label_.get() });

    room_name_lbl_ = std::make_unique<DMTextBox>("Room Name", room_name_);
    room_name_lbl_w_ = std::make_unique<TextBoxWidget>(room_name_lbl_.get());
    rows.push_back({ room_name_lbl_w_.get() });

    auto width_bounds = compute_slider_range(room_w_min_, room_w_max_);
    room_w_slider_ = std::make_unique<DMRangeSlider>(width_bounds.first, width_bounds.second, room_w_min_, room_w_max_);
    room_w_slider_w_ = std::make_unique<RangeSliderWidget>(room_w_slider_.get());
    rows.push_back({ room_w_slider_w_.get() });

    auto height_bounds = compute_slider_range(room_h_min_, room_h_max_);
    room_h_slider_ = std::make_unique<DMRangeSlider>(height_bounds.first, height_bounds.second, room_h_min_, room_h_max_);
    room_h_slider_w_ = std::make_unique<RangeSliderWidget>(room_h_slider_.get());
    rows.push_back({ room_h_slider_w_.get() });

    int geom_index = room_geom_options_.empty() ? 0 : std::clamp(room_geom_, 0, static_cast<int>(room_geom_options_.size()) - 1);
    room_geom_dd_ = std::make_unique<DMDropdown>("Geometry", room_geom_options_, geom_index);
    room_geom_dd_w_ = std::make_unique<DropdownWidget>(room_geom_dd_.get());
    rows.push_back({ room_geom_dd_w_.get() });

    room_spawn_cb_ = std::make_unique<DMCheckbox>("Spawn", room_is_spawn_);
    room_spawn_cb_w_ = std::make_unique<CheckboxWidget>(room_spawn_cb_.get());
    room_boss_cb_ = std::make_unique<DMCheckbox>("Boss", room_is_boss_);
    room_boss_cb_w_ = std::make_unique<CheckboxWidget>(room_boss_cb_.get());
    room_inherit_cb_ = std::make_unique<DMCheckbox>("Inherit Map Assets", room_inherits_assets_);
    room_inherit_cb_w_ = std::make_unique<CheckboxWidget>(room_inherit_cb_.get());
    rows.push_back({ room_spawn_cb_w_.get(), room_boss_cb_w_.get(), room_inherit_cb_w_.get() });

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
            int index = 1;
            for (const auto& entry : *groups) {
                if (!entry.is_object()) continue;
                std::string spawn_id = entry.value("spawn_id", std::string{});
                if (spawn_id.empty()) continue;
                auto row = std::make_unique<SpawnGroupRow>();
                row->spawn_id = spawn_id;
                row->summary = std::make_unique<RoomConfigLabel>(build_spawn_summary(index, entry));
                row->edit_btn = std::make_unique<DMButton>("Edit", &DMStyles::HeaderButton(), 72, DMButton::height());
                if (row->edit_btn) {
                    std::string id_copy = spawn_id;
                    row->edit_btn_w = std::make_unique<ButtonWidget>(row->edit_btn.get(), [this, id_copy]() {
                        if (on_spawn_edit_) on_spawn_edit_(id_copy);
                    });
                }
                row->duplicate_btn = std::make_unique<DMButton>("Duplicate", &DMStyles::HeaderButton(), 96, DMButton::height());
                if (row->duplicate_btn) {
                    std::string id_copy = spawn_id;
                    row->duplicate_btn_w = std::make_unique<ButtonWidget>(row->duplicate_btn.get(), [this, id_copy]() {
                        if (on_spawn_duplicate_) on_spawn_duplicate_(id_copy);
                    });
                }
                row->delete_btn = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 80, DMButton::height());
                if (row->delete_btn) {
                    std::string id_copy = spawn_id;
                    row->delete_btn_w = std::make_unique<ButtonWidget>(row->delete_btn.get(), [this, id_copy]() {
                        if (on_spawn_delete_) on_spawn_delete_(id_copy);
                    });
                }
                DockableCollapsible::Row spawn_row;
                if (row->summary) spawn_row.push_back(row->summary.get());
                if (row->edit_btn_w) spawn_row.push_back(row->edit_btn_w.get());
                if (row->duplicate_btn_w) spawn_row.push_back(row->duplicate_btn_w.get());
                if (row->delete_btn_w) spawn_row.push_back(row->delete_btn_w.get());
                if (!spawn_row.empty()) {
                    rows.push_back(spawn_row);
                    spawn_rows_.push_back(std::move(row));
                    have_groups = true;
                    ++index;
                }
            }
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

    set_rows(rows);
}

void RoomConfigurator::update(const Input& input, int screen_w, int screen_h) {
    if (is_visible()) {
        apply_bounds_if_needed();
        DockableCollapsible::update(input, screen_w, screen_h);
    }

    bool changed = false;

    if (room_name_lbl_) {
        std::string new_name = room_name_lbl_->value();
        if (new_name != room_name_) {
            room_name_ = std::move(new_name);
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

    if (room_geom_dd_ && !room_geom_options_.empty()) {
        int selected = room_geom_dd_->selected();
        selected = std::clamp(selected, 0, static_cast<int>(room_geom_options_.size()) - 1);
        if (selected != room_geom_) {
            room_geom_ = selected;
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
        loaded_json_["name"] = room_name_;
        loaded_json_["min_width"] = room_w_min_;
        loaded_json_["max_width"] = room_w_max_;
        loaded_json_["width_min"] = room_w_min_;
        loaded_json_["width_max"] = room_w_max_;
        loaded_json_["min_height"] = room_h_min_;
        loaded_json_["max_height"] = room_h_max_;
        loaded_json_["height_min"] = room_h_min_;
        loaded_json_["height_max"] = room_h_max_;
        loaded_json_["geometry"] = selected_geometry();
        loaded_json_["is_spawn"] = room_is_spawn_;
        loaded_json_["is_boss"] = room_is_boss_;
        loaded_json_["inherits_map_assets"] = room_inherits_assets_;

        if (room_) {
            auto& r = room_->assets_data();
            r["name"] = room_name_;
            r["min_width"] = room_w_min_;
            r["max_width"] = room_w_max_;
            r["width_min"] = room_w_min_;
            r["width_max"] = room_w_max_;
            r["min_height"] = room_h_min_;
            r["max_height"] = room_h_max_;
            r["height_min"] = room_h_min_;
            r["height_max"] = room_h_max_;
            r["geometry"] = selected_geometry();
            r["is_spawn"] = room_is_spawn_;
            r["is_boss"] = room_is_boss_;
            r["inherits_map_assets"] = room_inherits_assets_;
            room_->save_assets_json();
        }
    }
}

bool RoomConfigurator::handle_event(const SDL_Event& e) {
    bool used = false;
    if (is_visible()) {
        apply_bounds_if_needed();
        used |= DockableCollapsible::handle_event(e);
    }
    return used;
}

void RoomConfigurator::render(SDL_Renderer* r) const {
    if (is_visible()) DockableCollapsible::render(r);
}

nlohmann::json RoomConfigurator::build_json() const {
    nlohmann::json result = loaded_json_.is_object() ? loaded_json_ : nlohmann::json::object();
    result["name"] = room_name_;
    result["min_width"] = room_w_min_;
    result["max_width"] = room_w_max_;
    result["width_min"] = room_w_min_;
    result["width_max"] = room_w_max_;
    result["min_height"] = room_h_min_;
    result["max_height"] = room_h_max_;
    result["height_min"] = room_h_min_;
    result["height_max"] = room_h_max_;
    result["geometry"] = selected_geometry();
    result["is_spawn"] = room_is_spawn_;
    result["is_boss"] = room_is_boss_;
    result["inherits_map_assets"] = room_inherits_assets_;
    return result;
}

bool RoomConfigurator::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    return DockableCollapsible::is_point_inside(x, y);
}

void RoomConfigurator::set_spawn_group_callbacks(std::function<void(const std::string&)> on_edit,
                                                 std::function<void(const std::string&)> on_duplicate,
                                                 std::function<void(const std::string&)> on_delete,
                                                 std::function<void()> on_add) {
    on_spawn_edit_ = std::move(on_edit);
    on_spawn_duplicate_ = std::move(on_duplicate);
    on_spawn_delete_ = std::move(on_delete);
    on_spawn_add_ = std::move(on_add);
}
