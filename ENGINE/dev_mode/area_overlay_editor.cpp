#include "area_overlay_editor.hpp"

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "draw_utils.hpp"
#include "dm_styles.hpp"

#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "render/camera.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <iostream>
#include <stdexcept>

namespace {
    // Point in polygon (even-odd) for integer coords
    static bool point_in_poly(int px, int py, const std::vector<SDL_Point>& poly) {
        bool inside = false;
        size_t n = poly.size();
        if (n < 3) return false;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const int xi = poly[i].x, yi = poly[i].y;
            const int xj = poly[j].x, yj = poly[j].y;
            bool intersect = false;
            if ((yi > py) != (yj > py)) {
                if (yj != yi) {
                    double x_intersect = (xj - xi) * (py - yi) / static_cast<double>(yj - yi) + xi;
                    if (px < x_intersect) {
                        intersect = true;
                    }
                }
            }
            if (intersect) inside = !inside;
        }
        return inside;
    }

    static float compute_reference_screen_height(const Assets* assets, const camera& cam) {
        if (!assets) return 1.0f;
        Asset* player = assets->player;
        if (!player) return 1.0f;

        SDL_Texture* player_final = player->get_final_texture();
        SDL_Texture* player_frame = player->get_current_frame();
        int pw = player->cached_w;
        int ph = player->cached_h;
        if ((pw == 0 || ph == 0) && player_final) {
            SDL_QueryTexture(player_final, nullptr, nullptr, &pw, &ph);
        }
        if ((pw == 0 || ph == 0) && player_frame) {
            SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
        }
        if (pw != 0) player->cached_w = pw;
        if (ph != 0) player->cached_h = ph;

        float scale = cam.get_scale();
        if (scale <= 0.0f) return 1.0f;
        float inv_scale = 1.0f / scale;

        if (ph > 0) {
            float screen_h = static_cast<float>(ph) * inv_scale;
            if (screen_h > 0.0f) return screen_h;
        }
        return 1.0f;
    }
}

constexpr Uint8 kDefaultMaskAlpha = 128;

AreaOverlayEditor::AreaOverlayEditor()
    : mask_alpha_(kDefaultMaskAlpha) {}

AreaOverlayEditor::~AreaOverlayEditor() {
    apply_camera_override(false);
    if (mask_) SDL_FreeSurface(mask_);
    if (mask_tex_) SDL_DestroyTexture(mask_tex_);
    discard_autogen_base();
}

bool AreaOverlayEditor::begin(AssetInfo* info, Asset* asset, const std::string& area_name) {
    if (!assets_ || !info || !asset) return false;

    int cw = std::max(32, static_cast<int>(std::lround(info->original_canvas_width * info->scale_factor)));
    int ch = std::max(32, static_cast<int>(std::lround(info->original_canvas_height * info->scale_factor)));

    info_ = info;
    asset_ = asset;
    area_name_ = area_name;
    canvas_w_ = cw;
    canvas_h_ = ch;
    mask_origin_x_ = 0;
    mask_origin_y_ = 0;

    if (mask_) {
        SDL_FreeSurface(mask_);
        mask_ = nullptr;
    }
    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }
    discard_autogen_base();
    pending_mask_generation_ = false;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    mask_ = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask_) return false;
    clear_mask();

    init_mask_from_existing_area();
    upload_mask();

    ensure_toolbox();

    const int max_brush = std::max(16, std::max(canvas_w_, canvas_h_));
    brush_slider_ = std::make_unique<DMSlider>("Brush Size", 1, max_brush, brush_radius_);
    crop_left_slider_ = std::make_unique<DMSlider>("Crop Left", 0, canvas_w_, 0);
    crop_right_slider_ = std::make_unique<DMSlider>("Crop Right", 0, canvas_w_, 0);
    crop_top_slider_ = std::make_unique<DMSlider>("Crop Top", 0, canvas_h_, 0);
    crop_bottom_slider_ = std::make_unique<DMSlider>("Crop Bottom", 0, canvas_h_, 0);
    reset_mask_crop_values();
    if (brush_slider_) brush_slider_->set_value(std::max(1, brush_radius_));

    set_mode(Mode::Draw);
    apply_camera_override(true);

    active_ = true;
    drawing_ = false;
    saved_since_begin_ = false;
    toolbox_autoplace_done_ = false;

    return true;
}

void AreaOverlayEditor::cancel() {
    active_ = false;
    drawing_ = false;
    pending_mask_generation_ = false;
    apply_camera_override(false);
}

void AreaOverlayEditor::clear_mask() {
    if (!mask_) return;
    SDL_FillRect(mask_, nullptr, SDL_MapRGBA(mask_->format, 255, 0, 0, 0));
}

void AreaOverlayEditor::upload_mask() {
    if (!mask_) return;
    if (mask_tex_) {
        SDL_UpdateTexture(mask_tex_, nullptr, mask_->pixels, mask_->pitch);
    }
}

void AreaOverlayEditor::stamp(int cx, int cy, int radius, bool erase) {
    if (!mask_) return;
    Uint32 col = SDL_MapRGBA(mask_->format, 255, 0, 0, erase ? 0 : 255);
    dm_draw::stamp_circle(mask_, cx, cy, radius, col);
    upload_mask();
}

void AreaOverlayEditor::ensure_mask_contains(int lx, int ly, int radius) {
    if (!mask_) return;
    int sx = lx - mask_origin_x_;
    int sy = ly - mask_origin_y_;
    int min_sx = std::min(sx - radius, 0);
    int min_sy = std::min(sy - radius, 0);
    int max_sx = std::max(sx + radius + 1, mask_->w);
    int max_sy = std::max(sy + radius + 1, mask_->h);
    bool needs_expand = (min_sx < 0) || (min_sy < 0) || (max_sx > mask_->w) || (max_sy > mask_->h);
    if (!needs_expand) return;

    int new_w = max_sx - min_sx;
    int new_h = max_sy - min_sy;
    SDL_Surface* grown = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!grown) return;
    SDL_FillRect(grown, nullptr, SDL_MapRGBA(grown->format, 255, 0, 0, 0));

    int dst_x = -min_sx;
    int dst_y = -min_sy;
    SDL_Rect dst{ dst_x, dst_y, mask_->w, mask_->h };
    SDL_BlitSurface(mask_, nullptr, grown, &dst);

    SDL_FreeSurface(mask_);
    mask_ = grown;

    if (mask_autogen_base_) {
        SDL_Surface* base_grown = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (base_grown) {
            SDL_FillRect(base_grown, nullptr, SDL_MapRGBA(base_grown->format, 255, 0, 0, 0));
            SDL_Rect base_dst{ dst_x, dst_y, mask_autogen_base_->w, mask_autogen_base_->h };
            SDL_BlitSurface(mask_autogen_base_, nullptr, base_grown, &base_dst);
            SDL_FreeSurface(mask_autogen_base_);
            mask_autogen_base_ = base_grown;
        }
    }

    mask_origin_x_ += min_sx;
    mask_origin_y_ += min_sy;
    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }
}

void AreaOverlayEditor::init_mask_from_existing_area() {
    if (!mask_ || !info_) return;
    Area* a = info_->find_area(area_name_);
    if (!a) return;
    const auto& pts = a->get_points();
    if (pts.size() < 3) return;
    int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (const auto& p : pts) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    ensure_mask_contains(minx, miny, 2);
    ensure_mask_contains(maxx, maxy, 2);

    SDL_LockSurface(mask_);
    Uint8* pixels = static_cast<Uint8*>(mask_->pixels);
    const int pitch = mask_->pitch;
    for (int y = 0; y < mask_->h; ++y) {
        for (int x = 0; x < mask_->w; ++x) {
            int lx = x + mask_origin_x_;
            int ly = y + mask_origin_y_;
            bool inside = point_in_poly(lx, ly, pts);
            Uint32* p = reinterpret_cast<Uint32*>(pixels + y * pitch) + x;
            *p = SDL_MapRGBA(mask_->format, 255, 0, 0, inside ? 255 : 0);
        }
    }
    SDL_UnlockSurface(mask_);
}

std::vector<SDL_Point> AreaOverlayEditor::extract_edge_points(int step) const {
    std::vector<SDL_Point> out;
    if (!mask_) return out;
    SDL_LockSurface(mask_);
    const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
    const int pitch = mask_->pitch;
    auto getA = [&](int x, int y) -> Uint8 {
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint8 r, g, b, a;
        SDL_GetRGBA(*p, mask_->format, &r, &g, &b, &a);
        return a;
    };
    for (int y = 1; y < mask_->h - 1; y += step) {
        for (int x = 1; x < mask_->w - 1; x += step) {
            Uint8 a = getA(x, y);
            if (a == 0) continue;
            if (getA(x - 1, y) == 0 || getA(x + 1, y) == 0 || getA(x, y - 1) == 0 || getA(x, y + 1) == 0) {
                out.push_back(SDL_Point{ x, y });
            }
        }
    }
    SDL_UnlockSurface(mask_);
    return out;
}

void AreaOverlayEditor::apply_camera_override(bool enable) {
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

void AreaOverlayEditor::ensure_toolbox() {
    if (toolbox_) return;
    toolbox_ = std::make_unique<DockableCollapsible>("Area Tools", true);
    btn_draw_  = std::make_unique<DMButton>("Draw",  &DMStyles::CreateButton(), 180, DMButton::height());
    btn_erase_ = std::make_unique<DMButton>("Erase", &DMStyles::CreateButton(), 180, DMButton::height());
    btn_mask_  = std::make_unique<DMButton>("Mask",  &DMStyles::CreateButton(), 180, DMButton::height());
    btn_save_  = std::make_unique<DMButton>("Save",  &DMStyles::CreateButton(), 180, DMButton::height());
    rebuild_toolbox_rows();
}

void AreaOverlayEditor::rebuild_toolbox_rows() {
    if (!toolbox_) return;

    owned_widgets_.clear();
    DockableCollapsible::Rows rows;

    if (btn_draw_ && btn_erase_ && btn_mask_ && btn_save_) {
        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_draw_.get(), [this]() {
            set_mode(Mode::Draw);
        }));
        Widget* draw_widget = owned_widgets_.back().get();

        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_erase_.get(), [this]() {
            set_mode(Mode::Erase);
        }));
        Widget* erase_widget = owned_widgets_.back().get();

        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_mask_.get(), [this]() {
            discard_autogen_base();
            reset_mask_crop_values();
            pending_mask_generation_ = true;
            set_mode(Mode::Mask);
        }));
        Widget* mask_widget = owned_widgets_.back().get();

        rows.push_back({ draw_widget, erase_widget, mask_widget });

        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_save_.get(), [this]() {
            save_area();
        }));
        rows.push_back({ owned_widgets_.back().get() });
    }

    if (mode_ == Mode::Draw && brush_slider_) {
        owned_widgets_.push_back(std::make_unique<SliderWidget>(brush_slider_.get()));
        rows.push_back({ owned_widgets_.back().get() });
    }

    if (mode_ == Mode::Mask) {
        if (crop_left_slider_ && crop_right_slider_) {
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_left_slider_.get()));
            Widget* left_widget = owned_widgets_.back().get();
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_right_slider_.get()));
            Widget* right_widget = owned_widgets_.back().get();
            rows.push_back({ left_widget, right_widget });
        }
        if (crop_top_slider_ && crop_bottom_slider_) {
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_top_slider_.get()));
            Widget* top_widget = owned_widgets_.back().get();
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_bottom_slider_.get()));
            Widget* bottom_widget = owned_widgets_.back().get();
            rows.push_back({ top_widget, bottom_widget });
        }
    }

    toolbox_->set_rows(rows);
    update_tool_button_states();
}

void AreaOverlayEditor::set_mode(Mode mode) {
    mode_ = mode;
    if (mode_ == Mode::Mask) {
        drawing_ = false;
    }
    update_tool_button_states();
    if (toolbox_) {
        rebuild_toolbox_rows();
    }
}

void AreaOverlayEditor::update_tool_button_states() {
    if (btn_draw_)  btn_draw_->set_text(mode_ == Mode::Draw  ? "[Draw]"  : "Draw");
    if (btn_erase_) btn_erase_->set_text(mode_ == Mode::Erase ? "[Erase]" : "Erase");
    if (btn_mask_)  btn_mask_->set_text(mode_ == Mode::Mask  ? "[Mask]"  : "Mask");
}

void AreaOverlayEditor::reset_mask_crop_values() {
    crop_left_px_ = 0;
    crop_right_px_ = 0;
    crop_top_px_ = 0;
    crop_bottom_px_ = 0;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    if (crop_left_slider_)   crop_left_slider_->set_value(0);
    if (crop_right_slider_)  crop_right_slider_->set_value(0);
    if (crop_top_slider_)    crop_top_slider_->set_value(0);
    if (crop_bottom_slider_) crop_bottom_slider_->set_value(0);
}

void AreaOverlayEditor::discard_autogen_base() {
    if (mask_autogen_base_) {
        SDL_FreeSurface(mask_autogen_base_);
        mask_autogen_base_ = nullptr;
    }
}

bool AreaOverlayEditor::generate_mask_from_asset(SDL_Renderer* renderer) {
    if (!renderer || !asset_ || canvas_w_ <= 0 || canvas_h_ <= 0) return false;

    SDL_Texture* source = asset_->get_final_texture();
    if (!source) source = asset_->get_current_frame();
    if (!source) return false;

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(source, nullptr, nullptr, &tex_w, &tex_h) != 0) {
        std::cerr << "[AreaOverlayEditor] SDL_QueryTexture failed: " << SDL_GetError() << "\n";
        return false;
    }
    if (tex_w <= 0 || tex_h <= 0) {
        std::cerr << "[AreaOverlayEditor] Source texture has invalid dimensions" << "\n";
        return false;
    }

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_Texture* staging = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
    if (!staging) {
        std::cerr << "[AreaOverlayEditor] Failed to create staging texture: " << SDL_GetError() << "\n";
        return false;
    }

    Uint8 prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
    SDL_GetTextureColorMod(source, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureAlphaMod(source, &prev_a);
    SDL_SetTextureColorMod(source, 255, 255, 255);
    SDL_SetTextureAlphaMod(source, 255);

    SDL_SetRenderTarget(renderer, staging);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderCopyEx(renderer, source, nullptr, nullptr, 0.0, nullptr, SDL_FLIP_NONE);

    SDL_Surface* captured = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!captured) {
        SDL_SetRenderTarget(renderer, prev_target);
        SDL_DestroyTexture(staging);
        SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(source, prev_a);
        std::cerr << "[AreaOverlayEditor] Failed to create capture surface: " << SDL_GetError() << "\n";
        return false;
    }
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, captured->pixels, captured->pitch) != 0) {
        SDL_FreeSurface(captured);
        SDL_SetRenderTarget(renderer, prev_target);
        SDL_DestroyTexture(staging);
        SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(source, prev_a);
        std::cerr << "[AreaOverlayEditor] SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_SetRenderTarget(renderer, prev_target);
    SDL_DestroyTexture(staging);
    SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
    SDL_SetTextureAlphaMod(source, prev_a);

    SDL_Surface* base = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* working = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    if (!base || !working) {
        if (base) SDL_FreeSurface(base);
        if (working) SDL_FreeSurface(working);
        SDL_FreeSurface(captured);
        std::cerr << "[AreaOverlayEditor] Failed to allocate mask surfaces: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_FillRect(base, nullptr, SDL_MapRGBA(base->format, 255, 0, 0, 0));
    SDL_FillRect(working, nullptr, SDL_MapRGBA(working->format, 255, 0, 0, 0));

    if (SDL_LockSurface(captured) != 0) {
        SDL_FreeSurface(base);
        SDL_FreeSurface(working);
        SDL_FreeSurface(captured);
        std::cerr << "[AreaOverlayEditor] Failed to lock capture surface: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_LockSurface(base);
    SDL_LockSurface(working);

    const int src_w = captured->w;
    const int src_h = captured->h;
    const int dst_w = working->w;
    const int dst_h = working->h;

    for (int y = 0; y < dst_h; ++y) {
        int sy = src_h > 0 ? std::clamp((y * src_h) / std::max(1, dst_h), 0, src_h - 1) : 0;
        const Uint8* src_row_bytes = static_cast<const Uint8*>(captured->pixels) + sy * captured->pitch;
        const Uint32* src_row = reinterpret_cast<const Uint32*>(src_row_bytes);
        Uint32* base_row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(base->pixels) + y * base->pitch);
        Uint32* working_row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(working->pixels) + y * working->pitch);
        for (int x = 0; x < dst_w; ++x) {
            int sx = src_w > 0 ? std::clamp((x * src_w) / std::max(1, dst_w), 0, src_w - 1) : 0;
            Uint32 pixel = src_row[sx];
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, captured->format, &r, &g, &b, &a);
            Uint32 value = SDL_MapRGBA(base->format, 255, 0, 0, a > 0 ? 255 : 0);
            base_row[x] = value;
            working_row[x] = value;
        }
    }

    SDL_UnlockSurface(working);
    SDL_UnlockSurface(base);
    SDL_UnlockSurface(captured);
    SDL_FreeSurface(captured);

    if (mask_) SDL_FreeSurface(mask_);
    mask_ = working;
    mask_origin_x_ = 0;
    mask_origin_y_ = 0;

    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }

    discard_autogen_base();
    mask_autogen_base_ = base;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    upload_mask();
    return true;
}

void AreaOverlayEditor::apply_mask_crop() {
    if (!mask_ || !mask_autogen_base_) return;
    if (mask_->w != mask_autogen_base_->w || mask_->h != mask_autogen_base_->h) return;

    SDL_BlitSurface(mask_autogen_base_, nullptr, mask_, nullptr);

    const int width = mask_->w;
    const int height = mask_->h;

    int left = std::clamp(crop_left_px_, 0, width);
    int right = std::clamp(crop_right_px_, 0, width);
    int top = std::clamp(crop_top_px_, 0, height);
    int bottom = std::clamp(crop_bottom_px_, 0, height);

    if (left + right >= width || top + bottom >= height) {
        SDL_FillRect(mask_, nullptr, SDL_MapRGBA(mask_->format, 255, 0, 0, 0));
        applied_crop_left_ = left;
        applied_crop_right_ = right;
        applied_crop_top_ = top;
        applied_crop_bottom_ = bottom;
        upload_mask();
        return;
    }

    Uint32 clear = SDL_MapRGBA(mask_->format, 255, 0, 0, 0);

    if (left > 0) {
        SDL_Rect rect{0, 0, left, height};
        SDL_FillRect(mask_, &rect, clear);
    }
    if (right > 0) {
        SDL_Rect rect{width - right, 0, right, height};
        SDL_FillRect(mask_, &rect, clear);
    }
    if (top > 0) {
        SDL_Rect rect{0, 0, width, top};
        SDL_FillRect(mask_, &rect, clear);
    }
    if (bottom > 0) {
        SDL_Rect rect{0, height - bottom, width, bottom};
        SDL_FillRect(mask_, &rect, clear);
    }

    applied_crop_left_ = left;
    applied_crop_right_ = right;
    applied_crop_top_ = top;
    applied_crop_bottom_ = bottom;

    upload_mask();
}

void AreaOverlayEditor::position_toolbox_left_of_asset(int screen_w, int screen_h) {
    if (!toolbox_ || !assets_ || !asset_) return;
    SDL_Point ap = assets_->getView().map_to_screen(SDL_Point{asset_->pos.x, asset_->pos.y});
    int tb_w = toolbox_->rect().w;
    int x = ap.x - tb_w - 16;
    int y = ap.y - 200;
    x = std::max(8, x);
    y = std::max(8, y);
    toolbox_->set_position(x, y);
    toolbox_->set_work_area(SDL_Rect{0, 0, screen_w, screen_h});
}

void AreaOverlayEditor::update(const Input& input, int screen_w, int screen_h) {
    if (!active_ || !assets_ || !asset_) return;

    ensure_toolbox();
    if (!toolbox_autoplace_done_) {
        position_toolbox_left_of_asset(screen_w, screen_h);
        toolbox_autoplace_done_ = true;
    } else if (toolbox_) {
        toolbox_->set_work_area(SDL_Rect{0, 0, screen_w, screen_h});
    }
    if (toolbox_) toolbox_->update(input, screen_w, screen_h);

    if (brush_slider_) {
        int slider_value = std::max(1, brush_slider_->value());
        brush_radius_ = slider_value;
    }

    if (mode_ == Mode::Mask && mask_autogen_base_) {
        int left = crop_left_slider_ ? crop_left_slider_->value() : 0;
        int right = crop_right_slider_ ? crop_right_slider_->value() : 0;
        int top = crop_top_slider_ ? crop_top_slider_->value() : 0;
        int bottom = crop_bottom_slider_ ? crop_bottom_slider_->value() : 0;
        if (left != applied_crop_left_ || right != applied_crop_right_ || top != applied_crop_top_ || bottom != applied_crop_bottom_) {
            crop_left_px_ = left;
            crop_right_px_ = right;
            crop_top_px_ = top;
            crop_bottom_px_ = bottom;
            apply_mask_crop();
        }
    }

    const int mx = input.getX();
    const int my = input.getY();
    SDL_Point mouse_point{mx, my};
    const bool over_toolbox = (toolbox_ && SDL_PointInRect(&mouse_point, &toolbox_->rect()));
    const bool painting_enabled = (mode_ == Mode::Draw || mode_ == Mode::Erase);

    if (painting_enabled && input.isDown(Input::LEFT) && !over_toolbox) {
        drawing_ = true;
    } else if (!input.isDown(Input::LEFT) || over_toolbox || !painting_enabled) {
        drawing_ = false;
    }

    if (!painting_enabled) {
        drawing_ = false;
    }

    if (drawing_) {
        SDL_Point world = assets_->getView().screen_to_map(SDL_Point{mx, my});
        int dxp = world.x - asset_->pos.x;
        int dyp = world.y - asset_->pos.y;
        if (asset_->flipped) dxp = -dxp;
        int lx = (canvas_w_ / 2) + dxp;
        int ly = (canvas_h_) + dyp;
        ensure_mask_contains(lx, ly, brush_radius_);
        int sx = lx - mask_origin_x_;
        int sy = ly - mask_origin_y_;
        if (sx >= 0 && sx < mask_->w && sy >= 0 && sy < mask_->h) {
            stamp(sx, sy, brush_radius_, mode_ == Mode::Erase);
        }
    }
}

bool AreaOverlayEditor::handle_event(const SDL_Event& e) {
    if (!active_) return false;
    if (toolbox_ && toolbox_->handle_event(e)) return true;

    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_ESCAPE) {
            cancel();
            return true;
        }
    }
    return false;
}

void AreaOverlayEditor::render(SDL_Renderer* r) {
    if (!active_ || !assets_ || !asset_ || !mask_) return;

    if (pending_mask_generation_) {
        if (!generate_mask_from_asset(r)) {
            std::cerr << "[AreaOverlayEditor] Failed to generate mask from asset for mask tool" << "\n";
        } else if (mode_ == Mode::Mask) {
            apply_mask_crop();
        }
        pending_mask_generation_ = false;
    }

    camera& cam = assets_->getView();
    float scale = cam.get_scale();
    if (scale <= 0.0f) return;
    float inv_scale = 1.0f / scale;

    if (mask_->w <= 0 || mask_->h <= 0) return;

    float base_sw = static_cast<float>(mask_->w) * inv_scale;
    float base_sh = static_cast<float>(mask_->h) * inv_scale;
    if (base_sw <= 0.0f || base_sh <= 0.0f) return;

    float reference_screen_height = compute_reference_screen_height(assets_, cam);
    if (reference_screen_height <= 0.0f) reference_screen_height = 1.0f;

    const camera::RenderEffects effects = cam.compute_render_effects(
        SDL_Point{ asset_->pos.x, asset_->pos.y }, base_sh, reference_screen_height);

    float scaled_sw = base_sw * effects.distance_scale;
    float scaled_sh = base_sh * effects.distance_scale;
    float final_visible_h = scaled_sh * effects.vertical_scale;

    int sw = std::max(1, static_cast<int>(std::round(scaled_sw)));
    int sh = std::max(1, static_cast<int>(std::round(final_visible_h)));
    if (sw <= 0 || sh <= 0) return;

    int pivot_x = canvas_w_ / 2;
    int pivot_y = canvas_h_;
    int offset_x_px = mask_origin_x_ - pivot_x;
    int offset_y_px = mask_origin_y_ - pivot_y;

    float offset_x_screen = static_cast<float>(offset_x_px) * inv_scale * effects.distance_scale;
    float offset_y_screen = static_cast<float>(offset_y_px) * inv_scale * effects.distance_scale * effects.vertical_scale;

    const SDL_Point& base = effects.screen_position;
    SDL_Rect dst{
        base.x + static_cast<int>(std::round(offset_x_screen)),
        base.y + static_cast<int>(std::round(offset_y_screen)),
        sw,
        sh
    };

    if (!mask_tex_) {
        mask_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, mask_->w, mask_->h);
        if (!mask_tex_) return;
        upload_mask();
    }
    int tex_w = 0, tex_h = 0;
    SDL_QueryTexture(mask_tex_, nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w != mask_->w || tex_h != mask_->h) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, mask_->w, mask_->h);
        if (!mask_tex_) return;
        upload_mask();
    }

    SDL_SetTextureBlendMode(mask_tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(mask_tex_, mask_alpha_);
    SDL_RenderCopyEx(r, mask_tex_, nullptr, &dst, 0.0, nullptr, asset_->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

    if (toolbox_) toolbox_->render(r);
}

std::vector<SDL_Point> AreaOverlayEditor::trace_polygon_from_mask() const {
    std::vector<SDL_Point> poly;
    if (!mask_) return poly;
    SDL_LockSurface(mask_);
    auto getA = [&](int x, int y) -> Uint8 {
        if (x < 0 || y < 0 || x >= mask_->w || y >= mask_->h) return 0;
        const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
        const int pitch = mask_->pitch;
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint8 r, g, b, a;
        (void)r; (void)g; (void)b;
        SDL_GetRGBA(*p, mask_->format, &r, &g, &b, &a);
        return a;
    };
    int sx = -1, sy = -1;
    for (int y = 0; y < mask_->h && sy < 0; ++y) {
        for (int x = 0; x < mask_->w; ++x) {
            if (getA(x, y) == 0) continue;
            if (getA(x - 1, y) == 0 || getA(x + 1, y) == 0 || getA(x, y - 1) == 0 || getA(x, y + 1) == 0) {
                sx = x;
                sy = y;
                break;
            }
        }
    }
    if (sy < 0) {
        SDL_UnlockSurface(mask_);
        return poly;
    }

    static const int ndx[8] = { 1, 1, 0,-1,-1,-1, 0, 1 };
    static const int ndy[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
    auto neighbor_index = [&](int cx, int cy, int nx, int ny) -> int {
        for (int i = 0; i < 8; ++i) if (cx + ndx[i] == nx && cy + ndy[i] == ny) return i;
        return 0;
    };
    constexpr int POLYGON_TRACE_GUARD_LIMIT = 200000;
    int px = sx - 1, py = sy;
    int cx = sx, cy = sy;
    int first_nx = -1, first_ny = -1;
    int guard = 0;
    while (guard++ < POLYGON_TRACE_GUARD_LIMIT) {
        poly.push_back(SDL_Point{ cx, cy });
        int bi = neighbor_index(cx, cy, px, py);
        int i = (bi + 1) & 7;
        bool found = false;
        for (int k = 0; k < 8; ++k) {
            int nx = cx + ndx[i];
            int ny = cy + ndy[i];
            if (getA(nx, ny) > 0) {
                px = cx;
                py = cy;
                cx = nx;
                cy = ny;
                if (first_nx < 0) {
                    first_nx = cx;
                    first_ny = cy;
                }
                found = true;
                break;
            }
            i = (i + 1) & 7;
        }
        if (!found) break;
        if ((cx == sx && cy == sy) && (poly.size() > 1 && first_nx == poly[1].x && first_ny == poly[1].y)) {
            break;
        }
    }

    std::vector<SDL_Point> out;
    out.reserve(poly.size());
    SDL_Point last{INT_MIN, INT_MIN};
    for (const auto& p : poly) {
        if (p.x != last.x || p.y != last.y) out.push_back(p);
        last = p;
    }
    SDL_UnlockSurface(mask_);
    return out;
}

void AreaOverlayEditor::save_area() {
    if (!info_) {
        return;
    }
    if (!mask_) {
        bool removed = info_->remove_area(area_name_);
        if (removed) {
            (void)info_->update_info_json();
            saved_since_begin_ = true;
        }
        cancel();
        return;
    }

    bool has_alpha = false;
    int min_sx = mask_->w;
    int min_sy = mask_->h;
    int max_sx = -1;
    int max_sy = -1;

    if (SDL_LockSurface(mask_) == 0) {
        const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
        const int pitch = mask_->pitch;
        for (int y = 0; y < mask_->h; ++y) {
            const Uint32* row = reinterpret_cast<const Uint32*>(pixels + y * pitch);
            for (int x = 0; x < mask_->w; ++x) {
                Uint8 r, g, b, a;
                SDL_GetRGBA(row[x], mask_->format, &r, &g, &b, &a);
                if (a > 0) {
                    has_alpha = true;
                    min_sx = std::min(min_sx, x);
                    min_sy = std::min(min_sy, y);
                    max_sx = std::max(max_sx, x);
                    max_sy = std::max(max_sy, y);
                }
            }
        }
        SDL_UnlockSurface(mask_);
    }

    if (!has_alpha) {
        bool removed = info_->remove_area(area_name_);
        if (removed) {
            (void)info_->update_info_json();
            saved_since_begin_ = true;
        }
        cancel();
        return;
    }

    auto polygon = trace_polygon_from_mask();
    if (polygon.size() < 3) {
        int rect_max_sx = max_sx;
        int rect_max_sy = max_sy;
        if (min_sx == rect_max_sx) rect_max_sx = min_sx + 1;
        if (min_sy == rect_max_sy) rect_max_sy = min_sy + 1;
        polygon.clear();
        polygon.push_back(SDL_Point{ min_sx, min_sy });
        polygon.push_back(SDL_Point{ rect_max_sx, min_sy });
        polygon.push_back(SDL_Point{ rect_max_sx, rect_max_sy });
        polygon.push_back(SDL_Point{ min_sx, rect_max_sy });
    }

    std::vector<Area::Point> area_points;
    area_points.reserve(polygon.size());
    for (const auto& p : polygon) {
        area_points.push_back(SDL_Point{ p.x + mask_origin_x_, p.y + mask_origin_y_ });
    }

    if (area_points.size() >= 2) {
        const auto& first = area_points.front();
        const auto& last = area_points.back();
        if (first.x == last.x && first.y == last.y) {
            area_points.pop_back();
        }
    }

    if (area_points.size() < 3) {
        int min_lx = min_sx + mask_origin_x_;
        int min_ly = min_sy + mask_origin_y_;
        int max_lx = max_sx + mask_origin_x_;
        int max_ly = max_sy + mask_origin_y_;
        if (min_lx == max_lx) max_lx = min_lx + 1;
        if (min_ly == max_ly) max_ly = min_ly + 1;
        area_points.clear();
        area_points.push_back(SDL_Point{ min_lx, min_ly });
        area_points.push_back(SDL_Point{ max_lx, min_ly });
        area_points.push_back(SDL_Point{ max_lx, max_ly });
        area_points.push_back(SDL_Point{ min_lx, max_ly });
    }

    if (area_points.size() < 3) {
        cancel();
        return;
    }

    Area area(area_name_, area_points);
    info_->upsert_area_from_editor(area);
    (void)info_->update_info_json();
    saved_since_begin_ = true;
    cancel();
}
