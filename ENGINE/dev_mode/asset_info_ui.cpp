#include "asset_info_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <SDL_log.h>
#include <stdexcept>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <functional>

#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"
#include "utils/string_utils.hpp"
#include "widgets.hpp"
#include "tag_utils.hpp"

#include "DockableCollapsible.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "SlidingWindowContainer.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "dm_styles.hpp"
#include "dev_mode_utils.hpp"
#include "asset_sections/Section_BasicInfo.hpp"
#include "asset_sections/Section_Tags.hpp"
#include "asset_sections/Section_Lighting.hpp"
#include "asset_sections/Section_Shading.hpp"
#include "asset_sections/Section_Spacing.hpp"
#include "spawn_group_config/SpawnGroupConfig.hpp"
#include "asset_sections/Section_SpawnGroups.hpp"
#include "map_generation/room.hpp"
#include "core/AssetsManager.hpp"
#include "world/grid.hpp"
#include "world/chunk.hpp"
#include "utils/map_grid_settings.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "asset_sections/animation_editor_window/AnimationEditorWindow.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "render/global_light_source.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "search_assets.hpp"
#include "draw_utils.hpp"
#include <SDL_ttf.h>
#include "dev_mode/manifest_spawn_group_utils.hpp"

namespace {

using vibble::strings::to_lower_copy;

std::string resolve_asset_manifest_key(devmode::core::ManifestStore* store, const std::string& selection) {
    if (!store) return {};

    std::string trimmed = selection;
    if (trimmed.empty()) {
        return {};
    }

    if (auto resolved = store->resolve_asset_name(trimmed)) {
        return *resolved;
    }

    const std::string target = to_lower_copy(trimmed);
    for (const auto& view : store->assets()) {
        if (!view || !view.data || !view.data->is_object()) {
            continue;
        }
        const auto& asset_json = *view.data;
        std::string asset_name = asset_json.value("asset_name", view.name);
        if (!asset_name.empty() && to_lower_copy(asset_name) == target) {
            return view.name;
        }
        auto dir_it = asset_json.find("asset_directory");
        if (dir_it != asset_json.end() && dir_it->is_string()) {
            try {
                std::filesystem::path dir = dir_it->get<std::string>();
                if (!dir.empty()) {
                    std::string folder = to_lower_copy(dir.filename().string());
                    if (!folder.empty() && folder == target) {
                        return view.name;
                    }
                    std::string normalized = to_lower_copy(dir.lexically_normal().generic_string());
                    if (!normalized.empty() && normalized == target) {
                        return view.name;
                    }
                }
            } catch (...) {
            }
        }
    }

    return {};
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
            // Support both keys; canonical UI uses "tileable"
            changed |= copy_key("tileable");
            changed |= copy_key("tillable");
            break;
        }
        case AssetInfoSectionId::Tags:
            changed |= copy_key("tags");
            break;
        case AssetInfoSectionId::Lighting:
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
    rebuild_default_sections();
    if (!configure_btn_) {
        configure_btn_ = std::make_unique<DMButton>("Configure Animations", &DMStyles::CreateButton(), 220, DMButton::height());
    }
    if (!configure_btn_widget_) {
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
    }
    if (!animation_editor_window_) {
        animation_editor_window_ = std::make_unique<animation_editor::AnimationEditorWindow>();
        if (animation_editor_window_) {
            animation_editor_window_->set_manifest_store(manifest_store_);
            animation_editor_window_->set_on_document_saved([this]() { this->on_animation_document_saved(); });
        }
    }

    // Duplicate and Delete buttons
    if (!duplicate_btn_) {
        duplicate_btn_ = std::make_unique<DMButton>("Duplicate Asset", &DMStyles::FooterToggleButton(), 220, DMButton::height());
    }
    if (!duplicate_btn_widget_) {
        duplicate_btn_widget_ = std::make_unique<ButtonWidget>(duplicate_btn_.get(), [this]() {
            if (!info_) return;
            showing_duplicate_popup_ = true;
            duplicate_asset_name_.clear();
        });
    }

    if (!delete_btn_) {
        delete_btn_ = std::make_unique<DMButton>("Delete Asset", &DMStyles::DeleteButton(), 220, DMButton::height());
    }
    if (!delete_btn_widget_) {
        delete_btn_widget_ = std::make_unique<ButtonWidget>(delete_btn_.get(), [this]() {
            this->request_delete_current_asset();
        });
    }

    container_.set_header_text_provider([this]() {
        if (area_mode_ && !area_name_.empty()) return std::string("Area: ") + area_name_;
        return info_ ? info_->name : std::string();
    });

    container_.set_scrollbar_visible(true);

    container_.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        int y = ctx.content_top;
        for (auto& section : sections_) {
            const int previous_height = section->height();
            section->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, previous_height});
            y += previous_height + ctx.gap;
        }
        if (!area_mode_) {
            if (configure_btn_widget_) {
                configure_btn_widget_->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, DMButton::height()});
                y += DMButton::height() + ctx.gap;
            }
            if (duplicate_btn_widget_) {
                duplicate_btn_widget_->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, DMButton::height()});
                y += DMButton::height() + ctx.gap;
            }
            if (delete_btn_widget_) {
                delete_btn_widget_->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, DMButton::height()});
                y += DMButton::height() + ctx.gap;
            }
        }
        return y;
    });

    container_.set_render_function([this](SDL_Renderer* renderer) {
        for (auto& section : sections_) section->render(renderer);
        if (!area_mode_) {
            if (configure_btn_) configure_btn_->render(renderer);
            if (duplicate_btn_) duplicate_btn_->render(renderer);
            if (delete_btn_) delete_btn_->render(renderer);
        }
    });

    container_.set_on_close([this]() { this->close(); });

    container_.set_update_function([this](const Input& input, int screen_w, int screen_h) {
        // Constrain panel between header and footer
        SDL_Rect usable = FloatingPanelLayoutManager::instance().usableRect();
        if (usable.w > 0 && usable.h > 0) {
            int panel_x = screen_w - std::max(screen_w / 3, 320);
            panel_x = std::clamp(panel_x, 0, screen_w);
            int panel_w = std::max(0, screen_w - panel_x);
            SDL_Rect bounds{panel_x, usable.y, panel_w, usable.h};
            container_.set_panel_bounds_override(bounds);
        } else {
            container_.clear_panel_bounds_override();
        }
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
        if (!area_mode_) {
            if (duplicate_btn_widget_ && duplicate_btn_widget_->handle_event(e)) return true;
            if (delete_btn_widget_ && delete_btn_widget_->handle_event(e)) return true;
        }
        return false;
    });
}

AssetInfoUI::~AssetInfoUI() {
    apply_camera_override(false);
    sync_map_light_panel_visibility(false);
    if (assets_) {
        if (auto* gl = assets_->map_light_source()) {
            gl->set_alpha_override(std::nullopt);
        }
    }
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
    // Clear any map light override on the previous assets context
    if (assets_) {
        if (auto* gl = assets_->map_light_source()) {
            gl->set_alpha_override(std::nullopt);
        }
    }
    if (map_light_panel_auto_opened_ && assets_) {
        assets_->set_map_light_panel_visible(false);
        map_light_panel_auto_opened_ = false;
    }
    if (camera_override_active_) {
        apply_camera_override(false);
    }
    assets_ = a;
    set_manifest_store(assets_ ? assets_->manifest_store() : nullptr);
    if (animation_editor_window_) {
        animation_editor_window_->set_assets(assets_);
    }
    if (visible_) {
        apply_camera_override(true);
    }
    validate_target_asset();
}

void AssetInfoUI::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
    if (spawn_groups_section_) {
        spawn_groups_section_->set_manifest_store(manifest_store_);
    }
    if (animation_editor_window_) {
        animation_editor_window_->set_manifest_store(manifest_store_);
    }
}

void AssetInfoUI::set_target_asset(Asset* a) {
    target_asset_ = a;
    validate_target_asset();
    if (animation_editor_window_) {
        animation_editor_window_->set_target_asset(target_asset_);
    }
}

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    container_.reset_scroll();
    if (asset_selector_) asset_selector_->close();
    if (animation_editor_window_) {
        try {
            animation_editor_window_->set_manifest_store(manifest_store_);
            animation_editor_window_->set_on_animation_properties_changed([this](const std::string& animation_id, const nlohmann::json& properties) {
                if (info_ && info_->update_animation_properties(animation_id, properties)) {
                    // Immediately refresh loaded asset instances
                    refresh_loaded_asset_instances();
                }
            });
            animation_editor_window_->set_info(info_);
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to configure animation editor for %s: %s", info_ ? info_->name.c_str() : "<null>", ex.what());
            animation_editor_window_->clear_info();
            animation_editor_window_->set_visible(false);
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to configure animation editor for %s due to unknown error.", info_ ? info_->name.c_str() : "<null>");
            animation_editor_window_->clear_info();
            animation_editor_window_->set_visible(false);
        }
    }
    for (auto& s : sections_) {
        try {
            s->set_info(info_);
            // Hide some sections if the asset is an area
            bool is_area_asset = false;
            if (info_) {
                std::string t = info_->type;
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
                is_area_asset = (t == "area");
            }
            if (is_area_asset) {
                if (auto* lighting = dynamic_cast<Section_Lighting*>(s.get())) lighting->set_visible(false);
                if (auto* shading  = dynamic_cast<Section_Shading*>(s.get()))  shading->set_visible(false);
                if (auto* spawns   = dynamic_cast<Section_SpawnGroups*>(s.get())) spawns->set_visible(false);
            } else {
                if (auto* lighting = dynamic_cast<Section_Lighting*>(s.get())) lighting->set_visible(true);
                if (auto* shading  = dynamic_cast<Section_Shading*>(s.get()))  shading->set_visible(true);
                if (auto* spawns   = dynamic_cast<Section_SpawnGroups*>(s.get())) spawns->set_visible(true);
            }
            s->reset_scroll();
            s->build();
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to build section while loading %s: %s", info_ ? info_->name.c_str() : "<null>", ex.what());
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to build section while loading %s due to unknown error.", info_ ? info_->name.c_str() : "<null>");
        }
    }
}

void AssetInfoUI::clear_info() {
    sync_map_light_panel_visibility(false);
    if (assets_) {
        if (auto* gl = assets_->map_light_source()) {
            gl->set_alpha_override(std::nullopt);
        }
    }
    if (assets_ && forcing_high_quality_rendering_) {
        assets_->set_force_high_quality_rendering(false);
        forcing_high_quality_rendering_ = false;
    }
    info_.reset();
    hovered_light_index_ = -1;
    if (lighting_section_) {
        lighting_section_->set_highlighted_light(std::nullopt);
    }
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
    if (assets_) {
        if (auto* gl = assets_->map_light_source()) {
            gl->set_alpha_override(std::nullopt);
        }
    }
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
    int editor_y = panel.y;
    int editor_height = panel.h > 0 ? panel.h : std::max(0, screen_h - editor_y);
    if (editor_width <= 0 || editor_height <= 0) {
        animation_editor_rect_ = SDL_Rect{0, 0, 0, 0};
    } else {
        animation_editor_rect_ = SDL_Rect{0, editor_y, editor_width, editor_height};
    }
}

bool AssetInfoUI::handle_event(const SDL_Event& e) {
    // Give active dropdown overlays global priority so option clicks are reliable
    if (auto* active_dd = DMDropdown::active_dropdown()) {
        if (active_dd->handle_event(e)) {
            return true;
        }
    }

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

    if (showing_delete_popup_) {
        if (handle_delete_modal_event(e)) {
            return true;
        }
        switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEWHEEL:
            case SDL_KEYDOWN:
            case SDL_TEXTINPUT:
                return true; // consume inputs while modal is visible
            default:
                break;
        }
    }

    if (showing_duplicate_popup_) {
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) {
                if (duplicate_current_asset(duplicate_asset_name_)) {
                    duplicate_asset_name_.clear();
                }
                showing_duplicate_popup_ = false;
                return true;
            } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                showing_duplicate_popup_ = false;
                duplicate_asset_name_.clear();
                return true;
            } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                if (!duplicate_asset_name_.empty()) duplicate_asset_name_.pop_back();
                return true;
            }
        } else if (e.type == SDL_TEXTINPUT) {
            duplicate_asset_name_ += e.text.text;
            return true;
        }
    }

    if (animation_editor_window_ && animation_editor_window_->is_visible()) {
        if (animation_editor_window_->handle_event(e)) {
            return true;
        }
    }

    if (!area_mode_ && !info_) return false;

    // World overlay: click-and-drag light crosshairs when Lighting section is open
    auto clear_light_hover = [&]() {
        if (hovered_light_index_ == -1) {
            return;
        }
        hovered_light_index_ = -1;
        if (lighting_section_) {
            lighting_section_->set_highlighted_light(std::nullopt);
        }
    };
    if (lighting_section_ && lighting_section_->is_expanded() && info_ && assets_) {
        const bool pointer_event =
            (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
        if (pointer_event && target_asset_ && target_asset_->info.get() == info_.get()) {
            const camera& cam = assets_->getView();

            // Build a transform consistent with SceneRenderer::render_lights_for_source
            auto compute_light_transform = [&]() {
                struct Xform { float cx; float cy; float sx; float sy; } out{0,0,1,1};
                // Resolve base frame size
                int fw = target_asset_->cached_w;
                int fh = target_asset_->cached_h;
                if ((fw <= 0 || fh <= 0)) {
                    if (SDL_Texture* ft = target_asset_->get_final_texture()) {
                        SDL_QueryTexture(ft, nullptr, nullptr, &fw, &fh);
                    }
                }
                if (fw <= 0) fw = 1;
                if (fh <= 0) fh = 1;

                const float base_scale = (target_asset_->info && std::isfinite(target_asset_->info->scale_factor) && target_asset_->info->scale_factor > 0.0f)
                    ? target_asset_->info->scale_factor
                    : 1.0f;
                const float scale = cam.get_scale();
                const float inv_scale = (scale > 0.0f) ? (1.0f / scale) : 1.0f;

                const float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
                const float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

                const float ref_sh = compute_player_screen_height(cam);
                const camera::RenderSmoothingKey smoothing_key = reinterpret_cast<camera::RenderSmoothingKey>(target_asset_);
                camera::RenderEffects ef = cam.compute_render_effects(
                    SDL_Point{ target_asset_->pos.x, target_asset_->pos.y },
                    base_sh,
                    ref_sh,
                    smoothing_key);

                SDL_Point world_point{ target_asset_->pos.x, target_asset_->pos.y };
                float adjusted_cx = ef.screen_position.x;
                if (assets_ && target_asset_) {
                    // Do not apply grid parallax to the player asset
                    if (!(assets_->player == target_asset_)) {
                        adjusted_cx = assets_->world_grid().parallax_adjusted_screen_x(world_point, ef.screen_position.x);
                    }
                }
                const float distance_scale  = ef.distance_scale;
                const float vertical_scale  = ef.vertical_scale;

                const float width_px  = base_sw * distance_scale;
                const float height_px = base_sh * distance_scale * vertical_scale;

                out.cx = adjusted_cx;
                out.cy = ef.screen_position.y;
                // Per-axis screen-pixels per local-offset unit
                out.sx = (fw > 0) ? (width_px  / static_cast<float>(fw)) : base_scale * inv_scale * distance_scale;
                out.sy = (fh > 0) ? (height_px / static_cast<float>(fh)) : base_scale * inv_scale * distance_scale * vertical_scale;
                return out;
            };

            auto xform = compute_light_transform();

            auto light_screen_pos = [&](const LightSource& light) -> SDL_Point {
                int offx = light.offset_x;
                if (target_asset_->flipped) offx = -offx;
                const float cx = xform.cx + static_cast<float>(offx) * xform.sx;
                const float cy = xform.cy + static_cast<float>(light.offset_y) * xform.sy;
                return SDL_Point{ static_cast<int>(std::lround(cx)), static_cast<int>(std::lround(cy)) };
            };

            const int mx = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
            const int my = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;

            auto hit_test_index = [&](int sx, int sy) -> int {
                const int kHitRadius = 10;
                for (size_t i = 0; i < info_->light_sources.size(); ++i) {
                    SDL_Point sp = light_screen_pos(info_->light_sources[i]);
                    const int dx = sp.x - sx;
                    const int dy = sp.y - sy;
                    if (dx*dx + dy*dy <= kHitRadius * kHitRadius) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };

            auto set_light_hover = [&](int idx) {
                if (idx == hovered_light_index_) {
                    return;
                }
                hovered_light_index_ = idx;
                if (!lighting_section_) return;
                if (idx >= 0) {
                    lighting_section_->set_highlighted_light(static_cast<std::size_t>(idx));
                } else {
                    lighting_section_->set_highlighted_light(std::nullopt);
                }
            };

            const int hovered_idx = hit_test_index(mx, my);

            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                set_light_hover(hovered_idx);
                if (hovered_idx >= 0) {
                    light_drag_active_ = true;
                    light_drag_index_ = hovered_idx;
                    lighting_section_->open();
                    lighting_section_->set_expanded(true);
                    lighting_section_->expand_light_row(static_cast<std::size_t>(hovered_idx));
                    return true;
                }
            } else if (e.type == SDL_MOUSEMOTION && light_drag_active_ && light_drag_index_ >= 0 &&
                       light_drag_index_ < static_cast<int>(info_->light_sources.size())) {
                auto& L = info_->light_sources[light_drag_index_];
                // Invert the screen mapping so the crosshair sits under the mouse
                const float dx_screen = static_cast<float>(mx) - xform.cx;
                const float dy_screen = static_cast<float>(my) - xform.cy;
                const float unflipped_x = (xform.sx != 0.0f) ? (dx_screen / xform.sx) : 0.0f;
                const float new_off_x   = target_asset_->flipped ? -unflipped_x : unflipped_x;
                const float new_off_y   = (xform.sy != 0.0f) ? (dy_screen / xform.sy) : 0.0f;
                const int final_off_x = static_cast<int>(std::lround(new_off_x));
                const int final_off_y = static_cast<int>(std::lround(new_off_y));
                if (L.offset_x == final_off_x && L.offset_y == final_off_y) {
                    set_light_hover(light_drag_index_);
                    return true;
                }
                L.offset_x = final_off_x;
                L.offset_y = final_off_y;
                // Update serialized lighting payload without disturbing other properties.
                info_->set_lighting(info_->light_sources);
                if (lighting_section_) {
                    lighting_section_->update_light_offsets(static_cast<std::size_t>(light_drag_index_), final_off_x, final_off_y);
                }
                set_light_hover(light_drag_index_);
                this->notify_light_sources_modified(true);
                (void)info_->commit_manifest();
                return true;
            } else if (e.type == SDL_MOUSEMOTION) {
                set_light_hover(hovered_idx);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (light_drag_active_) {
                    light_drag_active_ = false;
                    if (light_drag_index_ >= 0) {
                        light_drag_index_ = -1;
                    }
                    return true;
                }
            }
        } else if (pointer_event) {
            clear_light_hover();
        }
    } else {
        clear_light_hover();
    }

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

    sync_map_light_panel_visibility(false);

    bool shading_requires_high_quality = false;
    if (visible_ && info_) {
        if (shading_section_ && shading_section_->is_expanded()) {
            shading_requires_high_quality = shading_section_->shading_enabled();
        }
    }

    const bool need_high_quality = shading_requires_high_quality;
    if (assets_) {
        if (need_high_quality != forcing_high_quality_rendering_) {
            assets_->set_force_high_quality_rendering(need_high_quality);
            forcing_high_quality_rendering_ = need_high_quality;
        }
    } else {
        forcing_high_quality_rendering_ = false;
    }

    // While the Lighting section is expanded, force the map light to darkest opacity
    if (assets_) {
        Global_Light_Source* gl = assets_->map_light_source();
        if (gl) {
            const bool lighting_open = visible_ && info_ && lighting_section_ && lighting_section_->is_expanded();
            if (lighting_open) {
                gl->set_alpha_override(static_cast<Uint8>(255));
            } else {
                gl->set_alpha_override(std::nullopt);
            }
        }
    }

    if (!visible_ || (!area_mode_ && !info_)) return;

    if (info_ && asset_selector_ && asset_selector_->visible()) {
        asset_selector_->update(input);
        const SDL_Rect& panel = container_.panel_rect();
        FloatingPanelLayoutManager::SlidingParentInfo parent;
        parent.bounds = panel;
        parent.padding = DMSpacing::panel_padding();
        parent.anchor_left = true;
        parent.align_top = true;
        asset_selector_->layout_with_parent(parent);
    }

    container_.update(input, screen_w, screen_h);

    layout_widgets(screen_w, screen_h);

    if (showing_delete_popup_) {
        update_delete_modal_geometry(screen_w, screen_h);
        SDL_StopTextInput();
    } else if (showing_duplicate_popup_) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_) return;

    layout_widgets(screen_w, screen_h);

    if (animation_editor_window_ && animation_editor_window_->is_visible()) {
        animation_editor_window_->render(r);
    }

    if (!info_) {
        if (!area_mode_) {
            if (asset_selector_ && asset_selector_->visible()) {
                asset_selector_->render(r);
            }
            last_renderer_ = r;
            return;
        }
    }

    container_.render(r, screen_w, screen_h);
    if (lighting_section_) {
        lighting_section_->render_overlays(r);
    }

    if (info_ && asset_selector_ && asset_selector_->visible())
        asset_selector_->render(r);

    DMDropdown::render_active_options(r);

    // Duplicate asset popup: reuse AssetLibraryUI input style
    if (showing_duplicate_popup_) {
        SDL_Rect box{ screen_w/2 - 150, screen_h/2 - 40, 300, 80 };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const SDL_Color panel_bg = DMStyles::PanelBG();
        const SDL_Color& highlight = DMStyles::HighlightColor();
        const SDL_Color& shadow = DMStyles::ShadowColor();
        const int corner_radius = DMStyles::CornerRadius();
        const int bevel_depth = DMStyles::BevelDepth();
        dm_draw::DrawBeveledRect( r, box, corner_radius, bevel_depth, panel_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        const SDL_Color panel_border = DMStyles::Border();
        dm_draw::DrawRoundedOutline( r, box, corner_radius, 1, panel_border);

        SDL_Rect input_rect{ box.x + 8, box.y + 8, box.w - 16, box.h - 16 };
        const DMTextBoxStyle& textbox = DMStyles::TextBox();
        dm_draw::DrawBeveledRect( r, input_rect, corner_radius, bevel_depth, textbox.bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline( r, input_rect, corner_radius, 1, textbox.border);

        const int text_padding = 12 + bevel_depth;
        const int interior_h = std::max(0, input_rect.h - 2 * bevel_depth);
        TTF_Font* font = devmode::utils::load_font(18);
        if (font) {
            std::string display = duplicate_asset_name_.empty() ? "Enter asset name..." : duplicate_asset_name_;
            SDL_Color color = duplicate_asset_name_.empty() ? textbox.label.color : textbox.text;
            int available_w = input_rect.w - 2 * text_padding;
            if (available_w < 0) available_w = 0;
            int tw = 0;
            int th = 0;
            std::string render_text = display;
            if (TTF_SizeUTF8(font, render_text.c_str(), &tw, &th) == 0 && tw > available_w) {
                const std::string ellipsis = "...";
                std::string base = display;
                while (!base.empty()) {
                    base.pop_back();
                    std::string candidate = base + ellipsis;
                    if (TTF_SizeUTF8(font, candidate.c_str(), &tw, &th) == 0 && tw <= available_w) {
                        render_text = std::move(candidate);
                        break;
                    }
                }
                if (base.empty()) {
                    render_text = ellipsis;
                    (void)TTF_SizeUTF8(font, render_text.c_str(), &tw, &th);
                }
            } else {
                (void)TTF_SizeUTF8(font, render_text.c_str(), &tw, &th);
            }

            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, render_text.c_str(), color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    const int text_area_h = std::max(0, interior_h - th);
                    int text_y = input_rect.y + bevel_depth + text_area_h / 2;
                    text_y = std::max(text_y, input_rect.y + bevel_depth);
                    text_y = std::min(text_y, input_rect.y + input_rect.h - bevel_depth - th);
                    SDL_Rect dst{ input_rect.x + text_padding,
                                  text_y,
                                  tw,
                                  th };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }
    }

    // Delete confirmation popup
    if (showing_delete_popup_) {
        const SDL_Color panel_bg = DMStyles::PanelBG();
        const SDL_Color& highlight = DMStyles::HighlightColor();
        const SDL_Color& shadow = DMStyles::ShadowColor();
        const int corner_radius = DMStyles::CornerRadius();
        const int bevel_depth = DMStyles::BevelDepth();
        dm_draw::DrawBeveledRect( r, delete_modal_rect_, corner_radius, bevel_depth, panel_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        const SDL_Color panel_border = DMStyles::Border();
        dm_draw::DrawRoundedOutline( r, delete_modal_rect_, corner_radius, 1, panel_border);

        auto render_button = [&](const SDL_Rect& rect, bool hovered, bool pressed, const std::string& caption, const DMButtonStyle& style) {
            SDL_Color bg = style.bg;
            if (pressed) bg = style.press_bg; else if (hovered) bg = style.hover_bg;
            dm_draw::DrawBeveledRect( r, rect, corner_radius, bevel_depth, bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
            dm_draw::DrawRoundedOutline( r, rect, corner_radius, 1, style.border);

            TTF_Font* btn_font = devmode::utils::load_font(style.label.font_size > 0 ? style.label.font_size : 16);
            if (!btn_font) btn_font = devmode::utils::load_font(16);
            if (btn_font) {
                SDL_Surface* text = TTF_RenderUTF8_Blended(btn_font, caption.c_str(), style.text);
                if (text) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, text);
                    SDL_FreeSurface(text);
                    if (tex) {
                        int tw = 0;
                        int th = 0;
                        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                        const int interior_h = std::max(0, rect.h - 2 * bevel_depth);
                        int text_y = rect.y + bevel_depth + std::max(0, interior_h - th) / 2;
                        text_y = std::max(text_y, rect.y + bevel_depth);
                        text_y = std::min(text_y, rect.y + rect.h - bevel_depth - th);
                        SDL_Rect dst{ rect.x + (rect.w - tw) / 2, text_y, tw, th };
                        SDL_RenderCopy(r, tex, nullptr, &dst);
                        SDL_DestroyTexture(tex);
                    }
                }
            }
        };

        render_button(delete_yes_rect_, delete_yes_hovered_, delete_yes_pressed_, "Yes, delete", DMStyles::DeleteButton());
        render_button(delete_no_rect_, delete_no_hovered_, delete_no_pressed_, "Cancel", DMStyles::HeaderButton());
    }

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
    const float base_scale = (player_asset->info && std::isfinite(player_asset->info->scale_factor) && player_asset->info->scale_factor >= 0.0f) ? player_asset->info->scale_factor : 1.0f;
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

    // When the Lighting section is expanded, draw a crosshair at each light's anchor
    if (lighting_section_ && lighting_section_->is_expanded() && target_asset_ && target_asset_->info.get() == info_.get()) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const SDL_Color lh = DMStyles::AccentButton().hover_bg;
        SDL_SetRenderDrawColor(r, lh.r, lh.g, lh.b, 220);

        // Compute transform consistent with runtime light rendering
        const camera& cam = assets_->getView();
        auto compute_light_transform = [&]() {
            struct Xform { float cx; float cy; float sx; float sy; } out{0,0,1,1};
            int fw = target_asset_->cached_w;
            int fh = target_asset_->cached_h;
            if ((fw <= 0 || fh <= 0)) {
                if (SDL_Texture* ft = target_asset_->get_final_texture()) {
                    SDL_QueryTexture(ft, nullptr, nullptr, &fw, &fh);
                }
            }
            if (fw <= 0) fw = 1;
            if (fh <= 0) fh = 1;

            const float base_scale = (target_asset_->info && std::isfinite(target_asset_->info->scale_factor) && target_asset_->info->scale_factor > 0.0f)
                ? target_asset_->info->scale_factor
                : 1.0f;
            const float scale = cam.get_scale();
            const float inv_scale = (scale > 0.0f) ? (1.0f / scale) : 1.0f;

            const float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
            const float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

            const float ref_sh = compute_player_screen_height(cam);
            const camera::RenderSmoothingKey smoothing_key = reinterpret_cast<camera::RenderSmoothingKey>(target_asset_);
            camera::RenderEffects ef = cam.compute_render_effects(
                SDL_Point{ target_asset_->pos.x, target_asset_->pos.y },
                base_sh,
                ref_sh,
                smoothing_key);

            SDL_Point world_point{ target_asset_->pos.x, target_asset_->pos.y };
            float adjusted_cx = ef.screen_position.x;
            if (assets_ && target_asset_) {
                // Do not apply grid parallax to the player asset
                if (!(assets_->player == target_asset_)) {
                    adjusted_cx = assets_->world_grid().parallax_adjusted_screen_x(world_point, ef.screen_position.x);
                }
            }
            const float distance_scale  = ef.distance_scale;
            const float vertical_scale  = ef.vertical_scale;

            const float width_px  = base_sw * distance_scale;
            const float height_px = base_sh * distance_scale * vertical_scale;

            out.cx = adjusted_cx;
            out.cy = ef.screen_position.y;
            out.sx = (fw > 0) ? (width_px  / static_cast<float>(fw)) : base_scale * inv_scale * distance_scale;
            out.sy = (fh > 0) ? (height_px / static_cast<float>(fh)) : base_scale * inv_scale * distance_scale * vertical_scale;
            return out;
        }();

        for (const auto& light : info_->light_sources) {
            int offx = light.offset_x;
            if (target_asset_->flipped) {
                offx = -offx;
            }
            const float cx = compute_light_transform.cx + static_cast<float>(offx) * compute_light_transform.sx;
            const float cy = compute_light_transform.cy + static_cast<float>(light.offset_y) * compute_light_transform.sy;

            const int arm = 6; // pixels
            const int ix = static_cast<int>(std::lround(cx));
            const int iy = static_cast<int>(std::lround(cy));
            SDL_RenderDrawLine(r, ix - arm, iy, ix + arm, iy);
            SDL_RenderDrawLine(r, ix, iy - arm, ix, iy + arm);
        }
    }

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

void AssetInfoUI::sync_target_tiling_state() {
    if (!info_) return;
    Asset* current_target = target_asset_;
    const bool target_valid = validate_target_asset();
    if (!assets_) {
        return;
    }

    auto compute_tiling = [&](Asset* asset) -> std::optional<Asset::TilingInfo> {
        if (!assets_) return std::nullopt;
        if (!asset || !asset->info) return std::nullopt;
        if (!asset->info->tillable) return std::nullopt;
        return assets_->compute_tiling_for_asset(asset);
    };

    auto apply_for_asset = [&](Asset* asset) {
        if (!asset) return false;
        if (asset->info.get() != info_.get()) return false;
        if (info_->tillable) {
            auto t = compute_tiling(asset);
            if (t && t->is_valid()) {
                asset->set_tiling_info(*t);
                return true;
            }
            // Fallback to disabling if invalid
            asset->set_tiling_info(std::nullopt);
            return true;
        } else {
            asset->set_tiling_info(std::nullopt);
            return true;
        }
    };

    bool updated_any = false;
    for (Asset* asset : assets_->all) {
        updated_any |= apply_for_asset(asset);
    }
    for (const auto& owned : assets_->owned_assets) {
        updated_any |= apply_for_asset(owned.get());
    }
    if (!updated_any && target_valid && current_target) {
        (void)apply_for_asset(current_target);
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
        std::string asset_key = resolve_asset_manifest_key(manifest_store_, selection);
        if (asset_key.empty()) {
            SDL_Log("Unable to resolve manifest asset for '%s'", selection.c_str());
            return;
        }
        std::vector<std::string> assets{asset_key};
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

    if (!manifest_store_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Manifest store unavailable; cannot apply settings to other assets.");
        return false;
    }

    (void)info_->commit_manifest();
    auto source_view = manifest_store_->get_asset(info_->name);
    if (!source_view || !source_view.data || !source_view.data->is_object()) {
        SDL_Log("Failed to load manifest payload for source asset '%s'", info_->name.c_str());
        return false;
    }
    const nlohmann::json& source = *source_view.data;

    bool all_success = true;
    bool any_written = false;
    for (const auto& name : asset_names) {
        if (name.empty()) {
            continue;
        }
        std::string target_key = name;
        if (auto resolved = manifest_store_->resolve_asset_name(name)) {
            target_key = *resolved;
        }

        auto session = manifest_store_->begin_asset_edit(target_key, false);
        if (!session) {
            SDL_Log("Failed to open manifest session for '%s'", target_key.c_str());
            all_success = false;
            continue;
        }

        nlohmann::json& target = session.data();
        if (!target.is_object()) {
            target = nlohmann::json::object();
        }
        if (!copy_section_from_source(section_id, source, target)) {
            continue;
        }
        if (!session.commit()) {
            SDL_Log("Failed to commit manifest changes for '%s'", target_key.c_str());
            all_success = false;
        } else {
            any_written = true;
        }
    }

    if (any_written) {
        tag_utils::notify_tags_changed();
        manifest_store_->flush();
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

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        asset->is_shaded = info_->is_shaded;
        asset->clear_render_caches();
        if (assets_) {
            // Ensure any light-map and runtime lighting consumers react immediately
            assets_->notify_light_map_asset_moved(asset);
        }
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
        assets_->notify_light_map_static_assets_changed();
    }

    if (!purge_light_cache) {
        return;
    }

    std::error_code ec;
    std::filesystem::path cache_dir = std::filesystem::path("cache") / info_->name / "lights";
    std::filesystem::remove_all(cache_dir, ec);
}

void AssetInfoUI::notify_spawn_group_entry_changed(const nlohmann::json& entry) {
    if (!assets_) {
        return;
    }
    assets_->notify_spawn_group_config_changed(entry);
}

void AssetInfoUI::notify_spawn_group_removed(const std::string& spawn_id) {
    if (!assets_) {
        return;
    }
    assets_->notify_spawn_group_removed(spawn_id);
}

void AssetInfoUI::regenerate_shadow_masks() {
    if (!info_) {
        return;
    }

    SDL_Renderer* renderer = last_renderer_;
    if (!renderer && assets_) {
        renderer = assets_->renderer();
    }

    if (!renderer) {
        return;
    }

    info_->loadAnimations(renderer);
    refresh_loaded_asset_instances();
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
    if (info_) (void)info_->commit_manifest();
}

void AssetInfoUI::open_area_editor(const std::string& name) {
    if (!info_ || !assets_) return;
    assets_->begin_area_edit_for_selected_asset(name);
}

void AssetInfoUI::open_room_area_editor(const std::string& name) {
    if (!assets_) return;
    assets_->begin_room_area_edit(name);
}

void AssetInfoUI::open_for_room_area(Room* room, const std::string& area_name) {
    area_mode_ = true;
    area_room_ = room;
    area_name_ = area_name;
    clear_info();

    sections_.clear();

    class Section_AreaSettings : public DockableCollapsible {
    public:
        AssetInfoUI* ui = nullptr; Room* room = nullptr; std::string name;
        std::unique_ptr<DMTextBox> name_box; std::unique_ptr<TextBoxWidget> name_widget;
        std::unique_ptr<DMCheckbox> cb_visible; std::unique_ptr<CheckboxWidget> cb_visible_w;
        std::unique_ptr<DMCheckbox> cb_scale; std::unique_ptr<CheckboxWidget> cb_scale_w;
        std::unique_ptr<DMNumericStepper> z_step; std::unique_ptr<StepperWidget> z_step_w;
        std::unique_ptr<DMButton> btn_edit; std::unique_ptr<ButtonWidget> btn_edit_w;
        Section_AreaSettings(): DockableCollapsible("Area Settings", false) { set_scroll_enabled(false); }
        void set_ctx(AssetInfoUI* u, Room* r, const std::string& n){ ui=u; room=r; name=n; }
        void build() override {
            Rows rows; if (!name_box) name_box = std::make_unique<DMTextBox>("Area Name", name);
            if (!name_widget) name_widget = std::make_unique<TextBoxWidget>(name_box.get(), true);
            rows.push_back({ name_widget.get() });
            if (!cb_visible) cb_visible = std::make_unique<DMCheckbox>("Visible", true);
            if (!cb_visible_w) cb_visible_w = std::make_unique<CheckboxWidget>(cb_visible.get());
            rows.push_back({ cb_visible_w.get() });
            if (!cb_scale) cb_scale = std::make_unique<DMCheckbox>("Scale to room", false);
            if (!cb_scale_w) cb_scale_w = std::make_unique<CheckboxWidget>(cb_scale.get());
            rows.push_back({ cb_scale_w.get() });
            if (!z_step) z_step = std::make_unique<DMNumericStepper>("Z Index", -1000, 1000, 0);
            if (!z_step_w) z_step_w = std::make_unique<StepperWidget>(z_step.get());
            rows.push_back({ z_step_w.get() });
            if (!btn_edit) btn_edit = std::make_unique<DMButton>("Edit Shape", &DMStyles::CreateButton(), 160, DMButton::height());
            if (!btn_edit_w) btn_edit_w = std::make_unique<ButtonWidget>(btn_edit.get(), [this](){ if (ui) ui->open_room_area_editor(name); });
            rows.push_back({ btn_edit_w.get() });
            set_rows(rows);
            if (room) {
                nlohmann::json& root = room->assets_data();
                if (root.contains("areas") && root["areas"].is_array()) {
                    for (auto& entry : root["areas"]) {
                        if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue;
                        cb_visible->set_value(entry.value("visible", true));
                        cb_scale->set_value(entry.value("scale_to_room", false));
                        z_step->set_value(entry.value("z", 0));
                        break;
                    }
                }
            }
        }
        void update(const Input& input, int w, int h) override {
            DockableCollapsible::update(input,w,h);
            if (!room) return;
            nlohmann::json& root = room->assets_data();
            if (!root.contains("areas") || !root["areas"].is_array()) return;
            for (auto& entry : root["areas"]) {
                if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue;
                if (name_box && !name_box->is_editing()) {
                    std::string desired = name_box->value();
                    if (!desired.empty() && desired != name) { if (room->rename_area(name, desired)) { name = desired; room->save_assets_json(); } }
                }
                entry["visible"] = cb_visible ? cb_visible->value() : true;
                if (cb_scale && cb_scale->value()) entry["scale_to_room"] = true; else entry.erase("scale_to_room");
                entry["z"] = z_step ? z_step->value() : 0;
                room->save_assets_json();
                break;
            }
        }
    };

    auto area_settings = std::make_unique<Section_AreaSettings>(); area_settings->set_ctx(this, area_room_, area_name_);
    sections_.push_back(std::move(area_settings));

    // Area Tags section
    class Section_AreaTags : public DockableCollapsible {
    public:
        Room* room = nullptr; std::string name; std::unique_ptr<DMTextBox> tags_box; std::unique_ptr<TextBoxWidget> tags_widget;
        Section_AreaTags(): DockableCollapsible("Area Tags", false) { set_scroll_enabled(false); }
        void set_ctx(Room* r, const std::string& n){ room=r; name=n; }
        static std::string join(const std::vector<std::string>& arr){ std::string s; for(size_t i=0;i<arr.size();++i){ if(i) s+=", "; s+=arr[i]; } return s; }
        static std::vector<std::string> split(const std::string& s){ std::vector<std::string> out; std::string cur; for(char ch: s){ if(ch==','){ if(!cur.empty()){ size_t a=cur.find_first_not_of(" \t\n\r"); size_t b=cur.find_last_not_of(" \t\n\r"); if(a!=std::string::npos) out.push_back(cur.substr(a,b-a+1)); cur.clear(); } } else { cur.push_back(ch);} } if(!cur.empty()){ size_t a=cur.find_first_not_of(" \t\n\r"); size_t b=cur.find_last_not_of(" \t\n\r"); if(a!=std::string::npos) out.push_back(cur.substr(a,b-a+1)); } return out; }
        void build() override {
            Rows rows; if (!tags_box) tags_box = std::make_unique<DMTextBox>("Tags (comma separated)", ""); if (!tags_widget) tags_widget = std::make_unique<TextBoxWidget>(tags_box.get(), true); rows.push_back({ tags_widget.get() }); set_rows(rows);
            if (room) { nlohmann::json& root = room->assets_data(); if (root.contains("areas") && root["areas"].is_array()) { for (auto& entry : root["areas"]) { if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue; std::vector<std::string> tags; if (entry.contains("tags") && entry["tags"].is_array()){ for (auto& v: entry["tags"]) if (v.is_string()) tags.push_back(v.get<std::string>());} tags_box->set_value(join(tags)); break; } } }
        }
        void update(const Input& input, int w, int h) override {
            DockableCollapsible::update(input,w,h); if (!room) return; if (tags_box && tags_box->is_editing()) return; nlohmann::json& root = room->assets_data(); if (!root.contains("areas") || !root["areas"].is_array()) return; for (auto& entry : root["areas"]) { if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue; auto tags = split(tags_box ? tags_box->value() : std::string{}); entry["tags"] = nlohmann::json::array(); for (auto& t : tags) entry["tags"].push_back(t); room->save_assets_json(); break; }
        }
    };

    auto area_tags = std::make_unique<Section_AreaTags>(); area_tags->set_ctx(area_room_, area_name_);
    sections_.push_back(std::move(area_tags));

    // Area Spacing section
    class Section_AreaSpacing : public DockableCollapsible {
    public:
        Room* room=nullptr; std::string name; std::unique_ptr<DMNumericStepper> min_same; std::unique_ptr<StepperWidget> min_same_w; std::unique_ptr<DMNumericStepper> min_all; std::unique_ptr<StepperWidget> min_all_w;
        Section_AreaSpacing(): DockableCollapsible("Area Spacing", false) { set_scroll_enabled(false); }
        void set_ctx(Room* r, const std::string& n){ room=r; name=n; }
        void build() override {
            Rows rows; if (!min_same) min_same = std::make_unique<DMNumericStepper>("Min Same Type Distance", 0, 10000, 0); if (!min_same_w) min_same_w = std::make_unique<StepperWidget>(min_same.get()); rows.push_back({ min_same_w.get() }); if (!min_all) min_all = std::make_unique<DMNumericStepper>("Min Distance All", 0, 10000, 0); if (!min_all_w) min_all_w = std::make_unique<StepperWidget>(min_all.get()); rows.push_back({ min_all_w.get() }); set_rows(rows); if (room) { nlohmann::json& root = room->assets_data(); if (root.contains("areas") && root["areas"].is_array()) { for (auto& entry : root["areas"]) { if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue; min_same->set_value(entry.value("min_same_type_distance", 0)); min_all->set_value(entry.value("min_distance_all", 0)); break; } } }
        }
        void update(const Input& input, int w, int h) override { DockableCollapsible::update(input,w,h); if (!room) return; nlohmann::json& root = room->assets_data(); if (!root.contains("areas") || !root["areas"].is_array()) return; for (auto& entry : root["areas"]) { if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue; entry["min_same_type_distance"] = min_same ? min_same->value() : 0; entry["min_distance_all"] = min_all ? min_all->value() : 0; room->save_assets_json(); break; } }
    };

    auto area_spacing = std::make_unique<Section_AreaSpacing>(); area_spacing->set_ctx(area_room_, area_name_);
    sections_.push_back(std::move(area_spacing));

    // Spawn group configuration is now embedded in the Area Tool panel (AreaOverlayEditor).

    open();
}

void AssetInfoUI::clear_area_context() {
    area_mode_ = false;
    area_room_ = nullptr;
    area_name_.clear();
    rebuild_default_sections();
}

void AssetInfoUI::rebuild_default_sections() {
    sections_.clear();

    // Zone Asset quick tools section
    auto add_zone_tools = [this]() {
        if (!info_) return;
        std::string t = info_->type;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
        if (t != std::string{"zone_asset"}) return;
        class Section_ZoneTools : public DockableCollapsible {
        public:
            AssetInfoUI* ui = nullptr;
            std::unique_ptr<DMButton> btn_edit; std::unique_ptr<ButtonWidget> btn_edit_w;
            Section_ZoneTools(): DockableCollapsible("Zone Asset", false) { set_scroll_enabled(false); }
            void set_ui(AssetInfoUI* owner) { ui = owner; }
            void build() override {
                Rows rows;
                if (!btn_edit) btn_edit = std::make_unique<DMButton>("Edit Zone Geometry", &DMStyles::CreateButton(), 200, DMButton::height());
                if (!btn_edit_w) btn_edit_w = std::make_unique<ButtonWidget>(btn_edit.get(), [this](){ if (ui) ui->open_area_editor("zone"); });
                rows.push_back({ btn_edit_w.get() });
                set_rows(rows);
            }
        };
        auto zone = std::make_unique<Section_ZoneTools>();
        zone->set_ui(this);
        sections_.push_back(std::move(zone));
    };

    add_zone_tools();

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
    spawn_groups_section_ = spawns.get();
    spawns->set_ui(this);
    spawns->set_manifest_store(manifest_store_);
    spawns->set_spawn_config_listener([this](const nlohmann::json& entry) {
        this->notify_spawn_group_entry_changed(entry);
    });
    spawns->set_spawn_group_removed_listener([this](const std::string& spawn_id) {
        this->notify_spawn_group_removed(spawn_id);
    });
    sections_.push_back(std::move(spawns));
}

bool AssetInfoUI::apply_to_assets_with_info(const std::function<void(Asset*)>& fn) {
    if (!info_) {
        return false;
    }

    std::unordered_set<Asset*> visited;
    auto visit = [&](Asset* asset) {
        if (!asset || asset->info.get() != info_.get()) {
            return;
        }
        if (!visited.insert(asset).second) {
            return;
        }
        fn(asset);
};

    if (assets_) {
        for (Asset* asset : assets_->all) {
            visit(asset);
        }
        for (const auto& owned : assets_->owned_assets) {
            visit(owned.get());
        }
    }
    visit(target_asset_);
    return !visited.empty();
}

void AssetInfoUI::refresh_loaded_asset_instances() {
    if (!info_) {
        return;
    }

    // Clear animation cache when animation sources change
    if (!info_->name.empty()) {
        std::filesystem::path asset_cache = std::filesystem::path("cache") / info_->name / "animations";
        try {
            if (std::filesystem::exists(asset_cache)) {
                std::filesystem::remove_all(asset_cache);
                std::cout << "[AssetInfoUI] Cleared animation cache for " << info_->name << " due to source changes\n";
            }
        } catch (const std::exception& ex) {
            std::cerr << "[AssetInfoUI] Failed to clear animation cache for " << info_->name
                      << ": " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[AssetInfoUI] Failed to clear animation cache for " << info_->name
                      << " due to unknown error\n";
        }
    }

    // Force scaling profile refresh for this asset
    if (!info_->name.empty()) {
        // This will trigger a new profile entry to be created/updated
        render_pipeline::ScalingLogic::LoadPrecomputedProfiles(true);
        auto profile = render_pipeline::ScalingLogic::ProfileForAsset(info_->name);
        if (profile.created_entry) {
            std::cout << "[AssetInfoUI] Updated scaling profile for " << info_->name << "\n";
        }
        info_->scale_profile_revision = profile.revision;
    }

    // First refresh direct instances
    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        asset->clear_render_caches();
        asset->clear_downscale_cache();
        asset->set_final_texture(nullptr);
        asset->current_frame = nullptr;
        asset->frame_progress = 0.0f;
        asset->static_frame = false;

        std::string desired = asset->current_animation.empty() ? std::string{"default"} : asset->current_animation;
        if (asset->anim_) {
            asset->anim_->move(SDL_Point{ 0, 0 }, desired);
        } else if (asset->info) {
            auto it = asset->info->animations.find(desired);
            if (it == asset->info->animations.end()) {
                it = asset->info->animations.find("default");
            }
            if (it == asset->info->animations.end() && !asset->info->animations.empty()) {
                it = asset->info->animations.begin();
            }
            if (it != asset->info->animations.end()) {
                auto& anim = it->second;
                asset->current_animation = it->first;
                asset->current_frame = anim.get_first_frame();
                asset->static_frame = anim.is_static() || anim.locked;
            } else {
                asset->current_animation.clear();
                asset->current_frame = nullptr;
            }
        }

        asset->refresh_cached_dimensions();
    });

    // Also refresh sourced animations that reference this animation
    if (assets_ && !info_->name.empty()) {
        for (auto& [lib_name, lib_info] : assets_->library().all()) {
            if (!lib_info || lib_name == info_->name) continue;

            // Check if this asset has animations that source from the updated asset
            bool needs_refresh = false;
            for (const auto& [anim_id, anim_data] : lib_info->animations) {
                if (anim_data.source.kind == "animation" && anim_data.source.path == info_->name) {
                    needs_refresh = true;
                    break;
                }
            }

            if (needs_refresh) {
                // Refresh instances of this sourced asset
                std::unordered_set<Asset*> visited;
                auto visit = [&](Asset* asset) {
                    if (!asset || asset->info.get() != lib_info.get()) {
                        return;
                    }
                    if (!visited.insert(asset).second) {
                        return;
                    }
                    asset->clear_render_caches();
                    asset->clear_downscale_cache();
                    asset->set_final_texture(nullptr);
                    asset->current_frame = nullptr;
                    asset->frame_progress = 0.0f;
                    asset->static_frame = false;

                    std::string desired = asset->current_animation.empty() ? std::string{"default"} : asset->current_animation;
                    if (asset->anim_) {
                        asset->anim_->move(SDL_Point{ 0, 0 }, desired);
                    } else if (asset->info) {
                        auto it = asset->info->animations.find(desired);
                        if (it == asset->info->animations.end()) {
                            it = asset->info->animations.find("default");
                        }
                        if (it == asset->info->animations.end() && !asset->info->animations.empty()) {
                            it = asset->info->animations.begin();
                        }
                        if (it != asset->info->animations.end()) {
                            auto& anim = it->second;
                            asset->current_animation = it->first;
                            asset->current_frame = anim.get_first_frame();
                            asset->static_frame = anim.is_static() || anim.locked;
                        } else {
                            asset->current_animation.clear();
                            asset->current_frame = nullptr;
                        }
                    }

                    asset->refresh_cached_dimensions();
                };

                if (assets_) {
                    for (Asset* asset : assets_->all) {
                        visit(asset);
                    }
                    for (const auto& owned : assets_->owned_assets) {
                        visit(owned.get());
                    }
                }
                updated_any = true;
            }
        }
    }

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::on_animation_document_saved() {
    if (!info_) {
        return;
    }

    // Prioritize assets_->renderer() over potentially stale last_renderer_
    SDL_Renderer* renderer = nullptr;
    if (assets_) {
        renderer = assets_->renderer();
    }

    if (!renderer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] No renderer available for animation reload");
        return;
    }

    const bool reloaded = info_->reload_animations_from_disk();
    if (!reloaded) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to reload animations for %s.", info_->name.c_str());
        return;
    }

    info_->loadAnimations(renderer);
    refresh_loaded_asset_instances();
}

bool AssetInfoUI::duplicate_current_asset(const std::string& raw_name) {
    if (!info_) return false;
    std::string name = devmode::utils::trim_whitespace_copy(raw_name);
    if (name.empty()) return false;
    if (!manifest_store_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Manifest store unavailable; cannot duplicate '%s' to '%s'", info_->name.c_str(), name.c_str());
        return false;
    }

    // Begin new asset edit; ensure it does not already exist
    auto session = manifest_store_->begin_asset_edit(name, true);
    if (!session) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to begin manifest session for '%s'", name.c_str());
        return false;
    }
    if (!session.is_new_asset()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Asset '%s' already exists", name.c_str());
        session.cancel();
        return false;
    }

    namespace fs = std::filesystem;
    fs::path base("SRC");
    fs::path src_dir;
    try {
        const std::string src_dir_str = info_->asset_dir_path();
        if (!src_dir_str.empty()) src_dir = fs::path(src_dir_str);
        if (src_dir.empty()) src_dir = base / info_->name;
    } catch (...) {
        src_dir.clear();
    }
    fs::path dst_dir = base / name;

    try {
        if (!fs::exists(base)) {
            fs::create_directories(base);
        }
        if (fs::exists(dst_dir)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Destination directory '%s' already exists", dst_dir.string().c_str());
            session.cancel();
            return false;
        }
        fs::create_directories(dst_dir);

        // Copy folder content if source exists
        std::error_code ec;
        if (!src_dir.empty() && fs::exists(src_dir, ec)) {
            fs::copy(src_dir, dst_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Some files failed to copy from '%s' to '%s': %s", src_dir.string().c_str(), dst_dir.string().c_str(), ec.message().c_str());
            }
        }

        // Build new manifest entry: start from source manifest if available
        nlohmann::json manifest_entry;
        if (manifest_store_) {
            auto view = manifest_store_->get_asset(info_->name);
            if (view && view.data) {
                manifest_entry = *view.data;
            }
        }
        if (!manifest_entry.is_object()) manifest_entry = nlohmann::json::object();

        const std::string dst_dir_str = dst_dir.lexically_normal().generic_string();
        manifest_entry["asset_name"] = name;
        manifest_entry["asset_directory"] = dst_dir_str;
        // Maintain animations/tags etc.; update start path if present
        manifest_entry["start"] = dst_dir_str;

        session.data() = manifest_entry;
        if (!session.commit()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to commit manifest entry for '%s'", name.c_str());
            std::error_code cleanup_ec;
            fs::remove_all(dst_dir, cleanup_ec);
            return false;
        }
        manifest_store_->flush();

        // Reload asset library and animations
        if (assets_) {
            assets_->library().load_all_from_SRC();
            if (SDL_Renderer* renderer = assets_->renderer()) {
                assets_->library().ensureAllAnimationsLoaded(renderer);
            }
            assets_->show_dev_notice(std::string("Duplicated asset as '") + name + "'");
        }
        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Exception duplicating asset '%s' -> '%s': %s", info_->name.c_str(), name.c_str(), e.what());
        std::error_code cleanup_ec;
        fs::remove_all(dst_dir, cleanup_ec);
        return false;
    }
}

void AssetInfoUI::request_delete_current_asset() {
    if (!info_) return;
    PendingDeleteInfo pending;
    pending.name = info_->name;
    pending.asset_dir = info_->asset_dir_path();
    if (pending.asset_dir.empty() && !info_->name.empty()) {
        pending.asset_dir = (std::filesystem::path("SRC") / info_->name).lexically_normal().string();
    }
    pending_delete_ = std::move(pending);
    showing_delete_popup_ = true;
    delete_yes_hovered_ = delete_no_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = false;
}

void AssetInfoUI::cancel_delete_request() {
    showing_delete_popup_ = false;
    clear_delete_state();
}

void AssetInfoUI::confirm_delete_request() {
    if (!pending_delete_) {
        clear_delete_state();
        showing_delete_popup_ = false;
        return;
    }

    const PendingDeleteInfo pending = *pending_delete_;
    const std::string asset_name = pending.name;
    const std::filesystem::path asset_dir = pending.asset_dir.empty() ? std::filesystem::path("SRC") / asset_name : std::filesystem::path(pending.asset_dir);
    const std::filesystem::path cache_dir = std::filesystem::path("cache") / asset_name;

    showing_delete_popup_ = false;

    // Remove live instances
    if (assets_) {
        assets_->clear_editor_selection();
        std::vector<Asset*> doomed;
        doomed.reserve(assets_->all.size());
        for (Asset* asset : assets_->all) {
            if (!asset || !asset->info) continue;
            if (asset->info->name == asset_name) {
                doomed.push_back(asset);
            }
        }
        for (Asset* asset : doomed) {
            asset->Delete();
        }
    }

    bool manifest_flush_required = false;
    if (!asset_name.empty()) {
        if (manifest_store_) {
            bool removed = manifest_store_->remove_asset(asset_name);
            if (!removed) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to remove '%s' from manifest", asset_name.c_str());
            } else {
                manifest_flush_required = true;
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Manifest store unavailable; manifest not updated for '%s'", asset_name.c_str());
        }
    }

    auto remove_directory_if_exists = [](const std::filesystem::path& path) {
        std::error_code ec;
        if (path.empty()) return true;
        if (!std::filesystem::exists(path, ec)) return true;
        std::filesystem::remove_all(path, ec);
        return !ec;
    };

    if (!asset_dir.empty()) {
        const std::string dir_name = asset_dir.filename().string();
        const bool is_src_root = (dir_name == "SRC" && asset_dir.parent_path().empty());
        if (!is_src_root) {
            remove_directory_if_exists(asset_dir);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Refusing to remove root SRC directory");
        }
    }
    if (!asset_name.empty()) {
        remove_directory_if_exists(cache_dir);
    }

    if (!asset_name.empty() && manifest_store_) {
        const nlohmann::json& manifest = manifest_store_->manifest_json();
        auto maps_it = manifest.find("maps");
        if (maps_it != manifest.end() && maps_it->is_object()) {
            for (auto it = maps_it->begin(); it != maps_it->end(); ++it) {
                nlohmann::json map_entry = *it;
                if (devmode::manifest_utils::remove_asset_from_spawn_groups(map_entry, asset_name)) {
                    if (!manifest_store_->update_map_entry(it.key(), map_entry)) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to update manifest map entry '%s' while removing '%s'", it.key().c_str(), asset_name.c_str());
                    } else {
                        manifest_flush_required = true;
                    }
                }
            }
        }

        auto assets_it = manifest.find("assets");
        if (assets_it != manifest.end() && assets_it->is_object()) {
            for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
                const std::string& referenced_asset = it.key();
                if (referenced_asset == asset_name) continue;
                auto transaction = manifest_store_->begin_asset_transaction(referenced_asset);
                if (!transaction) continue;
                if (devmode::manifest_utils::remove_asset_from_spawn_groups(transaction.data(), asset_name)) {
                    if (!transaction.finalize()) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to update manifest asset entry '%s' while removing '%s'", referenced_asset.c_str(), asset_name.c_str());
                    } else {
                        manifest_flush_required = true;
                    }
                }
            }
        }
    }

    if (manifest_store_ && manifest_flush_required) {
        manifest_store_->flush();
    }

    if (assets_ && !asset_name.empty()) {
        assets_->library().remove(asset_name);
        if (SDL_Renderer* renderer = assets_->renderer()) {
            assets_->library().ensureAllAnimationsLoaded(renderer);
        }
    }

    // Close the editor if we just deleted the current asset
    if (info_ && info_->name == asset_name) {
        clear_info();
        close();
    }

    clear_delete_state();
}

void AssetInfoUI::clear_delete_state() {
    pending_delete_.reset();
    delete_yes_hovered_ = delete_no_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = false;
    delete_modal_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_yes_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_no_rect_ = SDL_Rect{0, 0, 0, 0};
}

void AssetInfoUI::update_delete_modal_geometry(int screen_w, int screen_h) {
    const int modal_w = 420;
    const int modal_h = 160;
    delete_modal_rect_ = SDL_Rect{
        std::max(0, screen_w / 2 - modal_w / 2), std::max(0, screen_h / 2 - modal_h / 2), modal_w, modal_h };
    const int button_w = 140;
    const int button_h = 40;
    const int button_gap = 20;
    const int total_w = button_w * 2 + button_gap;
    const int buttons_x = delete_modal_rect_.x + (delete_modal_rect_.w - total_w) / 2;
    const int buttons_y = delete_modal_rect_.y + delete_modal_rect_.h - button_h - 20;
    delete_yes_rect_ = SDL_Rect{ buttons_x, buttons_y, button_w, button_h };
    delete_no_rect_ = SDL_Rect{ buttons_x + button_w + button_gap, buttons_y, button_w, button_h };
}

bool AssetInfoUI::handle_delete_modal_event(const SDL_Event& e) {
    if (!showing_delete_popup_) return false;
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        delete_yes_hovered_ = SDL_PointInRect(&p, &delete_yes_rect_);
        delete_no_hovered_ = SDL_PointInRect(&p, &delete_no_rect_);
        return SDL_PointInRect(&p, &delete_modal_rect_);
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &delete_yes_rect_)) { delete_yes_pressed_ = true; return true; }
        if (SDL_PointInRect(&p, &delete_no_rect_)) { delete_no_pressed_ = true; return true; }
        if (SDL_PointInRect(&p, &delete_modal_rect_)) return true;
        return false;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        const bool inside_yes = SDL_PointInRect(&p, &delete_yes_rect_);
        const bool inside_no  = SDL_PointInRect(&p, &delete_no_rect_);
        bool consumed = SDL_PointInRect(&p, &delete_modal_rect_);
        if (inside_yes && delete_yes_pressed_) { delete_yes_pressed_ = false; delete_no_pressed_ = false; confirm_delete_request(); return true; }
        if (inside_no  && delete_no_pressed_)  { delete_yes_pressed_ = false; delete_no_pressed_ = false; cancel_delete_request();  return true; }
        delete_yes_pressed_ = false; delete_no_pressed_ = false; return consumed;
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_y || e.key.keysym.sym == SDLK_SPACE) { confirm_delete_request(); return true; }
        if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_n) { cancel_delete_request(); return true; }
        return true;
    }
    if (e.type == SDL_TEXTINPUT) {
        return true;
    }
    return false;
}
