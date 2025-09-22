#include "room_configurator.hpp"
#include "FloatingDockableManager.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "room/room.hpp"
#include "widgets.hpp"

#include <SDL_ttf.h>

#include <algorithm>
#include <sstream>

namespace {
class SpawnSummaryWidget : public Widget {
public:
    explicit SpawnSummaryWidget(std::string text) : text_(std::move(text)) {}

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
        TTF_Font* font = st.open_font();
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), st.color);
        if (!surf) {
            TTF_CloseFont(font);
            return;
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_Rect dst = rect_;
        dst.h = surf->h;
        dst.w = surf->w;
        if (tex) {
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
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
} // namespace

RoomConfigurator::RoomConfigurator()
    : DockableCollapsible("Room", true, 32, 32) {
    room_geom_options_ = {"Square", "Circle"};
    set_expanded(true);
    set_visible(false);
    set_show_header(true);
    set_scroll_enabled(true);
    set_cell_width(180);
}

RoomConfigurator::~RoomConfigurator() = default;

void RoomConfigurator::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    applied_bounds_ = SDL_Rect{-1, -1, 0, 0};
    apply_bounds_if_needed();
}

void RoomConfigurator::apply_bounds_if_needed() {
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
    set_available_height_override(available > 0 ? available : -1);
    set_work_area(bounds_);
    DockableCollapsible::set_rect(bounds_);
    applied_bounds_ = bounds_;
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
        room_inherits_assets_ = data.value("inherits_map_assets", false);
    }
    rebuild_rows();
    FloatingDockableManager::instance().open_floating("Room Config", this, [this]() { this->close(); });
    set_visible(true);
    set_expanded(true);
}

void RoomConfigurator::open(Room* room) {
    room_ = room;
    nlohmann::json j;
    if (room) {
        j = room->assets_data();
    }
    open(j);
}

void RoomConfigurator::close() { set_visible(false); }

bool RoomConfigurator::visible() const { return is_visible(); }

bool RoomConfigurator::any_panel_visible() const {
    return visible();
}

void RoomConfigurator::rebuild_rows() {
    DockableCollapsible::Rows rows;
    spawn_rows_.clear();
    spawn_groups_label_.reset();
    add_group_btn_.reset();
    add_group_btn_w_.reset();
    empty_spawn_label_.reset();

    room_name_lbl_ = std::make_unique<DMTextBox>("Room", room_name_);
    room_name_lbl_w_ = std::make_unique<TextBoxWidget>(room_name_lbl_.get());
    room_w_slider_ = std::make_unique<DMRangeSlider>(1000, 10000, room_w_min_, room_w_max_);
    room_w_slider_w_ = std::make_unique<RangeSliderWidget>(room_w_slider_.get());
    room_h_slider_ = std::make_unique<DMRangeSlider>(1000, 10000, room_h_min_, room_h_max_);
    room_h_slider_w_ = std::make_unique<RangeSliderWidget>(room_h_slider_.get());
    room_geom_dd_ = std::make_unique<DMDropdown>("Geometry", room_geom_options_, room_geom_);
    room_geom_dd_w_ = std::make_unique<DropdownWidget>(room_geom_dd_.get());
    room_spawn_cb_ = std::make_unique<DMCheckbox>("Spawn", room_is_spawn_);
    room_spawn_cb_w_ = std::make_unique<CheckboxWidget>(room_spawn_cb_.get());
    room_boss_cb_ = std::make_unique<DMCheckbox>("Boss", room_is_boss_);
    room_boss_cb_w_ = std::make_unique<CheckboxWidget>(room_boss_cb_.get());
    room_inherit_cb_ = std::make_unique<DMCheckbox>("Inherit Map Assets", room_inherits_assets_);
    room_inherit_cb_w_ = std::make_unique<CheckboxWidget>(room_inherit_cb_.get());
    rows.push_back({ room_name_lbl_w_.get() });
    rows.push_back({ room_w_slider_w_.get() });
    rows.push_back({ room_h_slider_w_.get() });
    rows.push_back({ room_geom_dd_w_.get() });
    rows.push_back({ room_spawn_cb_w_.get(), room_boss_cb_w_.get(), room_inherit_cb_w_.get() });

    spawn_groups_label_ = std::make_unique<SpawnSummaryWidget>("Spawn Groups");
    if (spawn_groups_label_) {
        rows.push_back({ spawn_groups_label_.get() });
    }

    bool have_groups = false;
    if (room_) {
        const auto& data = room_->assets_data();
        if (data.contains("spawn_groups") && data["spawn_groups"].is_array()) {
            const auto& groups = data["spawn_groups"];
            int index = 1;
            for (const auto& entry : groups) {
                if (!entry.is_object()) continue;
                std::string spawn_id = entry.value("spawn_id", std::string{});
                if (spawn_id.empty()) continue;
                auto row = std::make_unique<SpawnGroupRow>();
                row->spawn_id = spawn_id;
                row->summary = std::make_unique<SpawnSummaryWidget>(build_spawn_summary(index, entry));
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
        empty_spawn_label_ = std::make_unique<SpawnSummaryWidget>("No spawn groups configured.");
        if (empty_spawn_label_) {
            rows.push_back({ empty_spawn_label_.get() });
        }
    }

    add_group_btn_ = std::make_unique<DMButton>("Add Group", &DMStyles::CreateButton(), 120, DMButton::height());
    if (add_group_btn_) {
        add_group_btn_w_ = std::make_unique<ButtonWidget>(add_group_btn_.get(), [this]() {
            if (on_spawn_add_) on_spawn_add_();
        });
        rows.push_back({ add_group_btn_w_.get() });
    }

    set_cell_width(180);
    set_rows(rows);
}

void RoomConfigurator::update(const Input& input, int screen_w, int screen_h) {
    if (is_visible()) {
        apply_bounds_if_needed();
        DockableCollapsible::update(input, screen_w, screen_h);
    }
    if (room_w_slider_) { room_w_min_ = room_w_slider_->min_value(); room_w_max_ = room_w_slider_->max_value(); }
    if (room_h_slider_) { room_h_min_ = room_h_slider_->min_value(); room_h_max_ = room_h_slider_->max_value(); }
    if (room_geom_dd_) room_geom_ = room_geom_dd_->selected();
    room_is_spawn_ = room_spawn_cb_ && room_spawn_cb_->value();
    room_is_boss_ = room_boss_cb_ && room_boss_cb_->value();
    room_inherits_assets_ = room_inherit_cb_ && room_inherit_cb_->value();
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
        r["inherits_map_assets"] = room_inherits_assets_;
        room_->save_assets_json();
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
    nlohmann::json r;
    r["min_width"] = room_w_min_;
    r["max_width"] = room_w_max_;
    r["min_height"] = room_h_min_;
    r["max_height"] = room_h_max_;
    r["geometry"] = room_geom_options_[room_geom_];
    r["is_spawn"] = room_is_spawn_;
    r["is_boss"] = room_is_boss_;
    r["inherits_map_assets"] = room_inherits_assets_;
    r["name"] = room_name_;
    return r;
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
