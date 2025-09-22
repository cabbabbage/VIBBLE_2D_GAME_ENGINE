#include "asset_info_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <SDL_log.h>
#include <stdexcept>
#include <vector>

#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"

#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "asset_sections/Section_BasicInfo.hpp"
#include "asset_sections/Section_Tags.hpp"
#include "asset_sections/Section_Lighting.hpp"
#include "asset_sections/Section_Areas.hpp"
#include "asset_sections/Section_Spacing.hpp"
#include "asset_sections/Section_ChildAssets.hpp"
#include "widgets.hpp"
#include "core/AssetsManager.hpp"
#include "animations_editor_panel.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "utils/light_source.hpp"

AssetInfoUI::AssetInfoUI() {
    auto basic = std::make_unique<Section_BasicInfo>();
    basic_info_section_ = basic.get();
    basic_info_section_->set_ui(this);
    sections_.push_back(std::move(basic));
    sections_.push_back(std::make_unique<Section_Tags>());
    auto lighting = std::make_unique<Section_Lighting>();
    lighting->set_ui(this);
    lighting_section_ = lighting.get();
    sections_.push_back(std::move(lighting));
    auto spacing = std::make_unique<Section_Spacing>();
    sections_.push_back(std::move(spacing));
    auto areas = std::make_unique<Section_Areas>();
    areas_section_ = areas.get();
    areas_section_->set_open_editor_callback([this](const std::string& nm){ open_area_editor(nm); });
    areas_section_->set_delete_callback([this](const std::string& nm){
        if (!info_) return;
        if (info_->remove_area(nm)) {
            (void)info_->update_info_json();
        }
    });
    sections_.push_back(std::move(areas));
    auto children = std::make_unique<Section_ChildAssets>();
    children->set_open_area_editor_callback([this](const std::string& nm){ open_area_editor(nm); });
    sections_.push_back(std::move(children));
    // Configure Animations footer button
    configure_btn_ = std::make_unique<DMButton>("Configure Animations", &DMStyles::CreateButton(), 220, DMButton::height());
    animations_panel_ = std::make_unique<AnimationsEditorPanel>();
}

AssetInfoUI::~AssetInfoUI() {
    apply_camera_override(false);
}

void AssetInfoUI::set_assets(Assets* a) {
    if (assets_ == a) return;
    if (camera_override_active_) {
        apply_camera_override(false);
    }
    assets_ = a;
    if (visible_) {
        apply_camera_override(true);
    }
}

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    scroll_ = 0;
    for (auto& s : sections_) {
        s->set_info(info_);
        s->reset_scroll();
        s->build();
    }
}

void AssetInfoUI::clear_info() {
    info_.reset();
    scroll_ = 0;
    for (auto& s : sections_) {
        s->set_info(nullptr);
        s->reset_scroll();
        s->build();
    }
    target_asset_ = nullptr;
}

void AssetInfoUI::open()  {
    visible_ = true;
    apply_camera_override(true);
    for (auto& s : sections_) s->set_expanded(false);
}
void AssetInfoUI::close() {
    if (!visible_) return;
    apply_camera_override(false);
    visible_ = false;
}
void AssetInfoUI::toggle(){
    if (visible_) {
        close();
    } else {
        open();
    }
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    int panel_x = (screen_w * 2) / 3;
    int panel_w = screen_w - panel_x;
    panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };
    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::section_gap();
    const int content_x = panel_.x + padding;
    const int content_w = panel_.w - 2 * padding;
    const int content_top = panel_.y + padding;

    auto layout_with_scroll = [&](int scroll_value) {
        int y = content_top;
        for (auto& s : sections_) {
            s->set_rect(SDL_Rect{ content_x, y - scroll_value, content_w, 0 });
            y += s->height() + gap;
        }
        if (configure_btn_) {
            configure_btn_->set_rect(SDL_Rect{ content_x, y - scroll_value, content_w, DMButton::height() });
            y += DMButton::height() + gap;
        }
        return y;
    };

    int end_y = layout_with_scroll(scroll_);
    int content_height = end_y - content_top;
    int visible_height = panel_.h - padding;
    max_scroll_ = std::max(0, content_height - std::max(0, visible_height));
    int clamped = std::max(0, std::min(max_scroll_, scroll_));
    if (clamped != scroll_) {
        scroll_ = clamped;
        end_y = layout_with_scroll(scroll_);
        content_height = end_y - content_top;
        max_scroll_ = std::max(0, content_height - std::max(0, visible_height));
    }

    scroll_region_ = panel_;
}



void AssetInfoUI::handle_event(const SDL_Event& e) {
    if (!visible_ || !info_) return;

    if (animations_panel_ && animations_panel_->is_open() && animations_panel_->handle_event(e))
        return;

    bool pointer_inside = false;
    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    if (pointer_event) {
        SDL_Point p{
            e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
            e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y
        };
        pointer_inside = SDL_PointInRect(&p, &panel_);
        if (!pointer_inside) {
            return;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};
        pointer_inside = SDL_PointInRect(&p, &panel_);
        if (!pointer_inside) {
            return;
        }
    }

    if (e.type == SDL_MOUSEWHEEL) {
        scroll_ -= e.wheel.y * 40;
        scroll_ = std::max(0, std::min(max_scroll_, scroll_));
        return;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        close();
        return;
    }

    for (auto& s : sections_) {
        if (s->handle_event(e)) return;
    }

    if (configure_btn_ && configure_btn_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (info_) {
                try {
                    std::string cmd = std::string("python scripts/animation_ui.py \"") + info_->info_json_path() + "\"";
                    int rc = std::system(cmd.c_str());
                    if (rc != 0) {
                        SDL_Log("animation_ui.py exited with code %d", rc);
                    }
                } catch (const std::exception& ex) {
                    SDL_Log("Failed to launch animation_ui.py: %s", ex.what());
                }
            }
        }
        return;
    }

    if (pointer_inside) {
        return;
    }
}



void AssetInfoUI::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_ || !info_) return;
    layout_widgets(screen_w, screen_h);

    int mx = input.getX();
    int my = input.getY();
    if (mx >= scroll_region_.x && mx < scroll_region_.x + scroll_region_.w &&
        my >= scroll_region_.y && my < scroll_region_.y + scroll_region_.h) {
        int dy = input.getScrollY();
        if (dy != 0) {
            scroll_ -= dy * 40;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_));
        }
    }

    for (auto& s : sections_) s->update(input, screen_w, screen_h);

    // Accordion behavior: only one open at a time
    for (size_t i = 0; i < sections_.size(); ++i) {
        if (sections_[i]->is_expanded()) {
            for (size_t j = 0; j < sections_.size(); ++j) {
                if (i != j) sections_[j]->set_expanded(false);
            }
            break;
        }
    }

    if (pulse_frames_ > 0) {
        --pulse_frames_;
    }

    layout_widgets(screen_w, screen_h);

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->update(input, screen_w, screen_h);
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_ || !info_) return;

    layout_widgets(screen_w, screen_h);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &panel_);

    if (pulse_frames_ > 0) {
        Uint8 alpha = static_cast<Uint8>(std::clamp(pulse_frames_ * 12, 0, 180));
        SDL_Rect header_rect{panel_.x, panel_.y, panel_.w, DMButton::height()};
        SDL_SetRenderDrawColor(r, 255, 220, 64, alpha);
        SDL_RenderFillRect(r, &header_rect);
    }

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(r, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(r);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(r, &panel_);

    // Render sections (already offset by scroll_)
    for (auto& s : sections_) s->render(r);

    // Footer button
    if (configure_btn_) configure_btn_->render(r);

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(r, &prev_clip);
    } else {
        SDL_RenderSetClipRect(r, nullptr);
    }

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->render(r, screen_w, screen_h);

    last_renderer_ = r;
}

void AssetInfoUI::pulse_header() {
    pulse_frames_ = 20;
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
    if (ph > 0) {
        float screen_h = static_cast<float>(ph) * inv_scale;
        return screen_h > 0.0f ? screen_h : 1.0f;
    }
    return 1.0f;
}





void AssetInfoUI::render_world_overlay(SDL_Renderer* r, const camera& cam) const {
    if (!visible_ || !info_) return;

    float reference_screen_height = compute_player_screen_height(cam);

    if (basic_info_section_ && basic_info_section_->is_expanded()) {
        basic_info_section_->render_world_overlay(r, cam, target_asset_, reference_screen_height);
    }

    if (!lighting_section_ || !lighting_section_->is_expanded() || !lighting_section_->shading_enabled() || !target_asset_) return;
    const LightSource& light = lighting_section_->shading_light();
    if (light.x_radius <= 0 && light.y_radius <= 0) return;
    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    for (int deg = 0; deg < 360; ++deg) {
        double rad = deg * M_PI / 180.0;
        int wx = target_asset_->pos.x + light.offset_x + static_cast<int>(std::round(std::cos(rad) * light.x_radius));
        int wy = target_asset_->pos.y + light.offset_y + static_cast<int>(std::round(std::sin(rad) * light.y_radius));
        SDL_Point p = cam.map_to_screen(SDL_Point{wx, wy});
        SDL_RenderDrawPoint(r, p.x, p.y);
    }
}

void AssetInfoUI::refresh_target_asset_scale() {
    if (!info_ || !target_asset_) return;
    SDL_Renderer* renderer = last_renderer_;
    if (!renderer) return;
    info_->loadAnimations(renderer);
    target_asset_->finalize_setup();
    target_asset_->set_final_texture(nullptr);
    target_asset_->cached_w = 0;
    target_asset_->cached_h = 0;
}

void AssetInfoUI::sync_target_z_threshold() {
    if (!target_asset_) return;
    target_asset_->set_z_index();
}

bool AssetInfoUI::is_point_inside(int x, int y) const {
    SDL_Point p{ x, y };
    return visible_ && SDL_PointInRect(&p, &panel_);
}

void AssetInfoUI::save_now() const {
    if (info_) (void)info_->update_info_json();
}

void AssetInfoUI::open_area_editor(const std::string& name) {
    if (!info_ || !assets_) return;
    assets_->begin_area_edit_for_selected_asset(name);
}
