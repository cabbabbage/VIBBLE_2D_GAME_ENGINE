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
#include <cstdlib>
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

AssetInfoUI::~AssetInfoUI() = default;

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
    for (auto& s : sections_) s->set_expanded(false);
}
void AssetInfoUI::close() { visible_ = false; }
void AssetInfoUI::toggle(){ visible_ = !visible_; }

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    int panel_x = (screen_w * 2) / 3;
    int panel_w = screen_w - panel_x;
    panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };

    int y = panel_.y + DMSpacing::panel_padding() - scroll_;
    int maxw = panel_.w - 2 * DMSpacing::panel_padding();
    for (auto& s : sections_) {
        s->set_rect(SDL_Rect{ panel_.x + DMSpacing::panel_padding(), y, maxw, 0 });
        y += s->height() + DMSpacing::section_gap();
    }

    if (configure_btn_) {
        configure_btn_->set_rect(SDL_Rect{ panel_.x + DMSpacing::panel_padding(), y, maxw, DMButton::height() });
        y += DMButton::height() + DMSpacing::section_gap();
    }

    int total = y - (panel_.y + DMSpacing::panel_padding());
    max_scroll_ = std::max(0, total - panel_.h);
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
    if (mx >= panel_.x && mx < panel_.x + panel_.w &&
        my >= panel_.y && my < panel_.y + panel_.h) {
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

    // Render sections (already offset by scroll_)
    for (auto& s : sections_) s->render(r);

    // Footer button
    if (configure_btn_) configure_btn_->render(r);

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->render(r, screen_w, screen_h);

    last_renderer_ = r;
}





void AssetInfoUI::render_world_overlay(SDL_Renderer* r, const camera& cam) const {
    if (!visible_ || !info_) return;

    if (basic_info_section_ && basic_info_section_->is_expanded()) {
        basic_info_section_->render_world_overlay(r, cam, target_asset_);
    }

    if (!lighting_section_ || !lighting_section_->is_expanded() || !lighting_section_->shading_enabled() || !target_asset_) return;
    const LightSource& light = lighting_section_->shading_light();
    if (light.x_radius <= 0 && light.y_radius <= 0) return;
    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    for (int deg = 0; deg < 360; ++deg) {
        double rad = deg * M_PI / 180.0;
        int wx = target_asset_->pos.x + static_cast<int>(std::round(std::cos(rad) * light.x_radius));
        int wy = target_asset_->pos.y + static_cast<int>(std::round(std::sin(rad) * light.y_radius));
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
