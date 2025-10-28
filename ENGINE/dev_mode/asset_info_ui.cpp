#include "asset_info_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <SDL_log.h>
#include <stdexcept>
#include <vector>
#include <unordered_set>

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
#include "dm_styles.hpp"
#include "asset_sections/Section_BasicInfo.hpp"
#include "asset_sections/Section_Tags.hpp"
#include "asset_sections/Section_Lighting.hpp"
#include "asset_sections/Section_Shading.hpp"
#include "asset_sections/Section_Spacing.hpp"
#include "spawn_group_config/SpawnGroupConfig.hpp"
#include "asset_sections/Section_SpawnGroups.hpp"
#include "dev_mode/spawn_group_config/SpawnGroupConfig.hpp"
#include "map_generation/room.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "asset_sections/animation_editor_window/AnimationEditorWindow.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "search_assets.hpp"

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
    if (animation_editor_window_) {
        animation_editor_window_->set_manifest_store(manifest_store_);
        animation_editor_window_->set_on_document_saved([this]() { this->on_animation_document_saved(); });
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
        if (!area_mode_ && configure_btn_widget_) {
            configure_btn_widget_->set_rect(SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, DMButton::height()});
            y += DMButton::height() + ctx.gap;
        }
        return y;
    });

    container_.set_render_function([this](SDL_Renderer* renderer) {
        for (auto& section : sections_) section->render(renderer);
        if (!area_mode_ && configure_btn_) configure_btn_->render(renderer);
    });

    container_.set_on_close([this]() { this->close(); });

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
    set_manifest_store(assets_ ? assets_->manifest_store() : nullptr);
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
}

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    container_.reset_scroll();
    if (asset_selector_) asset_selector_->close();
    if (animation_editor_window_) {
        try {
            animation_editor_window_->set_manifest_store(manifest_store_);
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
    if (shading_section_ && info_ && info_->is_shaded) {
        shading_section_->set_expanded(true);
    }
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
    int editor_y = panel.y;
    int editor_height = panel.h > 0 ? panel.h : std::max(0, screen_h - editor_y);
    if (editor_width <= 0 || editor_height <= 0) {
        animation_editor_rect_ = SDL_Rect{0, 0, 0, 0};
    } else {
        animation_editor_rect_ = SDL_Rect{0, editor_y, editor_width, editor_height};
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

    if (!visible_ || !info_) return;

    if (asset_selector_ && asset_selector_->visible()) {
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

    DMDropdown::render_active_options(r);

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
    });

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
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

    class Section_AreaSpawns : public DockableCollapsible {
    public:
        Room* room = nullptr; std::string name; std::unique_ptr<SpawnGroupConfig> list;
        Section_AreaSpawns(): DockableCollapsible("Area Spawn Groups", false) { set_scroll_enabled(true); }
        void set_ctx(Room* r, const std::string& n){ room=r; name=n; }
        void build() override {
            Rows rows; if (!room) { set_rows(rows); return; }
            if (!list) list = std::make_unique<SpawnGroupConfig>(false);
            list->set_embedded_mode(true);
            nlohmann::json* groups_ptr = nullptr;
            nlohmann::json& root = room->assets_data();
            if (root.contains("areas") && root["areas"].is_array()) {
                for (auto& entry : root["areas"]) {
                    if (!entry.is_object()) continue; if (entry.value("name", std::string{}) != name) continue;
                    if (!entry.contains("spawn_groups") || !entry["spawn_groups"].is_array()) entry["spawn_groups"] = nlohmann::json::array();
                    groups_ptr = &entry["spawn_groups"]; break;
                }
            }
            if (groups_ptr) {
                auto on_change = [this]() { if (room) room->save_assets_json(); };
                auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&){ if (room) room->save_assets_json(); };
                SpawnGroupConfig::Callbacks cb{};
                cb.on_add = [this, groups_ptr]() { if (!groups_ptr) return; nlohmann::json entry = nlohmann::json::object(); entry["position"] = "Random"; entry["min_number"] = 1; entry["max_number"] = 1; entry["candidates"] = nlohmann::json::array({ nlohmann::json::object({{"name","null"},{"chance",100}}) }); groups_ptr->push_back(std::move(entry)); if (room) room->save_assets_json(); list->refresh_row_configuration(); };
                cb.on_delete = [this, groups_ptr](const std::string& id) { if (!groups_ptr) return; if (!groups_ptr->is_array()) return; for (auto it = groups_ptr->begin(); it != groups_ptr->end(); ++it) { if (it->is_object() && it->value("spawn_id", std::string{}) == id) { groups_ptr->erase(it); break; } } if (room) room->save_assets_json(); list->refresh_row_configuration(); };
                cb.on_reorder = [this](const std::string&, size_t){ if (room) room->save_assets_json(); };
                SpawnGroupConfig::ConfigureEntryCallback cfg = [](SpawnGroupConfig::EntryController& ctrl, const nlohmann::json&){ ctrl.set_linkable_room_areas_provider({}); ctrl.set_linkable_asset_areas_provider({}); };
                list->set_callbacks(std::move(cb));
                list->load(*groups_ptr, std::move(on_change), std::move(on_entry_change), std::move(cfg));
                list->append_rows(rows);
            }
            set_rows(rows);
        }
        void update(const Input& input, int w, int h) override { if (list){ list->set_screen_dimensions(w,h); list->update(input,w,h);} DockableCollapsible::update(input,w,h); }
        void render(SDL_Renderer* r) const override { DockableCollapsible::render(r); if (list) list->render(r); }
    };

    auto area_spawns = std::make_unique<Section_AreaSpawns>(); area_spawns->set_ctx(area_room_, area_name_);
    sections_.push_back(std::move(area_spawns));

    open();
}

void AssetInfoUI::clear_area_context() { area_mode_ = false; area_room_ = nullptr; area_name_.clear(); }

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

    if (updated_any && assets_) {
        assets_->mark_active_assets_dirty();
    }
}

void AssetInfoUI::on_animation_document_saved() {
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

    const bool reloaded = info_->reload_animations_from_disk();
    if (!reloaded) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AssetInfoUI] Failed to reload animations for %s.", info_->name.c_str());
        return;
    }

    info_->loadAnimations(renderer);
    refresh_loaded_asset_instances();
}
