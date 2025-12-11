#include "dev_controls.hpp"

#include <SDL.h>
#include <fstream>
#include <sstream>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <numeric>
#include <limits>

#include "dev_mode/map_editor.hpp"
#include "dev_mode/room_editor.hpp"
#include "dev_mode/map_mode_ui.hpp"
#include "dev_mode/frame_editor_session.hpp"
#include "FloatingDockableManager.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "dev_mode/dev_footer_bar.hpp"
#include "dev_mode/camera_ui.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "dev_mode/foreground_background_effect_panel.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/sdl_pointer_utils.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "utils/log.hpp"
#include "asset/asset_info.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "widgets.hpp"
#include "dev_controls_persistence.hpp"
#include "render/render.hpp"
#include "map_generation/map_layers_geometry.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_types.hpp"
#include "asset/asset_utils.hpp"
#include "core/AssetsManager.hpp"
#include "render/warped_screen_grid.hpp"
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
#include "spawn/spacing_util.hpp"
#include "utils/map_grid_settings.hpp"
#include "spawn/spawn_context.hpp"
#include "utils/area.hpp"
#include "utils/grid.hpp"
#include "utils/grid_occupancy.hpp"
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
#include <limits>
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
        vibble::log::debug(std::string{"[DevMode] "} + message);
    } catch (...) {}
}

constexpr const char* kModeIdRoom = "room";
constexpr const char* kModeIdMap = "map";
constexpr int kPopupOutlineThickness = 1;

constexpr const char* kGridOverlayEnabledKey = "dev.grid.overlay.enabled";
constexpr const char* kGridSnapEnabledKey    = "dev.grid.snap.enabled";
constexpr const char* kGridCellSizePxKey     = "dev.grid.cell_size_px";
constexpr const char* kGridOverlayResolutionKey = "dev.grid.overlay.r";

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
    const bool handled = modal->handle_event(event);
    const bool pointer_inside = pointer_relevant && modal->is_point_inside(pointer.x, pointer.y);
    if (handled && input) {
        if (!pointer_relevant || pointer_inside) {
            input->consumeEvent(event);
        }
    }
    return handled || pointer_inside;
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

    grid_overlay_enabled_ = devmode::ui_settings::load_bool(kGridOverlayEnabledKey, false);
    snap_to_grid_enabled_ = devmode::ui_settings::load_bool(kGridSnapEnabledKey, false);
    const int saved_overlay_r = static_cast<int>(devmode::ui_settings::load_number(kGridOverlayResolutionKey, -1));
    if (saved_overlay_r >= 0) {
        grid_overlay_resolution_user_override_ = true;
        grid_overlay_resolution_r_ = vibble::grid::clamp_resolution(saved_overlay_r);
    } else {
        grid_overlay_resolution_r_ = 0;
    }
    grid_cell_size_px_ = vibble::grid::delta(grid_overlay_resolution_r_);
    room_editor_ = std::make_unique<RoomEditor>(assets_, screen_w_, screen_h_);
    if (room_editor_) {
        room_editor_->set_manifest_store(&manifest_store_);

        room_editor_->set_header_visibility_callback([this](bool visible) {
            sliding_headers_hidden_ = visible;
            apply_header_suppression();
        });
        room_editor_->set_map_assets_panel_callback([this]() { this->open_map_assets_modal(); });
        room_editor_->set_boundary_assets_panel_callback([this]() { this->open_boundary_assets_modal(); });
    }
    map_editor_ = std::make_unique<MapEditor>(assets_);

    map_editor_->set_label_safe_area_provider([this]() -> SDL_Rect {

        SDL_Rect area{0, 0, screen_w_, screen_h_};

        if (!asset_filter_.header_suppressed()) {
            const SDL_Rect header = asset_filter_.header_rect();
            if (header.h > 0) {
                const int safe_top = header.y + header.h;
                if (safe_top < area.y + area.h) {
                    area.h = std::max(0, (area.y + area.h) - safe_top);
                    area.y = safe_top;
                }
            }
        }

        if (map_mode_ui_) {
            if (DevFooterBar* fb = map_mode_ui_->get_footer_bar()) {
                if (fb->visible()) {
                    const SDL_Rect fr = fb->rect();
                    const int safe_bottom = fr.y;
                    if (safe_bottom > area.y) {
                        area.h = std::max(0, safe_bottom - area.y);
                    }
                }
            }
        }
        return area;
    });
    map_mode_ui_ = std::make_unique<MapModeUI>(assets_);
    if (map_mode_ui_) {
        map_mode_ui_->set_manifest_store(&manifest_store_);
    }
    map_grid_regen_cb_ = [this]() { this->regenerate_map_grid_assets(); };
    apply_header_suppression();

    grid_resolution_stepper_ = std::make_unique<DMNumericStepper>("Grid Resolution (r)", 0, vibble::grid::kMaxResolution, grid_overlay_resolution_r_);
    grid_resolution_stepper_->set_on_change([this](int new_r){
        const int clamped_r = vibble::grid::clamp_resolution(new_r);
        if (clamped_r == grid_overlay_resolution_r_) {
            return;
        }
        apply_overlay_grid_resolution(clamped_r, true, false, true);
    });
    if (grid_resolution_stepper_) {
        grid_resolution_stepper_->set_value(grid_overlay_resolution_r_);
    }

    grid_overlay_checkbox_ = std::make_unique<DMCheckbox>("Show Grid", grid_overlay_enabled_);
    camera_panel_ = std::make_unique<CameraUIPanel>(assets_, 72, 72);
    if (camera_panel_) {
        camera_panel_->close();
    }
    image_effect_panel_ = std::make_unique<ForegroundBackgroundEffectPanel>(assets_, 96, 160);
    if (image_effect_panel_) {
        image_effect_panel_->close();
    }
    if (camera_panel_) {
        camera_panel_->set_image_effects_panel_callback([this]() { this->toggle_image_effect_panel(); });
        camera_panel_->set_on_realism_enabled_changed([this](bool enabled) {
            if (map_mode_ui_) {
                DevFooterBar* footer = map_mode_ui_->get_footer_bar();
                if (footer) {
                    footer->set_depth_effects_enabled(enabled);
                }
            }
        });
        camera_panel_->set_on_depth_effects_enabled_changed([this](bool enabled) {
            if (map_mode_ui_) {
                DevFooterBar* footer = map_mode_ui_->get_footer_bar();
                if (footer) {
                    footer->set_depth_effects_enabled(enabled);
                }
            }
        });
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

    if (map_mode_ui_) {
        if (auto* footer = map_mode_ui_->get_footer_bar()) {
            const bool depth_effects_enabled = assets_
                ? assets_->depth_effects_enabled() : devmode::camera_prefs::load_depthcue_enabled();
            footer->set_depth_effects_enabled(depth_effects_enabled);
            footer->set_depth_effects_callbacks([this](bool enabled) {
                auto* cam = camera_override_for_testing_
                    ? camera_override_for_testing_
                    : (assets_ ? &assets_->getView() : nullptr);
                if (assets_) {
                    assets_->set_depth_effects_enabled(enabled);
                    if (cam) {
                        if (!enabled) {
                            if (!depth_effects_forced_realism_disabled_) {
                                depth_effects_prev_realism_enabled_ = cam->realism_enabled();
                                depth_effects_forced_realism_disabled_ = true;
                            }
                            if (cam->realism_enabled()) {
                                cam->set_realism_enabled(false);
                            }
                        } else if (depth_effects_forced_realism_disabled_) {
                            cam->set_realism_enabled(depth_effects_prev_realism_enabled_);
                            depth_effects_forced_realism_disabled_ = false;
                        }
                    }
                    assets_->apply_camera_runtime_settings();
                    if (camera_panel_) {
                        camera_panel_->sync_from_camera();
                    }
                } else {
                    devmode::camera_prefs::save_depthcue_enabled(enabled);
                }
            });

            if (assets_) {
                assets_->set_depth_effects_enabled(true);
                footer->set_depth_effects_enabled(true);
                devmode::camera_prefs::save_depthcue_enabled(true);
            } else {
                devmode::camera_prefs::save_depthcue_enabled(true);
            }
            footer->set_grid_overlay_enabled(grid_overlay_enabled_);
            footer->set_grid_resolution(grid_overlay_resolution_r_);
            footer->set_grid_controls_callbacks(
                [this](bool enabled) {
                    grid_overlay_enabled_ = enabled;
                    devmode::ui_settings::save_bool(kGridOverlayEnabledKey, enabled);
                },
                [this](int resolution, bool from_user) {
                    const int clamped = vibble::grid::clamp_resolution(resolution);
                    if (clamped == grid_overlay_resolution_r_) {
                        return;
                    }
                    apply_overlay_grid_resolution(clamped, from_user, true, false);
                }
            );
        }
    }
    configure_header_button_sets();
    trail_suite_ = std::make_unique<TrailEditorSuite>();
    if (trail_suite_) {
        trail_suite_->set_screen_dimensions(screen_w_, screen_h_);
    }
    asset_filter_.initialize();
    asset_filter_.set_state_changed_callback([this]() { refresh_active_asset_filters(); });

    asset_filter_.set_enabled(enabled_);
    asset_filter_.set_screen_dimensions(screen_w_, screen_h_);
    asset_filter_.set_map_info(map_info_json_);
    asset_filter_.set_current_room(current_room_);

    asset_filter_.set_extra_panel_height(0);
    asset_filter_.set_extra_panel_renderer({});
    asset_filter_.set_extra_panel_event_handler({});
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

    if (map_info_json_) {
        ensure_map_grid_settings(*map_info_json_);
        const nlohmann::json& section = (*map_info_json_)["map_grid_settings"];
        MapGridSettings settings = MapGridSettings::from_json(&section);
        settings.clamp();
        if (!grid_overlay_resolution_user_override_) {
            apply_overlay_grid_resolution(settings.resolution, false, true, true);
        } else {
            apply_overlay_grid_resolution(grid_overlay_resolution_r_, false, true, true);
        }
    } else {
        apply_overlay_grid_resolution(grid_overlay_resolution_r_, false, true, true);
    }
    configure_header_button_sets();
}

void DevControls::apply_overlay_grid_resolution(int resolution, bool user_override, bool update_stepper, bool update_footer) {
    const int clamped = vibble::grid::clamp_resolution(resolution);
    grid_overlay_resolution_r_ = clamped;
    grid_cell_size_px_ = vibble::grid::delta(clamped);
    if (user_override) {
        grid_overlay_resolution_user_override_ = true;
        devmode::ui_settings::save_number(kGridOverlayResolutionKey, clamped);
        devmode::ui_settings::save_number(kGridCellSizePxKey, grid_cell_size_px_);
    }
    if (update_stepper && grid_resolution_stepper_ && grid_resolution_stepper_->value() != clamped) {
        grid_resolution_stepper_->set_value(clamped);
    }
    if (update_footer && map_mode_ui_) {
        if (auto* footer = map_mode_ui_->get_footer_bar()) {
            if (footer->grid_resolution() != clamped) {
                footer->set_grid_resolution(clamped);
            }
        }
    }
    if (frame_editor_session_ && frame_editor_session_->is_active()) {
        frame_editor_session_->set_snap_resolution(clamped);
    }
}

void DevControls::set_player(Asset* player) {
    player_ = player;
    if (room_editor_) room_editor_->set_player(player);
}

void DevControls::set_active_assets(std::vector<Asset*>& actives, std::uint64_t version) {
    active_assets_ = &actives;
    active_assets_version_ = version;
    if (room_editor_) {
        room_editor_->set_active_assets(actives, version);
    }
}

void DevControls::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;

    if (room_editor_) room_editor_->set_screen_dimensions(width, height);
    if (map_editor_) map_editor_->set_screen_dimensions(width, height);
    if (map_mode_ui_) map_mode_ui_->set_screen_dimensions(width, height);

    SDL_Rect bounds{0, 0, screen_w_, screen_h_};
    if (camera_panel_) camera_panel_->set_work_area(bounds);
    if (image_effect_panel_) image_effect_panel_->set_work_area(bounds);
    if (trail_suite_) trail_suite_->set_screen_dimensions(width, height);

    asset_filter_.set_screen_dimensions(width, height);
    if (map_assets_modal_) map_assets_modal_->set_screen_dimensions(width, height);
    if (boundary_assets_modal_) boundary_assets_modal_->set_screen_dimensions(width, height);

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

void DevControls::set_camera_override_for_testing(WarpedScreenGrid* camera_override) {
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
}

bool DevControls::is_pointer_over_dev_ui(int x, int y) const {
    if (camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(x, y)) {
        return true;
    }
    if (image_effect_panel_ && image_effect_panel_->is_visible() && image_effect_panel_->is_point_inside(x, y)) {
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

    asset_filter_.set_enabled(enabled_);

    if (enabled_) {
        const char* msg = "[DevControls] preparing enable flow";
        dev_mode_trace(msg);
        std::cout << msg << "\n";
        WarpedScreenGrid* camera_ptr = assets_ ? &assets_->getView() : nullptr;
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
        apply_dark_mask_visibility();
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
        if (assets_) {
            assets_->set_render_dark_mask_enabled(true);
        }
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
    apply_dark_mask_visibility();

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

    pointer_over_camera_panel_ =
        camera_panel_ && camera_panel_->is_visible() && camera_panel_->is_point_inside(input.getX(), input.getY());
    pointer_over_image_effect_panel_ =
        image_effect_panel_ && image_effect_panel_->is_visible() && image_effect_panel_->is_point_inside(input.getX(), input.getY());
    if (mode_ == Mode::MapEditor) {
        if (map_mode_ui_ && input.wasScancodePressed(SDL_SCANCODE_F8)) {
            map_mode_ui_->toggle_layers_panel();
        }
        if (map_editor_) {
            map_editor_->update(input);
            handle_map_selection();
        }
    } else if (mode_ == Mode::RoomEditor && room_editor_ && room_editor_->is_enabled()) {

        const bool frame_editing = frame_editor_session_ && frame_editor_session_->is_active();
        if (!frame_editing) {
            const bool camera_panel_blocking = camera_panel_ && camera_panel_->is_visible() && (pointer_over_camera_panel_ || pointer_over_image_effect_panel_);
            if (!camera_panel_blocking) {
                room_editor_->update(input);
            }
        } else {
            room_editor_->clear_highlighted_assets();
        }
    }

    if (camera_panel_) {
        camera_panel_->update(input, screen_w_, screen_h_);
    }
    if (image_effect_panel_) {
        image_effect_panel_->update(input, screen_w_, screen_h_);
    }
    if (regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->update(input);
    }
    bool modal_hide = is_modal_blocking_panels();
    modal_headers_hidden_ = modal_hide;
    bool hide_headers = modal_hide;

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

    if (trail_suite_) {
        trail_suite_->update(input);
        if (pending_trail_template_ && !trail_suite_->is_open()) {
            pending_trail_template_.reset();
        }
    }

    asset_filter_.ensure_layout();

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

    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();
    hide_headers = modal_hide || sliding_headers_hidden_ || layers_panel_open;
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

    if (camera_panel_ && camera_panel_->is_blur_section_visible() && assets_ && enabled_) {
        const WarpedScreenGrid& cam = assets_->getView();
        const WarpedScreenGrid::RealismSettings& settings = cam.realism_settings();
        auto clamp_line = [&](float value) -> float {
            if (!std::isfinite(value)) {
                return static_cast<float>(screen_h_) * 0.5f;
            }
            return std::clamp(value, 0.0f, static_cast<float>(screen_h_));
};
        float fg_y = clamp_line(settings.foreground_plane_screen_y);
        float bg_y = clamp_line(settings.background_plane_screen_y);
        int mouse_y = input.getY();
        const int hover_threshold = 5;
        bool hovering_foreground = std::abs(mouse_y - fg_y) < hover_threshold;
        bool hovering_background = std::abs(mouse_y - bg_y) < hover_threshold;
        bool is_top_zone = mouse_y < static_cast<float>(screen_h_) * 0.1f;
        bool is_bottom_zone = mouse_y > static_cast<float>(screen_h_) * 0.9f;
        hover_depthcue_foreground_ = hovering_foreground || (is_bottom_zone && !hovering_background);
        hover_depthcue_background_ = hovering_background || (is_top_zone && !hovering_foreground);
    } else {
        hover_depthcue_foreground_ = false;
        hover_depthcue_background_ = false;
    }

    sync_header_button_states();

    if (frame_editor_session_ && frame_editor_session_->is_active()) {
        frame_editor_session_->update(input);
    }

    if (render_suppression_in_progress_) {
        WarpedScreenGrid* cam = assets_ ? &assets_->getView() : nullptr;
        const bool camera_idle = !cam || !cam->is_zooming();
        if (camera_idle) {
            if (assets_) {
                assets_->set_render_suppressed(false);
            }
            render_suppression_in_progress_ = false;
        }
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
    const bool hide_headers_pre = modal_hide_pre || sliding_headers_hidden_ || layers_panel_open_pre;
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
    const bool keyboard_like_event =
        (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT);
    SDL_Point pointer{0, 0};
    if (pointer_relevant) {
        pointer = event_point(event);
    }

    const bool modal_hide = is_modal_blocking_panels();
    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();
    modal_headers_hidden_ = modal_hide;
    const bool hide_headers = modal_hide || sliding_headers_hidden_ || layers_panel_open;
    asset_filter_.set_enabled(enabled_);
    asset_filter_.set_header_suppressed(hide_headers);
    apply_header_suppression();

    auto consume_if_handled = [&](bool handled, bool pointer_inside) {
        if (handled && input_) {
            if (!pointer_relevant || pointer_inside) {
                input_->consumeEvent(event);
            }
        }
        return handled;
};

    auto handle_floating_panels = [&]() -> bool {
        auto floating = FloatingDockableManager::instance().open_panels();
        if (floating.empty()) {
            return false;
        }
        SDL_Point wheel_point{0, 0};
        bool wheel_point_valid = false;
        for (auto it = floating.rbegin(); it != floating.rend(); ++it) {
            DockableCollapsible* panel = *it;
            if (!panel || !panel->is_visible()) {
                continue;
            }
            bool pointer_inside = false;
            if (pointer_relevant) {
                SDL_Point probe = pointer;
                if (!pointer_event) {
                    if (!wheel_point_valid) {
                        SDL_GetMouseState(&wheel_point.x, &wheel_point.y);
                        wheel_point_valid = true;
                    }
                    probe = wheel_point;
                }
                pointer_inside = panel->is_point_inside(probe.x, probe.y);
            }
            if (consume_if_handled(panel->handle_event(event), pointer_inside)) {
                return true;
            }
            if (pointer_relevant && pointer_inside) {
                return true;
            }
        }
        return false;
};

    if (handle_floating_panels()) {
        return;
    }

    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        if (layers_panel_open && map_mode_ui_) {
            map_mode_ui_->toggle_layers_panel();
            if (input_) {
                input_->consumeEvent(event);
            }
            return;
        }
    }

    if (!asset_filter_.header_suppressed()) {
        const bool pointer_inside_header = pointer_relevant && enabled_ && asset_filter_.contains_point(pointer.x, pointer.y);
        if (pointer_event && consume_if_handled(asset_filter_.handle_event(event), pointer_inside_header)) {
            return;
        }
        if (pointer_inside_header) {
            return;
        }
    }

    if (trail_suite_ && trail_suite_->is_open()) {
        bool pointer_inside_trail = pointer_relevant && trail_suite_->contains_point(pointer.x, pointer.y);
        if (consume_if_handled(trail_suite_->handle_event(event), pointer_inside_trail)) {
            return;
        }
        if (pointer_inside_trail) {
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

    if (map_mode_ui_) {
        if (DevFooterBar* footer = map_mode_ui_->get_footer_bar()) {
            if (footer->visible()) {
                const bool pointer_in_footer = pointer_relevant && footer->contains(pointer.x, pointer.y);
                if (consume_if_handled(footer->handle_event(event), pointer_in_footer)) {
                    return;
                }
                if (pointer_in_footer) {
                    return;
                }
            }
        }
    }

    const bool room_editor_active = can_use_room_editor_ui();
    const bool spawn_panel_visible = room_editor_ && room_editor_->is_spawn_group_panel_visible();
    const bool can_route_room_editor = room_editor_ && (room_editor_active || spawn_panel_visible);
    const bool pointer_over_room_ui = can_route_room_editor && pointer_relevant &&
                                      room_editor_->is_room_ui_blocking_point(pointer.x, pointer.y);

    if (pointer_over_room_ui) {
        const bool handled = room_editor_->handle_sdl_event(event);
        if (handled && input_) {
            input_->consumeEvent(event);
        }
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
    bool pointer_event_inside_image_effect_panel = false;
    if (image_effect_panel_ && image_effect_panel_->is_visible()) {
        switch (event.type) {
        case SDL_MOUSEMOTION:
            pointer_event_inside_image_effect_panel = image_effect_panel_->is_point_inside(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            pointer_event_inside_image_effect_panel = image_effect_panel_->is_point_inside(event.button.x, event.button.y);
            break;
        case SDL_MOUSEWHEEL: {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            pointer_event_inside_image_effect_panel = image_effect_panel_->is_point_inside(mx, my);
            break;
        }
        default:
            break;
        }
    }

    if (camera_panel_ && camera_panel_->is_visible()) {
        if (consume_if_handled(camera_panel_->handle_event(event), pointer_event_inside_camera)) {
            return;
        }
    }
    if (image_effect_panel_ && image_effect_panel_->is_visible()) {
        if (consume_if_handled(image_effect_panel_->handle_event(event), pointer_event_inside_image_effect_panel)) {
            return;
        }
    }

    if (frame_editor_session_ && frame_editor_session_->is_active()) {
        if (consume_if_handled(frame_editor_session_->handle_event(event), pointer_relevant)) {
            return;
        }
    }

    bool block_for_camera = pointer_event_inside_camera;
    if (keyboard_like_event && pointer_over_camera_panel_) {
        block_for_camera = true;
    }
    if (block_for_camera) {
        if (!pointer_relevant && input_) {
            input_->consumeEvent(event);
        }
        return;
    }
    const bool block_image_effect = pointer_event_inside_image_effect_panel ||
        (keyboard_like_event && pointer_over_image_effect_panel_);
    if (block_image_effect) {
        if (!pointer_relevant && input_) {
            input_->consumeEvent(event);
        }
        return;
    }

    if (!pointer_over_room_ui && map_mode_ui_) {
        const bool pointer_inside_map_mode = pointer_relevant && map_mode_ui_->is_point_inside(pointer.x, pointer.y);
        if (consume_if_handled(map_mode_ui_->handle_event(event), pointer_inside_map_mode)) {
            return;
        }
        if (pointer_inside_map_mode) {
            return;
        }
    }

    if (mode_ == Mode::MapEditor) {
        return;
    }

    if (depthcue_drag_state_ == DepthCueDragState::None) {
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
            camera_panel_ && camera_panel_->is_blur_section_visible() && assets_ && enabled_) {
            auto clamp_line = [&](float value) -> float {
                if (!std::isfinite(value)) {
                    return static_cast<float>(screen_h_) * 0.5f;
                }
                return std::clamp(value, 0.0f, static_cast<float>(screen_h_));
};
            if (hover_depthcue_foreground_) {
                depthcue_drag_state_ = DepthCueDragState::Foreground;
                const WarpedScreenGrid::RealismSettings& settings = assets_->getView().realism_settings();
                depthcue_drag_start_y_ = clamp_line(settings.foreground_plane_screen_y);
                depthcue_drag_mouse_start_ = event.button.y;
                if (input_) {
                    input_->consumeEvent(event);
                }
                return;
            } else if (hover_depthcue_background_) {
                depthcue_drag_state_ = DepthCueDragState::Background;
                const WarpedScreenGrid::RealismSettings& settings = assets_->getView().realism_settings();
                depthcue_drag_start_y_ = clamp_line(settings.background_plane_screen_y);
                depthcue_drag_mouse_start_ = event.button.y;
                if (input_) {
                    input_->consumeEvent(event);
                }
                return;
            }
        }
    } else {
        if (event.type == SDL_MOUSEMOTION) {
            int delta_y = event.motion.y - depthcue_drag_mouse_start_;
            float new_y = depthcue_drag_start_y_ + delta_y;
            if (assets_) {
                WarpedScreenGrid& cam = assets_->getView();
                WarpedScreenGrid::RealismSettings new_settings = cam.realism_settings();
                if (depthcue_drag_state_ == DepthCueDragState::Foreground) {
                    new_settings.foreground_plane_screen_y = new_y;
                } else if (depthcue_drag_state_ == DepthCueDragState::Background) {
                    new_settings.background_plane_screen_y = new_y;
                }
                cam.set_realism_settings(new_settings);
                assets_->apply_camera_runtime_settings();
            }
        } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            depthcue_drag_state_ = DepthCueDragState::None;
        }
        if (depthcue_drag_state_ != DepthCueDragState::None) {
            if (input_) {
                input_->consumeEvent(event);
            }
            return;
        }
    }

    if (!(frame_editor_session_ && frame_editor_session_->is_active()) && can_route_room_editor &&
        (camera_panel_->is_visible() || keyboard_like_event)) {
        const bool handled = room_editor_ && room_editor_->handle_sdl_event(event);
        if (handled && input_) {
            const bool pointer_inside_room_ui = pointer_relevant && room_editor_ && room_editor_->is_room_ui_blocking_point(pointer.x, pointer.y);
            if (!pointer_relevant || pointer_inside_room_ui) {
                input_->consumeEvent(event);
            }
        }
        if (handled) {
            return;
        }
    }
}

void DevControls::render_overlays(SDL_Renderer* renderer) {
    if (!enabled_) return;

    const bool layers_panel_open = map_mode_ui_ && map_mode_ui_->is_layers_panel_visible();

    const bool hide_headers = modal_headers_hidden_ || sliding_headers_hidden_ || layers_panel_open;
    asset_filter_.set_header_suppressed(hide_headers);

    if (!renderer) {
        return;
    }

    auto floor_warped_screen_position = [&](const WarpedScreenGrid& c, SDL_Point w) {
        SDL_FPoint linear = c.map_to_screen(w);
        float warped_y = c.warp_floor_screen_y(static_cast<float>(w.y), linear.y);
        return SDL_FPoint{linear.x, warped_y};
};

    const bool show_depth_guides = camera_panel_ && camera_panel_->is_depth_section_visible();
    std::optional<float> horizon_screen_y;
    std::optional<std::string> parallax_probe_label;

        const bool need_grid_helpers = assets_ && (grid_overlay_enabled_ || show_depth_guides);
        if (renderer && need_grid_helpers) {
            const WarpedScreenGrid& cam = assets_->getView();
            const WarpedScreenGrid::FloorDepthParams depth_params = cam.compute_floor_depth_params();
            world::WorldGrid& grid = assets_->world_grid();

            auto parallax_offset = [&](SDL_Point w) { return 0.0f; };

        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        if (grid_overlay_enabled_) {
            SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        }

        SDL_Color minor{0, 255, 255, 48};
        SDL_Color major{0, 255, 255, 80};

        SDL_FPoint top_left_world = cam.screen_to_map(SDL_Point{0, 0});
        SDL_FPoint bottom_right_world = cam.screen_to_map(SDL_Point{screen_w_, screen_h_});
        const float cam_scale = std::max(0.0001f, cam.get_scale());

        int cell = std::max(1, grid_cell_size_px_);
        if (cell > 0) {
            const float world_padding = static_cast<float>(cell) * 4.0f;
            const float depth_world_padding = cam_scale * std::max(0.0f, cam.current_depth_offset_px());
            const float min_world_x = std::min(top_left_world.x, bottom_right_world.x) - world_padding;
            const float max_world_x = std::max(top_left_world.x, bottom_right_world.x) + world_padding;
            const float min_world_y = std::min(top_left_world.y, bottom_right_world.y) - world_padding - depth_world_padding * 0.5f;
            const float max_world_y = std::max(top_left_world.y, bottom_right_world.y) + world_padding + depth_world_padding;

            if (depth_params.enabled) {
                horizon_screen_y = static_cast<float>(depth_params.horizon_screen_y);
            }

            const int major_interval = 8;
            const int samples_per_line = 32;
            const float mid_world_x = (min_world_x + max_world_x) * 0.5f;

            float start_x = std::floor(min_world_x / cell) * cell;
            bool have_horizon_x = false;
            float best_horizon_x = 0.0f;
            const float screen_center_x = static_cast<float>(screen_w_) * 0.5f;
            for (float x = start_x; x <= max_world_x + cell; x += cell) {
                std::vector<SDL_Point> polyline;
                polyline.reserve(static_cast<std::size_t>(samples_per_line + 1));
                for (int s = 0; s <= samples_per_line; ++s) {
                    const float t = static_cast<float>(s) / static_cast<float>(samples_per_line);
                    const float wy = min_world_y + (max_world_y - min_world_y) * t;
                    SDL_Point world_point{
                        static_cast<int>(std::lround(x)), static_cast<int>(std::lround(wy)) };
                    SDL_FPoint screen = floor_warped_screen_position(cam, world_point);
                    polyline.push_back(SDL_Point{
                        static_cast<int>(std::lround(screen.x)),
                        static_cast<int>(std::lround(screen.y))
                    });
                }
                if (grid_overlay_enabled_ && polyline.size() >= 2) {
                    const bool is_major = (static_cast<long long>(std::llround(x)) % (cell * major_interval) == 0);
                    SDL_Color c = is_major ? major : minor;
                    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                    SDL_RenderDrawLines(renderer, polyline.data(), static_cast<int>(polyline.size()));
                }

                if (depth_params.enabled && horizon_screen_y) {
                    const float hy = *horizon_screen_y;

                    for (size_t i = 1; i < polyline.size(); ++i) {
                        const float y0 = static_cast<float>(polyline[i-1].y);
                        const float y1 = static_cast<float>(polyline[i].y);
                        if ((y0 <= hy && hy <= y1) || (y1 <= hy && hy <= y0)) {
                            const float x0 = static_cast<float>(polyline[i-1].x);
                            const float x1 = static_cast<float>(polyline[i].x);
                            if (std::fabs(y1 - y0) > 1e-6f) {
                                const float t = (hy - y0) / (y1 - y0);
                                const float ix = x0 + t * (x1 - x0);
                                const float dist = std::fabs(ix - screen_center_x);
                                if (!have_horizon_x || dist < std::fabs(best_horizon_x - screen_center_x)) {
                                    have_horizon_x = true;
                                    best_horizon_x = ix;
                                }
                            }
                        }
                    }
                }
            }

            if (have_horizon_x) {
                const int xi = static_cast<int>(std::lround(best_horizon_x));
                SDL_BlendMode prev_mode2 = SDL_BLENDMODE_NONE;
                SDL_GetRenderDrawBlendMode(renderer, &prev_mode2);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 255, 140, 0, 220);
                SDL_RenderDrawLine(renderer, xi, 0, xi, screen_h_);
                SDL_SetRenderDrawBlendMode(renderer, prev_mode2);
            }

            float start_y = std::floor(max_world_y / cell) * cell;
            float highest_horizontal_screen_y = std::numeric_limits<float>::infinity();
            for (float y = start_y; y >= min_world_y - cell; y -= cell) {
                SDL_Point sample_world{
                    static_cast<int>(std::lround(mid_world_x)), static_cast<int>(std::lround(y)) };
                SDL_FPoint sample_screen = floor_warped_screen_position(cam, sample_world);
                const float screen_y = sample_screen.y;
                if (std::isfinite(screen_y)) {
                    highest_horizontal_screen_y = std::min(highest_horizontal_screen_y, screen_y);
                }

                std::vector<SDL_Point> polyline;
                polyline.reserve(static_cast<std::size_t>(samples_per_line + 1));
                for (int s = 0; s <= samples_per_line; ++s) {
                    const float t = static_cast<float>(s) / static_cast<float>(samples_per_line);
                    const float wx = min_world_x + (max_world_x - min_world_x) * t;
                    SDL_Point world_point{
                        static_cast<int>(std::lround(wx)), static_cast<int>(std::lround(y)) };
                    SDL_FPoint screen = floor_warped_screen_position(cam, world_point);
                    polyline.push_back(SDL_Point{
                        static_cast<int>(std::lround(screen.x)),
                        static_cast<int>(std::lround(screen.y))
                    });
                }
                if (grid_overlay_enabled_ && polyline.size() >= 2) {
                    const bool is_major = (static_cast<long long>(std::llround(y)) % (cell * major_interval) == 0);
                    SDL_Color c = is_major ? major : minor;
                    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                    SDL_RenderDrawLines(renderer, polyline.data(), static_cast<int>(polyline.size()));
                }
            }

            if (grid_overlay_enabled_ && horizon_screen_y) {
                const float hy = *horizon_screen_y;
                const bool already_at_horizon =
                    std::isfinite(highest_horizontal_screen_y) && std::fabs(highest_horizontal_screen_y - hy) < 0.5f;
                if (!already_at_horizon) {
                    SDL_Color c = major;
                    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                    const int yi = static_cast<int>(std::lround(hy));
                    SDL_RenderDrawLine(renderer, 0, yi, screen_w_, yi);
                }
            }

            if (depth_params.enabled) {

                horizon_screen_y = static_cast<float>(depth_params.horizon_screen_y);
            }

            if (grid_overlay_enabled_ && cam.parallax_enabled()) {
                const int sample_dx = std::max(cell * 2, 64);
                const int sample_dy = std::max(cell * 3, 96);
                const SDL_FPoint view_center = cam.get_view_center_f();
                const double anchor_y = cam.current_anchor_world_y();
                const int sample_x = static_cast<int>(std::lround( std::clamp(view_center.x + static_cast<float>(sample_dx), min_world_x, max_world_x)));
                const double clamped_anchor_y = std::clamp(anchor_y, static_cast<double>(min_world_y), static_cast<double>(max_world_y));
                const SDL_Point anchor_sample{
                    sample_x,
                    static_cast<int>(std::lround(clamped_anchor_y)) };
                const SDL_Point above_sample{
                    sample_x,
                    static_cast<int>(std::lround(std::clamp(clamped_anchor_y - static_cast<double>(sample_dy), static_cast<double>(min_world_y), static_cast<double>(max_world_y)))) };
                const SDL_Point below_sample{
                    sample_x,
                    static_cast<int>(std::lround(std::clamp(clamped_anchor_y + static_cast<double>(sample_dy), static_cast<double>(min_world_y), static_cast<double>(max_world_y)))) };

                const float parallax_anchor = parallax_offset(anchor_sample);
                const float parallax_above  = parallax_offset(above_sample);
                const float parallax_below  = parallax_offset(below_sample);
                if (std::isfinite(parallax_anchor) && std::isfinite(parallax_above) && std::isfinite(parallax_below)) {
                    char buffer[192];
                    std::snprintf(buffer, sizeof(buffer), "Parallax probe dx=+%d | above %.1f px  anchor %.1f px  below %.1f px", sample_dx, parallax_above, parallax_anchor, parallax_below);
                    parallax_probe_label = std::string(buffer);
                }
            }
        }

        if (grid_overlay_enabled_) {
            SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
            SDL_SetRenderDrawBlendMode(renderer, prev_mode);
        }
    }

    if (renderer && grid_overlay_enabled_ && parallax_probe_label) {
        DMLabelStyle style = DMStyles::Label();
        const int text_x = DMSpacing::panel_padding();
        const int text_y = screen_h_ - style.font_size - DMSpacing::panel_padding();
        DrawLabelText(renderer, *parallax_probe_label, text_x, text_y, style);
    }

    if (renderer && camera_panel_ && camera_panel_->is_visible() && assets_) {
        const WarpedScreenGrid& cam = assets_->getView();
        const WarpedScreenGrid::FloorDepthParams depth_params = cam.compute_floor_depth_params();
        if (depth_params.enabled) {
            SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
            Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
            SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);

            const world::WorldGrid& grid = assets_->world_grid();
            SDL_FPoint center_world_f = cam.get_view_center_f();
            SDL_Point depth_world{
                static_cast<int>(std::lround(center_world_f.x)), static_cast<int>(std::lround(depth_params.base_world_y)) };
            SDL_FPoint depth_screen = floor_warped_screen_position(cam, depth_world);
            const int y_line = static_cast<int>(std::lround(depth_screen.y));

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 160, 210, 255, 200);
            SDL_RenderDrawLine(renderer, 0, y_line, screen_w_, y_line);
            const int marker_x = screen_w_ / 2;
            SDL_RenderDrawLine(renderer, marker_x - 8, y_line, marker_x + 8, y_line);
            DMLabelStyle style = DMStyles::Label();
            style.color = SDL_Color{160, 210, 255, 200};
            const int label_y = std::max(0, y_line - style.font_size - 2);
            DrawLabelText(renderer, "Depth", marker_x + 12, label_y, style);

            SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
            SDL_SetRenderDrawBlendMode(renderer, prev_mode);
        }
    }

    if (renderer && show_depth_guides) {
        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);

        auto draw_labeled_line = [&](float y, SDL_Color color, const char* label) {
            const int yi = static_cast<int>(std::lround(y));
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_RenderDrawLine(renderer, 0, yi, screen_w_, yi);
            DMLabelStyle style = DMStyles::Label();
            style.color = color;
            const int label_y = std::max(0, yi - style.font_size - 2);
            DrawLabelText(renderer, label, 8, label_y, style);
};

        if (horizon_screen_y) {
            draw_labeled_line(*horizon_screen_y, SDL_Color{255, 140, 0, 220}, "Horizon");
        }

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }

    if (renderer && mode_ == Mode::MapEditor) {
        if (map_editor_) map_editor_->render(renderer);
    } else if (renderer && mode_ == Mode::RoomEditor && room_editor_) {
        room_editor_->render_overlays(renderer);

        if (frame_editor_session_ && frame_editor_session_->is_active()) {
            frame_editor_session_->render(renderer);
        }
    }
    if (renderer && map_mode_ui_ && map_mode_ui_->is_light_panel_visible() && assets_) {
        const WarpedScreenGrid& cam = assets_->getView();
        SDL_Point screen_center_map = cam.get_screen_center();
        SDL_FPoint screen_center_f = cam.map_to_screen(screen_center_map);
        SDL_Point screen_center{static_cast<int>(std::lround(screen_center_f.x)),
                                static_cast<int>(std::lround(screen_center_f.y))};
        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(renderer, 220, 32, 32, 230);
        SDL_RenderDrawLine(renderer, screen_center.x - 6, screen_center.y - 6, screen_center.x + 6, screen_center.y + 6);
        SDL_RenderDrawLine(renderer, screen_center.x - 6, screen_center.y + 6, screen_center.x + 6, screen_center.y - 6);

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }

    if (renderer && camera_panel_ && camera_panel_->is_blur_section_visible() && assets_ && screen_w_ > 0 && screen_h_ > 0) {
        const WarpedScreenGrid& cam = assets_->getView();
        const WarpedScreenGrid::RealismSettings& settings = cam.realism_settings();
        SDL_FPoint center_world_f = cam.get_view_center_f();
        SDL_FPoint center_screen_f = cam.map_to_screen_f(center_world_f);
        float center_y = std::isfinite(center_screen_f.y) ? std::clamp(center_screen_f.y, 0.0f, static_cast<float>(screen_h_)) : static_cast<float>(screen_h_) * 0.5f;
        auto clamp_line = [&](float value) {
            if (!std::isfinite(value)) {
                return center_y;
            }
            return std::clamp(value, 0.0f, static_cast<float>(screen_h_));
};
        const float bg_line = clamp_line(settings.background_plane_screen_y);
        const float fg_line = clamp_line(settings.foreground_plane_screen_y);

        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const SDL_Color accent = DMStyles::AccentButton().hover_bg;
        SDL_Color fg_color = dm_draw::LightenColor(accent, 0.2f);
        fg_color.a = 220;
        SDL_Color bg_color = dm_draw::LightenColor(accent, 0.05f);
        bg_color.a = 220;
        SDL_Color center_color = DMStyles::AccentButton().text;
        center_color.a = 230;

        DMLabelStyle base_label = DMStyles::Label();
        base_label.font_size = std::max(12, base_label.font_size - 2);

        auto draw_line = [&](float y, const SDL_Color& color, bool is_hover_or_drag = false) {
            const int yi = static_cast<int>(std::lround(y));
            SDL_Color actual_color = is_hover_or_drag ? SDL_Color{255, 255, 255, 220} : color;
            SDL_SetRenderDrawColor(renderer, actual_color.r, actual_color.g, actual_color.b, actual_color.a);
            SDL_RenderDrawLine(renderer, 0, yi, screen_w_, yi);
};
        auto draw_label = [&](float line_y, const SDL_Color& color, const std::string& text) {
            DMLabelStyle style = base_label;
            style.color = color;
            const int yi = static_cast<int>(std::lround(line_y));
            int text_y = yi - style.font_size - DMSpacing::small_gap();
            if (text_y < 0) {
                text_y = yi + DMSpacing::small_gap();
            }
            DrawLabelText(renderer, text, DMSpacing::panel_padding(), text_y, style);
};
        auto make_depthcue_label = [](const char* prefix, int opacity_max) {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), "%s Max Opacity: %d / 255", prefix, opacity_max);
            return std::string(buffer);
};

        draw_line(bg_line, bg_color, hover_depthcue_background_ || (depthcue_drag_state_ == DepthCueDragState::Background));
        {
            const int bg_opacity = settings.background_texture_max_opacity;
            draw_label(bg_line, bg_color, make_depthcue_label("BG", bg_opacity));
        }

        draw_line(center_y, center_color);
        draw_label(center_y, center_color, "Base Layer");

        draw_line(fg_line, fg_color, hover_depthcue_foreground_ || (depthcue_drag_state_ == DepthCueDragState::Foreground));
        {
            const int fg_opacity = settings.foreground_texture_max_opacity;
            draw_label(fg_line, fg_color, make_depthcue_label("FG", fg_opacity));
        }

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }

    if (renderer && camera_panel_ && camera_panel_->is_visible() && assets_) {
        const WarpedScreenGrid& cam = assets_->getView();
        SDL_FPoint center_world_f = cam.get_view_center_f();
        SDL_FPoint center_screen_f = cam.map_to_screen_f(center_world_f);
        const int cx = static_cast<int>(std::lround(center_screen_f.x));
        const int cy = static_cast<int>(std::lround(center_screen_f.y));

        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        Uint8 pr = 0, pg = 0, pb = 0, pa = 0;
        SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const SDL_Color c = DMStyles::AccentButton().hover_bg;
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 230);

        constexpr int arm = 8;
        constexpr int thickness = 3;
        const int offset_start = -thickness / 2;
        const int offset_end   =  thickness / 2;
        for (int o = offset_start; o <= offset_end; ++o) {
            SDL_RenderDrawLine(renderer, cx - arm, cy + o, cx + arm, cy + o);
            SDL_RenderDrawLine(renderer, cx + o, cy - arm, cx + o, cy + arm);
        }

        SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }

    if (renderer && map_mode_ui_) map_mode_ui_->render(renderer);
    if (renderer && map_assets_modal_ && map_assets_modal_->visible()) {
        map_assets_modal_->render(renderer);
    }
    if (renderer && boundary_assets_modal_ && boundary_assets_modal_->visible()) {
        boundary_assets_modal_->render(renderer);
    }
    if (renderer && trail_suite_) trail_suite_->render(renderer);
    if (frame_editor_session_ && frame_editor_session_->is_active()) {

    }
    if (renderer && camera_panel_ && camera_panel_->is_visible()) {
        camera_panel_->render(renderer);
    }
    if (renderer && image_effect_panel_ && image_effect_panel_->is_visible()) {
        image_effect_panel_->render(renderer);
    }
    if (renderer && regenerate_popup_ && regenerate_popup_->visible()) {
        regenerate_popup_->render(renderer);
    }
    if (renderer && !hide_headers && !is_modal_blocking_panels()) {

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
    frame_editor_session_->set_snap_resolution(grid_overlay_resolution_r_);

    frame_editor_prev_grid_overlay_ = grid_overlay_enabled_;
    grid_overlay_enabled_ = true;

    frame_editor_prev_asset_info_open_ = false;
    frame_editor_asset_for_reopen_ = nullptr;
    const bool launched_from_animation_editor = (host_to_toggle != nullptr);
    bool asset_info_was_open = false;
    if (room_editor_) {
        asset_info_was_open = room_editor_->is_asset_info_editor_open();
        if (asset_info_was_open) {
            room_editor_->close_asset_info_editor();
        }
    }
    frame_editor_prev_asset_info_open_ = asset_info_was_open || launched_from_animation_editor;
    if (frame_editor_prev_asset_info_open_) {
        frame_editor_asset_for_reopen_ = asset;
    }
    frame_editor_session_->begin(assets_, asset, std::move(document), std::move(preview), animation_id, host_to_toggle, [this]() {

        this->grid_overlay_enabled_ = this->frame_editor_prev_grid_overlay_;

        if (this->frame_editor_prev_asset_info_open_ && this->room_editor_ && this->frame_editor_asset_for_reopen_) {
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

void DevControls::open_animation_editor_for_asset(const std::shared_ptr<AssetInfo>& info) {
    if (!can_use_room_editor_ui()) return;
    room_editor_->open_animation_editor_for_asset(info);
}

void DevControls::close_asset_info_editor() {
    if (room_editor_) room_editor_->close_asset_info_editor();

    end_frame_editor_session();
}

bool DevControls::is_asset_info_editor_open() const {
    if (!room_editor_) return false;
    return room_editor_->is_asset_info_editor_open();
}

bool DevControls::is_asset_info_lighting_section_expanded() const {
    return lighting_section_forces_dark_mask();
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

void DevControls::focus_camera_on_asset(Asset* asset, double zoom_factor, int duration_steps) {
    if (!room_editor_) return;
    room_editor_->focus_camera_on_asset(asset, zoom_factor, duration_steps);
}

void DevControls::reset_click_state() {
    if (room_editor_) room_editor_->reset_click_state();
}

void DevControls::clear_selection() {
    if (room_editor_) room_editor_->clear_selection();
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

    Asset::ClearFlipOverrideForSpawnId(spawn_id);
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

        room_editor_->set_blocking_panel_visible(RoomEditor::BlockingPanel::Lighting, lights_open);
        room_editor_->set_blocking_panel_visible(RoomEditor::BlockingPanel::MapLayers, layers_open);
    }

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
    if (image_effect_panel_) {
        image_effect_panel_->close();
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
        if (asset) {
            asset->Delete();
            (void)assets_->world_grid().remove_asset(asset);
        }
    }
    assets_->rebuild_from_grid_state();
    assets_->refresh_active_asset_lists();
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
        raw = assets_->world_grid().create_asset_at_point(std::move(uptr));
        if (raw) {
            assets_->all.push_back(raw);
        }
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

    const auto& asset_info_library = assets_->library().all();
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

        int resolution = std::max(0, grid_settings.resolution);
        try {
            if (entry.contains("grid_resolution")) {
                resolution = std::max(5, entry.value("grid_resolution", resolution));
            }
        } catch (...) {

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
        ctx.set_spacing_filter(collect_spacing_asset_names(queue));
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
                        const bool track_spacing = ctx.track_spacing_for(result->info, enforce_spacing);
                        ctx.checker().register_asset(result, enforce_spacing, track_spacing);
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
    WarpedScreenGrid* cam_ptr = nullptr;
    if (camera_override_for_testing_) {
        cam_ptr = camera_override_for_testing_;
    } else if (assets_) {
        cam_ptr = &assets_->getView();
    }

    if (!cam_ptr) {
        return;
    }

    cam_ptr->set_render_areas_enabled(false);
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

void DevControls::toggle_image_effect_panel() {
    if (!image_effect_panel_) {
        image_effect_panel_ = std::make_unique<ForegroundBackgroundEffectPanel>(assets_, 96, 160);
        image_effect_panel_->close();
    }
    if (image_effect_panel_->is_visible()) {
        image_effect_panel_->set_close_callback({});
        image_effect_panel_->close();
    } else {
        if (is_modal_blocking_panels()) {
            pulse_modal_header();
            sync_header_button_states();
            return;
        }

        if (camera_panel_ && camera_panel_->is_visible()) {
            camera_panel_->close();
        }
        image_effect_panel_->set_assets(assets_);

        image_effect_panel_->set_close_callback([this]() {
            if (camera_panel_) {
                camera_panel_->open();
            }
        });
        image_effect_panel_->open();
    }
    sync_header_button_states();
}

void DevControls::close_image_effect_panel() {
    if (image_effect_panel_) {
        image_effect_panel_->close();
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

    if (assets_) {
        assets_->set_render_suppressed(true);
        render_suppression_in_progress_ = true;
    }
    if (assets_) {
        WarpedScreenGrid* cam = &assets_->getView();
        if (cam && selected && selected->room_area) {
            const SDL_Point center = selected->room_area->get_center();
            const double current_scale = std::max(0.0001, static_cast<double>(cam->get_scale()));
            const double target_scale  = cam->default_zoom_for_room(selected);
            const double factor = (target_scale > 0.0) ? (target_scale / current_scale) : 1.0;
            const int duration_steps = 30;
            cam->pan_and_zoom_to_point(center, factor, duration_steps);
        }
    }
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
    set_active_assets(filtered, assets_->dev_active_state_version());
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
    apply_dark_mask_visibility();
}

void DevControls::apply_dark_mask_visibility() {
    if (!assets_) {
        return;
    }
    const bool force_dark_mask = lighting_section_forces_dark_mask();
    const bool should_render = asset_filter_.render_dark_mask_enabled() || force_dark_mask;
    assets_->set_render_dark_mask_enabled(should_render);
}

bool DevControls::lighting_section_forces_dark_mask() const {
    if (!enabled_) {
        return false;
    }
    if (mode_ != Mode::RoomEditor) {
        return false;
    }
    if (!room_editor_ || !room_editor_->is_enabled()) {
        return false;
    }
    return room_editor_->is_asset_info_lighting_section_expanded();
}

bool DevControls::should_hide_assets_for_map_mode() const {
    if (!enabled_) {
        return false;
    }
    if (mode_ != Mode::MapEditor) {
        return false;
    }
    const bool map_assets_open = map_assets_modal_ && map_assets_modal_->visible();
    const bool boundary_open = boundary_assets_modal_ && boundary_assets_modal_->visible();
    return !(map_assets_open || boundary_open);
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
    if (should_hide_assets_for_map_mode()) {
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
