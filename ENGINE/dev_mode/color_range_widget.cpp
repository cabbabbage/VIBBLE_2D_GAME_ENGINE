#include "color_range_widget.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "font_cache.hpp"
#include "utils/ranged_color.hpp"

namespace {
constexpr int kMapWidth = 256;
constexpr int kMapHeight = 160;
constexpr int kSliderLabelWidth = 28;

SDL_Rect make_rect(int x, int y, int w, int h) {
    return SDL_Rect{x, y, w, h};
}

int clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

} // namespace

class DMColorRangeWidget::Picker {
public:
    explicit Picker(DMColorRangeWidget& owner)
        : owner_(owner) {
        r_slider_ = std::make_unique<DMRangeSlider>(0, 255, 0, 255);
        g_slider_ = std::make_unique<DMRangeSlider>(0, 255, 0, 255);
        b_slider_ = std::make_unique<DMRangeSlider>(0, 255, 0, 255);
        a_slider_ = std::make_unique<DMRangeSlider>(0, 255, 0, 255);
        r_slider_->set_defer_commit_until_unfocus(false);
        g_slider_->set_defer_commit_until_unfocus(false);
        b_slider_->set_defer_commit_until_unfocus(false);
        a_slider_->set_defer_commit_until_unfocus(false);
    }

    ~Picker() {
        if (map_texture_) {
            SDL_DestroyTexture(map_texture_);
            map_texture_ = nullptr;
        }
    }

    bool visible() const { return visible_; }

    void open(const SDL_Rect& anchor, const RangedColor& value) {
        anchor_ = anchor;
        value_ = utils::color::clamp_ranged_color(value);
        resolved_color_ = utils::color::resolve_ranged_color(value_);
        visible_ = true;
        dragging_map_ = false;
        update_layout();
        sync_sliders_from_value();
        texture_dirty_ = true;
    }

    void close() {
        visible_ = false;
        dragging_map_ = false;
    }

    bool contains_point(int x, int y) const {
        SDL_Point p{x, y};
        return SDL_PointInRect(&p, &rect_);
    }

    bool handle_event(const SDL_Event& e) {
        if (!visible_) {
            return false;
        }
        bool used = false;
        const bool pointer_event =
            (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);

        if (pointer_event) {
            SDL_Point p{0, 0};
            if (e.type == SDL_MOUSEMOTION) {
                p = SDL_Point{e.motion.x, e.motion.y};
            } else {
                p = SDL_Point{e.button.x, e.button.y};
            }
            if (SDL_PointInRect(&p, &map_rect_)) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    dragging_map_ = true;
                    handle_map_interaction(p.x, p.y);
                    used = true;
                } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    if (dragging_map_) {
                        handle_map_interaction(p.x, p.y);
                        dragging_map_ = false;
                        used = true;
                    }
                } else if (e.type == SDL_MOUSEMOTION && dragging_map_) {
                    handle_map_interaction(p.x, p.y);
                    used = true;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                dragging_map_ = false;
            }
        }

        const RangedColor before = value_;
        if (r_slider_ && r_slider_->handle_event(e)) used = true;
        if (g_slider_ && g_slider_->handle_event(e)) used = true;
        if (b_slider_ && b_slider_->handle_event(e)) used = true;
        if (a_slider_ && a_slider_->handle_event(e)) used = true;

        const bool changed = before_changed(before);
        if (changed) {
            texture_dirty_ = texture_dirty_ || (before.b.min != value_.b.min ||
                                                before.b.max != value_.b.max);
            notify_parent();
        }

        return used || changed;
    }

    void render(SDL_Renderer* r) const {
        if (!visible_) {
            return;
        }

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        dm_draw::DrawBeveledRect(
            r,
            rect_,
            DMStyles::CornerRadius(),
            DMStyles::BevelDepth(),
            DMStyles::PanelBG(),
            DMStyles::HighlightColor(),
            DMStyles::ShadowColor(),
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());

        SDL_Color border = DMStyles::Border();
        dm_draw::DrawRoundedOutline(r, rect_, DMStyles::CornerRadius(), 1, border);

        ensure_texture(r);
        if (map_texture_) {
            update_texture(r);
            SDL_RenderCopy(r, map_texture_, nullptr, &map_rect_);
        }

        draw_overlay(r);
        render_sliders(r);
    }

    void set_value(const RangedColor& value) {
        value_ = utils::color::clamp_ranged_color(value);
        resolved_color_ = utils::color::resolve_ranged_color(value_);
        sync_sliders_from_value();
        texture_dirty_ = true;
    }

private:
    bool before_changed(const RangedColor& before) {
        sync_value_from_sliders();
        return before.r.min != value_.r.min || before.r.max != value_.r.max ||
               before.g.min != value_.g.min || before.g.max != value_.g.max ||
               before.b.min != value_.b.min || before.b.max != value_.b.max ||
               before.a.min != value_.a.min || before.a.max != value_.a.max;
    }

    void notify_parent() {
        resolved_color_ = utils::color::resolve_ranged_color(value_);
        owner_.on_picker_value_changed(value_);
    }

    void update_layout() {
        const int pad = DMSpacing::item_gap();
        const int gap = DMSpacing::small_gap();
        const int width = std::max(kMapWidth + pad * 2, anchor_.w + pad * 2);
        const int slider_height = DMRangeSlider::height();
        const int slider_count = 4;
        const int slider_area_height = slider_count * slider_height + (slider_count - 1) * gap;
        const int height = pad + kMapHeight + pad + slider_area_height + pad;

        rect_ = make_rect(anchor_.x, anchor_.y + anchor_.h + gap, width, height);
        map_rect_ = make_rect(rect_.x + pad, rect_.y + pad, rect_.w - pad * 2, kMapHeight);

        const int slider_x = map_rect_.x + kSliderLabelWidth + gap;
        const int slider_w = std::max(80, map_rect_.w - kSliderLabelWidth - gap);
        int slider_y = map_rect_.y + map_rect_.h + pad;

        if (r_slider_) r_slider_->set_rect(make_rect(slider_x, slider_y, slider_w, slider_height));
        slider_y += slider_height + gap;
        if (g_slider_) g_slider_->set_rect(make_rect(slider_x, slider_y, slider_w, slider_height));
        slider_y += slider_height + gap;
        if (b_slider_) b_slider_->set_rect(make_rect(slider_x, slider_y, slider_w, slider_height));
        slider_y += slider_height + gap;
        if (a_slider_) a_slider_->set_rect(make_rect(slider_x, slider_y, slider_w, slider_height));
    }

    void ensure_texture(SDL_Renderer* r) const {
        if (!r) {
            return;
        }
        if (!map_texture_) {
            map_texture_ = SDL_CreateTexture(r,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             std::max(1, map_rect_.w),
                                             std::max(1, map_rect_.h));
            if (map_texture_) {
                SDL_SetTextureBlendMode(map_texture_, SDL_BLENDMODE_BLEND);
                texture_dirty_ = true;
            }
            return;
        }
        int tex_w = 0, tex_h = 0;
        SDL_QueryTexture(map_texture_, nullptr, nullptr, &tex_w, &tex_h);
        if (tex_w != map_rect_.w || tex_h != map_rect_.h) {
            SDL_DestroyTexture(map_texture_);
            map_texture_ = SDL_CreateTexture(r,
                                             SDL_PIXELFORMAT_RGBA8888,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             std::max(1, map_rect_.w),
                                             std::max(1, map_rect_.h));
            if (map_texture_) {
                SDL_SetTextureBlendMode(map_texture_, SDL_BLENDMODE_BLEND);
                texture_dirty_ = true;
            }
        }
    }

    void update_texture(SDL_Renderer* r) const {
        if (!texture_dirty_ || !map_texture_) {
            return;
        }
        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(map_texture_, nullptr, &pixels, &pitch) != 0 || !pixels) {
            return;
        }
        static SDL_PixelFormat* fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
        if (!fmt) {
            SDL_UnlockTexture(map_texture_);
            texture_dirty_ = false;
            return;
        }
        const int width = std::max(1, map_rect_.w);
        const int height = std::max(1, map_rect_.h);
        const Uint8 fixed_b = resolved_color_.b;
        const Uint8 fixed_a = 255;
        for (int y = 0; y < height; ++y) {
            Uint32* row = reinterpret_cast<Uint32*>(static_cast<unsigned char*>(pixels) + y * pitch);
            float g_t = 1.0f - (static_cast<float>(y) / std::max(1, height - 1));
            int g_value = clamp_int(static_cast<int>(std::round(g_t * 255.0f)), 0, 255);
            for (int x = 0; x < width; ++x) {
                float r_t = static_cast<float>(x) / std::max(1, width - 1);
                int r_value = clamp_int(static_cast<int>(std::round(r_t * 255.0f)), 0, 255);
                row[x] = SDL_MapRGBA(fmt, r_value, g_value, fixed_b, fixed_a);
            }
        }
        SDL_UnlockTexture(map_texture_);
        texture_dirty_ = false;
    }

    void draw_overlay(SDL_Renderer* r) const {
        const auto clamped = utils::color::clamp_ranged_color(value_);
        if (map_rect_.w <= 0 || map_rect_.h <= 0) {
            return;
        }

        auto x_for_r = [&](int r_value) {
            const float t = static_cast<float>(r_value) / 255.0f;
            return map_rect_.x + static_cast<int>(std::round(t * (map_rect_.w - 1)));
        };
        auto y_for_g = [&](int g_value) {
            const float t = 1.0f - static_cast<float>(g_value) / 255.0f;
            return map_rect_.y + static_cast<int>(std::round(t * (map_rect_.h - 1)));
        };

        int x0 = x_for_r(clamped.r.min);
        int x1 = x_for_r(clamped.r.max);
        if (x0 > x1) std::swap(x0, x1);
        int y0 = y_for_g(clamped.g.max);
        int y1 = y_for_g(clamped.g.min);
        if (y0 > y1) std::swap(y0, y1);

        SDL_Rect overlay{x0, y0, std::max(1, x1 - x0 + 1), std::max(1, y1 - y0 + 1)};
        SDL_Color fill = resolved_color_;
        fill.a = 80;
        SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(r, &overlay);

        SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 160);
        SDL_RenderDrawRect(r, &overlay);

        const Uint8 r_mid = static_cast<Uint8>((clamped.r.min + clamped.r.max) / 2);
        const Uint8 g_mid = static_cast<Uint8>((clamped.g.min + clamped.g.max) / 2);
        SDL_Color top_color{r_mid, g_mid, static_cast<Uint8>(clamped.b.max), 220};
        SDL_Color bottom_color{r_mid, g_mid, static_cast<Uint8>(clamped.b.min), 220};
        SDL_Color left_alpha{static_cast<Uint8>(clamped.a.min), static_cast<Uint8>(clamped.a.min), static_cast<Uint8>(clamped.a.min), 220};
        SDL_Color right_alpha{static_cast<Uint8>(clamped.a.max), static_cast<Uint8>(clamped.a.max), static_cast<Uint8>(clamped.a.max), 220};

        SDL_SetRenderDrawColor(r, top_color.r, top_color.g, top_color.b, top_color.a);
        SDL_RenderDrawLine(r, overlay.x, overlay.y, overlay.x + overlay.w - 1, overlay.y);
        SDL_SetRenderDrawColor(r, bottom_color.r, bottom_color.g, bottom_color.b, bottom_color.a);
        SDL_RenderDrawLine(r, overlay.x, overlay.y + overlay.h - 1, overlay.x + overlay.w - 1, overlay.y + overlay.h - 1);
        SDL_SetRenderDrawColor(r, left_alpha.r, left_alpha.g, left_alpha.b, left_alpha.a);
        SDL_RenderDrawLine(r, overlay.x, overlay.y, overlay.x, overlay.y + overlay.h - 1);
        SDL_SetRenderDrawColor(r, right_alpha.r, right_alpha.g, right_alpha.b, right_alpha.a);
        SDL_RenderDrawLine(r, overlay.x + overlay.w - 1, overlay.y, overlay.x + overlay.w - 1, overlay.y + overlay.h - 1);
    }

    void render_sliders(SDL_Renderer* r) const {
        if (!r) return;
        const DMLabelStyle label_style = DMStyles::Label();
        const char* labels[4] = {"R", "G", "B", "A"};
        DMRangeSlider* sliders[4] = { r_slider_.get(), g_slider_.get(), b_slider_.get(), a_slider_.get() };
        for (int i = 0; i < 4; ++i) {
            DMRangeSlider* slider = sliders[i];
            if (!slider) continue;
            SDL_Rect slider_rect = slider->rect();
            SDL_Rect label_rect = make_rect(slider_rect.x - (kSliderLabelWidth + DMSpacing::small_gap()), slider_rect.y, kSliderLabelWidth, slider_rect.h);
            SDL_Point label_size = DMFontCache::instance().measure_text(label_style, labels[i]);
            DMFontCache::instance().draw_text(
                r,
                label_style,
                labels[i],
                label_rect.x + std::max(0, (kSliderLabelWidth - label_size.x) / 2),
                label_rect.y + std::max(0, (slider_rect.h - label_size.y) / 2));
            slider->render(r);
        }
    }

    void sync_sliders_from_value() {
        if (r_slider_) {
            r_slider_->set_min_value(clamp_int(value_.r.min, 0, 255));
            r_slider_->set_max_value(clamp_int(value_.r.max, 0, 255));
        }
        if (g_slider_) {
            g_slider_->set_min_value(clamp_int(value_.g.min, 0, 255));
            g_slider_->set_max_value(clamp_int(value_.g.max, 0, 255));
        }
        if (b_slider_) {
            b_slider_->set_min_value(clamp_int(value_.b.min, 0, 255));
            b_slider_->set_max_value(clamp_int(value_.b.max, 0, 255));
        }
        if (a_slider_) {
            a_slider_->set_min_value(clamp_int(value_.a.min, 0, 255));
            a_slider_->set_max_value(clamp_int(value_.a.max, 0, 255));
        }
    }

    void sync_value_from_sliders() {
        if (r_slider_) {
            value_.r.min = clamp_int(r_slider_->min_value(), 0, 255);
            value_.r.max = clamp_int(r_slider_->max_value(), 0, 255);
        }
        if (g_slider_) {
            value_.g.min = clamp_int(g_slider_->min_value(), 0, 255);
            value_.g.max = clamp_int(g_slider_->max_value(), 0, 255);
        }
        if (b_slider_) {
            value_.b.min = clamp_int(b_slider_->min_value(), 0, 255);
            value_.b.max = clamp_int(b_slider_->max_value(), 0, 255);
        }
        if (a_slider_) {
            value_.a.min = clamp_int(a_slider_->min_value(), 0, 255);
            value_.a.max = clamp_int(a_slider_->max_value(), 0, 255);
        }
        value_ = utils::color::clamp_ranged_color(value_);
    }

    void handle_map_interaction(int px, int py) {
        if (map_rect_.w <= 0 || map_rect_.h <= 0) {
            return;
        }
        const int clamped_x = clamp_int(px, map_rect_.x, map_rect_.x + map_rect_.w - 1);
        const int clamped_y = clamp_int(py, map_rect_.y, map_rect_.y + map_rect_.h - 1);
        const float r_t = static_cast<float>(clamped_x - map_rect_.x) / std::max(1, map_rect_.w - 1);
        const float g_t = 1.0f - static_cast<float>(clamped_y - map_rect_.y) / std::max(1, map_rect_.h - 1);
        const int r_val = clamp_int(static_cast<int>(std::round(r_t * 255.0f)), 0, 255);
        const int g_val = clamp_int(static_cast<int>(std::round(g_t * 255.0f)), 0, 255);
        value_.r.min = value_.r.max = r_val;
        value_.g.min = value_.g.max = g_val;
        sync_sliders_from_value();
        notify_parent();
    }

    DMColorRangeWidget& owner_;
    SDL_Rect anchor_{0, 0, 0, 0};
    SDL_Rect rect_{0, 0, 0, 0};
    SDL_Rect map_rect_{0, 0, 0, 0};
    RangedColor value_{};
    SDL_Color resolved_color_{255, 255, 255, 255};
    mutable SDL_Texture* map_texture_ = nullptr;
    mutable bool texture_dirty_ = true;
    std::unique_ptr<DMRangeSlider> r_slider_;
    std::unique_ptr<DMRangeSlider> g_slider_;
    std::unique_ptr<DMRangeSlider> b_slider_;
    std::unique_ptr<DMRangeSlider> a_slider_;
    bool visible_ = false;
    bool dragging_map_ = false;
};

// -----------------------------------------------------------------------------
// DMColorRangeWidget
// -----------------------------------------------------------------------------

DMColorRangeWidget::DMColorRangeWidget(std::string label)
    : label_(std::move(label)) {
    value_.r = utils::color::ChannelRange{255, 255};
    value_.g = utils::color::ChannelRange{255, 255};
    value_.b = utils::color::ChannelRange{255, 255};
    value_.a = utils::color::ChannelRange{255, 255};
    resolved_color_ = utils::color::resolve_ranged_color(value_);
}

DMColorRangeWidget::~DMColorRangeWidget() = default;

void DMColorRangeWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    update_layout();
}

int DMColorRangeWidget::height_for_width(int) const {
    const DMLabelStyle label_style = DMStyles::Label();
    SDL_Point label_size = DMFontCache::instance().measure_text(label_style, label_);
    const int gap = DMSpacing::small_gap();
    const int content_height = 32;
    return label_size.y + gap + content_height;
}

bool DMColorRangeWidget::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        SDL_Point p{e.button.x, e.button.y};
        if (SDL_PointInRect(&p, &swatch_rect_)) {
            if (e.type == SDL_MOUSEBUTTONUP) {
                open_picker();
            }
            return true;
        }
    }
    return false;
}

void DMColorRangeWidget::render(SDL_Renderer* r) const {
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const DMLabelStyle label_style = DMStyles::Label();
    DMFontCache::instance().draw_text(r, label_style, label_, label_rect_.x, label_rect_.y);

    dm_draw::DrawBeveledRect(
        r,
        swatch_rect_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        resolved_color_,
        DMStyles::HighlightColor(),
        DMStyles::ShadowColor(),
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    SDL_Color border = DMStyles::Border();
    dm_draw::DrawRoundedOutline(r, swatch_rect_, DMStyles::CornerRadius(), 1, border);
}

void DMColorRangeWidget::set_value(const RangedColor& value) {
    const RangedColor clamped = utils::color::clamp_ranged_color(value);
    if (clamped.r.min == value_.r.min && clamped.r.max == value_.r.max &&
        clamped.g.min == value_.g.min && clamped.g.max == value_.g.max &&
        clamped.b.min == value_.b.min && clamped.b.max == value_.b.max &&
        clamped.a.min == value_.a.min && clamped.a.max == value_.a.max) {
        return;
    }
    value_ = clamped;
    resolved_color_ = utils::color::resolve_ranged_color(value_);
    if (picker_) {
        picker_->set_value(value_);
    }
    if (on_value_changed_) {
        on_value_changed_(value_);
    }
}

void DMColorRangeWidget::set_on_value_changed(ValueChangedCallback cb) {
    on_value_changed_ = std::move(cb);
}

void DMColorRangeWidget::set_label(std::string label) {
    label_ = std::move(label);
    update_layout();
}

bool DMColorRangeWidget::handle_overlay_event(const SDL_Event& e) {
    if (!picker_ || !picker_->visible()) {
        return false;
    }
    if (picker_->handle_event(e)) {
        return true;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (!picker_->contains_point(e.button.x, e.button.y)) {
            picker_->close();
            return true;
        }
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        picker_->close();
        return true;
    }
    return false;
}

void DMColorRangeWidget::render_overlay(SDL_Renderer* r) const {
    if (picker_ && picker_->visible()) {
        picker_->render(r);
    }
}

bool DMColorRangeWidget::overlay_visible() const {
    return picker_ && picker_->visible();
}

void DMColorRangeWidget::close_overlay() {
    if (picker_) {
        picker_->close();
    }
}

void DMColorRangeWidget::update_layout() {
    const int gap = DMSpacing::small_gap();
    const DMLabelStyle label_style = DMStyles::Label();
    SDL_Point label_size = DMFontCache::instance().measure_text(label_style, label_);
    label_rect_ = make_rect(rect_.x, rect_.y, rect_.w, label_size.y);
    const int swatch_height = 32;
    swatch_rect_ = make_rect(rect_.x, rect_.y + label_rect_.h + gap, rect_.w, swatch_height);
}

void DMColorRangeWidget::open_picker() {
    ensure_picker();
    if (picker_) {
        picker_->open(swatch_rect_, value_);
    }
}

void DMColorRangeWidget::ensure_picker() {
    if (!picker_) {
        picker_ = std::make_unique<Picker>(*this);
    }
}

void DMColorRangeWidget::on_picker_value_changed(const RangedColor& value) {
    value_ = value;
    resolved_color_ = utils::color::resolve_ranged_color(value_);
    if (on_value_changed_) {
        on_value_changed_(value_);
    }
}

