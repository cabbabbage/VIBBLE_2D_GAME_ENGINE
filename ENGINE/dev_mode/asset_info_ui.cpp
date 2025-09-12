#include "asset_info_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"

#include "asset_sections/CollapsibleSection.hpp"
#include "dm_styles.hpp"
#include "asset_sections/Section_Areas.hpp"

AssetInfoUI::AssetInfoUI() {
    auto areas = std::make_unique<Section_Areas>();
    areas_section_ = areas.get();
    areas_section_->set_open_editor_callback([this](const std::string& nm){ open_area_editor(nm); });
    sections_.push_back(std::move(areas));
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

void AssetInfoUI::open()  { visible_ = true; }
void AssetInfoUI::close() { visible_ = false; }
void AssetInfoUI::toggle(){ visible_ = !visible_; }

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    int panel_x = (screen_w * 2) / 3;
    int panel_w = screen_w - panel_x;
    panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };

    int y = panel_.y + 16 - scroll_;
    int maxw = panel_.w - 32;
    for (auto& s : sections_) {
        s->set_rect(SDL_Rect{ panel_.x + 16, y, maxw, 0 });
        y += s->height() + 16;
    }

    int total = y - (panel_.y + 16);
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

    for (auto& s : sections_) s->update(input);
}

void AssetInfoUI::handle_event(const SDL_Event& e) {
    if (!visible_ || !info_) return;

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        close();
        return;
    }

    for (auto& s : sections_) {
        if (s->handle_event(e)) return;
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

    last_renderer_ = r;
}

void AssetInfoUI::save_now() const {
    if (info_) (void)info_->update_info_json();
}

void AssetInfoUI::open_area_editor(const std::string& name) {
    if (!info_ || !last_renderer_) return;

    Area* base = nullptr;
    for (auto& na : info_->areas) {
        if (na.name == name && na.area) { base = na.area.get(); break; }
    }

    const int canvas_w = std::max(32, (int)std::lround(info_->original_canvas_width  * info_->scale_factor));
    const int canvas_h = std::max(32, (int)std::lround(info_->original_canvas_height * info_->scale_factor));

    auto pick_sprite = [&]() -> SDL_Texture* {
        auto try_get = [&](const std::string& key) -> SDL_Texture* {
            auto it = info_->animations.find(key);
            if (it != info_->animations.end() && !it->second.frames.empty()) {
                return it->second.frames.front();
            }
            return nullptr;
        };
        if (!info_->start_animation.empty()) {
            if (auto t = try_get(info_->start_animation)) return t;
        }
        if (auto t = try_get("default")) return t;
        for (auto& kv : info_->animations) {
            if (!kv.second.frames.empty()) return kv.second.frames.front();
        }
        return nullptr;
    };

    try {
        SDL_Texture* bg = SDL_CreateTexture(last_renderer_, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, canvas_w, canvas_h);
        if (!bg) throw std::runtime_error("Failed to create editor canvas");
        SDL_SetTextureBlendMode(bg, SDL_BLENDMODE_BLEND);
        SDL_Texture* prev = SDL_GetRenderTarget(last_renderer_);
        SDL_SetRenderTarget(last_renderer_, bg);
        SDL_SetRenderDrawBlendMode(last_renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(last_renderer_, 0, 0, 0, 0);
        SDL_RenderClear(last_renderer_);
        if (SDL_Texture* sprite = pick_sprite()) {
            int sw = 0, sh = 0; (void)SDL_QueryTexture(sprite, nullptr, nullptr, &sw, &sh);
            SDL_Rect dst{ std::max(0, (canvas_w - sw) / 2), std::max(0,  canvas_h - sh), sw, sh };
            SDL_RenderCopy(last_renderer_, sprite, nullptr, &dst);
        }
        if (base) {
            SDL_SetRenderDrawColor(last_renderer_, 0, 200, 255, 180);
            std::vector<SDL_Point> pts; pts.reserve(base->get_points().size() + 1);
            for (const auto& p : base->get_points()) {
                pts.push_back(SDL_Point{ p.x, p.y });
            }
            if (!pts.empty()) {
                pts.push_back(pts.front());
                SDL_RenderDrawLines(last_renderer_, pts.data(), (int)pts.size());
            }
        }
        SDL_SetRenderTarget(last_renderer_, prev);
        Area edited(name, bg, last_renderer_);
        info_->upsert_area_from_editor(edited);
        SDL_DestroyTexture(bg);
        if (areas_section_) areas_section_->rebuild_buttons();
    } catch (...) {
        // editor failure is non-fatal
    }
}

