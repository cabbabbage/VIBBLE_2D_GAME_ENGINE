#include "trail_editor_panel.hpp"

#include "assets_config.hpp"
#include "dm_styles.hpp"
#include "room/room.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <SDL.h>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace trail_editor_panel_detail {
constexpr int kAnchorOffset = 16;
constexpr int kWidthMin = 0;
constexpr int kWidthMax = 4096;
constexpr int kCurvynessMin = 0;
constexpr int kCurvynessMax = 32;

class SimpleLabel : public Widget {
public:
    explicit SimpleLabel(std::string text) : text_(std::move(text)) {}

    void set_text(const std::string& text) { text_ = text; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int /*w*/) const override {
        const DMLabelStyle& st = DMStyles::Label();
        return st.font_size + DMSpacing::item_gap();
    }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        const DMLabelStyle& st = DMStyles::Label();
        TTF_Font* font = st.open_font();
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), st.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_Rect dst{rect_.x, rect_.y, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(font);
    }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
};
}

using trail_editor_panel_detail::kCurvynessMax;
using trail_editor_panel_detail::kCurvynessMin;
using trail_editor_panel_detail::kWidthMax;
using trail_editor_panel_detail::kWidthMin;
using trail_editor_panel_detail::SimpleLabel;

using nlohmann::json;

TrailEditorPanel::TrailEditorPanel(int x, int y)
    : DockableCollapsible("Trail", true, x, y) {
    set_expanded(true);
    set_visible(false);
    set_cell_width(220);
    assets_cfg_ = std::make_unique<AssetsConfig>();
    spawn_label_ = std::make_unique<SimpleLabel>("Spawn Groups");
    save_button_ = std::make_unique<DMButton>("Save", &DMStyles::CreateButton(), 100, DMButton::height());
    save_button_widget_ = std::make_unique<ButtonWidget>(save_button_.get(), [this]() { perform_save(); });
    close_button_ = std::make_unique<DMButton>("Close", &DMStyles::HeaderButton(), 100, DMButton::height());
    close_button_widget_ = std::make_unique<ButtonWidget>(close_button_.get(), [this]() { this->close(); });
}

TrailEditorPanel::~TrailEditorPanel() = default;

void TrailEditorPanel::set_on_save(SaveCallback cb) {
    on_save_ = std::move(cb);
}

void TrailEditorPanel::open(const std::string& trail_id, json* trail_json, Room* room) {
    trail_id_ = trail_id;
    trail_entry_ = trail_json;
    trail_room_ = room;
    trail_room_json_ = nullptr;

    if (trail_room_) {
        json& runtime = trail_room_->assets_data();
        if (trail_entry_) {
            runtime = *trail_entry_;
            // normalize legacy fields
            trail_room_->assets_data();
        }
        trail_room_json_ = &trail_room_->assets_data();
    } else if (trail_entry_) {
        // without a room instance, operate directly on the map data
        trail_room_json_ = trail_entry_;
    }

    refresh_cached_values();
    rebuild_rows();

    set_title(trail_id_.empty() ? std::string("Trail") : std::string("Trail: ") + trail_id_);
    set_visible(true);
    set_expanded(true);
    mark_clean();
}

void TrailEditorPanel::close() {
    set_visible(false);
    if (assets_cfg_) assets_cfg_->close_all_asset_configs();
}

void TrailEditorPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_visible()) return;
    DockableCollapsible::update(input, screen_w, screen_h);
    SDL_Rect r = rect();
    int anchor_x = r.x + r.w + trail_editor_panel_detail::kAnchorOffset;
    int anchor_y = r.y;
    if (assets_cfg_) assets_cfg_->set_anchor(anchor_x, anchor_y);
    if (assets_cfg_) assets_cfg_->update(input);

    if (width_slider_) {
        int new_min = width_slider_->min_value();
        int new_max = width_slider_->max_value();
        if (new_min != min_width_ || new_max != max_width_) {
            min_width_ = new_min;
            max_width_ = new_max;
            if (trail_room_json_) {
                (*trail_room_json_)["min_width"] = min_width_;
                (*trail_room_json_)["max_width"] = max_width_;
            }
            if (trail_entry_) {
                (*trail_entry_)["min_width"] = min_width_;
                (*trail_entry_)["max_width"] = max_width_;
            }
            mark_dirty();
        }
    }
    if (curvyness_slider_) {
        int new_curvy = curvyness_slider_->value();
        if (new_curvy != curvyness_) {
            curvyness_ = new_curvy;
            if (trail_room_json_) {
                (*trail_room_json_)["curvyness"] = curvyness_;
            }
            if (trail_entry_) {
                (*trail_entry_)["curvyness"] = curvyness_;
            }
            mark_dirty();
        }
    }
}

bool TrailEditorPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) return false;
    bool used = DockableCollapsible::handle_event(e);
    if (name_widget_ && name_widget_->handle_event(e)) {
        name_ = name_box_ ? name_box_->value() : name_;
        if (trail_room_json_) (*trail_room_json_)["name"] = name_;
        if (trail_entry_) (*trail_entry_)["name"] = name_;
        mark_dirty();
        used = true;
    }
    if (inherits_widget_ && inherits_widget_->handle_event(e)) {
        inherits_map_assets_ = inherits_checkbox_ ? inherits_checkbox_->value() : inherits_map_assets_;
        if (trail_room_json_) (*trail_room_json_)["inherits_map_assets"] = inherits_map_assets_;
        if (trail_entry_) (*trail_entry_)["inherits_map_assets"] = inherits_map_assets_;
        mark_dirty();
        used = true;
    }
    if (assets_cfg_ && assets_cfg_->handle_event(e)) {
        mark_dirty();
        used = true;
    }
    return used;
}

void TrailEditorPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
    if (assets_cfg_) assets_cfg_->render(renderer);
}

bool TrailEditorPanel::is_point_inside(int x, int y) const {
    if (!is_visible()) return false;
    if (DockableCollapsible::is_point_inside(x, y)) return true;
    if (assets_cfg_ && assets_cfg_->is_point_inside(x, y)) return true;
    return false;
}

void TrailEditorPanel::rebuild_rows() {
    DockableCollapsible::Rows rows;
    if (trail_room_json_ || trail_entry_) {
        name_box_ = std::make_unique<DMTextBox>("Trail Name", name_);
        name_widget_ = std::make_unique<TextBoxWidget>(name_box_.get());
        width_slider_ = std::make_unique<DMRangeSlider>(kWidthMin, kWidthMax, min_width_, max_width_);
        width_widget_ = std::make_unique<RangeSliderWidget>(width_slider_.get());
        curvyness_slider_ = std::make_unique<DMSlider>("Curvyness", kCurvynessMin, kCurvynessMax, curvyness_);
        curvyness_widget_ = std::make_unique<SliderWidget>(curvyness_slider_.get());
        inherits_checkbox_ = std::make_unique<DMCheckbox>("Inherit Map Assets", inherits_map_assets_);
        inherits_widget_ = std::make_unique<CheckboxWidget>(inherits_checkbox_.get());

        rows.push_back({ name_widget_.get() });
        rows.push_back({ width_widget_.get() });
        rows.push_back({ curvyness_widget_.get(), inherits_widget_.get() });

        ensure_spawn_groups();
        if (spawn_label_) rows.push_back({ spawn_label_.get() });
        if (assets_cfg_) assets_cfg_->append_rows(rows);

        DockableCollapsible::Row actions;
        if (save_button_widget_) actions.push_back(save_button_widget_.get());
        if (close_button_widget_) actions.push_back(close_button_widget_.get());
        if (!actions.empty()) rows.push_back(actions);
    }
    set_rows(rows);
}

void TrailEditorPanel::sync_room_to_entry() {
    if (trail_entry_ && trail_room_json_) {
        *trail_entry_ = *trail_room_json_;
    }
}

void TrailEditorPanel::ensure_spawn_groups() {
    json* source = trail_room_json_ ? trail_room_json_ : trail_entry_;
    if (!source) return;

    if (!source->contains("spawn_groups") || !(*source)["spawn_groups"].is_array()) {
        (*source)["spawn_groups"] = json::array();
    }
    if (trail_room_json_ && trail_entry_) {
        (*trail_entry_)["spawn_groups"] = (*trail_room_json_)["spawn_groups"];
    }
    if (assets_cfg_) {
        auto on_change = [this]() {
            mark_dirty();
            sync_room_to_entry();
        };
        assets_cfg_->load((*source)["spawn_groups"], on_change);
    }
}

void TrailEditorPanel::mark_dirty() {
    if (!dirty_) {
        dirty_ = true;
        if (save_button_) save_button_->set_text("Save*");
    }
    sync_room_to_entry();
}

void TrailEditorPanel::mark_clean() {
    dirty_ = false;
    if (save_button_) save_button_->set_text("Save");
}

bool TrailEditorPanel::perform_save() {
    sync_room_to_entry();
    bool ok = false;
    if (on_save_) {
        ok = on_save_();
    } else if (trail_room_) {
        trail_room_->save_assets_json();
        ok = true;
    }
    if (ok) {
        mark_clean();
    }
    return ok;
}

void TrailEditorPanel::refresh_cached_values() {
    const json* source = nullptr;
    if (trail_room_json_) {
        source = trail_room_json_;
    } else if (trail_entry_) {
        source = trail_entry_;
    }
    if (!source) {
        name_.clear();
        min_width_ = max_width_ = curvyness_ = 0;
        inherits_map_assets_ = false;
        return;
    }

    name_ = source->value("name", trail_id_);
    min_width_ = source->value("min_width", source->value("width_min", 0));
    max_width_ = source->value("max_width", source->value("width_max", min_width_));
    curvyness_ = source->value("curvyness", 0);
    inherits_map_assets_ = source->value("inherits_map_assets", false);

    if (trail_entry_) {
        (*trail_entry_)["name"] = name_;
        (*trail_entry_)["min_width"] = min_width_;
        (*trail_entry_)["max_width"] = max_width_;
        (*trail_entry_)["curvyness"] = curvyness_;
        (*trail_entry_)["inherits_map_assets"] = inherits_map_assets_;
    }
    if (trail_room_json_) {
        (*trail_room_json_)["name"] = name_;
        (*trail_room_json_)["min_width"] = min_width_;
        (*trail_room_json_)["max_width"] = max_width_;
        (*trail_room_json_)["curvyness"] = curvyness_;
        (*trail_room_json_)["inherits_map_assets"] = inherits_map_assets_;
    }
}

