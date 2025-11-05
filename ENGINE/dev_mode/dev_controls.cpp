#include "dev_controls.hpp"

#include <SDL.h>
#include <fstream>
#include <sstream>
#include <array>
#include <cmath>
#include <cctype>
#include <numeric>

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/frame_editor_session.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "dev_mode/dev_footer_bar.hpp"
#include "dev_mode/camera_ui.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"
#include "dev_mode/area_overlay_editor.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "asset/asset_info.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "room_overlay_renderer.hpp"
#include "widgets.hpp"
#include "dev_controls_persistence.hpp"
#include "render/global_light_source.hpp"
#include "map_generation/map_layers_geometry.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "asset/asset_utils.hpp"
#include "core/AssetsManager.hpp"
#include "render/camera.hpp"
#include "map_generation/room.hpp"
#include "spawn/asset_spawn_planner.hpp"
#include "spawn/asset_spawner.hpp"
#include "spawn/check.hpp"
#include "spawn/methods/center_spawner.hpp"
#include "spawn/methods/exact_spawner.hpp"
#include "spawn/methods/perimeter_spawner.hpp"
#include "spawn/methods/edge_spawner.hpp"
#include "spawn/methods/percent_spawner.hpp"
#include "spawn/methods/random_spawner.hpp"
#include "utils/map_grid_settings.hpp"
#include "spawn/spawn_context.hpp"
#include "utils/area.hpp"
#include "util/grid.hpp"
#include "util/grid_occupancy.hpp"
#include "utils/input.hpp"
#include "utils/string_utils.hpp"
#include "utils/display_color.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <tuple>
#include <cctype>
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <random>
#include <nlohmann/json.hpp>
#include <SDL_ttf.h>

using devmode::sdl::event_point;
using devmode::sdl::is_pointer_event;

namespace {

using vibble::strings::to_lower_copy;

void dev_mode_trace(const std::string& message) {
    try {
        std::ofstream log("dev_mode_trace.log", std::ios::app);
        log << message << '\n';
    } catch (...) {

    }
}

constexpr const char* kModeIdRoom = "room";
constexpr const char* kModeIdMap = "map";
constexpr int kPopupOutlineThickness = 1;

// Grid header settings keys
constexpr const char* kGridOverlayEnabledKey = "dev.grid.overlay.enabled";
constexpr const char* kGridSnapEnabledKey    = "dev.grid.snap.enabled";
constexpr const char* kGridCellSizePxKey     = "dev.grid.cell_size_px";

void draw_simple_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

bool is_trail_room(const Room* room) {
    if (!room || room->type.empty()) {
        return false;
    }
    return to_lower_copy(room->type) == "trail";
}

template <class Modal>
bool consume_modal_event(Modal* modal,
                         const SDL_Event& event,
                         const SDL_Point& pointer,
                         bool pointer_relevant,
                         Input* input) {
    if (!modal || !modal->visible()) {
        return false;
    }
    if (modal->handle_event(event)) {
        if (input) {
            input->consumeEvent(event);
        }
        return true;
    }
    if (pointer_relevant && modal->is_point_inside(pointer.x, pointer.y)) {
        if (input) {
            input->consumeEvent(event);
        }
        return true;
    }
    return false;
}

std::string normalize_area_name_base(const std::string& raw) {
    if (raw.empty()) {
        return std::string{"area"};
    }

    std::string result;
    result.reserve(raw.size());
    bool last_was_separator = false;
    for (char ch : raw) {
        unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0) {
            result.push_back(static_cast<char>(std::tolower(uch)));
            last_was_separator = false;
        } else if (ch == '_' || ch == '-' || std::isspace(uch)) {
            if (!last_was_separator && !result.empty()) {
                result.push_back('_');
                last_was_separator = true;
            }
        }
    }

    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    if (result.empty()) {
        return std::string{"area"};
    }

    return result;
}

std::string canonicalize_asset_area_type(std::string raw) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    raw.erase(raw.begin(), std::find_if(raw.begin(), raw.end(), [&](unsigned char ch) { return !is_space(ch); }));
    raw.erase(std::find_if(raw.rbegin(), raw.rend(), [&](unsigned char ch) { return !is_space(ch); }).base(), raw.end());
    std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return raw;
}

bool is_known_asset_area_type(const std::string& type) {
    static const std::array<const char*, 4> kKnownTypes = {
        "impassable",
        "trigger",
        "child",
        "spawning"
};
    for (const char* known : kKnownTypes) {
        if (type == known) {
            return true;
        }
    }
    return false;
}

std::string make_unique_asset_area_name(const AssetInfo& info, const std::string& preferred) {
    std::unordered_set<std::string> used_names;
    for (const auto& entry : info.areas) {
        if (!entry.name.empty()) {
            used_names.insert(entry.name);
        }
    }

    std::string base = normalize_area_name_base(preferred);
    if (base.size() < 5 || base.substr(base.size() - 5) != "_area") {
        base += "_area";
    }

    std::string candidate = base;
    int suffix = 1;
    while (used_names.count(candidate) > 0) {
        candidate = base + "_" + std::to_string(suffix++);
    }

    return candidate;
}

}

void DevControls::RoomAreaCache::set_listener(Listener listener) {
    listener_ = std::move(listener);
}

void DevControls::RoomAreaCache::invalidate() {
    dirty_ = true;
}

const DevControls::RoomAreaCache::PolygonList&
DevControls::RoomAreaCache::ensure_from_json(const nlohmann::json* root,
                                             std::optional<SDL_Point> default_anchor,
                                             std::optional<std::pair<int, int>> room_dimensions) {
    if (root != last_source_) {
        dirty_ = true;
    }
    if (!dirty_) {
        return cached_;
    }
    cached_.clear();
    last_source_ = root;
    if (root) {
        try {
            if (root->contains("areas") && (*root)["areas"].is_array()) {
                const int target_width = room_dimensions ? std::max(0, room_dimensions->first) : 0;
                const int target_height = room_dimensions ? std::max(0, room_dimensions->second) : 0;
                auto scale_component = [](int value, double factor) {
                    return static_cast<int>(std::llround(static_cast<double>(value) * factor));
                };
                for (const auto& item : (*root)["areas"]) {
                    if (!item.is_object()) continue;
                    const std::string name = item.contains("name") && item["name"].is_string()
                                                ? item["name"].get<std::string>()
                                                : std::string{};
                    if (name.empty()) continue;
                    const std::string type; // legacy area type ignored
                    RoomAreaSerialization::Kind kind =
                            RoomAreaSerialization::infer_kind_from_entry(item, type, name);
                    if (!RoomAreaSerialization::is_supported_kind(kind)) {
                        continue;
                    }
                    // Optional visibility
                    if (item.contains("visible") && item["visible"].is_boolean() && !item["visible"].get<bool>()) {
                        continue;
                    }
                    const auto& pts = item.contains("points") ? item["points"] : nlohmann::json();
                    if (!pts.is_array() || pts.size() < 3) continue;

                    if (default_anchor.has_value()) {
                        SDL_Point fallback = *default_anchor;
                        auto anchor = RoomAreaSerialization::resolve_anchor(item, fallback, kind);
                        const bool scale_to_room = item.value("scale_to_room", false);
                        const int stored_width = item.value("origional_width", 0);
                        const int stored_height = item.value("origional_height", 0);
                        const bool can_scale = scale_to_room && stored_width > 0 && stored_height > 0 &&
                                               target_width > 0 && target_height > 0;
                        std::vector<SDL_Point> poly;
                        if (can_scale) {
                            const double sx = static_cast<double>(target_width) / static_cast<double>(stored_width);
                            const double sy = static_cast<double>(target_height) / static_cast<double>(stored_height);
                            if (anchor.relative_to_center) {
                                anchor.relative_offset.x = scale_component(anchor.relative_offset.x, sx);
                                anchor.relative_offset.y = scale_component(anchor.relative_offset.y, sy);
                                anchor.world.x = fallback.x + anchor.relative_offset.x;
                                anchor.world.y = fallback.y + anchor.relative_offset.y;
                            }
                            auto relative_points = RoomAreaSerialization::decode_relative_points(item);
                            poly.reserve(relative_points.size());
                            for (const auto& rel : relative_points) {
                                const int dx = scale_component(rel.x, sx);
                                const int dy = scale_component(rel.y, sy);
                                poly.push_back(SDL_Point{anchor.world.x + dx, anchor.world.y + dy});
                            }
                        } else {
                            poly = RoomAreaSerialization::decode_points(item, anchor.world);
                        }
                        if (poly.size() >= 3) {
                            Polygon entry;
                            entry.name = name;
                            entry.points = std::move(poly);
                            entry.anchor = anchor.world;
                            entry.z = item.value("z", 0);
                            entry.visible = true;
                            cached_.push_back(std::move(entry));
                        }
                    } else {
                        int ax = 0;
                        int ay = 0;
                        if (item.contains("anchor") && item["anchor"].is_object()) {
                            ax = item["anchor"].value("x", 0);
                            ay = item["anchor"].value("y", 0);
                        }
                        std::vector<SDL_Point> poly;
                        poly.reserve(pts.size());
                        for (const auto& p : pts) {
                            if (!p.is_object()) continue;
                            int x = p.value("x", 0);
                            int y = p.value("y", 0);
                            poly.push_back(SDL_Point{ax + x, ay + y});
                        }
                        if (poly.size() >= 3) {
                            Polygon entry;
                            entry.name = name;
                            entry.points = std::move(poly);
                            entry.anchor = SDL_Point{ ax, ay };
                            entry.z = item.value("z", 0);
                            entry.visible = true;
                            cached_.push_back(std::move(entry));
                        }
                    }
                }
            }
        } catch (...) {
            cached_.clear();
        }
    }
    dirty_ = false;
    ++generation_;
    if (listener_) {
        listener_(cached_, generation_);
    }
    return cached_;
}

class RegenerateRoomPopup {
public:
    using Callback = std::function<void(Room*)>;

    void open(std::vector<std::pair<std::string, Room*>> rooms,
              Callback cb,
              int screen_w,
              int screen_h) {
        rooms_ = std::move(rooms);
        callback_ = std::move(cb);
        buttons_.clear();
        if (rooms_.empty()) {
            visible_ = false;
            return;
        }
        const int margin = DMSpacing::item_gap();
        const int spacing = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        const int button_width = std::max(220, screen_w / 6);
        rect_.w = button_width + margin * 2;
        const int total_buttons = static_cast<int>(rooms_.size());
        const int content_height = total_buttons * button_height + std::max(0, total_buttons - 1) * spacing;
        rect_.h = margin * 2 + content_height;
        const int padding = DMSpacing::panel_padding();
        const int max_height = std::max(240, screen_h - padding * 2);
        rect_.h = std::min(rect_.h, max_height);

        const int centered_x = screen_w / 2 - rect_.w / 2;
        const int centered_y = screen_h / 2 - rect_.h / 2;
        const int min_x = padding;
        const int max_x = screen_w - rect_.w - padding;
        const int min_y = padding;
        const int max_y = screen_h - rect_.h - padding;

        if (max_x < min_x) {
            rect_.x = min_x;
        } else {
            rect_.x = std::clamp(centered_x, min_x, max_x);
        }

        if (max_y < min_y) {
            rect_.y = min_y;
        } else {
            rect_.y = std::clamp(centered_y, min_y, max_y);
        }

        buttons_.reserve(rooms_.size());
        for (const auto& entry : rooms_) {
            auto btn = std::make_unique<DMButton>(entry.first, &DMStyles::ListButton(), button_width, button_height);
            buttons_.push_back(std::move(btn));
        }
        visible_ = true;
    }

    void close() {
        visible_ = false;
        callback_ = nullptr;
    }

    bool visible() const { return visible_; }

    void update(const Input&) {}

    bool handle_event(const SDL_Event& e) {
        if (!visible_) return false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            close();
            return true;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                         e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y };
            if (!SDL_PointInRect(&p, &rect_)) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    close();
                }
                return false;
            }
        }

        bool used = false;
        const int margin = DMSpacing::item_gap();
        const int spacing = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        SDL_Rect btn_rect{ rect_.x + margin, rect_.y + margin, rect_.w - margin * 2, button_height };
        const int bottom = rect_.y + rect_.h - margin;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            auto& btn = buttons_[i];
            if (!btn) continue;
            btn->set_rect(btn_rect);
            if (btn->handle_event(e)) {
                used = true;
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    if (callback_) callback_(rooms_[i].second);
                    close();
                }
            }
            btn_rect.y += button_height + spacing;
            if (btn_rect.y + button_height > bottom) {
                break;
            }
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const {
        if (!visible_ || !renderer) return;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        const SDL_Color bg = DMStyles::PanelBG();
        const SDL_Color highlight = DMStyles::HighlightColor();
        const SDL_Color shadow = DMStyles::ShadowColor();
        dm_draw::DrawBeveledRect( renderer, rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        const SDL_Color border = DMStyles::Border();
        dm_draw::DrawRoundedOutline( renderer, rect_, DMStyles::CornerRadius(), kPopupOutlineThickness, border);
        const int margin = DMSpacing::item_gap();
        const int spacing = DMSpacing::small_gap();
        const int button_height = DMButton::height();
        SDL_Rect btn_rect{ rect_.x + margin, rect_.y + margin, rect_.w - margin * 2, button_height };
        const int bottom = rect_.y + rect_.h - margin;
        for (const auto& btn : buttons_) {
            if (!btn) continue;
            btn->set_rect(btn_rect);
            btn->render(renderer);
            btn_rect.y += button_height + spacing;
            if (btn_rect.y > bottom) {
                break;
            }
        }
    }

    bool is_point_inside(int x, int y) const {
        if (!visible_) return false;
        SDL_Point p{x, y};
        return SDL_PointInRect(&p, &rect_);
    }

private:
    bool visible_ = false;
    SDL_Rect rect_{0, 0, 280, 320};
    std::vector<std::pair<std::string, Room*>> rooms_;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    Callback callback_;
};

DevControls::DevControls(Assets* owner, int screen_w, int screen_h)
    : assets_(owner),
      screen_w_(screen_w),
      screen_h_(screen_h) {
    const char* ctor_start = "[DevControls] ctor start";
    dev_mode_trace(ctor_start);
    std::cout << ctor_start << "\n";
    // Load grid header settings
    grid_overlay_enabled_ = devmode::ui_settings::load_bool(kGridOverlayEnabledKey, false);
    snap_to_grid_enabled_ = devmode::ui_settings::load_bool(kGridSnapEnabledKey, false);
    // Initialize cell size from map grid settings if available later; default to 2^0 = 1
    grid_cell_size_px_ = 1;
    room_editor_ = std::make_unique<RoomEditor>(assets_, screen_w_, screen_h_);
    if (room_editor_) {
        room_editor_->set_manifest_store(&manifest_store_);
        room_editor_->set_room_assets_saved_callback([this]() { notify_room_area_data_changed(); });
        // Hide top header/footer while sliding containers are visible
        room_editor_->set_header_visibility_callback([this](bool visible) {
            sliding_headers_hidden_ = visible;
            apply_header_suppression();
        });
        room_editor_->set_map_assets_panel_callback([this]() { this->open_map_assets_modal(); });
        room_editor_->set_boundary_assets_panel_callback([this]() { this->open_boundary_assets_modal(); });
    }
    map_editor_ = std::make_unique<MapEditor>(assets_);
    map_mode_ui_ = std::make_unique<MapModeUI>(assets_);
    if (map_mode_ui_) {
        map_mode_ui_->set_manifest_store(&manifest_store_);
    }
    map_grid_regen_cb_ = [this]() { this->regenerate_map_grid_assets(); };
    apply_header_suppression();
    // Resolution stepper uses same range as map grid (0..kMaxResolution)
    grid_resolution_stepper_ = std::make_unique<DMNumericStepper>("Grid Resolution (r)", 0, vibble::grid::kMaxResolution, 0);
    grid_resolution_stepper_->set_on_change([this](int new_r){
        // Clamp and apply to map grid settings, then update derived cell size for overlay
        const int clamped_r = std::clamp(new_r, 0, vibble::grid::kMaxResolution);
        // Persist into map info if available
        if (map_info_json_) {
            ensure_map_grid_settings(*map_info_json_);
            nlohmann::json& section = (*map_info_json_)["map_grid_settings"];
            MapGridSettings settings = MapGridSettings::from_json(&section);
            settings.resolution = clamped_r;
            settings.clamp();
            settings.apply_to_json(section);
            if (map_grid_save_cb_) {
                map_grid_save_cb_();
            }
            // Derive pixel cell size from resolution
            grid_cell_size_px_ = settings.spacing();
        } else {
            // Fallback: derive pixel size directly if no map info yet
            grid_cell_size_px_ = vibble::grid::delta(clamped_r);
        }
    });

    // Grid overlay checkbox
    grid_overlay_checkbox_ = std::make_unique<DMCheckbox>("Show Grid", grid_overlay_enabled_);
    camera_panel_ = std::make_unique<CameraUIPanel>(assets_, 72, 72);
    if (camera_panel_) {
        camera_panel_->close();
    }
    if (map_editor_) {
        map_editor_->set_ui_blocker([this](int x, int y) { return is_pointer_over_dev_ui(x, y); });
    }
    if (map_mode_ui_) {
        map_mode_ui_->set_footer_always_visible(true);
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
        apply_camera_area_render_flag();
        map_mode_ui_->set_on_mode_changed([this](MapModeUI::HeaderMode mode){
            if (mode == MapModeUI::HeaderMode::Map) {
                if (this->mode_ != Mode::MapEditor) {
                    enter_map_editor_mode();
                }
                asset_filter_.set_active_mode(kModeIdMap);
            } else if (mode == MapModeUI::HeaderMode::Room) {
                if (this->mode_ == Mode::MapEditor) {
                    exit_map_editor_mode(false, true);
                }
                this->set_mode(Mode::RoomEditor);
                if (map_mode_ui_) {
                    map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
                    if (auto* footer = map_mode_ui_->get_footer_bar()) {
                        std::string label = std::string("Room: ") + (current_room_ ? current_room_->room_name : std::string{});
                        footer->set_title(label);
                    }
                }
                asset_filter_.set_active_mode(kModeIdRoom);
            }
            sync_header_button_states();
        });
    }
    if (room_editor_ && map_mode_ui_) {
        room_editor_->set_shared_footer_bar(map_mode_ui_->get_footer_bar());
    }
    configure_header_button_sets();
    trail_suite_ = std::make_unique<TrailEditorSuite>();
    if (trail_suite_) {
        trail_suite_->set_screen_dimensions(screen_w_, screen_h_);
    }
    asset_filter_.initialize();
    asset_filter_.set_state_changed_callback([this]() { refresh_active_asset_filters(); });
    // Keep header always visible in dev mode
    asset_filter_.set_enabled(enabled_);
    asset_filter_.set_screen_dimensions(screen_w_, screen_h_);
    asset_filter_.set_map_info(map_info_json_);
    asset_filter_.set_current_room(current_room_);
    // Provide extra panel content under filters: Grid Overlay Checkbox and Grid Resolution Stepper
    const int checkbox_h = DMCheckbox::height();
    const int stepper_h = DMNumericStepper::height();
    const int total_height = checkbox_h + DMSpacing::small_gap() + stepper_h + DMSpacing::item_gap() * 2;
    asset_filter_.set_extra_panel_height(total_height);
    asset_filter_.set_extra_panel_renderer([this](SDL_Renderer* renderer, const SDL_Rect& area) {
        if (!renderer) return;
        const int gap = DMSpacing::item_gap();
        const int small_gap = DMSpacing::small_gap();
        const int checkbox_h = DMCheckbox::height();
        const int stepper_h = DMNumericStepper::height();
        const int stepper_w_min = 220;

        // Position checkbox at the top
        int y = area.y + gap;
        int x = area.x + gap;
        grid_checkbox_rect_ = SDL_Rect{ x, y, grid_overlay_checkbox_->preferred_width(), checkbox_h };
        if (grid_overlay_checkbox_) {
            grid_overlay_checkbox_->set_rect(grid_checkbox_rect_);
            grid_overlay_checkbox_->render(renderer);
        }

        // Position stepper below checkbox
        y += checkbox_h + small_gap;
        x = area.x + gap;
        int remaining = std::max(0, area.x + area.w - gap - x);
        int stepper_w = std::max(stepper_w_min, remaining);
        grid_stepper_rect_ = SDL_Rect{ x, y, stepper_w, stepper_h };
        if (grid_resolution_stepper_) {
            // Keep stepper value in sync with map grid settings each frame
            int r_value = 0;
            if (map_info_json_) {
                const nlohmann::json* section = nullptr;
                auto it = map_info_json_->find("map_grid_settings");
                if (it != map_info_json_->end() && it->is_object()) {
                    section = &(*it);
                }
                MapGridSettings settings = MapGridSettings::from_json(section);
                r_value = settings.resolution;
            }
            grid_resolution_stepper_->set_value(r_value);
            grid_resolution_stepper_->set_rect(grid_stepper_rect_);
            grid_resolution_stepper_->render(renderer);
        }
    });
    asset_filter_.set_extra_panel_event_handler([this](const SDL_Event& event, const SDL_Rect& area) -> bool {
        // Only care about pointer interactions inside panel area
        bool pointer_event = (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEWHEEL);
        SDL_Point p{0,0};
        if (pointer_event) {
            if (event.type == SDL_MOUSEMOTION) { p = SDL_Point{event.motion.x, event.motion.y}; }
            else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) { p = SDL_Point{event.button.x, event.button.y}; }
            else { int mx=0,my=0; SDL_GetMouseState(&mx,&my); p = SDL_Point{mx,my}; }
            if (!SDL_PointInRect(&p, &area)) {
                // Ignore pointer events outside the extra panel
                return false;
            }
        }
        bool used = false;
        // Update layout against latest area values
        const int gap = DMSpacing::item_gap();
        const int small_gap = DMSpacing::small_gap();
        const int checkbox_h = DMCheckbox::height();
        const int stepper_h = DMNumericStepper::height();
        const int stepper_w_min = 220;

        // Position checkbox at the top
        int y = area.y + gap;
        int x = area.x + gap;
        grid_checkbox_rect_ = SDL_Rect{ x, y, grid_overlay_checkbox_->preferred_width(), checkbox_h };
        if (grid_overlay_checkbox_) {
            grid_overlay_checkbox_->set_rect(grid_checkbox_rect_);
            if (grid_overlay_checkbox_->handle_event(event)) {
                // Checkbox state change is handled internally; sync our state
                grid_overlay_enabled_ = grid_overlay_checkbox_->value();
                devmode::ui_settings::save_bool(kGridOverlayEnabledKey, grid_overlay_enabled_);
                used = true;
            }
        }

        // Position stepper below checkbox
        y += checkbox_h + small_gap;
        x = area.x + gap;
        int remaining = std::max(0, area.x + area.w - gap - x);
        int stepper_w = std::max(stepper_w_min, remaining);
        grid_stepper_rect_ = SDL_Rect{ x, y, stepper_w, stepper_h };
        if (grid_resolution_stepper_) grid_resolution_stepper_->set_rect(grid_stepper_rect_);
        if (grid_resolution_stepper_ && grid_resolution_stepper_->handle_event(event)) {
            // DMNumericStepper will invoke set_on_change; we consider event handled
            used = true;
        }
        return used;
    });
    asset_filter_.set_mode_buttons({
        {kModeIdRoom, "Room", mode_ == Mode::RoomEditor},
        {kModeIdMap, "Map", mode_ == Mode::MapEditor}
    });
    asset_filter_.set_mode_changed_callback([this](const std::string& id) {
        if (id == kModeIdMap) {
            if (this->mode_ != Mode::MapEditor) {
                enter_map_editor_mode();
            }
        } else if (id == kModeIdRoom) {
            if (this->mode_ == Mode::MapEditor) {
                exit_map_editor_mode(false, true);
            }
            this->set_mode(Mode::RoomEditor);
            if (map_mode_ui_) {
                map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
                if (auto* footer = map_mode_ui_->get_footer_bar()) {
                    std::string label = std::string("Room: ") +
                                        (current_room_ ? current_room_->room_name : std::string{});
                    footer->set_title(label);
                }
            }
        }
        sync_header_button_states();
    });
    const char* ctor_end = "[DevControls] ctor complete";
    dev_mode_trace(ctor_end);
    std::cout << ctor_end << "\n";
    AssetInfo::set_manifest_store_provider([this]() -> devmode::core::ManifestStore* {
        return &manifest_store_;
    });
}

DevControls::~DevControls() {
    restore_filter_hidden_assets();
    manifest_store_.flush();
    AssetInfo::set_manifest_store_provider({});
}

devmode::core::ManifestStore& DevControls::manifest_store() {
    return manifest_store_;
}

const devmode::core::ManifestStore& DevControls::manifest_store() const {
    return manifest_store_;
}

void DevControls::set_input(Input* input) {
    input_ = input;
    if (room_editor_) room_editor_->set_input(input);
    if (map_editor_) map_editor_->set_input(input);
}

void DevControls::set_map_info(nlohmann::json* map_info, MapLightPanel::SaveCallback on_save) {
    map_info_json_ = map_info;
    map_light_save_cb_ = std::move(on_save);
    map_grid_save_cb_ = map_light_save_cb_;
    if (map_mode_ui_) {
        map_mode_ui_->set_light_save_callback(map_light_save_cb_);
        map_mode_ui_->set_map_context(map_info_json_, map_path_);
    }
    asset_filter_.set_map_info(map_info_json_);
    // Sync header stepper and cell size from current map grid settings
    if (map_info_json_) {
        ensure_map_grid_settings(*map_info_json_);
        const nlohmann::json& section = (*map_info_json_)["map_grid_settings"];
        MapGridSettings settings = MapGridSettings::from_json(&section);
        settings.clamp();
        grid_cell_size_px_ = settings.spacing();
        if (grid_resolution_stepper_) {
            grid_resolution_stepper_->set_value(settings.resolution);
        }
    }
    configure_header_button_sets();
}

void DevControls::set_player(Asset* player) {
    player_ = player;
    if (room_editor_) room_editor_->set_player(player);
}

void DevControls::set_active_assets(std::vector<Asset*>& actives) {
    active_assets_ = &actives;
    if (room_editor_) room_editor_->set_active_assets(actives);
}

void DevControls::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;

    if (room_editor_) room_editor_->set_screen_dimensions(width, height);
    if (map_editor_) map_editor_->set_screen_dimensions(width, height);
    if (map_mode_ui_) map_mode_ui_->set_screen_dimensions(width, height);

    SDL_Rect bounds{0, 0, screen_w_, screen_h_};
    if (camera_panel_) camera_panel_->set_work_area(bounds);
    if (trail_suite_) trail_suite_->set_screen_dimensions(width, height);

    asset_filter_.set_screen_dimensions(width, height);
    if (map_assets_modal_) map_assets_modal_->set_screen_dimensions(width, height);
    if (boundary_assets_modal_) boundary_assets_modal_->set_screen_dimensions(width, height);

    // No right-accessory reservation; grid controls now live in expanded panel
    asset_filter_.set_right_accessory_width(0);
    asset_filter_.ensure_layout();
    SDL_Rect usable = FloatingPanelLayoutManager::instance().computeUsableRect(
        bounds,
        SDL_Rect{0, 0, 0, 0},
        SDL_Rect{0, 0, 0, 0},
        {});
    if (map_mode_ui_) {
        map_mode_ui_->set_sliding_area_bounds(usable);
    }
}

void DevControls::set_current_room(Room* room, bool force_refresh) {
    if (!force_refresh && current_room_ == room) {
        current_room_ = room;
        dev_selected_room_ = room;
        return;
    }
    {
        std::ostringstream oss;
        oss << "[DevControls] set_current_room begin -> "
            << (room ? room->room_name : std::string("<null>"));
        dev_mode_trace(oss.str());
    }
    selected_room_area_name_.reset();
    hovered_room_area_name_.reset();
    // Legacy panel removed
    current_room_ = room;

    dev_selected_room_ = room;
    if (regenerate_popup_) regenerate_popup_->close();
    if (room_editor_) {
        dev_mode_trace("[DevControls] set_current_room -> room_editor set_current_room");
        room_editor_->set_current_room(room);
    }
    asset_filter_.set_current_room(room);
    if (map_mode_ui_) {
        if (auto* footer = map_mode_ui_->get_footer_bar()) {
            std::string label;
            if (mode_ == Mode::RoomEditor) label = std::string("Room: ") + (current_room_ ? current_room_->room_name : std::string{});
            else label = std::string("Map");
            footer->set_title(label);
        }
    }

    notify_room_area_data_changed();
    dev_mode_trace("[DevControls] set_current_room complete");
}

void DevControls::set_rooms(std::vector<Room*>* rooms, std::size_t generation) {
    if (rooms == rooms_ && generation == rooms_generation_) {
        return;
    }

    rooms_ = rooms;
    rooms_generation_ = generation;

    if (rooms_ && assets_) {
        const std::string map_id = assets_->map_id();
        nlohmann::json* map_info = &assets_->map_info_json();
        for (Room* room : *rooms_) {
            if (!room) continue;
            room->set_manifest_store(&manifest_store_, map_id, map_info);
        }
    }
    if (map_editor_) map_editor_->set_rooms(rooms);
}

void DevControls::set_camera_override_for_testing(camera* camera_override) {
    camera_override_for_testing_ = camera_override;
    if (map_editor_) {
        map_editor_->set_camera_override_for_testing(camera_override);
    }
    apply_camera_area_render_flag();
}

void DevControls::set_map_context(nlohmann::json* map_info, const std::string& map_path) {
    map_info_json_ = map_info;
    map_path_ = map_path;
    if (map_mode_ui_) {
        map_mode_ui_->set_map_context(map_info, map_path);
        map_mode_ui_->set_light_save_callback(map_light_save_cb_);
    }
    if (rooms_ && assets_) {
        const std::string map_id = assets_->map_id();
        nlohmann::json* info = &assets_->map_info_json();
        for (Room* room : *rooms_) {
            if (!room) continue;
            room->set_manifest_store(&manifest_store_, map_id, info);
        }
    }
    asset_filter_.set_map_info(map_info_json_);
    configure_header_button_sets();
    notify_room_area_data_changed();
}

bool DevControls::is_pointer_over_dev_ui(int x, int y) const {
    if (camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (room_editor_ && room_editor_->is_room_ui_blocking_point(x, y)) {
        return true;
    }
    if (trail_suite_ && trail_suite_->contains_point(x, y)) {
        return true;
    }
    if (map_mode_ui_ && map_mode_ui_->is_point_inside(x, y)) {
        return true;
    }
    if (regenerate_popup_ && regenerate_popup_->visible() && regenerate_popup_->is_point_inside(x, y)) {
        return true;
    }
    if (!is_modal_blocking_panels() && enabled_ && asset_filter_.contains_point(x, y)) {
        return true;
    }
    return false;
}

Room* DevControls::resolve_current_room(Room* detected_room) {
    detected_room_ = detected_room;
    Room* target = choose_room(detected_room_);
    if (!enabled_) {
        dev_selected_room_ = nullptr;
        set_current_room(target);
        return current_room_;
    }

    if (!dev_selected_room_) {
        dev_selected_room_ = choose_room(detected_room_);
    }

    target = choose_room(dev_selected_room_);
    dev_selected_room_ = target;
    set_current_room(target);
    return current_room_;
}

void DevControls::set_enabled(bool enabled) {
    {
        std::ostringstream oss;
        oss << "[DevControls] set_enabled(" << (enabled ? "true" : "false") << ") begin";
        const std::string msg = oss.str();
        dev_mode_trace(msg);
        std::cout << msg << "\n";
    }
    if (enabled == enabled_) {
        const char* msg = "[DevControls] set_enabled unchanged, exiting";
        dev_mode_trace(msg);
        std::cout << msg << "\n";
        return;
    }
    enabled_ = enabled;
    // Keep header always visible in dev mode
    asset_filter_.set_enabled(enabled_);

    if (enabled_) {
        const char* msg = "[DevControls] preparing enable flow";
        dev_mode_trace(msg);
        std::cout << msg << "\n";
        camera* camera_ptr = assets_ ? &assets_->getView() : nullptr;
        SDL_Point preserved_center{0, 0};
        float preserved_scale = 1.0f;
        bool should_restore_camera = false;
        if (camera_ptr) {
            preserved_center = camera_ptr->get_screen_center();
            preserved_scale = camera_ptr->get_scale();
            should_restore_camera = true;
        }
        const bool camera_was_visible = camera_panel_ && camera_panel_->is_visible();
        close_all_floating_panels();
        set_mode(Mode::RoomEditor);
        Room* target = choose_room(current_room_ ? current_room_ : detected_room_);
        dev_selected_room_ = target;
        if (room_editor_) room_editor_->set_enabled(true, true);
        if (map_editor_) map_editor_->set_enabled(false);
        if (camera_panel_) camera_panel_->set_assets(assets_);
        set_current_room(target);
        if (map_mode_ui_) {
            map_mode_ui_->set_map_mode_active(false);
            map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
        }
        if (should_restore_camera && camera_ptr) {
            camera_ptr->set_manual_zoom_override(true);
            camera_ptr->set_focus_override(preserved_center);
            camera_ptr->set_screen_center(preserved_center);
            camera_ptr->set_scale(preserved_scale);
            camera_ptr->update(0.0f);
        }
        if (camera_was_visible && camera_panel_) {
            camera_panel_->open();
        }
        const char* msg_enable_done = "[DevControls] enable flow complete";
        dev_mode_trace(msg_enable_done);
        std::cout << msg_enable_done << "\n";
    } else {
        const char* msg_disable = "[DevControls] preparing disable flow";
        dev_mode_trace(msg_disable);
        std::cout << msg_disable << "\n";
        close_all_floating_panels();
        if (map_editor_ && map_editor_->is_enabled()) {
            map_editor_->exit(true, false);
        }
        if (map_mode_ui_) {
            map_mode_ui_->set_map_mode_active(false);
            map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
        }
        set_mode(Mode::RoomEditor);
        dev_selected_room_ = nullptr;
        if (room_editor_) {
            room_editor_->set_enabled(false);
        }
        close_camera_panel();
        restore_filter_hidden_assets();
        const char* msg_disable_done = "[DevControls] disable flow complete";
        dev_mode_trace(msg_disable_done);
        std::cout << msg_disable_done << "\n";
    }

    sync_header_button_states();
    if (enabled_) {
        asset_filter_.ensure_layout();
    }
    {
        std::ostringstream oss;
        oss << "[DevControls] set_enabled(" << (enabled ? "true" : "false") << ") done";
        const std::string msg = oss.str();
        dev_mode_trace(msg);
        std::cout << msg << "\n";
    }
}

void DevControls::update(const Input& input) {
    if (!enabled_) return;

    const bool ctrl = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (ctrl && input.wasScancodePressed(SDL_SCANCODE_M)) {
        toggle_map_light_panel();
    }
    if (ctrl && input.wasScancodePressed(SDL_SCANCODE_C)) {
        const bool room_editor_active =
            mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled();
        if (!room_editor_active) {
            toggle_camera_panel();
        }
    }

    // No keyboard shortcuts for grid header fields in this scope
    pointer_over_camera_panel_ =
        camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(input.getX(), input.getY());
    if (mode_ == Mode::MapEditor) {
        if (map_mode_ui_ && input.wasScancodePressed(SDL_SCANCODE_F8)) {
            map_mode_ui_->toggle_layers_panel();
        }
        if (map_editor_) {
            map_editor_->update(input);
            handle_map_selection();
        }
    } else if (mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled()) {
        // While in-world Frame Editor session is active, disable dev mouse controls that
        // highlight/select assets by skipping RoomEditor update and clearing highlights.
        const bool frame_editing = frame_editor_session_ && frame_editor_session_->is_active();
        if (!frame_editing) {
            if (!pointer_over_camera_panel_) {
                room_editor_->update(input);
            }
            // Update Area Tool overlay/editor if active
            if (asset_area_editor_ && asset_area_editor_->is_active()) {
                asset_area_editor_->update(input, screen_w_, screen_h_);
            }
        } else {
            room_editor_->clear_highlighted_assets();
        }
    }

    if (camera_panel_) {
        camera_panel_->update(input, screen_w_, screen_h_);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->update(input);
    }
    bool modal_hide = is_modal_blocking_panels();
    modal_headers_hidden_ = modal_hide;
    bool hide_headers = modal_hide; // keep header visible unless a modal blocks panels
    // Keep header always visible in dev mode
    asset_filter_.set_enabled(enabled_);
    apply_header_suppression();
    if (map_mode_ui_) {
        map_mode_ui_->update(input);
    }
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->update(input);
    }
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->update(input);
    }
    // Legacy CreateRoomAreaPanel removed
    if (trail_suite_) {
        trail_suite_->update(input);
        if (pending_trail_template_ && !trail_suite_->is_open()) {
            pending_trail_template_.reset();
        }
    }

    asset_filter_.ensure_layout();

    // Gather current sliding container rects before deciding header visibility
    SDL_Rect layout_rect = asset_filter_.layout_bounds();
    SDL_Rect footer_rect{0, 0, 0, 0};
    std::vector<SDL_Rect> sliding_rects;
    if (map_mode_ui_) {
        map_mode_ui_->collect_sliding_container_rects(sliding_rects);
    }
    if (layout_rect.w > 0 && layout_rect.h > 0) {
        sliding_rects.push_back(layout_rect);
    }
    if (map_mode_ui_) {
        DevFooterBar* footer = map_mode_ui_->get_footer_bar();
        if (footer && footer->visible()) {
            footer_rect = footer->rect();
        }
    }
    modal_hide = is_modal_blocking_panels();
    hide_headers = modal_hide || sliding_headers_hidden_ || !sliding_rects.empty();
    SDL_Rect header_rect = hide_headers ? SDL_Rect{0, 0, 0, 0} : asset_filter_.header_rect();
    SDL_Rect usable_rect = FloatingPanelLayoutManager::instance().computeUsableRect(
        SDL_Rect{0, 0, screen_w_, screen_h_},
        header_rect,
        footer_rect,
        sliding_rects);
    if (map_mode_ui_) {
        map_mode_ui_->set_sliding_area_bounds(usable_rect);
    }

    if (room_editor_ && room_editor_->is_enabled()) {
        SDL_Point pointer{input.getX(), input.getY()};
        if (asset_filter_.contains_point(pointer.x, pointer.y)) {
            room_editor_->clear_highlighted_assets();
        } else if (!hide_headers) {
            DevFooterBar* footer = map_mode_ui_ ? map_mode_ui_->get_footer_bar() : nullptr;
            if (footer && footer->visible()) {
                const SDL_Rect& bar_rect = footer->rect();
                if (bar_rect.w > 0 && bar_rect.h > 0 && SDL_PointInRect(&pointer, &bar_rect)) {
                    room_editor_->clear_highlighted_assets();
                }
            }
        }
    }

    sync_header_button_states();
    // Update in-world frame editor session
    if (frame_editor_session_ && frame_editor_session_->is_active()) {
        frame_editor_session_->update(input);
    }
}

void DevControls::update_ui(const Input& input) {
    if (!enabled_) return;
    if (!room_editor_) return;

    const bool room_editor_active = (mode_ == Mode::RoomEditor) && room_editor_->is_enabled();
    const bool spawn_panel_visible = room_editor_->is_spawn_group_panel_visible();

    if (!room_editor_active && !spawn_panel_visible) {
        return;
    }

    room_editor_->update_ui(input);
}

void DevControls::handle_sdl_event(const SDL_Event& event) {
    if (!enabled_) return;

    asset_filter_.ensure_layout();
    SDL_Rect header_rect{0, 0, 0, 0};
    SDL_Rect layout_rect = asset_filter_.layout_bounds();
    SDL_Rect footer_rect{0, 0, 0, 0};
    std::vector<SDL_Rect> sliding_rects;
    if (map_mode_ui_) {
        map_mode_ui_->collect_sliding_container_rects(sliding_rects);
    }
    if (layout_rect.w > 0 && layout_rect.h > 0) {
        sliding_rects.push_back(layout_rect);
    }
    if (map_mode_ui_) {
        DevFooterBar* footer = map_mode_ui_->get_footer_bar();
        if (footer && footer->visible()) {
            footer_rect = footer->rect();
        }
    }
    const bool modal_hide_pre = is_modal_blocking_panels();
    const bool layers_panel_open_pre = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();
    const bool hide_headers_pre = modal_hide_pre || sliding_headers_hidden_ || !sliding_rects.empty() || layers_panel_open_pre;
    header_rect = hide_headers_pre ? SDL_Rect{0, 0, 0, 0} : asset_filter_.header_rect();
    SDL_Rect usable_rect = FloatingPanelLayoutManager::instance().computeUsableRect(
        SDL_Rect{0, 0, screen_w_, screen_h_},
        header_rect,
        footer_rect,
        sliding_rects);
    if (map_mode_ui_) {
        map_mode_ui_->set_sliding_area_bounds(usable_rect);
    }

    const bool pointer_event = is_pointer_event(event);
    const bool wheel_event = (event.type == SDL_MOUSEWHEEL);
    const bool pointer_relevant = pointer_event || wheel_event;
    SDL_Point pointer{0, 0};
    if (pointer_relevant) {
        pointer = event_point(event);
    }

    const bool modal_hide = is_modal_blocking_panels();
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();
    modal_headers_hidden_ = modal_hide;
    // Also suppress header when the Layers panel is open, to avoid intercepting clicks
    const bool hide_headers = modal_hide || layers_panel_open; // keep header visible unless a modal blocks panels or layers is open
    // Keep header always visible in dev mode
    asset_filter_.set_enabled(enabled_);
    asset_filter_.set_header_suppressed(hide_headers);
    apply_header_suppression();

    auto consume = [&](bool used) {
        if (used && input_) {
            input_->consumeEvent(event);
        }
        return used;
};

    // If Layers panel is open, allow ESC to close it and consume the key so main menu does not open
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        if (layers_panel_open && map_mode_ui_) {
            map_mode_ui_->toggle_layers_panel();
            consume(true);
            return;
        }
    }

    // Route the rest of header events to AssetFilterBar only when header is not suppressed;
    // extra grid panel events are handled via its extra event handler.
    if (!asset_filter_.header_suppressed()) {
        if (pointer_event && consume(asset_filter_.handle_event(event))) {
            return;
        }
    }
    if (pointer_relevant && enabled_ && asset_filter_.contains_point(pointer.x, pointer.y) && !asset_filter_.header_suppressed()) {
        consume(true);
        return;
    }

    if (trail_suite_ && trail_suite_->is_open()) {
        if (consume(trail_suite_->handle_event(event))) {
            return;
        }
        if (pointer_relevant && trail_suite_->contains_point(pointer.x, pointer.y)) {
            consume(true);
            return;
        }
    }

    if (consume_modal_event(map_assets_modal_.get(), event, pointer, pointer_relevant, input_)) {
        return;
    }
    if (consume_modal_event(boundary_assets_modal_.get(), event, pointer, pointer_relevant, input_)) {
        return;
    }
    if (consume_modal_event(regenerate_popup_.get(), event, pointer, pointer_relevant, input_)) {
        return;
    }

    const bool room_editor_active = can_use_room_editor_ui();
    const bool spawn_panel_visible = room_editor_ && room_editor_->is_spawn_group_panel_visible();
    const bool can_route_room_editor = room_editor_ && (room_editor_active || spawn_panel_visible);
    const bool pointer_over_room_ui = can_route_room_editor && pointer_relevant &&
                                      room_editor_->is_room_ui_blocking_point(pointer.x, pointer.y);

    if (pointer_over_room_ui) {
        room_editor_->handle_sdl_event(event);
        consume(true);
        return;
    }

    bool pointer_event_inside_camera = false;
    if (camera_panel_ && camera_panel_->is_visible()) {
        switch (event.type) {
        case SDL_MOUSEMOTION:
            pointer_event_inside_camera = camera_panel_->is_point_inside(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            pointer_event_inside_camera = camera_panel_->is_point_inside(event.button.x, event.button.y);
            break;
        case SDL_MOUSEWHEEL: {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            pointer_event_inside_camera = camera_panel_->is_point_inside(mx, my);
            break;
        }
        default:
            break;
        }
    }

    if (camera_panel_ && camera_panel_->is_visible()) {
        if (consume(camera_panel_->handle_event(event))) {
            return;
        }
    }

    // Route events to in-world frame editor session before footer/map UI
    if (frame_editor_session_ && frame_editor_session_->is_active()) {
        if (consume(frame_editor_session_->handle_event(event))) {
            return;
        }
        // While frame editor is active, prevent RoomEditor from consuming input to avoid
        // asset highlighting/selection changes.
    }

    bool block_for_camera = pointer_event_inside_camera;
    if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT) && pointer_over_camera_panel_) {
        block_for_camera = true;
    }
    if (block_for_camera) {
        consume(true);
        return;
    }

    if (!pointer_over_room_ui && map_mode_ui_) {
        if (consume(map_mode_ui_->handle_event(event))) {
            return;
        }
        if (pointer_relevant && map_mode_ui_->is_point_inside(pointer.x, pointer.y)) {
            consume(true);
            return;
        }
    }

    // Legacy CreateRoomAreaPanel removed

    if (mode_ == Mode::MapEditor) {
        return;
    }

    // Room mode: route area interactions without relying on legacy AreaMode
    if (mode_ == Mode::RoomEditor && assets_ && current_room_) {
        const auto& area_list = room_area_polygons();
        auto point_in_poly = [](const std::vector<SDL_Point>& poly, SDL_Point pt) -> bool {
            bool inside = false;
            const size_t n = poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const int xi = poly[i].x;
                const int yi = poly[i].y;
                const int xj = poly[j].x;
                const int yj = poly[j].y;
                const bool intersect = ((yi > pt.y) != (yj > pt.y)) && (pt.x < (xj - xi) * (pt.y - yi) / (double)(yj - yi + 1e-12) + xi);
                if (intersect) inside = !inside;
            }
            return inside;
        };

        if (pointer_event || pointer_relevant) {
            SDL_Point sp = pointer;
            SDL_Point world = sp;
            if (input_) {
                if (auto mapped = input_->screen_to_world(sp)) {
                    world = *mapped;
                } else if (assets_) {
                    SDL_FPoint mapped = assets_->getView().screen_to_map(sp);
                    world = SDL_Point{static_cast<int>(std::lround(mapped.x)), static_cast<int>(std::lround(mapped.y))};
                }
            } else if (assets_) {
                SDL_FPoint mapped = assets_->getView().screen_to_map(sp);
                world = SDL_Point{static_cast<int>(std::lround(mapped.x)), static_cast<int>(std::lround(mapped.y))};
            }

            int hover = -1;
            for (int i = static_cast<int>(area_list.size()) - 1; i >= 0; --i) {
                if (point_in_poly(area_list[i].points, world)) { hover = i; break; }
            }

            if (hover >= 0 && hover < static_cast<int>(area_list.size())) {
                const auto& hovered = area_list[hover];
                hovered_room_area_name_ = hovered.name;
                // Left click -> begin shape edit
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && event.button.clicks <= 1) {
                    selected_room_area_name_ = hovered.name;
                    hovered_room_area_name_ = hovered.name;
                    if (!asset_area_editor_) asset_area_editor_ = std::make_unique<AreaOverlayEditor>();
                    if (asset_area_editor_) {
                        asset_area_editor_->attach_assets(assets_);
                        asset_area_editor_->set_on_saved([this]() { this->notify_room_area_data_changed(); });
                        if (asset_area_editor_->begin_for_room(current_room_, hovered.name)) {
                            if (map_mode_ui_) {
                                if (auto* footer = map_mode_ui_->get_footer_bar()) {
                                    std::string label = std::string("Editing Area: ") + hovered.name;
                                    footer->set_title(label);
                                }
                            }
                            consume(true);
                            return;
                        }
                    }
                }
                // Right click -> open Area Info UI with Area sections
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_RIGHT) {
                    selected_room_area_name_ = hovered.name;
                    // Open both the Area Tool for this area and the Area Config panel
                    begin_room_area_edit(hovered.name);
                    if (room_editor_) {
                        room_editor_->open_area_info_editor(current_room_, hovered.name);
                    }
                    consume(true);
                    return;
                }
                // Double-click left -> open Area Info UI with Area sections
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && event.button.clicks >= 2) {
                    selected_room_area_name_ = hovered.name;
                    if (room_editor_) {
                        room_editor_->open_area_info_editor(current_room_, hovered.name);
                        consume(true);
                        return;
                    }
                }
            } else {
                if (pointer_event) {
                    hovered_room_area_name_.reset();
                }
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && event.button.clicks <= 1) {
                    selected_room_area_name_.reset();
                }
            }
        }
    }

    if (mode_ == Mode::RoomEditor) {
        if (asset_area_editor_ && asset_area_editor_->is_active()) {
            if (asset_area_editor_->handle_event(event)) {
                consume(true);
                return;
            }
        }
    }

    // Do not route to RoomEditor while the in-world Frame Editor is active
    if (!(frame_editor_session_ && frame_editor_session_->is_active()) && can_route_room_editor && room_editor_->handle_sdl_event(event)) {
        consume(true);
        return;
    }
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    // Render grid overlay if enabled (moved to beginning to render behind UI)
    if (grid_overlay_enabled_ && assets_) {
        const camera& cam = assets_->getView();
        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        // Grid colors (reduced alpha)
        SDL_Color minor{0, 255, 255, 48};
        SDL_Color major{0, 255, 255, 80};

        // Calculate visible world bounds
        SDL_FPoint top_left_world = cam.screen_to_map(SDL_Point{0, 0});
        SDL_FPoint bottom_right_world = cam.screen_to_map(SDL_Point{screen_w_, screen_h_});

        // Calculate grid lines using map grid resolution when available
        int cell = grid_cell_size_px_;
        if (map_info_json_) {
            const nlohmann::json* section = nullptr;
            auto it = map_info_json_->find("map_grid_settings");
            if (it != map_info_json_->end() && it->is_object()) {
                section = &(*it);
            }
            MapGridSettings settings = MapGridSettings::from_json(section);
            settings.clamp();
            cell = settings.spacing();
        }
        cell = std::max(1, cell);
        if (cell > 0) {
            const int major_interval = 8; // major line every N cells
            // Vertical lines
            float start_x = std::floor(top_left_world.x / cell) * cell;
            for (float x = start_x; x <= bottom_right_world.x + cell; x += cell) {
                // Compute parallax-adjusted screen coordinates
                SDL_Point world_start{ static_cast<int>(std::lround(x)), static_cast<int>(std::lround(top_left_world.y)) };
                SDL_Point world_end  { static_cast<int>(std::lround(x)), static_cast<int>(std::lround(bottom_right_world.y)) };
                SDL_FPoint screen_start = cam.map_to_screen_f(SDL_FPoint{static_cast<float>(world_start.x), static_cast<float>(world_start.y)});
                SDL_FPoint screen_end   = cam.map_to_screen_f(SDL_FPoint{static_cast<float>(world_end.x),   static_cast<float>(world_end.y)});
                camera::RenderEffects eff_start = cam.compute_render_effects(world_start, 1.0f, 1.0f);
                camera::RenderEffects eff_end   = cam.compute_render_effects(world_end,   1.0f, 1.0f);
                int sx0 = static_cast<int>(std::lround(screen_start.x + eff_start.parallax_offset_x));
                int sy0 = static_cast<int>(std::lround(screen_start.y));
                int sx1 = static_cast<int>(std::lround(screen_end.x   + eff_end.parallax_offset_x));
                int sy1 = static_cast<int>(std::lround(screen_end.y));
                const bool is_major = (static_cast<long long>(std::llround(x)) % (cell * major_interval) == 0);
                SDL_Color c = is_major ? major : minor;
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                SDL_RenderDrawLine(renderer, sx0, sy0, sx1, sy1);
            }

            // Horizontal lines
            float start_y = std::floor(top_left_world.y / cell) * cell;
            for (float y = start_y; y <= bottom_right_world.y + cell; y += cell) {
                SDL_Point world_start{ static_cast<int>(std::lround(top_left_world.x)),     static_cast<int>(std::lround(y)) };
                SDL_Point world_end  { static_cast<int>(std::lround(bottom_right_world.x)), static_cast<int>(std::lround(y)) };
                SDL_FPoint screen_start = cam.map_to_screen_f(SDL_FPoint{static_cast<float>(world_start.x), static_cast<float>(world_start.y)});
                SDL_FPoint screen_end   = cam.map_to_screen_f(SDL_FPoint{static_cast<float>(world_end.x),   static_cast<float>(world_end.y)});
                camera::RenderEffects eff_start = cam.compute_render_effects(world_start, 1.0f, 1.0f);
                camera::RenderEffects eff_end   = cam.compute_render_effects(world_end,   1.0f, 1.0f);
                int sx0 = static_cast<int>(std::lround(screen_start.x + eff_start.parallax_offset_x));
                int sy0 = static_cast<int>(std::lround(screen_start.y));
                int sx1 = static_cast<int>(std::lround(screen_end.x   + eff_end.parallax_offset_x));
                int sy1 = static_cast<int>(std::lround(screen_end.y));
                const bool is_major = (static_cast<long long>(std::llround(y)) % (cell * major_interval) == 0);
                SDL_Color c = is_major ? major : minor;
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                SDL_RenderDrawLine(renderer, sx0, sy0, sx1, sy1);
            }
        }

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }

    if (mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
    } else if (mode_ == Mode::RoomEditor && room_editor_) {
        room_editor_->render_overlays(renderer);
        // Render Area Tool overlay/editor if active
        if (asset_area_editor_ && asset_area_editor_->is_active()) {
            asset_area_editor_->render(renderer);
        }
        // Frame editor session (in-world)
        if (frame_editor_session_ && frame_editor_session_->is_active()) {
            frame_editor_session_->render(renderer);
        }
        // Draw room area overlays (always visible in Room mode)
        if (renderer && assets_ && current_room_) {
            const camera& cam = assets_->getView();
            SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            Uint8 pr=0,pg=0,pb=0,pa=0; SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);

            auto color_for_area_name = [](const std::string& name) -> SDL_Color {
                // Deterministic color from name
                static const SDL_Color palette[] = {
                    SDL_Color{255,140,0,96},   // orange
                    SDL_Color{0,120,255,96},   // blue
                    SDL_Color{0,200,0,96},     // green
                    SDL_Color{180,0,220,96},   // purple
                    SDL_Color{255,0,0,96},     // red
                    SDL_Color{255,220,0,96}    // yellow
                };
                unsigned int h = 2166136261u;
                for (unsigned char c : name) { h ^= c; h *= 16777619u; }
                return palette[h % (sizeof(palette)/sizeof(palette[0]))];
            };

            const auto& area_list = room_area_polygons();
            // Sort by z-index ascending for drawing order
            std::vector<int> order; order.reserve(area_list.size());
            for (size_t i = 0; i < area_list.size(); ++i) order.push_back(static_cast<int>(i));
            std::sort(order.begin(), order.end(), [&](int a, int b){ return area_list[a].z < area_list[b].z; });

            const std::string selected_name = selected_room_area_name_.value_or(std::string{});
            const std::string hovered_name = hovered_room_area_name_.value_or(std::string{});

            for (int idx : order) {
                const auto& poly = area_list[idx];
                if (poly.points.size() < 3) continue;
                SDL_Color base = color_for_area_name(poly.name);
                const bool is_selected = !selected_name.empty() && poly.name == selected_name;
                const bool is_hovered = !hovered_name.empty() && poly.name == hovered_name;

                SDL_Color fill = dm_draw::LightenColor(base, is_selected ? 0.35f : (is_hovered ? 0.2f : 0.05f));
                fill.a = is_selected ? 160 : (is_hovered ? 120 : 72);

                std::vector<SDL_Vertex> vertices;
                vertices.reserve(poly.points.size());
                for (const SDL_Point& p : poly.points) {
                    SDL_FPoint sp = cam.map_to_screen(p);
                    SDL_Vertex v{};
                    v.position.x = sp.x;
                    v.position.y = sp.y;
                    v.color = fill;
                    vertices.push_back(v);
                }
                if (vertices.size() >= 3) {
                    std::vector<int> indices;
                    indices.reserve((vertices.size() - 2) * 3);
                    for (size_t i = 1; i + 1 < vertices.size(); ++i) {
                        indices.push_back(0);
                        indices.push_back(static_cast<int>(i));
                        indices.push_back(static_cast<int>(i + 1));
                    }
                    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
                }

                SDL_Color outline = dm_draw::LightenColor(base, is_selected ? 0.25f : (is_hovered ? 0.12f : 0.0f));
                outline.a = is_selected ? 255 : (is_hovered ? 230 : base.a);
                SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
                for (size_t i = 0, n = poly.points.size(); i < n; ++i) {
                    const SDL_Point& a = poly.points[i];
                    const SDL_Point& b = poly.points[(i+1) % n];
                    SDL_FPoint as = cam.map_to_screen(a);
                    SDL_FPoint bs = cam.map_to_screen(b);
                    SDL_RenderDrawLine(renderer,
                                       static_cast<int>(std::lround(as.x)), static_cast<int>(std::lround(as.y)),
                                       static_cast<int>(std::lround(bs.x)), static_cast<int>(std::lround(bs.y)));
                }

                if (is_selected) {
                    SDL_FPoint anchor = cam.map_to_screen(poly.anchor);
                    const int arm = 6;
                    SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, outline.a);
                    SDL_RenderDrawLine(renderer,
                        static_cast<int>(std::lround(anchor.x)) - arm,
                        static_cast<int>(std::lround(anchor.y)),
                        static_cast<int>(std::lround(anchor.x)) + arm,
                        static_cast<int>(std::lround(anchor.y)));
                    SDL_RenderDrawLine(renderer,
                        static_cast<int>(std::lround(anchor.x)),
                        static_cast<int>(std::lround(anchor.y)) - arm,
                        static_cast<int>(std::lround(anchor.x)),
                        static_cast<int>(std::lround(anchor.y)) + arm);
                }
            }
            SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
            SDL_SetRenderDrawBlendMode(renderer, prev_mode);
        }
    } else if (mode_ == Mode::RoomEditor) {

        if (renderer && assets_) {
            // Legacy overlay and panel rendering removed in favor of asset-based Area editing.
        }
    }
    if (renderer && map_mode_ui_ && map_mode_ui_->is_light_panel_visible() && assets_) {
        const camera& cam = assets_->getView();
        SDL_Point screen_center_map = cam.get_screen_center();
        SDL_FPoint screen_center_f = cam.map_to_screen(screen_center_map);
        SDL_Point screen_center{static_cast<int>(std::lround(screen_center_f.x)),
                                static_cast<int>(std::lround(screen_center_f.y))};
        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        bool drew_indicator = false;
        if (const Global_Light_Source* light = assets_->map_light_source()) {
            SDL_Point start_map = light->get_direction_reference();
            SDL_Point end_map = light->get_direction_target();
            SDL_FPoint start_f = cam.map_to_screen(start_map);
            SDL_FPoint end_f = cam.map_to_screen(end_map);
            SDL_Point start{static_cast<int>(std::lround(start_f.x)), static_cast<int>(std::lround(start_f.y))};
            SDL_Point end{static_cast<int>(std::lround(end_f.x)), static_cast<int>(std::lround(end_f.y))};
            SDL_SetRenderDrawColor(renderer, 220, 32, 32, 230);
            SDL_RenderDrawLine(renderer, start.x, start.y, end.x, end.y);
            SDL_Rect tip{ end.x - 4, end.y - 4, 8, 8 };
            SDL_RenderFillRect(renderer, &tip);
            drew_indicator = true;
        }

        if (!drew_indicator) {
            SDL_SetRenderDrawColor(renderer, 220, 32, 32, 230);
            SDL_RenderDrawLine(renderer, screen_center.x - 6, screen_center.y - 6, screen_center.x + 6, screen_center.y + 6);
            SDL_RenderDrawLine(renderer, screen_center.x - 6, screen_center.y + 6, screen_center.x + 6, screen_center.y - 6);
        }

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }


    if (map_mode_ui_) map_mode_ui_->render(renderer);
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->render(renderer);
    }
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->render(renderer);
    }
    if (trail_suite_) trail_suite_->render(renderer);
    if (frame_editor_session_ && frame_editor_session_->is_active()) {
        // Panels are rendered as part of render(); nothing to do here.
    }
    if (camera_panel_ && camera_panel_->is_visible()) {
        camera_panel_->render(renderer);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->render(renderer);
    }
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();
    // Hide the top dev header when the Layers panel is open so its close button isn't obscured
    const bool hide_headers = modal_headers_hidden_ || layers_panel_open; // ignore sliding windows for header visibility
    asset_filter_.set_header_suppressed(hide_headers);
    if (!hide_headers && !is_modal_blocking_panels()) {
        // Render header and expanded filters (extra Grid panel is rendered inside AssetFilterBar)
        asset_filter_.set_right_accessory_width(0);
        asset_filter_.render(renderer);
    }
}

void DevControls::begin_frame_editor_session(Asset* asset,
                                             std::shared_ptr<animation_editor::AnimationDocument> document,
                                             std::shared_ptr<animation_editor::PreviewProvider> preview,
                                             const std::string& animation_id,
                                             animation_editor::AnimationEditorWindow* host_to_toggle) {
    if (!asset || !assets_ || animation_id.empty()) return;
    if (!frame_editor_session_) frame_editor_session_ = std::make_unique<FrameEditorSession>();
    // Snapshot grid overlay and force ON (non-persistent)
    frame_editor_prev_grid_overlay_ = grid_overlay_enabled_;
    grid_overlay_enabled_ = true;
    // Close AssetInfo panel while Frame Editor is active; remember to reopen on exit.
    frame_editor_prev_asset_info_open_ = false;
    frame_editor_asset_for_reopen_ = nullptr;
    if (room_editor_) {
        if (room_editor_->is_asset_info_editor_open()) {
            frame_editor_prev_asset_info_open_ = true;
            frame_editor_asset_for_reopen_ = asset;
            room_editor_->close_asset_info_editor();
        }
    }
    frame_editor_session_->begin(assets_, asset, std::move(document), std::move(preview), animation_id, host_to_toggle, [this]() {
        // Restore grid overlay when session ends
        this->grid_overlay_enabled_ = this->frame_editor_prev_grid_overlay_;
        // Reopen AssetInfo panel if it was previously open
        if (this->frame_editor_prev_asset_info_open_ && this->room_editor_) {
            this->room_editor_->open_asset_info_editor_for_asset(this->frame_editor_asset_for_reopen_);
        }
        this->frame_editor_prev_asset_info_open_ = false;
        this->frame_editor_asset_for_reopen_ = nullptr;
    });
}

void DevControls::end_frame_editor_session() {
    if (frame_editor_session_) {
        frame_editor_session_->end();
    }
    grid_overlay_enabled_ = frame_editor_prev_grid_overlay_;
}

bool DevControls::is_frame_editor_session_active() const {
    return frame_editor_session_ && frame_editor_session_->is_active();
}

void DevControls::toggle_asset_library() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->toggle_asset_library();
    sync_header_button_states();
}

void DevControls::open_asset_library() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_library();
    sync_header_button_states();
}

void DevControls::close_asset_library() {
    if (room_editor_) room_editor_->close_asset_library();
    sync_header_button_states();
}

bool DevControls::is_asset_library_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_asset_library_open();
}

std::shared_ptr<AssetInfo> DevControls::consume_selected_asset_from_library() {
    if (!can_use_room_editor_ui()) return nullptr;
    return room_editor_->consume_selected_asset_from_library();
}

void DevControls::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_info_editor(info);
}

void DevControls::open_asset_info_editor_for_asset(Asset* asset) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_asset_info_editor_for_asset(asset);
}

void DevControls::close_asset_info_editor() {
    if (room_editor_) room_editor_->close_asset_info_editor();
    // Also close any active in-world Frame Editor session
    end_frame_editor_session();
}

bool DevControls::is_asset_info_editor_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_asset_info_editor_open();
}

void DevControls::finalize_asset_drag(Asset* asset, const std::shared_ptr<AssetInfo>& info) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->finalize_asset_drag(asset, info);
}

void DevControls::toggle_room_config() {
    if (!can_use_room_editor_ui()) return;
    room_editor_->toggle_room_config();
    sync_header_button_states();
}

void DevControls::close_room_config() {
    if (room_editor_) room_editor_->close_room_config();
    sync_header_button_states();
}

bool DevControls::is_room_config_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_room_config_open();
}

void DevControls::begin_area_edit_for_selected_asset(const std::string& area_name) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->begin_area_edit_for_selected_asset(area_name);
}

void DevControls::begin_room_area_edit(const std::string& area_name) {
    // Open AreaOverlayEditor for a room-scoped Area by name in current room
    if (!assets_ || !current_room_) return;
    // Legacy panel removed
    selected_room_area_name_ = area_name;
    hovered_room_area_name_.reset();
    if (!asset_area_editor_) asset_area_editor_ = std::make_unique<AreaOverlayEditor>();
    if (!asset_area_editor_) return;
    asset_area_editor_->attach_assets(assets_);
    asset_area_editor_->set_on_saved([this]() { this->notify_room_area_data_changed(); });
    if (asset_area_editor_->begin_for_room(current_room_, area_name)) {
        if (map_mode_ui_) {
            if (auto* footer = map_mode_ui_->get_footer_bar()) {
                std::string label = std::string("Editing Area: ") + area_name;
                footer->set_title(label);
            }
        }
    }
}

void DevControls::focus_camera_on_asset(Asset* asset, double zoom_factor, int duration_steps) {
    if (!room_editor_) return;
    room_editor_->focus_camera_on_asset(asset, zoom_factor, duration_steps);
}

void DevControls::reset_click_state() {
    if (room_editor_) room_editor_->reset_click_state();
}

void DevControls::clear_selection() {
    if (room_editor_) room_editor_->clear_selection();
    selected_room_area_name_.reset();
    hovered_room_area_name_.reset();
}

void DevControls::purge_asset(Asset* asset) {
    if (!room_editor_) return;
    room_editor_->purge_asset(asset);
}

void DevControls::notify_spawn_group_config_changed(const nlohmann::json& entry) {
    if (room_editor_) {
        room_editor_->handle_spawn_config_change(entry);
    }
}

void DevControls::notify_spawn_group_removed(const std::string& spawn_id) {
    remove_spawn_group_assets(spawn_id);
}

void DevControls::refresh_reactive_shadow_settings() {
    if (map_mode_ui_) {
        map_mode_ui_->refresh_reactive_shadow_settings();
    }
}

void DevControls::clear_reactive_shadow_settings() {
    if (map_mode_ui_) {
        map_mode_ui_->clear_reactive_shadow_settings();
    }
}

const std::vector<Asset*>& DevControls::get_selected_assets() const {
    static std::vector<Asset*> empty;
    if (!can_use_room_editor_ui()) return empty;
    return room_editor_->get_selected_assets();
}

const std::vector<Asset*>& DevControls::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    if (!can_use_room_editor_ui()) return empty;
    return room_editor_->get_highlighted_assets();
}

Asset* DevControls::get_hovered_asset() const {
    if (!can_use_room_editor_ui()) return nullptr;
    return room_editor_->get_hovered_asset();
}

void DevControls::set_zoom_scale_factor(double factor) {
    if (room_editor_) room_editor_->set_zoom_scale_factor(factor);
}

double DevControls::get_zoom_scale_factor() const {
    if (!room_editor_) return 1.0;
    return room_editor_->get_zoom_scale_factor();
}

void DevControls::configure_header_button_sets() {
    if (!map_mode_ui_) return;

    auto make_camera_button = [this]() {
        MapModeUI::HeaderButtonConfig camera_btn;
        camera_btn.id = "camera";
        camera_btn.label = "Camera";
        camera_btn.active = camera_panel_ && camera_panel_->is_visible();
        camera_btn.style_override = &DMStyles::WarnButton();
        camera_btn.active_style_override = &DMStyles::AccentButton();
        camera_btn.on_toggle = [this](bool active) {
            if (room_editor_) {
                room_editor_->close_room_config();
            }
            if (!camera_panel_) {
                sync_header_button_states();
                return;
            }
            camera_panel_->set_assets(assets_);
            if (camera_panel_->is_visible() != active) {
                toggle_camera_panel();
            } else {
                sync_header_button_states();
            }
};
        return camera_btn;
};

    auto make_lighting_button = [this]() {
        MapModeUI::HeaderButtonConfig lights_btn;
        lights_btn.id = "lights";
        lights_btn.label = "Lighting";
        const bool lights_visible = map_mode_ui_ && map_mode_ui_->is_light_panel_visible();
        lights_btn.active = lights_visible;
        lights_btn.style_override = &DMStyles::WarnButton();
        lights_btn.active_style_override = &DMStyles::AccentButton();
        lights_btn.on_toggle = [this](bool active) {
            if (room_editor_) {
                room_editor_->close_room_config();
            }
            if (!map_mode_ui_) {
                sync_header_button_states();
                return;
            }
            const bool currently_open = map_mode_ui_->is_light_panel_visible();
            if (active != currently_open) {
                if (active && !currently_open && is_modal_blocking_panels()) {
                    pulse_modal_header();
                    sync_header_button_states();
                    return;
                }
                map_mode_ui_->toggle_light_panel();
            }
            sync_header_button_states();
};
        return lights_btn;
};

    auto make_layers_button = [this]() {
        MapModeUI::HeaderButtonConfig layers_btn;
        layers_btn.id = "layers";
        layers_btn.label = "Layers";
        const bool layers_visible = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();
        layers_btn.active = layers_visible;
        layers_btn.style_override = &DMStyles::WarnButton();
        layers_btn.active_style_override = &DMStyles::AccentButton();
        layers_btn.on_toggle = [this](bool active) {
            if (room_editor_) {
                room_editor_->close_room_config();
            }
            if (!map_mode_ui_) {
                sync_header_button_states();
                return;
            }
            const bool currently_open = map_mode_ui_->is_layers_panel_visible();
            if (active != currently_open) {
                if (active && !currently_open && is_modal_blocking_panels()) {
                    pulse_modal_header();
                    sync_header_button_states();
                    return;
                }
                if (active) {
                    map_mode_ui_->open_layers_panel();
                } else {
                    map_mode_ui_->toggle_layers_panel();
                }
            } else if (active) {
                map_mode_ui_->open_layers_panel();
            }
            sync_header_button_states();
};
        return layers_btn;
};

    std::vector<MapModeUI::HeaderButtonConfig> map_buttons;
    std::vector<MapModeUI::HeaderButtonConfig> room_buttons;

    map_buttons.push_back(make_camera_button());
    map_buttons.push_back(make_lighting_button());
    map_buttons.push_back(make_layers_button());



    {
        MapModeUI::HeaderButtonConfig map_assets_btn;
        map_assets_btn.id = "map_assets";
        map_assets_btn.label = "Map Assets";
        map_assets_btn.active = (map_assets_modal_ && map_assets_modal_->visible());
        map_assets_btn.on_toggle = [this](bool active) {
            if (active) {
                toggle_map_assets_modal();
            } else {
                if (room_editor_) room_editor_->clear_selection();
                if (map_assets_modal_) map_assets_modal_->close();
            }
            sync_header_button_states();
};
        map_buttons.push_back(std::move(map_assets_btn));
    }

    {
        MapModeUI::HeaderButtonConfig boundary_btn;
        boundary_btn.id = "map_boundary";
        boundary_btn.label = "Boundary Assets";
        boundary_btn.active = (boundary_assets_modal_ && boundary_assets_modal_->visible());
        boundary_btn.on_toggle = [this](bool active) {
            if (active) {
                toggle_boundary_assets_modal();
            } else {
                if (room_editor_) room_editor_->clear_selection();
                if (boundary_assets_modal_) boundary_assets_modal_->close();
            }
            sync_header_button_states();
};
        map_buttons.push_back(std::move(boundary_btn));
    }

    {
        MapModeUI::HeaderButtonConfig trail_btn;
        trail_btn.id = "create_trail";
        trail_btn.label = "New Trail";
        trail_btn.momentary = true;
        trail_btn.style_override = &DMStyles::CreateButton();
        trail_btn.on_toggle = [this](bool) {
            this->create_trail_template();
};
        map_buttons.push_back(std::move(trail_btn));
    }

    room_buttons.push_back(make_camera_button());
    room_buttons.push_back(make_lighting_button());
    room_buttons.push_back(make_layers_button());

    MapModeUI::HeaderButtonConfig room_config_btn;
    room_config_btn.id = "room_config";
    room_config_btn.label = "Room Config";
    room_config_btn.active = room_editor_ && room_editor_->is_room_config_open();
    room_config_btn.on_toggle = [this](bool active) {
        if (!room_editor_) return;
        room_editor_->set_room_config_visible(active);
        sync_header_button_states();
};
    room_buttons.push_back(std::move(room_config_btn));

    MapModeUI::HeaderButtonConfig library_btn;
    library_btn.id = "asset_library";
    library_btn.label = "Asset Library";
    library_btn.active = room_editor_ && room_editor_->is_asset_library_open();
    library_btn.on_toggle = [this](bool active) {
        if (!room_editor_) return;
        room_editor_->close_room_config();
        if (active) {
            room_editor_->open_asset_library();
        } else {
            room_editor_->close_asset_library();
        }
        sync_header_button_states();
};
    room_buttons.push_back(std::move(library_btn));

    MapModeUI::HeaderButtonConfig regenerate_btn;
    regenerate_btn.id = "regenerate";
    regenerate_btn.label = "regen";
    regenerate_btn.momentary = true;
    regenerate_btn.style_override = &DMStyles::DeleteButton();
    regenerate_btn.on_toggle = [this](bool) {
        if (!room_editor_) {
            sync_header_button_states();
            return;
        }
        room_editor_->close_room_config();
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
        open_regenerate_room_popup();
        sync_header_button_states();
};
    room_buttons.push_back(std::move(regenerate_btn));

    // Add Area: open Area Tool immediately (legacy type chooser removed)
    {
        MapModeUI::HeaderButtonConfig add_area_btn;
        add_area_btn.id = "add_area";
        add_area_btn.label = "Add Area";
        add_area_btn.momentary = true;
        add_area_btn.style_override = &DMStyles::CreateButton();
        add_area_btn.on_toggle = [this](bool) {
            if (!assets_ || !current_room_) {
                return;
            }
            // Open the Area Tool directly; no type selection
            this->create_room_area();
        };
        room_buttons.push_back(std::move(add_area_btn));
    }

    map_mode_ui_->set_mode_button_sets(std::move(map_buttons), std::move(room_buttons));
    asset_filter_.ensure_layout();
    sync_header_button_states();
}

void DevControls::sync_header_button_states() {
    if (!map_mode_ui_) return;
    const bool room_config_open = room_editor_ && room_editor_->is_room_config_open();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "room_config", room_config_open);
    const bool library_open = room_editor_ && room_editor_->is_asset_library_open();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "asset_library", library_open);
    const bool camera_open = camera_panel_ && camera_panel_->is_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "camera", camera_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "camera", camera_open);
    const bool lights_open = map_mode_ui_->is_light_panel_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "lights", lights_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "lights", lights_open);
    const bool light_map_open = map_mode_ui_->is_light_map_panel_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "light_map", light_map_open);
    // Grid panel removed
    const bool layers_open = map_mode_ui_->is_layers_panel_visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "layers", layers_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_layers", layers_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "layers", layers_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Room, "regenerate", false);

    const bool map_assets_open = map_assets_modal_ && map_assets_modal_->visible();
    const bool boundary_open = boundary_assets_modal_ && boundary_assets_modal_->visible();
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_assets", map_assets_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "map_boundary", boundary_open);
    map_mode_ui_->set_button_state(MapModeUI::HeaderMode::Map, "create_trail", false);

    if (room_editor_) {
        room_editor_->set_blocking_panel_visible(RoomEditor::BlockingPanel::AssetLibrary, library_open);
        room_editor_->set_blocking_panel_visible(RoomEditor::BlockingPanel::Camera, camera_open);
        room_editor_->set_blocking_panel_visible(RoomEditor::BlockingPanel::Lighting, lights_open);
        room_editor_->set_blocking_panel_visible(RoomEditor::BlockingPanel::MapLayers, layers_open);
    }

    // Legacy Area Mode filters removed; no area filter buttons to sync.
}

void DevControls::close_all_floating_panels() {
    if (room_editor_) {
        room_editor_->close_room_config();
        room_editor_->close_asset_library();
        room_editor_->close_asset_info_editor();
    }
    if (camera_panel_) {
        camera_panel_->close();
    }
    if (map_mode_ui_) {
        map_mode_ui_->close_all_panels();
    }
    if (map_assets_modal_) {
        if (room_editor_) room_editor_->clear_selection();
        map_assets_modal_->close();
    }
    if (boundary_assets_modal_) {
        if (room_editor_) room_editor_->clear_selection();
        boundary_assets_modal_->close();
    }
    if (trail_suite_) {
        trail_suite_->close();
    }
    pending_trail_template_.reset();
    if (regenerate_popup_) {
        regenerate_popup_->close();
    }
    sync_header_button_states();
}

void DevControls::maybe_update_mode_from_zoom() {}

bool DevControls::is_modal_blocking_panels() const {
    return room_editor_ && room_editor_->has_active_modal();
}

void DevControls::pulse_modal_header() {
    if (room_editor_) {
        room_editor_->pulse_active_modal_header();
    }
}

void DevControls::apply_header_suppression() {
    if (map_mode_ui_) {
        // Suppress headers when modals or sliding containers request it
        const bool modal_hide = is_modal_blocking_panels();
        map_mode_ui_->set_headers_suppressed(modal_hide);
        map_mode_ui_->set_dev_sliding_headers_hidden(sliding_headers_hidden_);
    }
}

int DevControls::map_radius_or_default() const {
    if (!assets_) {
        return 1000;
    }
    int radius = 0;
    try {
        const nlohmann::json& map_json = assets_->map_info_json();
        if (map_json.is_object()) {
            const double computed = map_layers::map_radius_from_map_info(map_json);
            if (computed > 0.0) {
                radius = static_cast<int>(std::lround(computed));
            }
        }
    } catch (...) {
        radius = 0;
    }
    if (radius <= 0) {
        const auto& rooms = assets_->rooms();
        for (Room* room : rooms) {
            if (!room || !room->room_area) {
                continue;
            }
            auto [minx, miny, maxx, maxy] = room->room_area->get_bounds();
            int extent = 0;
            extent = std::max(extent, std::abs(minx));
            extent = std::max(extent, std::abs(miny));
            extent = std::max(extent, std::abs(maxx));
            extent = std::max(extent, std::abs(maxy));
            radius = std::max(radius, extent);
        }
    }
    if (radius <= 0) {
        radius = 1000;
    }
    return radius;
}

void DevControls::remove_spawn_group_assets(const std::string& spawn_id) {
    if (!assets_ || spawn_id.empty()) {
        return;
    }
    std::vector<Asset*> to_remove;
    to_remove.reserve(assets_->all.size());
    for (Asset* asset : assets_->all) {
        if (!asset || asset->dead) {
            continue;
        }
        if (asset == assets_->player) {
            continue;
        }
        if (asset->spawn_id == spawn_id) {
            to_remove.push_back(asset);
        }
    }
    for (Asset* asset : to_remove) {
        purge_asset(asset);
        auto& all = assets_->all;
        all.erase(std::remove(all.begin(), all.end(), asset), all.end());
        asset->Delete();
    }
}

void DevControls::integrate_spawned_assets(std::vector<std::unique_ptr<Asset>>& spawned) {
    if (!assets_ || spawned.empty()) {
        return;
    }
    for (auto& uptr : spawned) {
        if (!uptr) {
            continue;
        }
        Asset* raw = uptr.get();
        set_camera_recursive(raw, &assets_->getView());
        set_assets_owner_recursive(raw, assets_);
        raw->finalize_setup();
        assets_->owned_assets.emplace_back(std::move(uptr));
        assets_->all.push_back(raw);
    }
    spawned.clear();
    assets_->initialize_active_assets(assets_->getView().get_screen_center());
    assets_->refresh_active_asset_lists();
    refresh_active_asset_filters();
}

void DevControls::regenerate_map_spawn_group(const nlohmann::json& entry) {
    if (!assets_ || !entry.is_object()) {
        return;
    }
    const std::string spawn_id = entry.value("spawn_id", std::string{});
    if (spawn_id.empty()) {
        return;
    }

    remove_spawn_group_assets(spawn_id);

    std::unordered_map<std::string, std::shared_ptr<AssetInfo>> asset_info_library = assets_->library().all();
    std::vector<std::unique_ptr<Asset>> spawned;
    Check checker(false);
    std::mt19937 rng(std::random_device{}());

    const auto& rooms = assets_->rooms();
    ExactSpawner exact;
    CenterSpawner center;
    RandomSpawner random;
    PerimeterSpawner perimeter;
    EdgeSpawner edge;
    PercentSpawner percent;

    for (Room* room : rooms) {
        if (!room || !room->room_area) {
            continue;
        }
        nlohmann::json& room_json = room->assets_data();
        if (!room_json.is_object()) {
            continue;
        }
        if (!room_json.value("inherits_map_assets", false)) {
            continue;
        }

        nlohmann::json root = nlohmann::json::object();
        root["spawn_groups"] = nlohmann::json::array();
        root["spawn_groups"].push_back(entry);
        std::vector<nlohmann::json> sources{root};
        AssetSpawnPlanner planner(sources, *room->room_area, assets_->library());

        MapGridSettings grid_settings = room->map_grid_settings();
        // Use the spawn-group-specific grid resolution from the modal slider when available
        int resolution = std::max(0, grid_settings.resolution);
        try {
            if (entry.contains("grid_resolution")) {
                resolution = std::max(5, entry.value("grid_resolution", resolution));
            }
        } catch (...) {
            // keep fallback resolution
        }
        resolution = vibble::grid::clamp_resolution(resolution);
        vibble::grid::Grid& grid_service = vibble::grid::global_grid();
        vibble::grid::Occupancy occupancy(*room->room_area, resolution, grid_service);
        checker.begin_session(grid_service, resolution);
        std::vector<Area> exclusion;
        SpawnContext ctx(rng, checker, exclusion, asset_info_library, spawned, &assets_->library(), grid_service, &occupancy);
        ctx.set_map_grid_settings(grid_settings);
        ctx.set_spawn_resolution(resolution);
        std::vector<const Area*> trail_areas;
        auto add_trail_area = [&trail_areas](const Area* candidate, const std::string& type) {
            if (!candidate) return;
            std::string lowered = type;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (lowered == "trail") {
                trail_areas.push_back(candidate);
            }
};
        if (room) {
            if (room->room_area) {
                add_trail_area(room->room_area.get(), room->room_area->get_type());
            }
            for (const auto& named : room->areas) {
                add_trail_area(named.area.get(), named.type);
            }
        }
        ctx.set_trail_areas(std::move(trail_areas));

        const auto& queue = planner.get_spawn_queue();
        const Area* area_ptr = room->room_area.get();
        for (const auto& info : queue) {
            if (info.name == "batch_map_assets") {
                std::vector<double> base_weights;
                base_weights.reserve(info.candidates.size());
                double total_weight = 0.0;
                for (const auto& cand : info.candidates) {
                    double weight = cand.weight;
                    if (weight < 0.0) weight = 0.0;
                    if (weight > 0.0) total_weight += weight;
                    base_weights.push_back(weight);
                }
                if (total_weight <= 0.0 && !base_weights.empty()) {
                    std::fill(base_weights.begin(), base_weights.end(), 1.0);
                }

                auto vertices = occupancy.vertices_in_area(*area_ptr);
                if (vertices.empty()) {
                    continue;
                }

                std::shuffle(vertices.begin(), vertices.end(), ctx.rng());

                for (auto* vertex : vertices) {
                    if (!vertex) continue;
                    SDL_Point spawn_pos{ vertex->world.x, vertex->world.y };
                    spawn_pos = apply_map_grid_jitter(grid_settings, spawn_pos, ctx.rng(), *area_ptr);
                    bool placed = false;
                    std::vector<double> attempt_weights = base_weights;
                    const size_t max_candidate_attempts = info.candidates.size();
                    const bool enforce_spacing = info.check_min_spacing;
                    for (size_t attempt = 0; attempt < max_candidate_attempts; ++attempt) {
                        double weight_total = std::accumulate(attempt_weights.begin(), attempt_weights.end(), 0.0);
                        if (weight_total <= 0.0) break;
                        std::discrete_distribution<size_t> dist(attempt_weights.begin(), attempt_weights.end());
                        size_t idx = dist(ctx.rng());
                        if (idx >= info.candidates.size()) break;
                        if (attempt_weights[idx] <= 0.0) {
                            attempt_weights[idx] = 0.0;
                            continue;
                        }
                        const SpawnCandidate& candidate = info.candidates[idx];
                        if (candidate.is_null || !candidate.info) {
                            occupancy.set_occupied(vertex, true);
                            placed = true;
                            break;
                        }
                        if (ctx.checker().check(candidate.info,
                                                spawn_pos,
                                                ctx.exclusion_zones(),
                                                ctx.all_assets(),
                                                true,
                                                enforce_spacing,
                                                false,
                                                false,
                                                5)) {
                            attempt_weights[idx] = 0.0;
                            continue;
                        }
                        auto* result = ctx.spawnAsset(candidate.name, candidate.info, *area_ptr, spawn_pos, 0, nullptr, info.spawn_id, info.position);
                        if (!result) {
                            attempt_weights[idx] = 0.0;
                            continue;
                        }
                        ctx.checker().register_asset(result, enforce_spacing, true);
                        occupancy.set_occupied(vertex, true);
                        placed = true;
                        break;
                    }
                    if (!placed) {
                        occupancy.set_occupied(vertex, true);
                    }
                }

                continue;
            }
            const std::string& pos = info.position;
            if (pos == "Exact" || pos == "Exact Position") {
                exact.spawn(info, area_ptr, ctx);
            } else if (pos == "Center") {
                center.spawn(info, area_ptr, ctx);
            } else if (pos == "Perimeter") {
                perimeter.spawn(info, area_ptr, ctx);
            } else if (pos == "Edge") {
                edge.spawn(info, area_ptr, ctx);
            } else if (pos == "Percent") {
                percent.spawn(info, area_ptr, ctx);
            } else {
                random.spawn(info, area_ptr, ctx);
            }
        }
        checker.reset_session();
    }

    integrate_spawned_assets(spawned);
}

void DevControls::regenerate_map_grid_assets() {
    if (!map_info_json_ || !map_info_json_->is_object()) {
        return;
    }
    ensure_map_grid_settings(*map_info_json_);
    if (assets_) {
        MapGridSettings settings = MapGridSettings::from_json(&(*map_info_json_)["map_grid_settings"]);
        assets_->apply_map_grid_settings(settings);
    }
    auto section_it = map_info_json_->find("map_assets_data");
    if (section_it == map_info_json_->end() || !section_it->is_object()) {
        return;
    }
    auto groups_it = section_it->find("spawn_groups");
    if (groups_it == section_it->end() || !groups_it->is_array()) {
        return;
    }
    for (const auto& group : *groups_it) {
        regenerate_map_spawn_group(group);
    }
}

void DevControls::regenerate_boundary_spawn_group(const nlohmann::json& entry) {
    if (!assets_ || !entry.is_object()) {
        return;
    }
    const std::string spawn_id = entry.value("spawn_id", std::string{});
    if (spawn_id.empty()) {
        return;
    }

    remove_spawn_group_assets(spawn_id);

    const int radius = map_radius_or_default();
    const int diameter = radius * 2;
    SDL_Point center{radius, radius};
    Area area("map_boundary_regen", center, diameter, diameter, "Circle", 1, diameter, diameter, 3);

    std::vector<Area> exclusion;
    const auto& rooms = assets_->rooms();
    exclusion.reserve(rooms.size());
    for (Room* room : rooms) {
        if (room && room->room_area) {
            exclusion.push_back(*room->room_area);
        }
    }

    AssetSpawner spawner(&assets_->library(), exclusion);
    nlohmann::json root = nlohmann::json::object();
    root["spawn_groups"] = nlohmann::json::array();
    root["spawn_groups"].push_back(entry);
    std::string source = assets_->map_id();
    if (!source.empty()) {
        source += "::map_boundary_data";
    }
    auto spawned = spawner.spawn_boundary_from_json(root, area, source);
    integrate_spawned_assets(spawned);
}

void DevControls::ensure_map_assets_modal_open() {
    if (!assets_) return;
    if (!map_assets_modal_) {
        map_assets_modal_ = std::make_unique<SingleSpawnGroupModal>();
        map_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
        map_assets_modal_->set_floating_stack_key("map_assets_modal");
    } else {
        map_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
    }
    map_assets_modal_->set_on_close([this]() {
        if (room_editor_) room_editor_->clear_selection();
        this->sync_header_button_states();
    });
    auto save = [this]() { return persist_map_info_to_disk(); };
    auto regen = [this](const nlohmann::json& entry) { this->regenerate_map_spawn_group(entry); };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{200, 200, 255, 255};
    map_assets_modal_->open(map_json, "map_assets_data", "batch_map_assets", "Map-wide", color, save, regen);
}

void DevControls::open_map_assets_modal() {
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
    } else {
        ensure_map_assets_modal_open();
    }
    sync_header_button_states();
}

void DevControls::toggle_map_assets_modal() {
    if (map_assets_modal_ && map_assets_modal_->visible()) {
        if (room_editor_) room_editor_->clear_selection();
        map_assets_modal_->close();
    } else {
        ensure_map_assets_modal_open();
    }
    sync_header_button_states();
}

void DevControls::apply_camera_area_render_flag() {
    camera* cam_ptr = nullptr;
    if (camera_override_for_testing_) {
        cam_ptr = camera_override_for_testing_;
    } else if (assets_) {
        cam_ptr = &assets_->getView();
    }

    if (!cam_ptr) {
        return;
    }

    cam_ptr->set_render_areas_enabled(false);

    const bool area_mode = (mode_ == Mode::RoomEditor);
    cam_ptr->set_realism_enabled(!area_mode);
}

void DevControls::set_mode(Mode new_mode) {
    if (mode_ == new_mode) {
        return;
    }
    const Mode previous = mode_;
    mode_ = new_mode;
    switch (mode_) {
    case Mode::RoomEditor:
        asset_filter_.set_active_mode(kModeIdRoom);
        break;
    case Mode::MapEditor:
        asset_filter_.set_active_mode(kModeIdMap);
        break;
    }
    apply_camera_area_render_flag();
}

std::string DevControls::generate_unique_room_area_name(const std::string& base) const {
    std::unordered_set<std::string> used_names;
    if (current_room_) {
        for (const auto& entry : current_room_->areas) {
            used_names.insert(entry.name);
        }
    }

    std::string prefix = base.empty() ? std::string("area") : base;
    const std::string suffix = "_area";
    if (prefix.size() < suffix.size() || prefix.substr(prefix.size() - suffix.size()) != suffix) {
        prefix += suffix;
    }

    std::string candidate = prefix;
    int counter = 1;
    while (used_names.count(candidate) > 0) {
        candidate = prefix + "_" + std::to_string(counter++);
    }
    return candidate;
}

void DevControls::restore_filter_hidden_assets() const {
    for (auto& kv : filter_hidden_assets_) {
        if (Asset* asset = kv.first) {
            asset->set_hidden(kv.second);
        }
    }
    filter_hidden_assets_.clear();
}

void DevControls::ensure_boundary_assets_modal_open() {
    if (!assets_) return;
    if (!boundary_assets_modal_) {
        boundary_assets_modal_ = std::make_unique<SingleSpawnGroupModal>();
        boundary_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
        boundary_assets_modal_->set_floating_stack_key("boundary_assets_modal");
    } else {
        boundary_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
    }
    boundary_assets_modal_->set_on_close([this]() {
        if (room_editor_) room_editor_->clear_selection();
        this->sync_header_button_states();
    });
    auto save = [this]() { return persist_map_info_to_disk(); };
    auto regen = [this](const nlohmann::json& entry) { this->regenerate_boundary_spawn_group(entry); };
    auto& map_json = assets_->map_info_json();
    SDL_Color color{255, 200, 120, 255};
    boundary_assets_modal_->open(map_json, "map_boundary_data", "batch_map_boundary", "Boundary", color, save, regen);
}

void DevControls::open_boundary_assets_modal() {
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->set_screen_dimensions(screen_w_, screen_h_);
    } else {
        ensure_boundary_assets_modal_open();
    }
    sync_header_button_states();
}

void DevControls::toggle_boundary_assets_modal() {
    if (boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        if (room_editor_) room_editor_->clear_selection();
        boundary_assets_modal_->close();
    } else {
        ensure_boundary_assets_modal_open();
    }
    sync_header_button_states();
}

void DevControls::create_trail_template() {
    if (!map_info_json_ || !assets_) {
        if (assets_) {
            assets_->show_dev_notice("Unable to create trail: missing map info");
        }
        sync_header_button_states();
        return;
    }

    nlohmann::json& map_info = *map_info_json_;
    if (!map_info.is_object()) {
        sync_header_button_states();
        return;
    }

    nlohmann::json& trails = map_info["trails_data"];
    if (!trails.is_object()) {
        trails = nlohmann::json::object();
    }

    const std::string base_name = "NewTrail";
    std::string key = base_name;
    int suffix = 1;
    while (trails.contains(key)) {
        key = base_name + std::to_string(suffix++);
    }

    std::vector<SDL_Color> used_colors = utils::display_color::collect(trails);
    SDL_Color display_color = utils::display_color::generate_distinct_color(used_colors);

    nlohmann::json entry = nlohmann::json::object();
    entry["name"] = key;
    entry["geometry"] = "Square";
    entry["min_width"] = 400;
    entry["max_width"] = 400;
    entry["min_height"] = 200;
    entry["max_height"] = 200;
    entry["inherits_map_assets"] = true;
    entry["is_spawn"] = false;
    entry["is_boss"] = false;
    entry["edge_smoothness"] = 8;
    entry["curvyness"] = 4;
    entry["spawn_groups"] = nlohmann::json::array();
    utils::display_color::write(entry, display_color);

    trails[key] = std::move(entry);
    nlohmann::json& inserted = trails[key];

    nlohmann::json* map_assets_section = nullptr;
    auto assets_it = map_info.find("map_assets_data");
    if (assets_it != map_info.end() && assets_it->is_object()) {
        map_assets_section = &(*assets_it);
    }

    const MapGridSettings grid_settings = assets_->map_grid_settings();
    const std::string manifest_context = assets_->map_id();

    pending_trail_template_ = std::make_unique<Room>(Room::Point{0, 0},
                                                     "trail",
                                                     key,
                                                     nullptr,
                                                     manifest_context,
                                                     &assets_->library(),
                                                     nullptr,
                                                     &inserted,
                                                     map_assets_section,
                                                     grid_settings,
                                                     static_cast<double>(map_radius_or_default()),
                                                     "trails_data",
                                                     &map_info,
                                                     &manifest_store_,
                                                     manifest_context,
                                                     Room::ManifestWriter{});

    if (pending_trail_template_) {
        pending_trail_template_->set_manifest_store(&manifest_store_, manifest_context, &map_info);
    }

    if (trail_suite_) {
        trail_suite_->open(pending_trail_template_.get());
    }

    persist_map_info_to_disk();
    if (assets_) {
        assets_->show_dev_notice(std::string("Created trail \"") + key + "\"");
    }
    sync_header_button_states();
}

void DevControls::open_regenerate_room_popup() {
    if (!can_use_room_editor_ui()) return;
    if (!room_editor_ || !current_room_) {
        if (regenerate_popup_) regenerate_popup_->close();
        return;
    }

    std::vector<std::pair<std::string, Room*>> entries;
    entries.reserve(1 + (rooms_ ? rooms_->size() : 0));
    entries.emplace_back(std::string("current room"), current_room_);

    if (rooms_) {
        std::vector<std::pair<std::string, Room*>> other_entries;
        other_entries.reserve(rooms_->size());
        for (Room* room : *rooms_) {
            if (!room || room == current_room_) continue;
            if (!room->room_area) continue;
            if (is_trail_room(room)) {
                continue;
            }
            std::string name = room->room_name.empty() ? std::string("<unnamed>") : room->room_name;
            other_entries.emplace_back(std::move(name), room);
        }

        std::sort(other_entries.begin(), other_entries.end(), [](const auto& a, const auto& b) {
            return to_lower_copy(a.first) < to_lower_copy(b.first);
        });

        entries.insert(entries.end(), other_entries.begin(), other_entries.end());
    }

    if (entries.empty()) {
        if (regenerate_popup_) regenerate_popup_->close();
        return;
    }

    if (!regenerate_popup_) {
        regenerate_popup_ = std::make_unique<RegenerateRoomPopup>();
    }

    regenerate_popup_->open(entries,
                            [this](Room* selected) {
                                if (!room_editor_) return;
                                if (!selected || selected == current_room_) {
                                    room_editor_->regenerate_room();
                                } else {
                                    room_editor_->regenerate_room_from_template(selected);
                                }
                                if (regenerate_popup_) regenerate_popup_->close();
                                sync_header_button_states();
                            },
                            screen_w_,
                            screen_h_);
}

void DevControls::set_room_area_cache_listener(RoomAreaCache::Listener listener) {
    room_area_cache_.set_listener(std::move(listener));
}

std::size_t DevControls::room_area_cache_generation() const {
    return room_area_cache_.generation();
}

void DevControls::notify_room_area_data_changed() {
    room_area_cache_.invalidate();
}

const DevControls::RoomAreaCache::PolygonList& DevControls::room_area_polygons() {
    const nlohmann::json* root = nullptr;
    std::optional<SDL_Point> default_anchor;
    std::optional<std::pair<int, int>> room_dimensions;
    if (current_room_) {
        root = &current_room_->assets_data();
        SDL_Point anchor{ current_room_->map_origin.first, current_room_->map_origin.second };
        if (current_room_->room_area) {
            anchor = current_room_->room_area->get_center();
            auto bounds = current_room_->room_area->get_bounds();
            const int width = std::max(0, std::get<2>(bounds) - std::get<0>(bounds));
            const int height = std::max(0, std::get<3>(bounds) - std::get<1>(bounds));
            if (width > 0 && height > 0) {
                room_dimensions = std::make_pair(width, height);
            }
        } else if (root && root->is_object()) {
            int min_w = root->value("min_width", 0);
            int max_w = root->value("max_width", min_w);
            int min_h = root->value("min_height", 0);
            int max_h = root->value("max_height", min_h);
            int width = std::max(min_w, max_w);
            int height = std::max(min_h, max_h);
            if ((width <= 0 || height <= 0) && root->contains("radius")) {
                int radius = root->value("radius", 0);
                if (radius > 0) {
                    int diameter = radius * 2;
                    if (width <= 0) width = diameter;
                    if (height <= 0) height = diameter;
                }
            }
            if (width > 0 && height > 0) {
                room_dimensions = std::make_pair(width, height);
            }
        }
        default_anchor = anchor;
    }
    const auto& list = room_area_cache_.ensure_from_json(root, default_anchor, room_dimensions);
    auto has_name = [&list](const std::string& name) {
        return std::any_of(list.begin(), list.end(), [&](const RoomAreaCache::Polygon& poly) {
            return poly.name == name;
        });
    };
    if (selected_room_area_name_ && !has_name(*selected_room_area_name_)) {
        selected_room_area_name_.reset();
    }
    if (hovered_room_area_name_ && !has_name(*hovered_room_area_name_)) {
        hovered_room_area_name_.reset();
    }
    return list;
}

void DevControls::create_room_area() {
    if (!assets_ || !current_room_) {
        return;
    }

    std::string area_name = generate_unique_room_area_name("");

    try {
        nlohmann::json& root = current_room_->assets_data();
        if (!root.contains("areas") || !root["areas"].is_array()) {
            root["areas"] = nlohmann::json::array();
        }
        auto& areas = root["areas"];
        bool exists = false;
        for (const auto& entry : areas) {
            if (!entry.is_object()) continue;
            if (entry.value("name", std::string{}) == area_name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            nlohmann::json stub = nlohmann::json::object({
                {"name", area_name},
                {"resolution", 3},
                {"points", nlohmann::json::array()}
            });
            areas.push_back(std::move(stub));
            current_room_->save_assets_json();
            notify_room_area_data_changed();
        }
    } catch (...) {
    }

    selected_room_area_name_ = area_name;
    hovered_room_area_name_.reset();

    if (!asset_area_editor_) {
        asset_area_editor_ = std::make_unique<AreaOverlayEditor>();
    }
    if (asset_area_editor_) {
        asset_area_editor_->attach_assets(assets_);
        asset_area_editor_->set_on_saved([this]() { this->notify_room_area_data_changed(); });
        if (asset_area_editor_->begin_for_room(current_room_, area_name)) {
            if (map_mode_ui_) {
                if (auto* footer = map_mode_ui_->get_footer_bar()) {
                    footer->set_title(std::string("Editing Area: ") + area_name);
                }
            }
        }
    }
}

void DevControls::toggle_map_light_panel() {
    if (!map_mode_ui_) {
        return;
    }
    const bool currently_open = map_mode_ui_->is_light_panel_visible();
    if (!currently_open && is_modal_blocking_panels()) {
        pulse_modal_header();
        sync_header_button_states();
        return;
    }
    map_mode_ui_->toggle_light_panel();
    sync_header_button_states();
}

void DevControls::set_map_light_panel_visible(bool visible) {
    if (!map_mode_ui_) {
        return;
    }
    const bool currently_open = map_mode_ui_->is_light_panel_visible();
    if (visible == currently_open) {
        return;
    }
    if (visible) {
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
        map_mode_ui_->open_light_panel();
    } else {
        map_mode_ui_->close_light_panel();
    }
    sync_header_button_states();
}

bool DevControls::is_map_light_panel_visible() const {
    return map_mode_ui_ && map_mode_ui_->is_light_panel_visible();
}

void DevControls::toggle_camera_panel() {
    if (!camera_panel_) {
        return;
    }
    camera_panel_->set_assets(assets_);
    if (camera_panel_->is_visible()) {
        camera_panel_->close();
    } else {
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }
        camera_panel_->open();
    }
    sync_header_button_states();
}

void DevControls::close_camera_panel() {
    if (camera_panel_) {
        camera_panel_->close();
    }
}

bool DevControls::can_use_room_editor_ui() const {
    return enabled_ && mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled();
}

void DevControls::enter_map_editor_mode() {
    if (!map_editor_) return;
    if (mode_ == Mode::MapEditor) return;

    close_all_floating_panels();
    set_mode(Mode::MapEditor);
    map_editor_->set_input(input_);
    map_editor_->set_rooms(rooms_);
    map_editor_->set_screen_dimensions(screen_w_, screen_h_);
    map_editor_->set_enabled(true);
    if (room_editor_) room_editor_->set_enabled(false, true);
    if (map_mode_ui_) {
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Map);
        map_mode_ui_->set_map_mode_active(true);
    }
    sync_header_button_states();
}

void DevControls::exit_map_editor_mode(bool focus_player, bool restore_previous_state) {
    if (!map_editor_) return;
    if (mode_ != Mode::MapEditor) return;

    const bool camera_was_visible = camera_panel_ && camera_panel_->is_visible();
    close_all_floating_panels();
    map_editor_->exit(focus_player, restore_previous_state);
    if (map_mode_ui_) map_mode_ui_->close_all_panels();
    if (map_mode_ui_) {
        map_mode_ui_->set_map_mode_active(false);
        map_mode_ui_->set_header_mode(MapModeUI::HeaderMode::Room);
    }
    set_mode(Mode::RoomEditor);
    if (room_editor_ && enabled_) {
        room_editor_->set_enabled(true, true);
        room_editor_->set_current_room(current_room_);
    }
    if (camera_was_visible && camera_panel_) {
        camera_panel_->open();
    }
    sync_header_button_states();
}

void DevControls::handle_map_selection() {
    if (!map_editor_) return;
    Room* selected = map_editor_->consume_selected_room();
    if (!selected) return;

    map_editor_->focus_on_room(selected);
    if (is_trail_room(selected)) {
        if (trail_suite_) {
            trail_suite_->open(selected);
        }
        pending_trail_template_.reset();
        return;
    }

    if (trail_suite_) {
        trail_suite_->close();
    }
    pending_trail_template_.reset();

    dev_selected_room_ = selected;
    set_current_room(selected);
    exit_map_editor_mode(false, false);
    if (room_editor_) {
        room_editor_->open_room_config();
    }
}

Room* DevControls::find_spawn_room() const {
    if (!rooms_) return nullptr;
    for (Room* room : *rooms_) {
        if (room && room->is_spawn_room()) {
            return room;
        }
    }
    return nullptr;
}

Room* DevControls::choose_room(Room* preferred) const {
    if (preferred) {
        return preferred;
    }
    if (Room* spawn = find_spawn_room()) {
        return spawn;
    }
    if (!rooms_) {
        return nullptr;
    }
    for (Room* room : *rooms_) {
        if (room && room->room_area) {
            return room;
        }
    }
    return nullptr;
}

void DevControls::filter_active_assets(std::vector<Asset*>& assets) const {
    if (!enabled_) {
        restore_filter_hidden_assets();
        return;
    }

    std::vector<Asset*> filtered_out;
    filtered_out.reserve(assets.size());
    assets.erase(std::remove_if(assets.begin(), assets.end(),
                                [this, &filtered_out](Asset* asset) {
                                    if (!asset) {
                                        return true;
                                    }
                                    if (!passes_asset_filters(asset)) {
                                        filtered_out.push_back(asset);
                                        return true;
                                    }
                                    return false;
                                }),
                 assets.end());

    std::unordered_map<Asset*, bool> next_hidden;
    next_hidden.reserve(filtered_out.size());

    for (Asset* asset : filtered_out) {
        if (!asset) {
            continue;
        }
        bool original_hidden = asset->is_hidden();
        auto it = filter_hidden_assets_.find(asset);
        if (it != filter_hidden_assets_.end()) {
            original_hidden = it->second;
        }
        asset->set_hidden(true);
        asset->set_highlighted(false);
        asset->set_selected(false);
        next_hidden.emplace(asset, original_hidden);
    }

    for (auto& kv : filter_hidden_assets_) {
        Asset* asset = kv.first;
        if (!asset) {
            continue;
        }
        if (next_hidden.find(asset) != next_hidden.end()) {
            continue;
        }
        asset->set_hidden(kv.second);
    }

    filter_hidden_assets_ = std::move(next_hidden);
}

void DevControls::refresh_active_asset_filters() {
    if (!assets_ || !enabled_) {
        return;
    }
    assets_->refresh_filtered_active_assets();
    auto& filtered = assets_->mutable_filtered_active_assets();
    set_active_assets(filtered);
    if (room_editor_) {
        room_editor_->clear_highlighted_assets();
    }
    const auto& active = assets_->getActive();
    for (Asset* asset : active) {
        if (!asset) {
            continue;
        }
        if (!passes_asset_filters(asset)) {
            asset->set_highlighted(false);
            asset->set_selected(false);
        }
    }
}

void DevControls::reset_asset_filters() {
    asset_filter_.reset();
    restore_filter_hidden_assets();
    refresh_active_asset_filters();
}

bool DevControls::passes_asset_filters(Asset* asset) const {
    if (!asset) {
        return false;
    }
    return asset_filter_.passes(*asset);
}

bool DevControls::persist_map_info_to_disk() {
    if (!assets_) {
        std::cerr << "[DevControls] Cannot persist map info: assets manager not set\n";
        return false;
    }
    const std::string map_id = assets_->map_id();
    const bool map_saved = devmode::persist_map_manifest_entry( manifest_store_, map_id, assets_->map_info_json(), std::cerr);
    if (map_saved) {
        manifest_store_.flush();
    }
    return map_saved;
}
