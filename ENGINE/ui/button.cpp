#include "button.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace {

constexpr int   kCaptureBleed  = 16;   // margin so refraction lookups are in-bounds
constexpr float kEdgeFeatherPx = 2.0f; // AA feather for rounded mask

struct SurfaceDeleter { void operator()(SDL_Surface* s) const { if (s) SDL_FreeSurface(s); } };
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

inline Uint8 clamp8(int v) { return static_cast<Uint8>(std::clamp(v, 0, 255)); }

inline Uint8 lerp8(Uint8 from, Uint8 to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return clamp8(static_cast<int>(std::round(static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t)));
}

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
    if (SDL_RenderReadPixels(renderer, &rect, s->format->format, s->pixels, s->pitch) != 0) {
        SDL_Log("GlassButton: SDL_RenderReadPixels failed: %s", SDL_GetError());
        return {};
    }
    return s;
}

// -------------------- Procedural value-noise FBM --------------------
static inline uint32_t wang_hash(uint32_t x) {
    x ^= 61u; x ^= x >> 16; x *= 9u; x ^= x >> 4; x *= 0x27d4eb2du; x ^= x >> 15;
    return x;
}
static inline float rand01(int xi, int yi) {
    return (wang_hash(static_cast<uint32_t>(xi) * 73856093u ^ static_cast<uint32_t>(yi) * 19349663u) & 0xFFFFFF) / float(0xFFFFFF);
}
static inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

static float value_noise(float x, float y) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float xf = x - static_cast<float>(xi);
    float yf = y - static_cast<float>(yi);
    float u = smooth(xf), v = smooth(yf);

    float v00 = rand01(xi,   yi);
    float v10 = rand01(xi+1, yi);
    float v01 = rand01(xi,   yi+1);
    float v11 = rand01(xi+1, yi+1);

    float a = v00 + (v10 - v00) * u;
    float b = v01 + (v11 - v01) * u;
    return a + (b - a) * v;
}

static float fbm(float x, float y, int octaves=4, float lacunarity=2.0f, float gain=0.5f) {
    float amp = 0.5f;
    float freq = 1.0f;
    float sum = 0.0f;
    for (int i=0; i<octaves; ++i) {
        sum += amp * value_noise(x * freq, y * freq);
        freq *= lacunarity;
        amp *= gain;
    }
    return sum;
}

// Gradient of FBM (central difference)
static std::array<float,2> fbm_grad(float x, float y, float eps=0.8f) {
    float nx1 = fbm(x + eps, y);
    float nx0 = fbm(x - eps, y);
    float ny1 = fbm(x, y + eps);
    float ny0 = fbm(x, y - eps);
    float gx = nx1 - nx0;
    float gy = ny1 - ny0;
    float len = std::max(1e-6f, std::sqrt(gx*gx + gy*gy));
    return { gx/len, gy/len };
}

// Rounded-rect coverage with 2x2 SSAA and subtle feathering (no visible border)
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
    return std::clamp(base * (1.0f + (kEdgeFeatherPx * 0.02f)), 0.0f, 1.0f);
}

SDL_Texture* to_texture(SDL_Renderer* r, SDL_Surface* s) {
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

// Clamp coordinates into the capture rect
inline void clamp_sample(int& sx, int& sy, int w, int h) {
    if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
    if (sy < 0) sy = 0; else if (sy >= h) sy = h - 1;
}

} // namespace

// ----------------------------- Public API -----------------------------
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
    // Fallback decoration (not used for your main/pause menus)
    draw_deco(renderer, rect_, hovered_);
    const SDL_Color chosen = hovered_ ? style_->text_hover : style_->text_normal;
    // basic text render via style (kept for compatibility)
    TTF_Font* f = style_->label.open_font();
    if (f) {
        int tw=0, th=0; TTF_SizeText(f, label_.c_str(), &tw, &th);
        SDL_Surface* s = TTF_RenderText_Blended(f, label_.c_str(), chosen);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_Rect dst{ rect_.x + (rect_.w - tw)/2, rect_.y + (rect_.h - th)/2, tw, th };
            SDL_RenderCopy(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_FreeSurface(s);
        }
        TTF_CloseFont(f);
    }
}

bool Button::is_hovered() const { return hovered_; }
bool Button::is_pressed() const { return pressed_; }
int  Button::width()  { return 520; }
int  Button::height() { return 64; }

void Button::draw_deco(SDL_Renderer* r, const SDL_Rect& b, bool hovered) const {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 20,20,20, hovered ? 120 : 96);
    SDL_RenderFillRect(r, &b);
    SDL_SetRenderDrawColor(r, 255,255,255, 36);
    SDL_RenderDrawRect(r, &b);
}

const GlassButtonStyle& Button::default_glass_style() {
    static const GlassButtonStyle kDefault{};
    return kDefault;
}

void Button::enable_glass_style(bool enabled) { glass_enabled_ = enabled; }
void Button::set_glass_style(const GlassButtonStyle& style) { glass_style_ = style; }

// ----------------------------- Hammered Glass -----------------------------
void Button::draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    SDL_Rect r = adjusted_for_state(rect, hovered_, pressed_);

    // 1) Capture the already-rendered background behind the button.
    SDL_Rect cap{ r.x - kCaptureBleed, r.y - kCaptureBleed, r.w + kCaptureBleed*2, r.h + kCaptureBleed*2 };
    cap = clamp_to_view(renderer, cap);

    SurfacePtr bg = capture(renderer, cap);
    if (!bg) return; // nothing to distort

    // 2) Allocate a composite surface for the button area.
    SurfacePtr comp(SDL_CreateRGBSurfaceWithFormat(0, r.w, r.h, 32, SDL_PIXELFORMAT_RGBA32));
    if (!comp) return;

    SDL_LockSurface(comp.get());
    Uint32* dst = static_cast<Uint32*>(comp->pixels);
    SDL_PixelFormat* fmt = comp->format;
    const int dpitch = comp->pitch / 4;

    SDL_LockSurface(bg.get());
    Uint32* src = static_cast<Uint32*>(bg->pixels);
    const int spitch = bg->pitch / 4;

    // Geometry
    const int  w = r.w, h = r.h;
    const int  ox = r.x - cap.x;
    const int  oy = r.y - cap.y;
    const float cx = (w - 1) * 0.5f;
    const float cy = (h - 1) * 0.5f;
    const float inv_cx = (cx > 0) ? 1.0f / cx : 0.0f;
    const float inv_cy = (cy > 0) ? 1.0f / cy : 0.0f;

    // State gains
    const float ref_base = glass_style_.refraction_strength
                         * (hovered_ ? 1.18f : 1.0f)
                         * (pressed_ ? 0.90f : 1.0f);
    const float chroma   = glass_style_.chroma_strength * (pressed_ ? 0.85f : 1.0f);

    const float mix_state = pressed_ ? glass_style_.mix_pressed
                           : (hovered_ ? glass_style_.mix_hover : glass_style_.mix_normal);

    // Diffusion sampling kernel (ring)
    const int taps = std::max(3, glass_style_.diffusion_taps);
    std::vector<std::array<float,2>> kernel;
    kernel.reserve(taps);
    for (int i=0; i<taps; ++i) {
        float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(taps);
        float ang = t * 6.2831853f; // 2*pi
        kernel.push_back({ std::cos(ang), std::sin(ang) });
    }

    double Lacc = 0.0; int Lcount = 0;

    // 3) Pixel loop
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float cov = rr_coverage_px(x, y, w, h, glass_style_.radius);
            if (cov <= 0.001f) { dst[y*dpitch + x] = 0; continue; }

            // Normalized center-relative coords
            const float ndx = (x - cx) * inv_cx;
            const float ndy = (y - cy) * inv_cy;
            const float r1  = std::sqrt(std::min(1.0f, ndx*ndx + ndy*ndy));

            // Convex lens warp (strongest near center)
            const float lens = std::max(0.0f, 1.0f - r1*r1);
            const float warp = ref_base * std::min(w, h) * 0.95f * lens;

            // Base refracted UV
            const float wx = ndx * warp + ndy * 0.06f * warp;
            const float wy = ndy * warp - ndx * 0.06f * warp;

            // Position in capture space for "original" sample
            int sx_o = ox + x;
            int sy_o = oy + y;
            clamp_sample(sx_o, sy_o, cap.w, cap.h);

            // Procedural hammered normals: FBM gradient
            const float ns = glass_style_.rough_scale * 100.0f; // scale to pleasant range
            auto g = fbm_grad((r.x + x) * ns, (r.y + y) * ns);   // stable in screen space
            const float rough_px = glass_style_.rough_ampl_px * (hovered_ ? 1.1f : (pressed_ ? 0.85f : 1.0f));

            // Chromatic offsets (radial direction)
            const float ax = ndx * chroma;
            const float ay = ndy * chroma;

            // Multi-tap diffusion around the refracted UV + hammered offset
            const float rad = glass_style_.diffusion_radius * (hovered_ ? 1.15f : (pressed_ ? 0.9f : 1.0f));

            // Accumulate
            int acc_r=0, acc_g=0, acc_b=0;

            // include center sample with hammered offset (weight 2)
            {
                int sx = static_cast<int>(std::round(ox + x + wx + g[0] * rough_px));
                int sy = static_cast<int>(std::round(oy + y + wy + g[1] * rough_px));
                clamp_sample(sx, sy, cap.w, cap.h);
                SDL_Color c = unpack(src[sy * spitch + sx]);
                acc_r += c.r * 2; acc_g += c.g * 2; acc_b += c.b * 2;
            }

            for (const auto& v : kernel) {
                // jitter orientation by the hammered normal (feels like facets)
                float jx = v[0] + g[0] * 0.5f;
                float jy = v[1] + g[1] * 0.5f;
                float m = rad;

                // Refracted UV with diffusion + hammered offset
                int sx = static_cast<int>(std::round(ox + x + wx + g[0]*rough_px + jx * m));
                int sy = static_cast<int>(std::round(oy + y + wy + g[1]*rough_px + jy * m));
                clamp_sample(sx, sy, cap.w, cap.h);

                // Chromatic micro-aberration per tap
                int sxr = static_cast<int>(std::round(sx + ax));
                int syr = static_cast<int>(std::round(sy + ay));
                int sxb = static_cast<int>(std::round(sx - ax));
                int syb = static_cast<int>(std::round(sy - ay));
                clamp_sample(sxr, syr, cap.w, cap.h);
                clamp_sample(sxb, syb, cap.w, cap.h);

                SDL_Color cg = unpack(src[sy * spitch + sx]);
                SDL_Color cr = unpack(src[syr * spitch + sxr]);
                SDL_Color cb = unpack(src[syb * spitch + sxb]);

                // blend R/B channels to give prismatic feel
                acc_r += (cg.r + cr.r) / 2;
                acc_g += cg.g;
                acc_b += (cg.b + cb.b) / 2;
            }

            const int weight = 2 + static_cast<int>(kernel.size());
            SDL_Color refr{};
            refr.r = clamp8(acc_r / weight);
            refr.g = clamp8(acc_g / weight);
            refr.b = clamp8(acc_b / weight);

            // "Original" undistorted background
            SDL_Color orig = unpack(src[sy_o * spitch + sx_o]);

            // Fresnel-like rim increases the distortion mix near edges (no outline drawn)
            const float fres = std::pow(std::clamp(r1, 0.0f, 1.0f), glass_style_.fresnel_power)
                             * glass_style_.fresnel_intensity;

            float mix_w = std::clamp(mix_state + fres, 0.0f, 1.0f);

            // Final color = lerp(original, refracted-diffused, mix)
            SDL_Color out{};
            out.r = clamp8(static_cast<int>(orig.r * (1.0f - mix_w) + refr.r * mix_w));
            out.g = clamp8(static_cast<int>(orig.g * (1.0f - mix_w) + refr.g * mix_w));
            out.b = clamp8(static_cast<int>(orig.b * (1.0f - mix_w) + refr.b * mix_w));

            // Specular-style glare sweeps from upper-left to lower-right to sell glass sheen.
            const float highlight_axis = std::clamp(1.0f - std::abs(ndx * 0.75f + ndy * 1.35f - 0.10f), 0.0f, 1.0f);
            float highlight_curve = std::pow(highlight_axis, 3.2f);

            float hotspot = 1.0f - std::min(1.0f, (std::pow(ndx + 0.2f, 2.0f) * 3.6f + std::pow(ndy - 0.45f, 2.0f) * 8.0f));
            hotspot = std::pow(std::max(0.0f, hotspot), 2.2f);

            const float base_glare = pressed_ ? 0.08f : (hovered_ ? 0.18f : 0.12f);
            float glare_mix = base_glare + highlight_curve * (hovered_ ? 0.42f : 0.32f) + hotspot * 0.55f;
            glare_mix = std::clamp(glare_mix, 0.0f, 0.85f);

            float glow_mix = std::clamp(highlight_curve * 0.5f + hotspot * 0.7f, 0.0f, 1.0f);
            glow_mix *= pressed_ ? 0.20f : (hovered_ ? 0.45f : 0.35f);

            const SDL_Color hi_col   = glass_style_.highlight_color;
            const SDL_Color glow_col = glass_style_.highlight_glow_color;

            out.r = lerp8(out.r, hi_col.r, glare_mix);
            out.g = lerp8(out.g, hi_col.g, glare_mix);
            out.b = lerp8(out.b, hi_col.b, glare_mix);

            if (glow_mix > 0.0f) {
                out.r = lerp8(out.r, glow_col.r, glow_mix);
                out.g = lerp8(out.g, glow_col.g, glow_mix);
                out.b = lerp8(out.b, glow_col.b, glow_mix);
            }

            const float lift = pressed_ ? 0.04f : (hovered_ ? 0.10f : 0.07f);
            out.r = lerp8(out.r, 255, lift);
            out.g = lerp8(out.g, 255, lift);
            out.b = lerp8(out.b, 255, lift);

            const float transparency = pressed_ ? 0.70f : (hovered_ ? 0.74f : 0.68f);
            out.a = clamp8(static_cast<int>(std::round(cov * 255.0f * transparency)));

            Lacc += luminance(out);
            ++Lcount;

            dst[y * dpitch + x] = pack(fmt, out);
        }
    }

    SDL_UnlockSurface(bg.get());
    SDL_UnlockSurface(comp.get());

    if (Lcount > 0) { glass_luminance_ = static_cast<float>(Lacc / Lcount); glass_has_luminance_ = true; }
    else            { glass_has_luminance_ = false; }

    SDL_Texture* tex = to_texture(renderer, comp.get());
    if (!tex) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, tex, nullptr, &r);
    SDL_DestroyTexture(tex);
}

// ----------------------------- Text -----------------------------
void Button::draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const {
    if (label_.empty()) return;
    TTF_Font* font = style_->label.open_font();
    if (!font) return;

    SDL_Rect rr = adjusted_for_state(rect, hovered_, pressed_);
    int tw=0, th=0;
    TTF_SizeText(font, label_.c_str(), &tw, &th);
    const int x = rr.x + (rr.w - tw)/2;
    const int y = rr.y + (rr.h - th)/2;

    SDL_Color text = glass_style_.text_color;
    SDL_Color stroke = glass_style_.text_stroke;

    if (hovered_ && !pressed_) {
        text.r = clamp8(text.r + 8); text.g = clamp8(text.g + 8); text.b = clamp8(text.b + 8);
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
