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
#include <sstream>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <functional>

#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"
#include "utils/string_utils.hpp"
#include "utils/cache_manager.hpp"
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
#include "core/manifest/manifest_loader.hpp"
#include "world/world_grid.hpp"
#include "world/chunk.hpp"
#include "utils/map_grid_settings.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "asset_sections/animation_editor_window/AnimationEditorWindow.hpp"
#include "asset_sections/animation_editor_window/AnimationDocument.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "render/warped_screen_grid.hpp"
#include "render/render.hpp"
#include "search_assets.hpp"
#include "dev_mode/widgets/ChildrenTimelinesPanel.hpp"
#include "draw_utils.hpp"
#include <SDL_ttf.h>
#include "dev_mode/manifest_spawn_group_utils.hpp"
#include "dev_mode/manifest_asset_utils.hpp"
#include "dev_mode/asset_paths.hpp"

#include "dev_mode/animation_runtime_refresh.hpp"
#include "utils/rebuild_queue.hpp"

namespace asset_paths = devmode::asset_paths;

namespace {

using vibble::strings::to_lower_copy;

void configure_panel_for_container(DockableCollapsible* panel) {
    if (!panel) {
        return;
    }

    panel->set_floatable(false);
    panel->set_close_button_enabled(false);
    panel->set_show_header(true);
    panel->set_scroll_enabled(false);
    panel->reset_scroll();
    panel->set_visible(true);
    panel->force_pointer_ready();
    panel->set_embedded_focus_state(false);
    panel->set_embedded_interaction_enabled(false);
}

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

            changed |= copy_key("tileable");
            changed |= copy_key("tillable");
            break;
        }
        case AssetInfoSectionId::Tags:
            changed |= copy_key("tags");
            changed |= copy_key("anti_tags");
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

struct LightTransform {
    float cx = 0.0f;
    float cy = 0.0f;
    float sx = 1.0f;
    float sy = 1.0f;
};

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
                if (animation_editor_rect_.w > 0 && animation_editor_rect_.h > 0) {
                    animation_editor_window_->set_bounds(animation_editor_rect_);
                }
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

    container_.set_header_text_provider([this]() { return info_ ? info_->name : std::string(); });

    container_.set_scrollbar_visible(true);
    container_.set_content_clip_enabled(false);

    container_.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        int y = ctx.content_top;
        section_bounds_.resize(sections_.size());
        const int embed_screen_h = last_screen_h_ > 0 ? last_screen_h_ : std::max(1, ctx.content_width);
        for (size_t i = 0; i < sections_.size(); ++i) {
            auto& section = sections_[i];
            if (!section) {
                section_bounds_[i] = SDL_Rect{0,0,0,0};
                continue;
            }
            int measured = section->embedded_height(ctx.content_width, embed_screen_h);
            SDL_Rect rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, measured};
            section_bounds_[i] = rect;
            y += measured + ctx.gap;
        }
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
        return y;
    });

    container_.set_render_function([this](SDL_Renderer* renderer) {
        for (size_t i = 0; i < sections_.size(); ++i) {
            auto& section = sections_[i];
            if (!section) continue;
            SDL_Rect bounds = (i < section_bounds_.size()) ? section_bounds_[i] : SDL_Rect{0,0,0,0};
            section->render_embedded(renderer, bounds, last_screen_w_, last_screen_h_);
        }
        if (configure_btn_) configure_btn_->render(renderer);
        if (duplicate_btn_) duplicate_btn_->render(renderer);
        if (delete_btn_) delete_btn_->render(renderer);
    });

    container_.set_on_close([this]() { this->close(); });

    container_.set_update_function([this](const Input& input, int screen_w, int screen_h) {

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
        if (handle_section_focus_event(e)) {
            return true;
        }
        if (focused_section_ && focused_section_->handle_event(e)) {
            return true;
        }
        if (configure_btn_widget_ && configure_btn_widget_->handle_event(e)) {
            return true;
        }
        if (duplicate_btn_widget_ && duplicate_btn_widget_->handle_event(e)) return true;
        if (delete_btn_widget_ && delete_btn_widget_->handle_event(e)) return true;
        return false;
    });
}

AssetInfoUI::~AssetInfoUI() {
    apply_camera_override(false);
    sync_map_light_panel_visibility(false);
    if (assets_) {

    }
    if (assets_ && forcing_high_quality_rendering_) {

    }
    forcing_high_quality_rendering_ = false;
    cancel_color_sampling(true);
    if (color_sampling_cursor_handle_) {
        SDL_FreeCursor(color_sampling_cursor_handle_);
        color_sampling_cursor_handle_ = nullptr;
    }
    destroy_mask_preview_texture();
}

void AssetInfoUI::set_assets(Assets* a) {
    if (assets_ == a) return;
    if (assets_ && forcing_high_quality_rendering_) {

        forcing_high_quality_rendering_ = false;
    }

    if (assets_) {

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
    if (children_panel_) {
        children_panel_->set_manifest_store(manifest_store_);
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
    destroy_mask_preview_texture();
    info_ = info;
    container_.reset_scroll();
    if (asset_selector_) asset_selector_->close();
    if (animation_editor_window_) {
        try {
            animation_editor_window_->set_manifest_store(manifest_store_);
            animation_editor_window_->set_on_animation_properties_changed([this](const std::string& animation_id, const nlohmann::json& properties) {
                if (info_ && info_->update_animation_properties(animation_id, properties)) {

                    refresh_loaded_asset_instances();
                }
            });
            animation_editor_window_->set_info(info_);
            if (children_panel_) {
                children_panel_->set_manifest_store(manifest_store_);
                children_panel_->set_document(animation_document());
                children_panel_->set_status_callback([](const std::string& msg, int) {
                    if (!msg.empty()) {
                        SDL_Log("[AssetInfoUI] %s", msg.c_str());
                    }
                });
                children_panel_->set_on_children_changed([this](const std::vector<std::string>& names) {
                    this->on_animation_children_changed(names);
                });
                children_panel_->refresh();
            }
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
    container_.request_layout();
}

void AssetInfoUI::clear_info() {
    sync_map_light_panel_visibility(false);
    destroy_mask_preview_texture();
    if (assets_) {

    }
    if (assets_ && forcing_high_quality_rendering_) {

        forcing_high_quality_rendering_ = false;
    }
    info_.reset();
    hovered_light_index_ = -1;
    if (lighting_section_) {
        lighting_section_->set_highlighted_light(std::nullopt);
    }
    container_.reset_scroll();
    if (asset_selector_) asset_selector_->close();
    pending_animation_editor_open_ = false;
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
    if (children_panel_) {
        children_panel_->close_overlay();
        children_panel_->set_document(nullptr);
        children_panel_->refresh();
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
    clear_section_focus();
}

void AssetInfoUI::open()  {
    visible_ = true;
    container_.open();
    apply_camera_override(true);
}
void AssetInfoUI::close() {
    if (!visible_) return;
    pending_animation_editor_open_ = false;
    apply_camera_override(false);
    visible_ = false;
    container_.close();
    clear_section_focus();
    sync_map_light_panel_visibility(false);
    if (assets_) {

    }
    if (animation_editor_window_) animation_editor_window_->set_visible(false);
    if (asset_selector_) asset_selector_->close();
    if (children_panel_) children_panel_->close_overlay();
    if (assets_ && forcing_high_quality_rendering_) {

        forcing_high_quality_rendering_ = false;
    }
    light_drag_active_ = false;
    light_drag_index_  = -1;
    hovered_light_index_ = -1;
    if (lighting_section_) {
        lighting_section_->set_highlighted_light(std::nullopt);
    }
}
void AssetInfoUI::toggle(){
    if (visible_) {
        close();
    } else {
        open();
    }
}

void AssetInfoUI::open_animation_editor_panel() {
    if (!animation_editor_window_ || !info_) {
        pending_animation_editor_open_ = false;
        return;
    }

    pending_animation_editor_open_ = true;

    if (last_screen_w_ > 0 && last_screen_h_ > 0) {
        layout_widgets(last_screen_w_, last_screen_h_);
        if (animation_editor_rect_.w > 0 && animation_editor_rect_.h > 0) {
            animation_editor_window_->set_bounds(animation_editor_rect_);
            animation_editor_window_->set_visible(true);
            pending_animation_editor_open_ = false;
        }
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

bool AssetInfoUI::is_lighting_section_expanded() const {
    return visible_ && info_ && lighting_section_ && lighting_section_->is_expanded();
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    container_.prepare_layout(screen_w, screen_h);
    const SDL_Rect& panel = container_.panel_rect();
    int editor_width = panel.x;
    int editor_y = panel.y;
    int editor_height = panel.h > 0 ? panel.h : std::max(0, screen_h - editor_y);
    if (editor_width <= 0) {

        editor_width = std::max( screen_w - std::max(panel.w, std::max(screen_w / 3, 320)), screen_w / 3);
    }
    if (editor_height <= 0) {
        editor_height = std::max(0, screen_h - editor_y);
    }
    if (editor_width <= 0 || editor_height <= 0) {
        animation_editor_rect_ = SDL_Rect{0, 0, 0, 0};
    } else {
        animation_editor_rect_ = SDL_Rect{0, editor_y, editor_width, editor_height};
    }

}

bool AssetInfoUI::handle_event(const SDL_Event& e) {

    if (color_sampling_active_) {
        const bool pointer_event =
            (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
        if (pointer_event) {
            color_sampling_cursor_.x = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
            color_sampling_cursor_.y = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (last_renderer_) {
                SDL_Rect sample_rect{ color_sampling_cursor_.x, color_sampling_cursor_.y, 1, 1 };
                Uint32 pixel = 0;
                if (SDL_RenderReadPixels(last_renderer_, &sample_rect, SDL_PIXELFORMAT_ARGB8888, &pixel, sizeof(pixel)) == 0) {
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    if (SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888)) {
                        SDL_GetRGBA(pixel, fmt, &r, &g, &b, &a);
                        SDL_FreeFormat(fmt);
                        complete_color_sampling(SDL_Color{r, g, b, a});
                        return true;
                    }
                }
            }

            cancel_color_sampling(true);
            return true;
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            cancel_color_sampling(false);
            return true;
        }

        switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEWHEEL:
            case SDL_KEYDOWN:
            case SDL_TEXTINPUT:
                return true;
            default:
                break;
        }
    }

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

    if (children_panel_ && children_panel_->overlay_visible()) {
        if (children_panel_->handle_overlay_event(e)) return true;
        if (pointer_event) {
            if (children_panel_->overlay_contains_point(pointer.x, pointer.y)) {
                return true;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                children_panel_->close_overlay();
                return true;
            }
        } else if (wheel_event) {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            if (children_panel_->overlay_contains_point(mx, my)) {
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
                return true;
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
            const WarpedScreenGrid& cam = assets_->getView();

            auto compute_light_transform = [&]() {
                LightTransform out{0.0f, 0.0f, 1.0f, 1.0f};

                int fw = target_asset_->cached_w;
                int fh = target_asset_->cached_h;
                if ((fw <= 0 || fh <= 0)) {
                    if (SDL_Texture* frame = target_asset_->get_current_frame()) {
                        SDL_QueryTexture(frame, nullptr, nullptr, &fw, &fh);
                    }
                }
                if ((fw <= 0 || fh <= 0) && target_asset_->info) {
                    fw = target_asset_->info->original_canvas_width;
                    fh = target_asset_->info->original_canvas_height;
                }
                if (target_asset_->cached_w == 0 && fw > 0) target_asset_->cached_w = fw;
                if (target_asset_->cached_h == 0 && fh > 0) target_asset_->cached_h = fh;
                if (fw <= 0) fw = 1;
                if (fh <= 0) fh = 1;

                const float base_scale = (target_asset_->info && std::isfinite(target_asset_->info->scale_factor) && target_asset_->info->scale_factor > 0.0f) ? target_asset_->info->scale_factor : 1.0f;
                const float scale = cam.get_scale();
                const float inv_scale = (scale > 0.0f) ? (1.0f / scale) : 1.0f;

                const float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
                const float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

                const float ref_sh = compute_player_screen_height(cam);
                const WarpedScreenGrid::RenderSmoothingKey smoothing_key = WarpedScreenGrid::RenderSmoothingKey(target_asset_);
                WarpedScreenGrid::RenderEffects ef = cam.compute_render_effects(
                    SDL_Point{ target_asset_->pos.x, target_asset_->pos.y },
                    base_sh,
                    ref_sh,
                    smoothing_key);

                SDL_Point world_point{ target_asset_->pos.x, target_asset_->pos.y };
                float adjusted_cx = ef.screen_position.x;
                if (assets_ && target_asset_) {

                    if (!(assets_->player == target_asset_)) {

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
                        focus_section(lighting_section_);
                        lighting_section_->expand_light_row(static_cast<std::size_t>(hovered_idx));
                        return true;
                    }
            } else if (e.type == SDL_MOUSEMOTION && light_drag_active_ && light_drag_index_ >= 0 &&
                       light_drag_index_ < static_cast<int>(info_->light_sources.size())) {
                auto& L = info_->light_sources[light_drag_index_];

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

                info_->set_lighting(info_->light_sources);
                if (lighting_section_) {
                    lighting_section_->update_light_offsets(static_cast<std::size_t>(light_drag_index_), final_off_x, final_off_y);
                }
                set_light_hover(light_drag_index_);
                this->notify_light_sources_modified(false);
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
    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;
    layout_widgets(screen_w, screen_h);

    if (animation_editor_window_) {
        animation_editor_window_->set_bounds(animation_editor_rect_);
        if (pending_animation_editor_open_ && info_ &&
            animation_editor_rect_.w > 0 && animation_editor_rect_.h > 0) {
            animation_editor_window_->set_visible(true);
            pending_animation_editor_open_ = false;
        }
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

            forcing_high_quality_rendering_ = need_high_quality;
        }
    } else {
        forcing_high_quality_rendering_ = false;
    }

    if (!visible_) return;

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

    if (children_panel_ && children_panel_->overlay_visible()) {
        children_panel_->update_overlays(input);
    }

    container_.update(input, screen_w, screen_h);

    layout_widgets(screen_w, screen_h);

    if (showing_delete_popup_) {
        update_delete_modal_geometry(screen_w, screen_h);
    }
    if (showing_duplicate_popup_) {
        SDL_StartTextInput();
    }
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_) return;

    layout_widgets(screen_w, screen_h);
    last_renderer_ = r;

    container_.render(r, screen_w, screen_h);
    if (lighting_section_) {
        lighting_section_->render_overlays(r);
    }

    if (animation_editor_window_ && animation_editor_window_->is_visible()) {
        animation_editor_window_->render(r);
    }

    if (asset_selector_ && asset_selector_->visible())
        asset_selector_->render(r);

    if (children_panel_) {
        children_panel_->render_overlays(r);
    }

    DMDropdown::render_active_options(r);

    if (color_sampling_active_ && r) {
        SDL_Rect sample_rect{ color_sampling_cursor_.x, color_sampling_cursor_.y, 1, 1 };
        Uint32 pixel = 0;
        if (SDL_RenderReadPixels(r, &sample_rect, SDL_PIXELFORMAT_ARGB8888, &pixel, sizeof(pixel)) == 0) {
            Uint8 rr = 0, gg = 0, bb = 0, aa = 0;
            if (SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888)) {
                SDL_GetRGBA(pixel, fmt, &rr, &gg, &bb, &aa);
                SDL_FreeFormat(fmt);
                const_cast<AssetInfoUI*>(this)->color_sampling_preview_ = SDL_Color{rr, gg, bb, aa};
                const_cast<AssetInfoUI*>(this)->color_sampling_preview_valid_ = true;
            } else {
                const_cast<AssetInfoUI*>(this)->color_sampling_preview_valid_ = false;
            }
        } else {
            const_cast<AssetInfoUI*>(this)->color_sampling_preview_valid_ = false;
        }

        const int preview_size = 48;
        SDL_Rect preview_rect{ color_sampling_cursor_.x + 18,
                               color_sampling_cursor_.y + 18,
                               preview_size,
                               preview_size };
        SDL_Rect inner_rect{ preview_rect.x + 4,
                             preview_rect.y + 4,
                             std::max(0, preview_rect.w - 8), std::max(0, preview_rect.h - 8) };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_Color border = DMStyles::Border();
        SDL_Color bg = dm_draw::DarkenColor(DMStyles::PanelBG(), 0.1f);
        SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 220);
        SDL_RenderFillRect(r, &preview_rect);
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(r, &preview_rect);
        if (color_sampling_preview_valid_) {
            SDL_Color fill = color_sampling_preview_;
            SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
            SDL_RenderFillRect(r, &inner_rect);
            SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
            SDL_RenderDrawRect(r, &inner_rect);
        }
    }

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
    WarpedScreenGrid& cam = assets_->getView();
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

float AssetInfoUI::compute_player_screen_height(const WarpedScreenGrid& cam) const {
    if (!assets_ || !assets_->player) return 1.0f;
    Asset* player_asset = assets_->player;
    if (!player_asset) return 1.0f;

    SDL_Texture* player_frame = player_asset->get_current_frame();
    if (!player_frame && player_asset->info && player_asset->info->animations.count(player_asset->current_animation)) {
        AnimationFrame* frame = player_asset->info->animations[player_asset->current_animation].get_first_frame();
        if (frame && !frame->variants.empty()) {
            player_frame = frame->get_base_texture(0);
        }
    }
    int pw = player_asset->cached_w;
    int ph = player_asset->cached_h;
    if ((pw == 0 || ph == 0) && player_frame) {
        SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
    }
    if ((pw == 0 || ph == 0) && player_asset->info) {
        pw = player_asset->info->original_canvas_width;
        ph = player_asset->info->original_canvas_height;
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

void AssetInfoUI::render_world_overlay(SDL_Renderer* r, const WarpedScreenGrid& cam) const {
    if (!visible_ || !info_) return;

    validate_target_asset();

    float reference_screen_height = compute_player_screen_height(cam);

    if (basic_info_section_ && basic_info_section_->is_expanded()) {
        basic_info_section_->render_world_overlay(r, cam, target_asset_, reference_screen_height);
    }

    if (lighting_section_ && lighting_section_->is_expanded() && target_asset_ && target_asset_->info.get() == info_.get()) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        const SDL_Color lh = DMStyles::AccentButton().hover_bg;
        SDL_SetRenderDrawColor(r, lh.r, lh.g, lh.b, 220);

        const WarpedScreenGrid& cam = assets_->getView();
        auto compute_light_transform = [&]() {
            LightTransform out{0.0f, 0.0f, 1.0f, 1.0f};
            int fw = target_asset_->cached_w;
            int fh = target_asset_->cached_h;
            if ((fw <= 0 || fh <= 0)) {
                if (SDL_Texture* frame = target_asset_->get_current_frame()) {
                    SDL_QueryTexture(frame, nullptr, nullptr, &fw, &fh);
                }
            }
            if ((fw <= 0 || fh <= 0) && target_asset_->info) {
                fw = target_asset_->info->original_canvas_width;
                fh = target_asset_->info->original_canvas_height;
            }
            if (target_asset_->cached_w == 0 && fw > 0) target_asset_->cached_w = fw;
            if (target_asset_->cached_h == 0 && fh > 0) target_asset_->cached_h = fh;
            if (fw <= 0) fw = 1;
            if (fh <= 0) fh = 1;

            const float base_scale = (target_asset_->info && std::isfinite(target_asset_->info->scale_factor) && target_asset_->info->scale_factor > 0.0f) ? target_asset_->info->scale_factor : 1.0f;
            const float scale = cam.get_scale();
            const float inv_scale = (scale > 0.0f) ? (1.0f / scale) : 1.0f;

            const float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
            const float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

            const float ref_sh = compute_player_screen_height(cam);
            const WarpedScreenGrid::RenderSmoothingKey smoothing_key = WarpedScreenGrid::RenderSmoothingKey(target_asset_);
            WarpedScreenGrid::RenderEffects ef = cam.compute_render_effects(
                SDL_Point{ target_asset_->pos.x, target_asset_->pos.y },
                base_sh,
                ref_sh,
                smoothing_key);

            SDL_Point world_point{ target_asset_->pos.x, target_asset_->pos.y };
            float adjusted_cx = ef.screen_position.x;
            if (assets_ && target_asset_) {

                if (!(assets_->player == target_asset_)) {

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

        const int crosshair_arm_px = 6;
        const int crosshair_thickness_px = 3;
        const int offset_start = -crosshair_thickness_px / 2;
        const int offset_end = crosshair_thickness_px / 2;
        auto draw_thick_line = [&](int x1, int y1, int x2, int y2) {
            if (y1 == y2) {
                for (int offset = offset_start; offset <= offset_end; ++offset) {
                    SDL_RenderDrawLine(r, x1, y1 + offset, x2, y2 + offset);
                }
                return;
            }
            for (int offset = offset_start; offset <= offset_end; ++offset) {
                SDL_RenderDrawLine(r, x1 + offset, y1, x2 + offset, y2);
            }
};

        for (const auto& light : info_->light_sources) {
            int offx = light.offset_x;
            if (target_asset_->flipped) {
                offx = -offx;
            }
            const float cx = compute_light_transform.cx + static_cast<float>(offx) * compute_light_transform.sx;
            const float cy = compute_light_transform.cy + static_cast<float>(light.offset_y) * compute_light_transform.sy;

            const int ix = static_cast<int>(std::lround(cx));
            const int iy = static_cast<int>(std::lround(cy));
            draw_thick_line(ix - crosshair_arm_px, iy, ix + crosshair_arm_px, iy);
            draw_thick_line(ix, iy - crosshair_arm_px, ix, iy + crosshair_arm_px);
        }
    }

}

void AssetInfoUI::refresh_target_asset_scale() {
    if (!info_) return;

    Asset* current_target = target_asset_;
    const bool target_valid = validate_target_asset();
    Asset* validated_target = target_asset_;

    const auto refresh_asset = [&](Asset* asset, bool force_update = false) {
        if (!asset || !asset->info) {
            return false;
        }
        if (!force_update && !asset_matches_current_info(asset)) {
            return false;
        }
        asset->info->set_scale_factor(info_->scale_factor);
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
    }

    if (target_valid && validated_target) {
        if (refresh_asset(validated_target, true)) {
            refreshed_any = true;
        }
    }

    if (current_target && current_target != validated_target) {
        if (refresh_asset(current_target, true)) {
            refreshed_any = true;
        }
    }

    if (assets_ && refreshed_any) {

        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::sync_target_z_threshold() {
    if (!info_) return;

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        if (!asset->info) {
            return;
        }
        asset->info->set_z_threshold(info_->z_threshold);
        asset->set_z_index();
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::begin_color_sampling(const utils::color::RangedColor&,
                                       std::function<void(SDL_Color)> on_sample,
                                       std::function<void()> on_cancel) {
    if (!on_sample) {
        if (on_cancel) {
            on_cancel();
        }
        return;
    }
    cancel_color_sampling(true);
    color_sampling_active_ = true;
    color_sampling_preview_valid_ = false;
    color_sampling_apply_ = std::move(on_sample);
    color_sampling_cancel_ = std::move(on_cancel);
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    color_sampling_cursor_.x = mx;
    color_sampling_cursor_.y = my;
    if (!color_sampling_cursor_handle_) {
        color_sampling_cursor_handle_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    }
    color_sampling_prev_cursor_ = SDL_GetCursor();
    if (color_sampling_cursor_handle_) {
        SDL_SetCursor(color_sampling_cursor_handle_);
    }
}

void AssetInfoUI::cancel_color_sampling(bool silent) {
    if (!color_sampling_active_) {
        return;
    }
    color_sampling_active_ = false;
    color_sampling_preview_valid_ = false;
    if (color_sampling_prev_cursor_) {
        SDL_SetCursor(color_sampling_prev_cursor_);
        color_sampling_prev_cursor_ = nullptr;
    }
    auto cancel_cb = std::move(color_sampling_cancel_);
    color_sampling_apply_ = nullptr;
    color_sampling_cancel_ = nullptr;
    if (!silent && cancel_cb) {
        cancel_cb();
    }
}

void AssetInfoUI::complete_color_sampling(SDL_Color color) {
    auto apply_cb = std::move(color_sampling_apply_);
    cancel_color_sampling(true);
    if (apply_cb) {
        apply_cb(color);
    }
}

void AssetInfoUI::apply_section_focus_states() {
    for (auto& section : sections_) {
        if (!section) {
            continue;
        }
        const bool focused = (section.get() == focused_section_);
        section->set_embedded_focus_state(focused);
        section->set_embedded_interaction_enabled(focused);
    }
}

void AssetInfoUI::focus_section(DockableCollapsible* section) {
    DockableCollapsible* resolved = nullptr;
    if (section) {
        for (auto& entry : sections_) {
            if (entry.get() == section) {
                resolved = section;
                break;
            }
        }
    }
    DockableCollapsible* previous = focused_section_;
    focused_section_ = resolved;
    apply_section_focus_states();
    if (focused_section_) {
        focused_section_->force_pointer_ready();
        if (!focused_section_->is_expanded()) {
            focused_section_->set_expanded(true);
        }
    }
    if (previous != focused_section_) {
        container_.request_layout();
    }
}

void AssetInfoUI::clear_section_focus() {
    focus_section(nullptr);
}

DockableCollapsible* AssetInfoUI::section_at_point(SDL_Point p) const {
    for (size_t i = 0; i < sections_.size(); ++i) {
        auto* section = sections_[i].get();
        if (!section) {
            continue;
        }
        SDL_Rect bounds = (i < section_bounds_.size()) ? section_bounds_[i] : section->rect();
        if (bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }
        if (SDL_PointInRect(&p, &bounds)) {
            return section;
        }
    }
    return nullptr;
}

bool AssetInfoUI::handle_section_focus_event(const SDL_Event& e) {
    if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) {
        return false;
    }
    SDL_Point pointer{e.button.x, e.button.y};
    DockableCollapsible* target = section_at_point(pointer);
    if (!target) {
        return false;
    }
    if (target == focused_section_) {
        return false;
    }
    focus_section(target);
    return true;
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
        if (!asset_matches_current_info(asset)) return false;
        if (asset->info) {
            asset->info->set_tillable(info_->tillable);
        }
        if (info_->tillable) {
            auto t = compute_tiling(asset);
            if (t && t->is_valid()) {
                asset->set_tiling_info(*t);
                return true;
            }

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
    if (!updated_any && target_valid && current_target) {
        (void)apply_for_asset(current_target);
    }
    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
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
        if (asset->info) {
            asset->info->set_lighting(info_->light_sources);
            asset->info->is_light_source = info_->is_light_source;
            asset->info->moving_asset = info_->moving_asset;
        }
        asset->is_shaded = info_->is_shaded;
        asset->mark_composite_dirty();
        asset->clear_render_caches();
        if (assets_) {
            assets_->ensure_light_textures_loaded(asset);

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

void AssetInfoUI::mark_target_asset_composite_dirty() {
    if (!assets_ || !target_asset_) {
        return;
    }
    target_asset_->mark_composite_dirty();
    assets_->mark_active_assets_dirty();
}

void AssetInfoUI::mark_light_for_rebuild(std::size_t light_index) {
    if (!info_) {
        return;
    }
    vibble::RebuildQueueCoordinator coordinator;
    coordinator.request_light_entry(info_->name, static_cast<int>(light_index));
    coordinator.run_light_tool();

    SDL_Renderer* renderer = assets_ ? assets_->renderer() : nullptr;
    if (renderer) {
        info_->rebuild_light_texture(renderer, light_index);
    }

    apply_to_assets_with_info([&](Asset* asset) {
        if (!asset || !asset->info) return;
        asset->info->set_lighting(info_->light_sources);
        asset->info->moving_asset = info_->moving_asset;
        if (renderer && assets_) {
            assets_->ensure_light_textures_loaded(asset);
        }
        asset->clear_render_caches();
        if (assets_) {
            assets_->notify_light_map_asset_moved(asset);
        }
    });

    if (assets_) {
        assets_->mark_active_assets_dirty();
        assets_->notify_light_map_static_assets_changed();
    }
}

void AssetInfoUI::mark_lighting_asset_for_rebuild() {
    if (!info_) {
        return;
    }
    vibble::RebuildQueueCoordinator coordinator;
    coordinator.request_light(info_->name);
    coordinator.run_light_tool();

    SDL_Renderer* renderer = assets_ ? assets_->renderer() : nullptr;
    if (renderer) {
        for (std::size_t i = 0; i < info_->light_sources.size(); ++i) {
            info_->rebuild_light_texture(renderer, i);
        }
    }

    apply_to_assets_with_info([&](Asset* asset) {
        if (!asset || !asset->info) return;
        asset->info->set_lighting(info_->light_sources);
        asset->info->is_light_source = info_->is_light_source;
        asset->info->moving_asset = info_->moving_asset;
        if (renderer && assets_) {
            assets_->ensure_light_textures_loaded(asset);
        }
        asset->clear_render_caches();
        if (assets_) {
            assets_->notify_light_map_asset_moved(asset);
        }
    });

    if (assets_) {
        assets_->mark_active_assets_dirty();
        assets_->notify_light_map_static_assets_changed();
    }
}

void AssetInfoUI::sync_target_shading_settings() {
    if (!info_) {
        return;
    }

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        if (!asset->info) {
            return;
        }
        asset->info->set_shading_enabled(info_->is_shaded);
        asset->info->set_shadow_mask_settings(info_->shadow_mask_settings);
        asset->info->set_shading_parallax_amount(info_->shading_parallax_amount);
        asset->info->set_shading_screen_brightness_multiplier(info_->shading_screen_brightness_multiplier);
        asset->info->set_shading_opacity_multiplier(info_->shading_opacity_multiplier);
        asset->is_shaded = info_->is_shaded;
        asset->clear_render_caches();
    });

    if (updated_any && assets_) {
        assets_->force_shaded_assets_rerender();
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::sync_target_spacing_settings() {
    if (!info_) {
        return;
    }

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        if (!asset->info) {
            return;
        }
        asset->info->set_min_same_type_distance(info_->min_same_type_distance);
        asset->info->set_min_distance_all(info_->min_distance_all);
        asset->info->set_neighbor_search_radius(info_->NeighborSearchRadius);
        asset->NeighborSearchRadius = info_->NeighborSearchRadius;
        asset->clear_grid_residency_cache();
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::sync_target_tags() {
    if (!info_) {
        return;
    }

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        if (!asset->info) {
            return;
        }
        asset->info->set_tags(info_->tags);
        asset->info->set_anti_tags(info_->anti_tags);
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::sync_animation_children() {
    if (!info_) {
        return;
    }

    info_->set_animation_children(info_->animation_children);
    (void)info_->commit_manifest();

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        if (!asset || !asset->info) {
            return;
        }
        if (asset->info != info_) {
            asset->info->set_animation_children(info_->animation_children);
        }
        asset->rebuild_animation_runtime();
        asset->initialize_animation_children_recursive();
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::sync_target_basic_render_settings(bool type_changed) {
    if (!info_) {
        return;
    }

    bool updated_any = apply_to_assets_with_info([&](Asset* asset) {
        if (!asset->info) {
            return;
        }
        asset->info->set_asset_type(info_->type);
        asset->info->set_flipable(info_->flipable);
        asset->info->set_apply_distance_scaling(info_->apply_distance_scaling);
        asset->info->set_apply_vertical_scaling(info_->apply_vertical_scaling);
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
        if (type_changed) {
            assets_->refresh_active_asset_lists();
        }
    }
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

    destroy_mask_preview_texture();

    SDL_Renderer* renderer = last_renderer_;
    if (!renderer && assets_) {
        renderer = assets_->renderer();
    }

    if (!renderer) {
        return;
    }
    last_renderer_ = renderer;

    vibble::RebuildQueueCoordinator coordinator;
    coordinator.request_asset(info_->name);
    std::cout << "[AssetInfoUI] Marked " << info_->name
              << " for mask regeneration. Run Rebuild Assets to process queued work." << "\n";

    info_->loadAnimations(renderer);
    (void)generate_mask_preview();
    refresh_loaded_asset_instances();
}

void AssetInfoUI::destroy_mask_preview_texture() {
    if (mask_preview_texture_) {
        SDL_DestroyTexture(mask_preview_texture_);
        mask_preview_texture_ = nullptr;
    }
    mask_preview_w_ = 0;
    mask_preview_h_ = 0;
}

bool AssetInfoUI::load_mask_preview_texture(const std::filesystem::path& png_path) {
    if (!last_renderer_) {
        return false;
    }
    SDL_Surface* surface = CacheManager::load_surface(png_path.generic_string());
    if (!surface) {
        return false;
    }
    SDL_Texture* tex = CacheManager::surface_to_texture(last_renderer_, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);
    if (!tex) {
        return false;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    destroy_mask_preview_texture();
    mask_preview_texture_ = tex;
    mask_preview_w_ = w;
    mask_preview_h_ = h;
    return true;
}

std::filesystem::path AssetInfoUI::resolve_mask_preview_frame_path() const {
    if (!info_) {
        return {};
    }
    const std::filesystem::path root = std::filesystem::path(manifest::manifest_path()).parent_path();
    std::filesystem::path cache_root = root / "cache" / info_->name / "animations";
    auto try_animation = [&](const std::string& anim_id) -> std::filesystem::path {
        if (anim_id.empty()) return {};
        std::filesystem::path candidate = cache_root / anim_id / "scale_100" / "normal" / "0.png";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
        std::filesystem::path anim_root = cache_root / anim_id;
        if (!std::filesystem::exists(anim_root, ec) || ec) {
            return {};
        }
        for (const auto& entry : std::filesystem::directory_iterator(anim_root, ec)) {
            if (ec) break;
            if (!entry.is_directory()) continue;
            std::filesystem::path alt = entry.path() / "normal" / "0.png";
            if (std::filesystem::exists(alt, ec) && !ec) {
                return alt;
            }
        }
        return {};
};

    std::string preferred = info_->start_animation.empty() ? std::string{"default"} : info_->start_animation;
    std::filesystem::path path = try_animation(preferred);
    if (!path.empty()) {
        return path;
    }
    if (preferred != "default") {
        path = try_animation("default");
        if (!path.empty()) return path;
    }
    std::error_code ec;
    if (std::filesystem::exists(cache_root, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(cache_root, ec)) {
            if (ec) break;
            if (!entry.is_directory()) continue;
            std::filesystem::path alt = entry.path() / "scale_100" / "normal" / "0.png";
            if (std::filesystem::exists(alt, ec) && !ec) {
                return alt;
            }
        }
    }
    return {};
}

bool AssetInfoUI::generate_mask_preview() {
    if (!info_ || !info_->is_shaded) {
        destroy_mask_preview_texture();
        return false;
    }
    SDL_Renderer* renderer = last_renderer_;
    if (!renderer && assets_) {
        renderer = assets_->renderer();
    }
    if (!renderer) {
        return false;
    }
    last_renderer_ = renderer;

    std::filesystem::path input_png = resolve_mask_preview_frame_path();
    if (input_png.empty()) {
        std::cerr << "[AssetInfoUI] Unable to locate cached frame for mask preview of "
                  << info_->name << "\n";
        return false;
    }

    try {
        const ShadowMaskSettings settings = SanitizeShadowMaskSettings(info_->shadow_mask_settings);
        const std::filesystem::path manifest_path = manifest::manifest_path();
        const std::filesystem::path root          = manifest_path.parent_path();
        const std::filesystem::path script        = root / "tools" / "shadow_mask.py";
        const std::filesystem::path output_png    = root / "cache" / info_->name / "mask_preview.png";
        const std::filesystem::path meta_path     = root / "cache" / info_->name / "mask_preview_meta.json";

        if (!std::filesystem::exists(script)) {
            std::cerr << "[AssetInfoUI] shadow_mask.py missing; cannot generate mask preview for "
                      << info_->name << "\n";
            return false;
        }

        std::ostringstream cmd;
        cmd << "python \"" << script.string() << "\" " << "\"" << input_png.string() << "\" " << "\"" << output_png.string() << "\" " << settings.expansion_ratio << " " << settings.blur_scale << " " << settings.falloff_start << " " << settings.falloff_exponent << " " << settings.alpha_multiplier << " " << "\"" << meta_path.string() << "\"";

        const std::string command = cmd.str();
        std::cout << "[AssetInfoUI] Generating mask preview with: " << command << "\n";
        int ret = std::system(command.c_str());
        if (ret != 0) {
            std::cerr << "[AssetInfoUI] shadow_mask.py exited with " << ret
                      << " while generating mask preview for " << info_->name << "\n";
            return false;
        }

        return load_mask_preview_texture(output_png);
    } catch (const std::exception& ex) {
        std::cerr << "[AssetInfoUI] Failed to generate mask preview for " << info_->name
                  << ": " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "[AssetInfoUI] Unknown error while generating mask preview for "
                  << info_->name << "\n";
    }
    return false;
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

    if (animation_editor_window_ && animation_editor_window_->is_visible()) {
        if (animation_editor_rect_.w > 0 && animation_editor_rect_.h > 0 &&
            SDL_PointInRect(&p, &animation_editor_rect_)) {
            return true;
        }
    }

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

std::shared_ptr<animation_editor::AnimationDocument> AssetInfoUI::animation_document() const {
    if (animation_editor_window_) {
        return animation_editor_window_->document();
    }
    return nullptr;
}

void AssetInfoUI::on_animation_children_changed(const std::vector<std::string>& names) {
    if (info_) {
        info_->animation_children = names;
        sync_animation_children();
    }
    if (auto doc = animation_document()) {
        try {
            doc->save_to_file();
        } catch (...) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to save animation document after child change.");
        }
    }
}
void AssetInfoUI::rebuild_default_sections() {
    sections_.clear();
    section_bounds_.clear();
    basic_info_section_ = nullptr;
    lighting_section_ = nullptr;
    shading_section_ = nullptr;
    spawn_groups_section_ = nullptr;
    focused_section_ = nullptr;
    children_panel_ = nullptr;

    auto adopt_section = [](auto* section) {
        configure_panel_for_container(section);
};

    auto finalize_section = [this](DockableCollapsible* section) {
        if (!section) {
            return;
        }
        section->set_info(info_);
        section->reset_scroll();
        section->set_expanded(false);
        try {
            section->build();
        } catch (const std::exception& ex) {
            SDL_Log("AssetInfoUI: failed to build section during initialization: %s", ex.what());
        } catch (...) {
            SDL_Log("AssetInfoUI: failed to build section during initialization due to unknown error.");
        }
};

    auto basic = std::make_unique<Section_BasicInfo>();
    basic_info_section_ = basic.get();
    basic_info_section_->set_ui(this);
    adopt_section(basic_info_section_);
    finalize_section(basic_info_section_);
    sections_.push_back(std::move(basic));

    auto tags = std::make_unique<Section_Tags>();
    tags->set_ui(this);
    adopt_section(tags.get());
    finalize_section(tags.get());
    sections_.push_back(std::move(tags));

    auto children_panel = std::make_unique<animation_editor::ChildrenTimelinesPanel>();
    children_panel_ = children_panel.get();
    adopt_section(children_panel_);
    finalize_section(children_panel_);
    sections_.push_back(std::move(children_panel));

    auto lighting = std::make_unique<Section_Lighting>();
    lighting->set_ui(this);
    lighting_section_ = lighting.get();
    adopt_section(lighting_section_);
    finalize_section(lighting_section_);
    sections_.push_back(std::move(lighting));

    auto shading = std::make_unique<Section_Shading>();
    shading->set_ui(this);
    shading_section_ = shading.get();
    adopt_section(shading_section_);
    finalize_section(shading_section_);
    sections_.push_back(std::move(shading));

    auto spacing = std::make_unique<Section_Spacing>();
    spacing->set_ui(this);
    adopt_section(spacing.get());
    finalize_section(spacing.get());
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
    adopt_section(spawn_groups_section_);
    finalize_section(spawn_groups_section_);
    sections_.push_back(std::move(spawns));

    container_.reset_scroll();
    container_.request_layout();
    clear_section_focus();
}

bool AssetInfoUI::apply_to_assets_with_info(const std::function<void(Asset*)>& fn) {
    if (!info_) {
        return false;
    }

    std::unordered_set<Asset*> visited;
    auto visit = [&](Asset* asset) {
        if (!asset_matches_current_info(asset)) {
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
    }
    visit(target_asset_);
    return !visited.empty();
}

bool AssetInfoUI::asset_matches_current_info(const Asset* asset) const {
    if (!info_ || !asset || !asset->info) {
        return false;
    }
    if (asset->info.get() == info_.get()) {
        return true;
    }
    if (!info_->name.empty() && asset->info->name == info_->name) {
        return true;
    }
    if (!info_->asset_dir_path().empty() && asset->info->asset_dir_path() == info_->asset_dir_path()) {
        return true;
    }
    return false;
}

void AssetInfoUI::refresh_loaded_asset_instances() {
    if (!info_) {
        return;
    }

    if (!info_->name.empty()) {

    }

    if (!info_->name.empty()) {

        render_pipeline::ScalingLogic::LoadPrecomputedProfiles(true);
        auto profile = render_pipeline::ScalingLogic::ProfileForAsset(info_->name);
        (void)profile;
    }

    if (assets_) {
        devmode::refresh_loaded_animation_instances(assets_, info_);
    }

    if (assets_ && !info_->name.empty()) {
        for (auto& [lib_name, lib_info] : assets_->library().all()) {
            if (!lib_info || lib_name == info_->name) continue;

            bool needs_refresh = false;
            for (const auto& [anim_id, anim_data] : lib_info->animations) {
                if (anim_data.source.kind == "animation" && anim_data.source.path == info_->name) {
                    needs_refresh = true;
                    break;
                }
            }

            if (!needs_refresh) {
                continue;
            }

            devmode::refresh_loaded_animation_instances(assets_, lib_info);
        }
    }
}

void AssetInfoUI::on_animation_document_saved() {
    if (!info_) {
        return;
    }

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
    fs::path base = asset_paths::assets_root_path();
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

        std::error_code ec;
        if (!src_dir.empty() && fs::exists(src_dir, ec)) {
            fs::copy(src_dir, dst_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Some files failed to copy from '%s' to '%s': %s", src_dir.string().c_str(), dst_dir.string().c_str(), ec.message().c_str());
            }
        }

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

        manifest_entry["start"] = dst_dir_str;

        session.data() = manifest_entry;
        if (!session.commit()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to commit manifest entry for '%s'", name.c_str());
            std::error_code cleanup_ec;
            fs::remove_all(dst_dir, cleanup_ec);
            return false;
        }
        manifest_store_->flush();

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
        pending.asset_dir = asset_paths::asset_folder_path(info_->name).generic_string();
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
    const std::filesystem::path asset_dir = pending.asset_dir.empty() ? asset_paths::asset_folder_path(asset_name) : std::filesystem::path(pending.asset_dir);
    const std::filesystem::path cache_dir = std::filesystem::path("cache") / asset_name;

    showing_delete_popup_ = false;

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
    bool manifest_entry_removed = false;
    if (!asset_name.empty()) {
        if (manifest_store_) {
            const auto remove_result = devmode::manifest_utils::remove_asset_entry(manifest_store_, asset_name, &std::cerr);
            manifest_entry_removed = remove_result.removed;
            if (!manifest_entry_removed) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to remove '%s' from manifest", asset_name.c_str());
            }
            manifest_flush_required = remove_result.used_store || manifest_flush_required;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Manifest store unavailable; manifest not updated for '%s'", asset_name.c_str());
            manifest_entry_removed = devmode::manifest_utils::remove_manifest_asset_entry(asset_name, &std::cerr);
            if (!manifest_entry_removed) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to remove '%s' from manifest assets list", asset_name.c_str());
            }
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
        const auto normalized_dir = asset_dir.lexically_normal();
        if (asset_paths::is_protected_asset_root(normalized_dir)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Refusing to remove protected asset root '%s'", normalized_dir.generic_string().c_str());
        } else {
            remove_directory_if_exists(normalized_dir);
        }
    }
    if (!asset_name.empty()) {
        remove_directory_if_exists(cache_dir);
    }

    if (!asset_name.empty() && manifest_store_ && manifest_entry_removed) {
        manifest_flush_required = manifest_flush_required || manifest_store_->dirty();
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
    }

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
