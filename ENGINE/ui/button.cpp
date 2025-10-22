#include "button.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr int   kCaptureBleed   = 14;     // extra margin around the button for warp lookups
constexpr float kEdgeFeatherPx  = 2.0f;   // AA feather width for mask edges

struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { if (s) SDL_FreeSurface(s); }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

inline Uint8 clamp8(int v) { return static_cast<Uint8>(std::clamp(v, 0, 255)); }

inline SDL_Color unpack(Uint32 px) {
    SDL_Color c;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    c.a =  px        & 0xFF;
    c.b = (px >> 8 ) & 0xFF;
    c.g = (px >> 16) & 0xFF;
    c.r = (px >> 24) & 0xFF;
#else
    c.a = (px >> 24) & 0xFF;
    c.r = (px >> 16) & 0xFF;
    c.g = (px >> 8 ) & 0xFF;
    c.b =  px        & 0xFF;
#endif
    return c;
}
inline Uint32 pack(SDL_PixelFormat* fmt, const SDL_Color& c) {
    return SDL_MapRGBA(fmt, c.r, c.g, c.b, c.a);
}

inline float luminance(const SDL_Color& c) {
    return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / 255.0f;
}

inline SDL_Rect adjusted_for_state(SDL_Rect r, bool hovered, bool pressed) {
    if (pressed)      r.y += 1;
    else if (hovered) r.y -= 1;
    return r;
}

inline SDL_Rect clamp_to_view(SDL_Renderer* r, SDL_Rect rect) {
    SDL_Rect vp{0,0,0,0};
    if (r) SDL_RenderGetViewport(r, &vp);
    int x1 = std::max(rect.x, vp.x);
    int y1 = std::max(rect.y, vp.y);
    int x2 = std::min(rect.x + rect.w, vp.x + vp.w);
    int y2 = std::min(rect.y + rect.h, vp.y + vp.h);
    return SDL_Rect{ x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1) };
}

SurfacePtr capture(SDL_Renderer* renderer, const SDL_Rect& rect) {
    if (rect.w <= 0 || rect.h <= 0) return {};
    SurfacePtr s(SDL_CreateRGBSurfaceWithFormat(0, rect.w, rect.h, 32, SDL_PIXELFORMAT_RGBA32));
    if (!s) return {};
    if (SDL_RenderReadPixels(renderer, &rect, s->format->format, s->pixels, s->pitch) != 0) return {};
    return s;
}
SurfacePtr clone(SDL_Surface* src) {
    if (!src) return {};
    SurfacePtr dst(SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, src->format->format));
    if (!dst) return {};
    SDL_LockSurface(src);
    SDL_LockSurface(dst.get());
    for (int y = 0; y < src->h; ++y)
        std::memcpy(static_cast<uint8_t*>(dst->pixels) + y * dst->pitch,
                    static_cast<uint8_t*>(src->pixels) + y * src->pitch,
                    std::min(dst->pitch, src->pitch));
    SDL_UnlockSurface(dst.get());
    SDL_UnlockSurface(src);
    return dst;
}

void box_blur(SDL_Surface* s, int radius) {
    if (!s || radius <= 0) return;
    SDL_LockSurface(s);
    const int w = s->w, h = s->h, pitch = s->pitch / 4;
    Uint32* px = static_cast<Uint32*>(s->pixels);
    std::vector<Uint32> temp(w * h);

    const int win = radius * 2 + 1;

    // H
    for (int y = 0; y < h; ++y) {
        int sr=0, sg=0, sb=0, sa=0;
        for (int x = -radius; x <= radius; ++x) {
            int sx = std::clamp(x, 0, w - 1);
            SDL_Color c = unpack(px[y * pitch + sx]);
            sr += c.r; sg += c.g; sb += c.b; sa += c.a;
        }
        for (int x = 0; x < w; ++x) {
            SDL_Color o;
            o.r = clamp8(sr / win); o.g = clamp8(sg / win);
            o.b = clamp8(sb / win); o.a = clamp8(sa / win);
            temp[y * w + x] = pack(s->format, o);

            int rx = std::clamp(x - radius, 0, w - 1);
            int ax = std::clamp(x + radius + 1, 0, w - 1);
            SDL_Color rem = unpack(px[y * pitch + rx]);
            SDL_Color add = unpack(px[y * pitch + ax]);
            sr += add.r - rem.r; sg += add.g - rem.g; sb += add.b - rem.b; sa += add.a - rem.a;
        }
    }

    // V
    for (int x = 0; x < w; ++x) {
        int sr=0, sg=0, sb=0, sa=0;
        for (int y = -radius; y <= radius; ++y) {
            int sy = std::clamp(y, 0, h - 1);
            SDL_Color c = unpack(temp[sy * w + x]);
            sr += c.r; sg += c.g; sb += c.b; sa += c.a;
        }
        for (int y = 0; y < h; ++y) {
            SDL_Color o;
            o.r = clamp8(sr / win); o.g = clamp8(sg / win);
            o.b = clamp8(sb / win); o.a = clamp8(sa / win);
            px[y * pitch + x] = pack(s->format, o);

            int ry = std::clamp(y - radius, 0, h - 1);
            int ay = std::clamp(y + radius + 1, 0, h - 1);
            SDL_Color rem = unpack(temp[ry * w + x]);
            SDL_Color add = unpack(temp[ay * w + x]);
            sr += add.r - rem.r; sg += add.g - rem.g; sb += add.b - rem.b; sa += add.a - rem.a;
        }
    }
    SDL_UnlockSurface(s);
}
inline void apply_kawase(SDL_Surface* s, int blur_px) {
    if (!s || blur_px <= 0) return;
    box_blur(s, std::max(1, blur_px / 3));
    box_blur(s, std::max(1, blur_px / 2));
}

// Rounded-rect coverage with 2x2 SSAA and soft feather to avoid hard borders.
inline float rr_coverage_px(int x, int y, int w, int h, int radius) {
    if (radius <= 0) return 1.0f;
    const float cx[2] = { x + 0.25f, x + 0.75f };
    const float cy[2] = { y + 0.25f, y + 0.75f };
    const float R = static_cast<float>(radius) - 0.5f;
    const float left   = R;
    const float right  = static_cast<float>(w) - R - 1.0f;
    const float top    = R;
    const float bottom = static_cast<float>(h) - R - 1.0f;

    auto inside = [&](float px, float py) -> bool {
        if (px >= left && px <= right && py >= top && py <= bottom) return true;
        float dx = (px < left)  ? (left  - px) : ((px > right)  ? (px - right)  : 0.0f);
        float dy = (py < top)   ? (top   - py) : ((py > bottom) ? (py - bottom) : 0.0f);
        return (dx*dx + dy*dy) <= (R * R);
    };

    int count = 0;
    for (float yy : cy) for (float xx : cx) if (inside(xx, yy)) ++count;

    float base = static_cast<float>(count) * 0.25f;
    // slight outward feather
    return std::clamp(base * (1.0f + (kEdgeFeatherPx * 0.02f)), 0.0f, 1.0f);
}

SDL_Texture* to_texture(SDL_Renderer* r, SDL_Surface* s) {
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

inline Uint8 add_gain(Uint8 ch, Uint8 target, float gain) {
    int v = static_cast<int>(std::round(ch + (target - ch) * std::clamp(gain, 0.0f, 2.0f)));
    return clamp8(v);
}

} // namespace

bool Button::glass_blur_enabled_ = true;

// --------- text helper (kept from your prior API style) ----------
static void blit_text_center(SDL_Renderer* r,
                             const LabelStyle& style,
                             const std::string& s,
                             const SDL_Rect& rect,
                             bool shadow,
                             SDL_Color override_col)
{
    if (s.empty()) return;
    TTF_Font* f = style.open_font();
    if (!f) return;

    int tw=0, th=0;
    TTF_SizeText(f, s.c_str(), &tw, &th);
    const int x = rect.x + (rect.w - tw)/2;
    const int y = rect.y + (rect.h - th)/2;
    const SDL_Color text_col = override_col.a ? override_col : style.color;

    SDL_Surface* surf_text = TTF_RenderText_Blended(f, s.c_str(), text_col);
    SDL_Surface* surf_shadow = nullptr;
    if (shadow) {
        SDL_Color coal = Styles::Coal();
        surf_shadow = TTF_RenderText_Blended(f, s.c_str(), coal);
    }
    if (surf_text) {
        SDL_Texture* tex_text = SDL_CreateTextureFromSurface(r, surf_text);
        if (surf_shadow) {
            SDL_Texture* tex_shadow = SDL_CreateTextureFromSurface(r, surf_shadow);
            if (tex_shadow) {
                SDL_Rect dsts { x+2, y+2, surf_shadow->w, surf_shadow->h };
                SDL_SetTextureAlphaMod(tex_shadow, 120);
                SDL_RenderCopy(r, tex_shadow, nullptr, &dsts);
                SDL_DestroyTexture(tex_shadow);
            }
        }
        if (tex_text) {
            SDL_Rect dst { x, y, surf_text->w, surf_text->h };
            SDL_RenderCopy(r, tex_text, nullptr, &dst);
            SDL_DestroyTexture(tex_text);
        }
    }
    if (surf_shadow) SDL_FreeSurface(surf_shadow);
    if (surf_text) SDL_FreeSurface(surf_text);
    TTF_CloseFont(f);
}

// --------- basic API plumbing ----------
Button Button::get_main_button(const std::string& text) {
    return Button(text, &Styles::MainDecoButton(), width(), height());
}
Button Button::get_exit_button(const std::string& text) {
    return Button(text, &Styles::ExitDecoButton(), width(), height());
}

Button::Button() = default;

Button::Button(const std::string& text, const ButtonStyle* style, int w, int h)
: rect_{0,0,w,h}, label_(text), style_(style), glass_style_(default_glass_style()) {}

void Button::set_position(SDL_Point p) { rect_.x = p.x; rect_.y = p.y; }
void Button::set_rect(const SDL_Rect& r) { rect_ = r; }
const SDL_Rect& Button::rect() const { return rect_; }
void Button::set_text(const std::string& text) { label_ = text; }
const std::string& Button::text() const { return label_; }

bool Button::handle_event(const SDL_Event& e) {
    bool clicked = false;
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        hovered_ = SDL_PointInRect(&p, &rect_);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &rect_)) pressed_ = true;
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        const bool inside = SDL_PointInRect(&p, &rect_);
        if (pressed_ && inside) clicked = true;
        pressed_ = false;
    }
    return clicked;
}

void Button::render(SDL_Renderer* renderer) const {
    if (!style_) return;
    if (glass_enabled_) {
        draw_glass(renderer, rect_);
        draw_glass_text(renderer, rect_);
        return;
    }
    // Non-glass fallback (dev UI or legacy) — not used for Main/Pause glass look.
    draw_deco(renderer, rect_, hovered_);
    const SDL_Color chosen = hovered_ ? style_->text_hover : style_->text_normal;
    blit_text_center(renderer, style_->label, label_, rect_, true, chosen);
}

bool Button::is_hovered() const { return hovered_; }
bool Button::is_pressed() const { return pressed_; }
int Button::width()  { return 520; }
int Button::height() { return 64; }

void Button::draw_deco(SDL_Renderer* r, const SDL_Rect& b, bool hovered) const {
    // Minimal fallback implementation to keep API intact.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 20, 20, 20, hovered ? 140 : 110);
    SDL_RenderFillRect(r, &b);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 40);
    SDL_RenderDrawRect(r, &b);
}

void Button::set_glass_blur_enabled(bool enabled) { glass_blur_enabled_ = enabled; }

const GlassButtonStyle& Button::default_glass_style() {
    static const GlassButtonStyle kDefault{}; // fields use constexpr defaults in header
    return kDefault;
}

void Button::enable_glass_style(bool enabled) { glass_enabled_ = enabled; }
void Button::set_glass_style(const GlassButtonStyle& style) { glass_style_ = style; }

// ---------------- THE GLASS RENDER ----------------
// Replaces the button bounds with a refracted copy of the background.
// No outlines, no color fills. Bounds read via distortion, fresnel rim, and
// luminance-driven highlights (all clipped to the rounded mask).
void Button::draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    SDL_Rect r = adjusted_for_state(rect, hovered_, pressed_);

    // Capture a padded region of what's already rendered behind the button.
    SDL_Rect cap{ r.x - kCaptureBleed, r.y - kCaptureBleed,
                  r.w + kCaptureBleed*2, r.h + kCaptureBleed*2 };
    cap = clamp_to_view(renderer, cap);

    SurfacePtr bg_sharp = capture(renderer, cap);
    SurfacePtr bg_blur  = bg_sharp ? clone(bg_sharp.get()) : SurfacePtr{};
    if (glass_blur_enabled_ && bg_blur) {
        const int blur_px = pressed_ ? glass_style_.blur_px_pressed
                        : (hovered_ ? glass_style_.blur_px_hover : glass_style_.blur_px);
        apply_kawase(bg_blur.get(), blur_px);
    }

    // Composite target covering only the button rect.
    SurfacePtr comp(SDL_CreateRGBSurfaceWithFormat(0, r.w, r.h, 32, SDL_PIXELFORMAT_RGBA32));
    if (!comp) return;

    SDL_LockSurface(comp.get());
    Uint32* dst = static_cast<Uint32*>(comp->pixels);
    SDL_PixelFormat* fmt = comp->format;
    const int dpitch = comp->pitch / 4;

    Uint32* sharp = bg_sharp ? static_cast<Uint32*>(bg_sharp->pixels) : nullptr;
    Uint32* blur  = bg_blur  ? static_cast<Uint32*>(bg_blur->pixels)  : nullptr;
    const int spitch = bg_sharp ? bg_sharp->pitch / 4 : 0;

    // Lens geometry
    const int  w = r.w, h = r.h;
    const int  ox = r.x - cap.x;
    const int  oy = r.y - cap.y;
    const float cx = (w - 1) * 0.5f;
    const float cy = (h - 1) * 0.5f;
    const float inv_cx = (cx > 0) ? 1.0f / cx : 0.0f;
    const float inv_cy = (cy > 0) ? 1.0f / cy : 0.0f;

    const float ref_base = glass_style_.refraction_strength
                         * (hovered_ ? 1.25f : 1.0f)
                         * (pressed_ ? 0.88f : 1.0f);

    const float chroma   = glass_style_.chroma_strength * (pressed_ ? 0.8f : 1.0f);
    const float flare_k  = glass_style_.flare_gain * (hovered_ ? 1.12f : 1.0f) * (pressed_ ? 0.92f : 1.0f);

    auto sample_sharp = [&](int sx, int sy) -> SDL_Color {
        if (!sharp) return SDL_Color{255,255,255,255};
        sx = std::clamp(sx, 0, cap.w - 1);
        sy = std::clamp(sy, 0, cap.h - 1);
        return unpack(sharp[sy * spitch + sx]);
    };
    auto sample_luma_blur = [&](int sx, int sy) -> float {
        if (!blur) return 0.0f;
        sx = std::clamp(sx, 0, cap.w - 1);
        sy = std::clamp(sy, 0, cap.h - 1);
        return luminance(unpack(blur[sy * spitch + sx]));
    };

    // Flare sampling patterns (short and fast)
    static const std::array<SDL_Point, 8> kCross {
        SDL_Point{-12,0},{-6,0},{-2,0},{2,0},{6,0},{12,0},{0,-12},{0,12}
    };
    static const std::array<SDL_Point, 6> kDiag {
        SDL_Point{-8,-8},{-4,-4},{-2,-2},{8,8},{4,4},{2,2}
    };

    double l_acc = 0.0; int l_n = 0;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Rounded-rect AA coverage controls alpha (no border drawing).
            float cov = rr_coverage_px(x, y, w, h, glass_style_.radius);
            if (cov <= 0.001f) { dst[y*dpitch + x] = 0; continue; }

            // Normalized coords around center (-1..1)
            const float ndx = (x - cx) * inv_cx;
            const float ndy = (y - cy) * inv_cy;
            const float r2  = ndx*ndx + ndy*ndy;
            const float r1  = std::sqrt(std::min(1.0f, r2));

            // Convex lens profile: stronger near center, taper to edge.
            const float lens = std::max(0.0f, 1.0f - r1*r1);
            const float warp = ref_base * std::min(w, h) * 0.95f * lens;

            // Small tangential swirl for "thick" look.
            const float wx = ndx * warp + ndy * 0.10f * warp;
            const float wy = ndy * warp - ndx * 0.10f * warp;

            // Chromatic aberration offsets along radial dir.
            const float ax = ndx * chroma;
            const float ay = ndy * chroma;

            const int sx_g = static_cast<int>(std::round(ox + x + wx));
            const int sy_g = static_cast<int>(std::round(oy + y + wy));
            const int sx_r = static_cast<int>(std::round(ox + x + wx + ax));
            const int sy_r = static_cast<int>(std::round(oy + y + wy + ay));
            const int sx_b = static_cast<int>(std::round(ox + x + wx - ax));
            const int sy_b = static_cast<int>(std::round(oy + y + wy - ay));

            // Base refracted color from SHARP capture
            SDL_Color cg = sample_sharp(sx_g, sy_g);
            SDL_Color cr = sample_sharp(sx_r, sy_r);
            SDL_Color cb = sample_sharp(sx_b, sy_b);

            SDL_Color col{};
            col.r = clamp8((cg.r + cr.r) / 2);
            col.g = cg.g;
            col.b = clamp8((cg.b + cb.b) / 2);

            // Fresnel rim (edge brightening) — derived from radial distance; no explicit border
            // Fresnel ~ r^power; stronger near the edge.
            float fresnel = std::pow(std::clamp(r1, 0.0f, 1.0f), glass_style_.fresnel_power)
                          * glass_style_.fresnel_intensity
                          * (hovered_ ? 1.20f : 1.0f)
                          * (pressed_ ? 0.90f : 1.0f);

            if (fresnel > 0.0f) {
                col.r = add_gain(col.r, 255, fresnel);
                col.g = add_gain(col.g, 255, fresnel);
                col.b = add_gain(col.b, 255, fresnel);
            }

            // Luminance-driven flares from BLUR buffer (drive highlights only)
            float l0 = sample_luma_blur(sx_g, sy_g);
            float lc = 0.0f, ld = 0.0f;
            for (auto p : kCross) lc += sample_luma_blur(sx_g + p.x, sy_g + p.y);
            for (auto p : kDiag)  ld += sample_luma_blur(sx_g + p.x, sy_g + p.y);
            lc /= static_cast<float>(kCross.size());
            ld /= static_cast<float>(kDiag.size());

            float hi = std::pow(std::max(0.0f, (l0 - 0.35f) / 0.65f), 2.2f) * glass_style_.highlight_intensity * (1.05f + lens * 0.5f);
            float gl = std::pow(std::max(0.0f, (l0 - 0.55f) / 0.45f), 2.6f) * glass_style_.glow_intensity     * (0.95f + lens * 0.6f);
            float dr = std::pow(std::max(lc, ld), 1.5f) * flare_k;

            if (hi > 0.0f) {
                col.r = add_gain(col.r, 255, hi);
                col.g = add_gain(col.g, 255, hi);
                col.b = add_gain(col.b, 255, hi);
            }
            if (gl > 0.0f) {
                col.r = add_gain(col.r, 255, gl * 0.8f);
                col.g = add_gain(col.g, 255, gl * 0.8f);
                col.b = add_gain(col.b, 255, gl * 1.05f);
            }
            if (dr > 0.0f) {
                // a bit more blue bias on directional flares to feel prismatic
                col.r = add_gain(col.r, 235, dr * 0.70f);
                col.g = add_gain(col.g, 235, dr * 0.70f);
                col.b = add_gain(col.b, 255, dr * 0.95f);
            }

            // Hover/Pressed toning
            if (hovered_ && !pressed_) {
                col.r = clamp8(col.r + 10);
                col.g = clamp8(col.g + 10);
                col.b = clamp8(col.b + 12);
            } else if (pressed_) {
                col.r = clamp8(static_cast<int>(col.r * 0.95f));
                col.g = clamp8(static_cast<int>(col.g * 0.95f));
                col.b = clamp8(static_cast<int>(col.b * 0.95f));
            }

            // Opaque inside the mask area (we're replacing the region with refracted background).
            col.a = clamp8(static_cast<int>(cov * 255.0f));
            dst[y * dpitch + x] = pack(fmt, col);

            l_acc += luminance(col);
            ++l_n;
        }
    }
    SDL_UnlockSurface(comp.get());

    if (l_n > 0) { glass_luminance_ = static_cast<float>(l_acc / l_n); glass_has_luminance_ = true; }
    else         { glass_has_luminance_ = false; }

    SDL_Texture* tex = to_texture(renderer, comp.get());
    if (!tex) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, tex, nullptr, &r);
    SDL_DestroyTexture(tex);
}

void Button::draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    if (label_.empty()) return;
    TTF_Font* font = style_->label.open_font();
    if (!font) return;

    SDL_Rect r = adjusted_for_state(rect, hovered_, pressed_);
    int tw=0, th=0;
    TTF_SizeText(font, label_.c_str(), &tw, &th);
    const int x = r.x + (r.w - tw)/2;
    const int y = r.y + (r.h - th)/2;

    SDL_Color text  = glass_style_.text_color;
    SDL_Color stroke= glass_style_.text_stroke;

    if (hovered_ && !pressed_) {
        text.r = clamp8(text.r + 8);
        text.g = clamp8(text.g + 8);
        text.b = clamp8(text.b + 8);
    } else if (pressed_) {
        text.r = clamp8(static_cast<int>(text.r * 0.95f));
        text.g = clamp8(static_cast<int>(text.g * 0.95f));
        text.b = clamp8(static_cast<int>(text.b * 0.95f));
    }

    SDL_Surface* s_text   = TTF_RenderText_Blended(font, label_.c_str(), text);
    SDL_Surface* s_stroke = stroke.a ? TTF_RenderText_Blended(font, label_.c_str(), stroke) : nullptr;

    SDL_Texture* t_text   = s_text   ? SDL_CreateTextureFromSurface(renderer, s_text)   : nullptr;
    SDL_Texture* t_stroke = s_stroke ? SDL_CreateTextureFromSurface(renderer, s_stroke) : nullptr;

    if (t_stroke) {
        SDL_SetTextureBlendMode(t_stroke, SDL_BLENDMODE_BLEND);
        static const std::array<SDL_Point, 8> offs{{
            {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}
        }};
        for (auto o : offs) {
            SDL_Rect d{ x + o.x, y + o.y, s_stroke->w, s_stroke->h };
            SDL_RenderCopy(renderer, t_stroke, nullptr, &d);
        }
    }
    if (t_text) {
        SDL_SetTextureBlendMode(t_text, SDL_BLENDMODE_BLEND);
        SDL_Rect d{ x, y, s_text->w, s_text->h };
        SDL_RenderCopy(renderer, t_text, nullptr, &d);
    }

    if (t_text) SDL_DestroyTexture(t_text);
    if (t_stroke) SDL_DestroyTexture(t_stroke);
    if (s_text) SDL_FreeSurface(s_text);
    if (s_stroke) SDL_FreeSurface(s_stroke);
    TTF_CloseFont(font);
}
