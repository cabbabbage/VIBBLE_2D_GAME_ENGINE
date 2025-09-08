#include "asset_library_ui.hpp"
#include <algorithm>
#include <unordered_map>
#include "utils/input.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "ui/styles.hpp"
namespace {
    
    const SDL_Color kLibraryPanelBG = [](){ auto c = Styles::Slate(); auto cc = c; cc.a = 180; return cc; }();
    const SDL_Color kTileBG        = [](){ auto c = Styles::Slate(); auto cc = c; cc.a = 140; return cc; }();
    const SDL_Color kTileHL        = [](){ auto c = Styles::Gold();  auto cc = c; cc.a = 100; return cc; }();
    const SDL_Color kTileBd        = [](){ auto c = Styles::Gold();  auto cc = c; cc.a = 200; return cc; }();
}

AssetLibraryUI::AssetLibraryUI() {}
AssetLibraryUI::~AssetLibraryUI() = default;

void AssetLibraryUI::toggle() {
    visible_ = !visible_;
}

void AssetLibraryUI::close() {
    visible_ = false;
}

void AssetLibraryUI::ensure_items(AssetLibrary& lib) {
    if (items_cached_) return;
    items_.clear();
    for (const auto& kv : lib.all()) {
        if (kv.second) items_.push_back(kv.second);
    }
    std::sort(items_.begin(), items_.end(), [](const auto& a, const auto& b){
        const std::string an = a ? a->name : std::string{};
        const std::string bn = b ? b->name : std::string{};
        return an < bn;
    });
    items_cached_ = true;
}

SDL_Texture* AssetLibraryUI::get_default_frame_texture(const AssetInfo& info) const {
    
    auto it = info.animations.find("default");
    if (it == info.animations.end()) it = info.animations.find("start");
    if (it == info.animations.end() && !info.animations.empty()) it = info.animations.begin();
    if (it == info.animations.end()) return nullptr;
    const Animation& anim = it->second;
    if (anim.frames.empty()) return nullptr;
    return anim.frames.front();
}

void AssetLibraryUI::update(const Input& input,
                            int screen_w,
                            int screen_h,
                            AssetLibrary& lib)
{
    (void)screen_w; (void)screen_h;
    if (!visible_) return;
    ensure_items(lib);
    
    const int total_h = int(items_.size()) * (tile_size_ + gap_y_) + padding_;
    max_scroll_ = std::max(0, total_h - screen_h);
    if (int sy = input.getScrollY(); sy != 0) {
        
        scroll_offset_ -= sy * 40;
        scroll_offset_ = std::max(0, std::min(max_scroll_, scroll_offset_));
    }
    
    hover_index_ = -1;
    const int mx = input.getX();
    const int my = input.getY();
    if (mx >= 0 && mx < panel_w_) {
        
        int y_with_offset = my + scroll_offset_ - padding_;
        if (y_with_offset >= 0) {
            int slot = y_with_offset / (tile_size_ + gap_y_);
            int within = y_with_offset % (tile_size_ + gap_y_);
            if (within <= tile_size_ && slot >= 0 && slot < (int)items_.size()) {
                hover_index_ = slot;
            }
        }
    }
    
    if (hover_index_ >= 0 && input.wasClicked(Input::LEFT)) {
        selection_ = items_[hover_index_];
        close();
    }
}

void AssetLibraryUI::render(SDL_Renderer* r,
                            AssetLibrary& lib,
                            int screen_w,
                            int screen_h) const
{
    if (!visible_) return;
    (void)screen_w;
    (void)lib;
    
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, kLibraryPanelBG.r, kLibraryPanelBG.g, kLibraryPanelBG.b, kLibraryPanelBG.a);
    SDL_Rect panel{ 0, 0, panel_w_, screen_h };
    SDL_RenderFillRect(r, &panel);
    
    const int inner_x = padding_;
    int y = padding_ - scroll_offset_;
    for (int i = 0; i < (int)items_.size(); ++i) {
        const SDL_Rect tile_rect{ inner_x, y, panel_w_ - 2 * padding_, tile_size_ };
        
        if (tile_rect.y + tile_rect.h < 0) { y += tile_size_ + gap_y_; continue; }
        if (tile_rect.y > screen_h) { break; }
        
        SDL_SetRenderDrawColor(r, kTileBG.r, kTileBG.g, kTileBG.b, kTileBG.a);
        SDL_RenderFillRect(r, &tile_rect);
        
        const AssetInfo* info = items_[i].get();
        if (info) {
            if (SDL_Texture* tex = get_default_frame_texture(*info)) {
                int tw=0, th=0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                if (tw > 0 && th > 0) {
                    float scale = std::min(
                        (tile_rect.w) / float(tw),
                        (tile_rect.h) / float(th)
                    );
                    int dw = int(tw * scale);
                    int dh = int(th * scale);
                    SDL_Rect dst{
                        tile_rect.x + (tile_rect.w - dw) / 2,
                        tile_rect.y + (tile_rect.h - dh) / 2,
                        dw, dh
                    };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                }
            }
        }
        
        if (i == hover_index_) {
            
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(r, kTileHL.r, kTileHL.g, kTileHL.b, kTileHL.a);
            SDL_RenderFillRect(r, &tile_rect);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, kTileBd.r, kTileBd.g, kTileBd.b, kTileBd.a);
            SDL_RenderDrawRect(r, &tile_rect);
        }
        y += tile_size_ + gap_y_;
    }
}

std::shared_ptr<AssetInfo> AssetLibraryUI::consume_selection() {
    auto out = selection_;
    selection_.reset();
    return out;
}
