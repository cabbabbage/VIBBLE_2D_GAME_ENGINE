// moved to dev_mode
#include "asset_library_ui.hpp"
#include <algorithm>
#include <unordered_map>
#include <functional>
#include "utils/input.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/Asset.hpp"
#include "dm_styles.hpp"
#include <iostream>
#include "core/AssetsManager.hpp"
#include "DockableCollapsible.hpp"
#include "widgets.hpp"

namespace {
    const SDL_Color kTileBG  = dm::rgba(40,40,40,180);
    const SDL_Color kTileHL  = dm::rgba(200,200,60,100);
    const SDL_Color kTileBd  = DMStyles::Border();
}

// Local widget implementation for asset tiles
struct AssetLibraryUI::AssetTileWidget : public Widget {
    std::shared_ptr<AssetInfo> info;
    SDL_Rect rect_{0,0,0,0};
    bool hovered = false;
    bool pressed = false;
    bool started_drag = false;
    int  press_x = 0, press_y = 0;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_click;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_begin_drag;

    explicit AssetTileWidget(std::shared_ptr<AssetInfo> i,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> begin_drag)
        : info(std::move(i)), on_click(std::move(click)), on_begin_drag(std::move(begin_drag)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int /*w*/) const override { return 160; }

    bool handle_event(const SDL_Event& e) override {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.motion.x, e.motion.y };
            hovered = SDL_PointInRect(&p, &rect_);
            if (pressed && !started_drag) {
                int mdx = std::abs(e.motion.x - press_x);
                int mdy = std::abs(e.motion.y - press_y);
                if (mdx + mdy >= 6) { // simple threshold
                    started_drag = true;
                    if (on_begin_drag) on_begin_drag(info);
                    return true;
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            if (SDL_PointInRect(&p, &rect_)) { pressed = true; press_x = p.x; press_y = p.y; return true; }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            bool inside = SDL_PointInRect(&p, &rect_);
            bool was = pressed;
            bool dragged = started_drag;
            pressed = false;
            started_drag = false;
            if (inside && was && !dragged) {
                if (on_click) on_click(info);
                return true;
            }
        }
        return false;
    }

    void render(SDL_Renderer* r) const override {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kTileBG.r, kTileBG.g, kTileBG.b, kTileBG.a);
        SDL_RenderFillRect(r, &rect_);
        const AssetInfo* in = info.get();
        if (in) {
            SDL_Texture* tex = nullptr;
            auto it = in->animations.find("default");
            if (it == in->animations.end()) it = in->animations.find("start");
            if (it == in->animations.end() && !in->animations.empty()) it = in->animations.begin();
            if (it != in->animations.end() && !it->second.frames.empty()) tex = it->second.frames.front();
            if (tex) {
                int tw=0, th=0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                if (tw > 0 && th > 0) {
                    int pad = 6;
                    float scale = std::min( (rect_.w - 2*pad) / float(tw), (rect_.h - 2*pad) / float(th) );
                    int dw = int(tw * scale);
                    int dh = int(th * scale);
                    SDL_Rect dst{ rect_.x + (rect_.w - dw)/2, rect_.y + (rect_.h - dh)/2, dw, dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                }
            }
        }
        if (hovered) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(r, kTileHL.r, kTileHL.g, kTileHL.b, kTileHL.a);
            SDL_RenderFillRect(r, &rect_);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kTileBd.r, kTileBd.g, kTileBd.b, kTileBd.a);
        SDL_RenderDrawRect(r, &rect_);
    }
};

AssetLibraryUI::AssetLibraryUI() {
    floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    floating_->set_expanded(false); // start collapsed on left
    add_button_ = std::make_unique<DMButton>("Add New Asset", &DMStyles::CreateButton(), 200, DMButton::height());
    add_button_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [](){ /* hook later */ });
}

AssetLibraryUI::~AssetLibraryUI() = default;

void AssetLibraryUI::toggle() {
    if (!floating_) return;
    floating_->set_visible(!is_visible());
}

bool AssetLibraryUI::is_visible() const { return floating_ && floating_->is_visible(); }

void AssetLibraryUI::open() {
    if (!floating_) floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    if (floating_) {
        floating_->set_visible(true);
        floating_->set_expanded(true); // ensure it opens expanded when requested
        const SDL_Rect r = floating_->rect();
        std::cout << "[AssetLibraryUI] Opened at (" << r.x << "," << r.y << ") expanded=1\n";
    }
}

void AssetLibraryUI::close() {
    if (floating_) floating_->set_visible(false);
}

void AssetLibraryUI::set_position(int x, int y) {
    if (floating_) floating_->set_position(x, y);
}

void AssetLibraryUI::set_expanded(bool e) {
    if (floating_) floating_->set_expanded(e);
}

bool AssetLibraryUI::is_expanded() const {
    return floating_ ? floating_->is_expanded() : false;
}

bool AssetLibraryUI::is_input_blocking() const {
    // Block when expanded (content can receive clicks/scroll) or while dragging from library
    return (floating_ && floating_->is_expanded()) || dragging_from_library_;
}

bool AssetLibraryUI::is_input_blocking_at(int mx, int my) const {
    if (dragging_from_library_) return true;
    if (!floating_ || !floating_->is_visible() || !floating_->is_expanded()) return false;
    SDL_Point p{ mx, my };
    return SDL_PointInRect(&p, &floating_->rect());
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

void AssetLibraryUI::rebuild_rows() {
    if (!floating_) return;
    std::vector<DockableCollapsible::Row> rows;
    // First row: Add button
    if (add_button_widget_) rows.push_back({ add_button_widget_.get() });
    // Then tiles as one-per-row
    rows.reserve(rows.size() + tiles_.size());
    for (auto& tw : tiles_) rows.push_back({ tw.get() });
    floating_->set_cell_width(220);
    floating_->set_rows(rows);
}

void AssetLibraryUI::update(const Input& input,
                            int screen_w,
                            int screen_h,
                            AssetLibrary& lib,
                            Assets& assets)
{
    if (!floating_) return;

    ensure_items(lib);
    if (tiles_.empty()) {
        tiles_.reserve(items_.size());
        for (auto& inf : items_) {
            tiles_.push_back(std::make_unique<AssetTileWidget>(
                inf,
                // on_click
                [&assets](const std::shared_ptr<AssetInfo>& info){ assets.open_asset_info_editor(info); },
                // on_begin_drag
                [this, &assets](const std::shared_ptr<AssetInfo>& info){
                    drag_info_ = info;
                    int mx = 0, my = 0; Uint32 mask = SDL_GetMouseState(&mx, &my); (void)mask;
                    // Spawn following cursor
                    SDL_Point wp = assets.getView().screen_to_map(SDL_Point{mx,my});
                    drag_spawned_ = assets.spawn_asset(info->name, wp);
                    dragging_from_library_ = drag_spawned_ != nullptr;
                }
            ));
        }
        rebuild_rows();
    }

    // Keep panel clamped to the visible screen area and update layout
    floating_->set_work_area(SDL_Rect{0,0,screen_w,screen_h});
    floating_->update(input, screen_w, screen_h);

    // During drag, move the spawned asset with cursor; finalize on release
    if (dragging_from_library_ && drag_spawned_) {
        int mx = input.getX();
        int my = input.getY();
        SDL_Point wp = assets.getView().screen_to_map(SDL_Point{mx,my});
        drag_spawned_->pos.x = wp.x;
        drag_spawned_->pos.y = wp.y;
        if (!input.isDown(Input::LEFT)) {
            dragging_from_library_ = false;
            drag_spawned_ = nullptr; // leave asset in world
            drag_info_.reset();
        }
    }
}

void AssetLibraryUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    (void)screen_w; (void)screen_h;
    if (!floating_) return;
    floating_->render(r);
}

void AssetLibraryUI::handle_event(const SDL_Event& e) {
    if (!floating_) return;
    floating_->handle_event(e);
}

std::shared_ptr<AssetInfo> AssetLibraryUI::consume_selection() {
    // Legacy; not used in new flow
    return nullptr;
}
