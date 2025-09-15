#include "asset_info_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

AssetInfoUI::AssetInfoUI() {
    sections_.push_back(std::make_unique<Section_BasicInfo>());
    sections_.push_back(std::make_unique<Section_Tags>());
    sections_.push_back(std::make_unique<Section_Lighting>());
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
    for (auto& s : sections_) s->set_info(info_);
}

void AssetInfoUI::clear_info() {
    info_.reset();
    for (auto& s : sections_) s->set_info(nullptr);
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

    // Footer button position after sections
    if (configure_btn_) {
        configure_btn_->set_rect(SDL_Rect{ panel_.x + DMSpacing::panel_padding(), y, maxw, DMButton::height() });
        y += DMButton::height() + DMSpacing::section_gap();
    }

    int total = y - (panel_.y + DMSpacing::panel_padding());
    max_scroll_ = std::max(0, total - panel_.h);
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

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->update(input, screen_w, screen_h);
}

void AssetInfoUI::handle_event(const SDL_Event& e) {
    if (!visible_ || !info_) return;

    if (animations_panel_ && animations_panel_->is_open() && animations_panel_->handle_event(e))
        return;

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        close();
        return;
    }

    for (auto& s : sections_) {
        if (s->handle_event(e)) return;
    }

    // Footer action: open C++ animations panel
    if (configure_btn_ && configure_btn_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (animations_panel_) {
                animations_panel_->set_asset_paths(info_->asset_dir_path(), info_->info_json_path());
                animations_panel_->open();
            }
        }
        return;
    }
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_ || !info_) return;

    layout_widgets(screen_w, screen_h);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &panel_);

    for (auto& s : sections_) s->render(r);

    // Render footer button
    if (configure_btn_) configure_btn_->render(r);

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->render(r, screen_w, screen_h);

    last_renderer_ = r;
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

