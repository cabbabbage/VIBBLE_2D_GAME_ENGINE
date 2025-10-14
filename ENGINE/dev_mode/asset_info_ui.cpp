#include "asset_info_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <SDL_log.h>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>
#include <functional>

#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"
#include "widgets.hpp"
#include "tag_utils.hpp"

#include "DockableCollapsible.hpp"
#include "SlidingWindowContainer.hpp"
#include "dm_styles.hpp"
#include "asset_sections/Section_BasicInfo.hpp"
#include "asset_sections/Section_Tags.hpp"
#include "asset_sections/Section_Lighting.hpp"
#include "asset_sections/Section_Shading.hpp"
#include "asset_sections/Section_Spacing.hpp"
#include "asset_sections/Section_SpawnGroups.hpp"
#include "asset_sections/animation_editor_window/AnimationEditorWindow.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "utils/light_source.hpp"
#include "search_assets.hpp"

namespace {

std::string to_lower_copy(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string resolve_asset_directory(const std::string& selection) {
    namespace fs = std::filesystem;
    fs::path root{"SRC"};
    if (!fs::exists(root) || !fs::is_directory(root)) return {};

    const std::string target = to_lower_copy(selection);
    for (const auto& dir : fs::directory_iterator(root)) {
        if (!dir.is_directory()) continue;
        const std::string folder = dir.path().filename().string();
        if (to_lower_copy(folder) == target) {
            return folder;
        }
        fs::path info_path = dir.path() / "info.json";
        if (!fs::exists(info_path)) continue;
        try {
            std::ifstream in(info_path);
            nlohmann::json j;
            in >> j;
            std::string asset_name = j.value("asset_name", folder);
            if (to_lower_copy(asset_name) == target) {
                return folder;
            }
        } catch (...) {
        }
    }
    return {};
}

bool load_json_file(const std::filesystem::path& path, nlohmann::json& out) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("Failed to open %s", path.string().c_str());
        return false;
    }
    try {
        in >> out;
        if (!out.is_object()) {
            out = nlohmann::json::object();
        }
        return true;
    } catch (const std::exception& ex) {
        SDL_Log("Failed to parse %s: %s", path.string().c_str(), ex.what());
        return false;
    }
}

bool write_json_file(const std::filesystem::path& path, const nlohmann::json& data) {
    std::ofstream out(path);
    if (!out) {
        SDL_Log("Failed to write %s", path.string().c_str());
        return false;
    }
    try {
        out << data.dump(4);
        return true;
    } catch (const std::exception& ex) {
        SDL_Log("Failed to serialize %s: %s", path.string().c_str(), ex.what());
        return false;
    }
}

bool copy_section_from_source(AssetInfoSectionId section_id, const nlohmann::json& source, nlohmann::json& target) {
    if (!target.is_object()) return false;
    bool changed = false;
    auto copy_key = [&](const char* key) {
        auto it = source.find(key);
        if (it != source.end()) {
            if (!target.contains(key) || target[key] != *it) {
                target[key] = *it;
                return true;
            }
        } else if (target.contains(key)) {
            target.erase(key);
            return true;
        }
        return false;
};

    switch (section_id) {
        case AssetInfoSectionId::BasicInfo: {
            changed |= copy_key("asset_type");
            if (source.contains("size_settings") && source["size_settings"].is_object()) {
                if (!target.contains("size_settings") || target["size_settings"] != source["size_settings"]) {
                    target["size_settings"] = source["size_settings"];
                    changed = true;
                }
            } else if (target.contains("size_settings")) {
                target.erase("size_settings");
                changed = true;
            }
            changed |= copy_key("z_threshold");
            changed |= copy_key("can_invert");
            break;
        }
        case AssetInfoSectionId::Tags:
            changed |= copy_key("tags");
            break;
        case AssetInfoSectionId::Lighting:
            changed |= copy_key("has_shading");
            changed |= copy_key("lighting_info");
            break;
        case AssetInfoSectionId::Spacing:
            changed |= copy_key("min_same_type_distance");
            changed |= copy_key("min_distance_all");
            break;
    }
    return changed;
}

}

AssetInfoUI::AssetInfoUI() {
    auto basic = std::make_unique<Section_BasicInfo>();
    basic_info_section_ = basic.get();
    basic_info_section_->set_ui(this);
    sections_.push_back(std::move(basic));
    auto tags = std::make_unique<Section_Tags>();
    tags->set_ui(this);
    sections_.push_back(std::move(tags));
    auto lighting = std::make_unique<Section_Lighting>();
    lighting->set_ui(this);
    lighting_section_ = lighting.get();
    sections_.push_back(std::move(lighting));
    auto shading = std::make_unique<Section_Shading>();
    shading->set_ui(this);
    shading_section_ = shading.get();
    sections_.push_back(std::move(shading));
    auto spacing = std::make_unique<Section_Spacing>();
    spacing->set_ui(this);
    sections_.push_back(std::move(spacing));

    auto spawns = std::make_unique<Section_SpawnGroups>();
    spawns->set_ui(this);
    sections_.push_back(std::move(spawns));

    configure_btn_ = std::make_unique<DMButton>("Configure Animations", &DMStyles::CreateButton(), 220, DMButton::height());
    configure_btn_widget_ = std::make_unique<ButtonWidget>(configure_btn_.get(), [this]() {
        if (!animation_editor_window_) {
            return;
        }
        if (animation_editor_window_->is_visible()) {
            animation_editor_window_->set_visible(false);
        } else if (info_) {
            animation_editor_window_->set_visible(true);
        }
    });
    animation_editor_window_ = std::make_unique<animation_editor::AnimationEditorWindow>();

    container_.set_header_text_provider([this]() {
        return info_ ? info_->name : std::string();
    });

    container_.set_scrollbar_visible(false);

    container_.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        int y = ctx.content_top;
        for (auto& section : sections_) {
            const int previous_height = section->height();
            section->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, previous_height});
            y += previous_height + ctx.gap;
        }
        if (configure_btn_widget_) {
            configure_btn_widget_->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, DMButton::height()});
            y += DMButton::height() + ctx.gap;
        }
        return y;
    });

    container_.set_render_function([this](SDL_Renderer* renderer) {
        for (auto& section : sections_) section->render(renderer);
        if (configure_btn_) configure_btn_->render(renderer);
    });

    container_.set_update_function([this](const Input& input, int screen_w, int screen_h) {
        std::vector<bool> previously_expanded;
        std::vector<int> previous_heights;
        previously_expanded.reserve(sections_.size());
        previous_heights.reserve(sections_.size());
        for (const auto& section : sections_) {
            previously_expanded.push_back(section->is_expanded());
            previous_heights.push_back(section->height());
        }

        for (auto& section : sections_) {
            section->update(input, screen_w, screen_h);
        }

        for (size_t i = 0; i < sections_.size(); ++i) {
            if (!sections_[i]->is_expanded()) {
                continue;
            }
            for (size_t j = 0; j < sections_.size(); ++j) {
                if (i == j) {
                    continue;
                }
                if (sections_[j]->is_expanded()) {
                    sections_[j]->set_expanded(false);
                }
            }
            break;
        }

        bool expansion_changed = false;
        bool height_changed = false;
        for (size_t i = 0; i < sections_.size(); ++i) {
            if (sections_[i]->is_expanded() != previously_expanded[i]) {
                expansion_changed = true;
                break;
            }
        }

        if (!height_changed) {
            for (size_t i = 0; i < sections_.size(); ++i) {
                if (sections_[i]->height() != previous_heights[i]) {
                    height_changed = true;
                    break;
                }
            }
        }

        if (expansion_changed || height_changed) {
            container_.request_layout();
        }
    });

    container_.set_event_function([this](const SDL_Event& e) {
        for (auto& section : sections_) {
            if (section->handle_event(e)) return true;
        }
        if (configure_btn_widget_ && configure_btn_widget_->handle_event(e)) {
            return true;
        }
        return false;
    });
}

AssetInfoUI::~AssetInfoUI() {
    apply_camera_override(false);
    sync_map_light_panel_visibility(false);
    if (assets_ && forcing_high_quality_rendering_) {
        assets_->set_force_high_quality_rendering(false);
    }
    forcing_high_quality_rendering_ = false;
}

void AssetInfoUI::set_assets(Assets* a) {
    if (assets_ == a) return;
    if (assets_ && forcing_high_quality_rendering_) {
        assets_->set_force_high_quality_rendering(false);
        forcing_high_quality_rendering_ = false;
    }
    if (map_light_panel_auto_opened_ && assets_) {
        assets_->set_map_light_panel_visible(false);
        map_light_panel_auto_opened_ = false;
    }
    if (camera_override_active_) {
        apply_camera_override(false);
    }
    assets_ = a;
    if (visible_) {
        apply_camera_override(true);
    }
    validate_target_asset();
}

void AssetInfoUI::set_target_asset(Asset* a) {
    target_asset_ = a;
    validate_target_asset();
}

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    container_.reset_scroll();
    if (asset_selector_) asset_selector_->close();
    if (animation_editor_window_) {
        try {
            animation_editor_window_->set_info(info_);
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to configure animation editor for %s: %s",
                    info_ ? info_->name.c_str() : "<null>",
                    ex.what());
            animation_editor_window_->clear_info();
            animation_editor_window_->set_visible(false);
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to configure animation editor for %s due to unknown error.",
                    info_ ? info_->name.c_str() : "<null>");
            animation_editor_window_->clear_info();
            animation_editor_window_->set_visible(false);
        }
    }
    for (auto& s : sections_) {
        try {
            s->set_info(info_);
            s->reset_scroll();
            s->build();
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to build section while loading %s: %s",
                    info_ ? info_->name.c_str() : "<null>",
                    ex.what());
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to build section while loading %s due to unknown error.",
                    info_ ? info_->name.c_str() : "<null>");
        }
    }
}

void AssetInfoUI::clear_info() {
    sync_map_light_panel_visibility(false);
    if (assets_ && forcing_high_quality_rendering_) {
        assets_->set_force_high_quality_rendering(false);
        forcing_high_quality_rendering_ = false;
    }
    info_.reset();
    container_.reset_scroll();
    if (asset_selector_) asset_selector_->close();
    if (animation_editor_window_) {
        try {
            animation_editor_window_->clear_info();
            animation_editor_window_->set_visible(false);
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to reset animation editor: %s", ex.what());
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to reset animation editor due to unknown error.");
        }
    }
    for (auto& s : sections_) {
        try {
            s->set_info(nullptr);
            s->reset_scroll();
            s->build();
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to reset section: %s", ex.what());
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to reset section due to unknown error.");
        }
    }
    target_asset_ = nullptr;
}

void AssetInfoUI::open()  {
    visible_ = true;
    container_.open();
    apply_camera_override(true);
    for (auto& s : sections_) s->set_expanded(false);
}
void AssetInfoUI::close() {
    if (!visible_) return;
    apply_camera_override(false);
    visible_ = false;
    container_.close();
    sync_map_light_panel_visibility(false);
    if (animation_editor_window_) animation_editor_window_->set_visible(false);
    if (asset_selector_) asset_selector_->close();
    if (assets_ && forcing_high_quality_rendering_) {
        assets_->set_force_high_quality_rendering(false);
        forcing_high_quality_rendering_ = false;
    }
}
void AssetInfoUI::toggle(){
    if (visible_) {
        close();
    } else {
        open();
    }
}

bool AssetInfoUI::is_locked() const {
    for (const auto& section : sections_) {
        if (section && section->isLocked()) {
            return true;
        }
    }
    return false;
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    container_.prepare_layout(screen_w, screen_h);
    const SDL_Rect& panel = container_.panel_rect();
    int editor_width = panel.x;
    int editor_height = screen_h;
    if (editor_width <= 0 || editor_height <= 0) {
        animation_editor_rect_ = SDL_Rect{0, 0, 0, 0};
    } else {
        animation_editor_rect_ = SDL_Rect{0, 0, editor_width, editor_height};
    }
}

bool AssetInfoUI::handle_event(const SDL_Event& e) {
    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    const bool wheel_event = (e.type == SDL_MOUSEWHEEL);
    SDL_Point pointer{0, 0};
    if (pointer_event) {
        pointer.x = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
        pointer.y = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
    }

    if (asset_selector_ && asset_selector_->visible()) {
        if (asset_selector_->handle_event(e)) return true;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            asset_selector_->close();
            return true;
        }
        if (pointer_event) {
            if (asset_selector_->is_point_inside(pointer.x, pointer.y)) {
                return true;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                asset_selector_->close();
                return true;
            }
        } else if (wheel_event) {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            if (asset_selector_->is_point_inside(mx, my)) {
                return true;
            }
        }
    }

    if (!visible_) return false;

    if (animation_editor_window_ && animation_editor_window_->is_visible()) {
        if (animation_editor_window_->handle_event(e)) {
            return true;
        }
    }

    if (!info_) return false;

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        close();
        return true;
    }

    if (container_.handle_event(e)) {
        return true;
    }

    return false;
}

void AssetInfoUI::update(const Input& input, int screen_w, int screen_h) {
    validate_target_asset();
    layout_widgets(screen_w, screen_h);

    if (animation_editor_window_) {
        animation_editor_window_->set_bounds(animation_editor_rect_);
        if (animation_editor_window_->is_visible()) {
            animation_editor_window_->update(input, screen_w, screen_h);
        }
    }

    bool want_map_light_panel = false;
    if (visible_ && info_ && shading_section_ && shading_section_->is_expanded()) {
        want_map_light_panel = shading_section_->shading_source_enabled();
    }
    sync_map_light_panel_visibility(want_map_light_panel);

    bool lighting_requires_high_quality = false;
    bool shading_requires_high_quality = false;
    if (visible_ && info_) {
        if (lighting_section_ && lighting_section_->is_expanded()) {
            lighting_requires_high_quality = info_->generate_rays;
        }
        if (shading_section_ && shading_section_->is_expanded()) {
            shading_requires_high_quality = shading_section_->shading_enabled();
        }
    }

    const bool need_high_quality = lighting_requires_high_quality || shading_requires_high_quality;
    if (assets_) {
        if (need_high_quality != forcing_high_quality_rendering_) {
            assets_->set_force_high_quality_rendering(need_high_quality);
            forcing_high_quality_rendering_ = need_high_quality;
        }
    } else {
        forcing_high_quality_rendering_ = false;
    }

    if (!visible_ || !info_) return;

    if (asset_selector_ && asset_selector_->visible()) {
        asset_selector_->update(input);
        const SDL_Rect& panel = container_.panel_rect();
        int search_width = 280;
        int search_x = panel.x - search_width - DMSpacing::panel_padding();
        if (search_x < DMSpacing::panel_padding()) search_x = DMSpacing::panel_padding();
        int search_y = panel.y + DMSpacing::panel_padding();
        asset_selector_->set_position(search_x, search_y);
    }

    container_.update(input, screen_w, screen_h);

    layout_widgets(screen_w, screen_h);
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_) return;

    layout_widgets(screen_w, screen_h);

    if (animation_editor_window_ && animation_editor_window_->is_visible()) {
        animation_editor_window_->render(r);
    }

    if (!info_) {
        if (asset_selector_ && asset_selector_->visible()) {
            asset_selector_->render(r);
        }
        last_renderer_ = r;
        return;
    }

    container_.render(r, screen_w, screen_h);

    if (asset_selector_ && asset_selector_->visible())
        asset_selector_->render(r);

    last_renderer_ = r;
}

void AssetInfoUI::pulse_header() {
    container_.pulse_header();
}

void AssetInfoUI::apply_camera_override(bool enable) {
    if (!assets_) return;
    camera& cam = assets_->getView();
    if (enable) {
        if (camera_override_active_) return;
        prev_camera_realism_enabled_ = cam.realism_enabled();
        prev_camera_parallax_enabled_ = cam.parallax_enabled();
        cam.set_realism_enabled(false);
        cam.set_parallax_enabled(false);
        camera_override_active_ = true;
    } else {
        if (!camera_override_active_) return;
        cam.set_realism_enabled(prev_camera_realism_enabled_);
        cam.set_parallax_enabled(prev_camera_parallax_enabled_);
        camera_override_active_ = false;
    }
}

float AssetInfoUI::compute_player_screen_height(const camera& cam) const {
    if (!assets_ || !assets_->player) return 1.0f;
    Asset* player_asset = assets_->player;
    if (!player_asset) return 1.0f;

    SDL_Texture* player_final = player_asset->get_final_texture();
    SDL_Texture* player_frame = player_asset->get_current_frame();
    int pw = player_asset->cached_w;
    int ph = player_asset->cached_h;
    if ((pw == 0 || ph == 0) && player_final) {
        SDL_QueryTexture(player_final, nullptr, nullptr, &pw, &ph);
    }
    if ((pw == 0 || ph == 0) && player_frame) {
        SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
    }
    if (pw != 0) player_asset->cached_w = pw;
    if (ph != 0) player_asset->cached_h = ph;

    float scale = cam.get_scale();
    float inv_scale = (scale > 0.0f) ? (1.0f / scale) : 1.0f;
    const float base_scale = (player_asset->info && std::isfinite(player_asset->info->scale_factor) &&
                              player_asset->info->scale_factor >= 0.0f)
                                 ? player_asset->info->scale_factor
                                 : 1.0f;
    if (ph > 0) {
        float screen_h = static_cast<float>(ph) * base_scale * inv_scale;
        return screen_h > 0.0f ? screen_h : 1.0f;
    }
    return 1.0f;
}

void AssetInfoUI::render_world_overlay(SDL_Renderer* r, const camera& cam) const {
    if (!visible_ || !info_) return;

    validate_target_asset();

    float reference_screen_height = compute_player_screen_height(cam);

    if (basic_info_section_ && basic_info_section_->is_expanded()) {
        basic_info_section_->render_world_overlay(r, cam, target_asset_, reference_screen_height);
    }

    if (!shading_section_ || !shading_section_->is_expanded() || !shading_section_->shading_enabled() || !target_asset_) return;
    const LightSource& light = shading_section_->shading_light();
    if (light.x_radius <= 0 && light.y_radius <= 0) return;
    const SDL_Color accent = DMStyles::AccentButton().hover_bg;
    SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 255);
    const bool flipped = target_asset_->flipped;
    const int base_offset_x = flipped ? -light.offset_x : light.offset_x;
    for (int deg = 0; deg < 360; ++deg) {
        double rad = deg * M_PI / 180.0;
        double cx = std::cos(rad) * static_cast<double>(light.x_radius);
        double cy = std::sin(rad) * static_cast<double>(light.y_radius);
        if (flipped) cx = -cx;
        int wx = target_asset_->pos.x + base_offset_x + static_cast<int>(std::llround(cx));
        int wy = target_asset_->pos.y + light.offset_y - static_cast<int>(std::llround(cy));
        SDL_Point p = cam.compute_render_effects(SDL_Point{wx, wy}, 0.0f, 0.0f).screen_position;
        SDL_RenderDrawPoint(r, p.x, p.y);
    }

    Uint8 prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 0;
    SDL_GetRenderDrawColor(r, &prev_r, &prev_g, &prev_b, &prev_a);
    SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(r, &prev_mode);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    const Global_Light_Source* global_light = assets_ ? assets_->map_light_source() : nullptr;
    SDL_Point screen_center_map = cam.get_screen_center();
    SDL_Point screen_center = cam.map_to_screen(screen_center_map);

    bool drew_indicator = false;
    if (global_light) {
        SDL_Point light_pos = global_light->get_position();
        SDL_Point end = cam.map_to_screen(light_pos);
        SDL_SetRenderDrawColor(r, 220, 32, 32, 230);
        SDL_RenderDrawLine(r, screen_center.x, screen_center.y, end.x, end.y);
        SDL_Rect tip{end.x - 4, end.y - 4, 8, 8};
        SDL_RenderFillRect(r, &tip);
        drew_indicator = true;
    }

    if (!drew_indicator) {
        SDL_SetRenderDrawColor(r, 220, 32, 32, 230);
        SDL_RenderDrawLine(r, screen_center.x - 6, screen_center.y - 6, screen_center.x + 6, screen_center.y + 6);
        SDL_RenderDrawLine(r, screen_center.x - 6, screen_center.y + 6, screen_center.x + 6, screen_center.y - 6);
    }

    const double center_x = static_cast<double>(target_asset_->pos.x + base_offset_x);
    const double center_y = static_cast<double>(target_asset_->pos.y + light.offset_y);
    SDL_Point orbit_center_screen = cam.compute_render_effects(
        SDL_Point{static_cast<int>(std::lround(center_x)), static_cast<int>(std::lround(center_y))},
        0.0f, 0.0f).screen_position;

    double angle = global_light ? static_cast<double>(global_light->get_angle()) : 0.0;
    double dir_x = std::cos(angle) * static_cast<double>(light.x_radius);
    if (flipped) dir_x = -dir_x;
    double dir_y = -std::sin(angle) * static_cast<double>(light.y_radius);
    double length = std::hypot(dir_x, dir_y);
    if (length > 0.0) {
        double max_length = 60.0;
        double scale = std::min(1.0, max_length / length);
        SDL_Point orbit_end_screen = cam.compute_render_effects(
            SDL_Point{
                static_cast<int>(std::lround(center_x + dir_x * scale)),
                static_cast<int>(std::lround(center_y + dir_y * scale))
            },
            0.0f, 0.0f).screen_position;
        SDL_Color orbit_color = DMStyles::AccentButton().hover_bg;
        SDL_SetRenderDrawColor(r, orbit_color.r, orbit_color.g, orbit_color.b, 255);
        SDL_RenderDrawLine(r, orbit_center_screen.x, orbit_center_screen.y, orbit_end_screen.x, orbit_end_screen.y);
    }

    SDL_SetRenderDrawColor(r, prev_r, prev_g, prev_b, prev_a);
    SDL_SetRenderDrawBlendMode(r, prev_mode);
}

void AssetInfoUI::refresh_target_asset_scale() {
    if (!info_) return;

    Asset* current_target = target_asset_;
    const bool target_valid = validate_target_asset();
    Asset* validated_target = target_asset_;

    const auto refresh_asset = [&](Asset* asset) {
        if (!asset || asset->info.get() != info_.get()) {
            return false;
        }
        asset->on_scale_factor_changed();
        return true;
    };

    bool refreshed_any = false;
    if (assets_) {
        for (Asset* asset : assets_->all) {
            if (refresh_asset(asset)) {
                refreshed_any = true;
            }
        }
        for (const auto& owned : assets_->owned_assets) {
            if (refresh_asset(owned.get())) {
                refreshed_any = true;
            }
        }
    }

    if (target_valid && validated_target) {
        if (refresh_asset(validated_target)) {
            refreshed_any = true;
        }
    }

    if (current_target && current_target != validated_target) {
        if (refresh_asset(current_target)) {
            refreshed_any = true;
        }
    }

    if (refreshed_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::sync_target_z_threshold() {
    if (!info_) return;

    Asset* current_target = target_asset_;
    const bool target_valid = validate_target_asset();

    const auto sync_asset = [&](Asset* asset) {
        if (!asset || asset->info.get() != info_.get()) {
            return false;
        }
        asset->set_z_index();
        return true;
    };

    bool updated_any = false;
    if (assets_) {
        for (Asset* asset : assets_->all) {
            if (sync_asset(asset)) {
                updated_any = true;
            }
        }
        for (const auto& owned : assets_->owned_assets) {
            if (sync_asset(owned.get())) {
                updated_any = true;
            }
        }
    }

    if (!updated_any && target_valid && current_target) {
        (void)sync_asset(current_target);
    }
}

void AssetInfoUI::sync_map_light_panel_visibility(bool want_visible) {
    if (!assets_) {
        map_light_panel_auto_opened_ = false;
        return;
    }

    bool panel_visible = assets_->is_map_light_panel_visible();

    if (want_visible) {
        if (!panel_visible) {
            assets_->set_map_light_panel_visible(true);
            panel_visible = assets_->is_map_light_panel_visible();
        }
        map_light_panel_auto_opened_ = panel_visible;
        if (!panel_visible) {
            map_light_panel_auto_opened_ = false;
        }
        return;
    }

    if (map_light_panel_auto_opened_ && panel_visible) {
        assets_->set_map_light_panel_visible(false);
        panel_visible = assets_->is_map_light_panel_visible();
    }
    if (!panel_visible) {
        map_light_panel_auto_opened_ = false;
    }
}

bool AssetInfoUI::validate_target_asset() const {
    if (!target_asset_) {
        return false;
    }
    if (!assets_) {
        return true;
    }
    if (!assets_->contains_asset(target_asset_)) {
        target_asset_ = nullptr;
        return false;
    }
    return true;
}

void AssetInfoUI::request_apply_section(AssetInfoSectionId section_id) {
    if (!info_) return;
    if (is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Panel is locked; bulk apply request ignored.");
        return;
    }
    if (!asset_selector_) asset_selector_ = std::make_unique<SearchAssets>();
    if (!asset_selector_) return;

    asset_selector_->open([this, section_id](const std::string& selection) {
        if (selection.empty()) return;
        if (!selection.empty() && selection.front() == '#') return;
        std::string folder = resolve_asset_directory(selection);
        if (folder.empty()) {
            SDL_Log("Unable to resolve asset directory for '%s'", selection.c_str());
            return;
        }
        std::vector<std::string> assets{folder};
        (void)apply_section_to_assets(section_id, assets);
    });

    const SDL_Rect& panel = container_.panel_rect();
    if (panel.w > 0) {
        int search_width = 280;
        int search_x = panel.x - search_width - DMSpacing::panel_padding();
        if (search_x < DMSpacing::panel_padding()) search_x = DMSpacing::panel_padding();
        int search_y = panel.y + DMSpacing::panel_padding();
        asset_selector_->set_position(search_x, search_y);
    }
}

bool AssetInfoUI::apply_section_to_assets(AssetInfoSectionId section_id, const std::vector<std::string>& asset_names) {
    if (!info_) return false;
    if (asset_names.empty()) return true;
    if (is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Panel is locked; apply_section_to_assets skipped.");
        return false;
    }

    (void)info_->update_info_json();
    nlohmann::json source;
    if (!load_json_file(info_->info_json_path(), source)) {
        return false;
    }

    bool all_success = true;
    bool any_written = false;
    for (const auto& name : asset_names) {
        try {
            std::filesystem::path path = std::filesystem::path("SRC") / name / "info.json";
            nlohmann::json target;
            if (!load_json_file(path, target)) {
                all_success = false;
                continue;
            }
            if (!copy_section_from_source(section_id, source, target)) {
                continue;
            }
            if (!write_json_file(path, target)) {
                all_success = false;
            } else {
                any_written = true;
            }
        } catch (const std::exception& ex) {
            SDL_Log("Failed to apply settings to %s: %s", name.c_str(), ex.what());
            all_success = false;
        } catch (...) {
            SDL_Log("Failed to apply settings to %s due to unknown error.", name.c_str());
            all_success = false;
        }
    }

    if (any_written) {
        tag_utils::notify_tags_changed();
    }

    if (all_success) {
        pulse_header();
    } else {
        SDL_Log("Some assets failed to receive applied settings.");
    }
    return all_success;
}

void AssetInfoUI::set_header_visibility_callback(std::function<void(bool)> cb) {
    container_.set_header_visibility_controller(std::move(cb));
}

void AssetInfoUI::notify_light_sources_modified(bool purge_light_cache) {
    if (!info_) {
        return;
    }

    render_pipeline::ScalingLogic::ResetAssetUsage(info_->name);

    if (!purge_light_cache) {
        return;
    }

    std::error_code ec;
    std::filesystem::path cache_dir = std::filesystem::path("cache") / info_->name / "lights";
    std::filesystem::remove_all(cache_dir, ec);
}

const char* AssetInfoUI::section_display_name(AssetInfoSectionId section_id) {
    switch (section_id) {
        case AssetInfoSectionId::BasicInfo:   return "Basic Info";
        case AssetInfoSectionId::Tags:        return "Tags";
        case AssetInfoSectionId::Lighting:    return "Lighting";
        case AssetInfoSectionId::Spacing:     return "Spacing";
    }
    return "Settings";
}

bool AssetInfoUI::is_point_inside(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{ x, y };
    if (container_.is_point_inside(x, y)) return true;
    if (asset_selector_ && asset_selector_->visible() && asset_selector_->is_point_inside(x, y)) return true;
    return false;
}

void AssetInfoUI::save_now() const {
    if (is_locked()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Panel is locked; save skipped.");
        return;
    }
    if (info_) (void)info_->update_info_json();
}

void AssetInfoUI::open_area_editor(const std::string& name) {
    if (!info_ || !assets_) return;
    assets_->begin_area_edit_for_selected_asset(name);
}
