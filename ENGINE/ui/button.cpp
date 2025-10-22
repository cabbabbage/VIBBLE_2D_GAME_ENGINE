#include "button.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr int kGlassBleed = 8;

struct SurfaceDeleter {
    void operator()(SDL_Surface* surf) const {
        if (surf) SDL_FreeSurface(surf);
    }
};

using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

inline Uint8 clamp_channel(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<Uint8>(value);
}

inline SDL_Color unpack_color(Uint32 pixel) {
    SDL_Color c;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    c.a = pixel & 0xFF;
    c.b = (pixel >> 8) & 0xFF;
    c.g = (pixel >> 16) & 0xFF;
    c.r = (pixel >> 24) & 0xFF;
#else
    c.a = (pixel >> 24) & 0xFF;
    c.r = (pixel >> 16) & 0xFF;
    c.g = (pixel >> 8) & 0xFF;
    c.b = pixel & 0xFF;
#endif
    return c;
}

inline Uint32 pack_color(SDL_PixelFormat* fmt, const SDL_Color& c) {
    return SDL_MapRGBA(fmt, c.r, c.g, c.b, c.a);
}

inline SDL_Rect adjusted_rect_for_state(const SDL_Rect& rect, bool hovered, bool pressed) {
    SDL_Rect adjusted = rect;
    if (pressed) {
        adjusted.y += 1;
    } else if (hovered) {
        adjusted.y -= 1;
    }
    return adjusted;
}

inline SDL_Rect clamp_rect(SDL_Renderer* renderer, SDL_Rect rect) {
    SDL_Rect viewport;
    if (SDL_RenderGetViewport(renderer, &viewport) != 0) {
        viewport = SDL_Rect{0, 0, rect.x + rect.w + kGlassBleed * 2, rect.y + rect.h + kGlassBleed * 2};
    }
    int x1 = std::max(rect.x, viewport.x);
    int y1 = std::max(rect.y, viewport.y);
    int x2 = std::min(rect.x + rect.w, viewport.x + viewport.w);
    int y2 = std::min(rect.y + rect.h, viewport.y + viewport.h);
    SDL_Rect clamped{ x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1) };
    return clamped;
}

SurfacePtr capture_region(SDL_Renderer* renderer, const SDL_Rect& rect) {
    if (rect.w <= 0 || rect.h <= 0) {
        return SurfacePtr(nullptr);
    }
    SurfacePtr surface(SDL_CreateRGBSurfaceWithFormat(0, rect.w, rect.h, 32, SDL_PIXELFORMAT_RGBA32));
    if (!surface) {
        return SurfacePtr(nullptr);
    }
    if (SDL_RenderReadPixels(renderer,
                             &rect,
                             surface->format->format,
                             surface->pixels,
                             surface->pitch) != 0) {
        return SurfacePtr(nullptr);
    }
    return surface;
}

void box_blur(SDL_Surface* surface, int radius) {
    if (!surface || radius <= 0) return;
    SDL_LockSurface(surface);
    const int w = surface->w;
    const int h = surface->h;
    const int pitch = surface->pitch / 4;
    std::vector<Uint32> temp(w * h, 0);
    Uint32* pixels = static_cast<Uint32*>(surface->pixels);

    const int window = radius * 2 + 1;

    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        int sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
        for (int x = -radius; x <= radius; ++x) {
            int sx = std::clamp(x, 0, w - 1);
            SDL_Color c = unpack_color(pixels[y * pitch + sx]);
            sum_r += c.r; sum_g += c.g; sum_b += c.b; sum_a += c.a;
        }
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            SDL_Color averaged{};
            averaged.r = clamp_channel(sum_r / window);
            averaged.g = clamp_channel(sum_g / window);
            averaged.b = clamp_channel(sum_b / window);
            averaged.a = clamp_channel(sum_a / window);
            temp[idx] = pack_color(surface->format, averaged);

            int remove_x = std::clamp(x - radius, 0, w - 1);
            int add_x = std::clamp(x + radius + 1, 0, w - 1);
            SDL_Color remove = unpack_color(pixels[y * pitch + remove_x]);
            SDL_Color add    = unpack_color(pixels[y * pitch + add_x]);
            sum_r += add.r - remove.r;
            sum_g += add.g - remove.g;
            sum_b += add.b - remove.b;
            sum_a += add.a - remove.a;
        }
    }

    // Vertical pass
    for (int x = 0; x < w; ++x) {
        int sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
        for (int y = -radius; y <= radius; ++y) {
            int sy = std::clamp(y, 0, h - 1);
            SDL_Color c = unpack_color(temp[sy * w + x]);
            sum_r += c.r; sum_g += c.g; sum_b += c.b; sum_a += c.a;
        }
        for (int y = 0; y < h; ++y) {
            SDL_Color averaged{};
            averaged.r = clamp_channel(sum_r / window);
            averaged.g = clamp_channel(sum_g / window);
            averaged.b = clamp_channel(sum_b / window);
            averaged.a = clamp_channel(sum_a / window);
            pixels[y * pitch + x] = pack_color(surface->format, averaged);

            int remove_y = std::clamp(y - radius, 0, h - 1);
            int add_y    = std::clamp(y + radius + 1, 0, h - 1);
            SDL_Color remove = unpack_color(temp[remove_y * w + x]);
            SDL_Color add    = unpack_color(temp[add_y * w + x]);
            sum_r += add.r - remove.r;
            sum_g += add.g - remove.g;
            sum_b += add.b - remove.b;
            sum_a += add.a - remove.a;
        }
    }
    SDL_UnlockSurface(surface);
}

void apply_kawase(SDL_Surface* surface, int blur_px) {
    if (!surface) return;
    const int radius_a = std::max(1, blur_px / 4);
    const int radius_b = std::max(1, blur_px / 2);
    box_blur(surface, radius_a);
    box_blur(surface, radius_b);
}

bool inside_corner(int x, int y, int w, int h, int radius) {
    if (radius <= 0) return true;
    const int rx = (x < radius) ? radius - x - 1 : (x >= w - radius ? x - (w - radius) : -1);
    const int ry = (y < radius) ? radius - y - 1 : (y >= h - radius ? y - (h - radius) : -1);
    if (rx < 0 && ry < 0) return true;
    if (rx < 0 || ry < 0) return true;
    return (rx * rx + ry * ry) <= radius * radius;
}

float hash_noise(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return static_cast<float>(h & 0xFFu) / 255.0f;
}

void apply_gradient(SDL_Surface* surface, const SDL_Color& top, const SDL_Color& bottom, int radius) {
    if (!surface) return;
    SDL_LockSurface(surface);
    const int w = surface->w;
    const int h = surface->h;
    SDL_PixelFormat* fmt = surface->format;
    Uint32* pixels = static_cast<Uint32*>(surface->pixels);
    const int pitch = surface->pitch / 4;
    for (int y = 0; y < h; ++y) {
        float t = (h > 1) ? static_cast<float>(y) / static_cast<float>(h - 1) : 0.0f;
        SDL_Color line{};
        line.r = clamp_channel(static_cast<int>(top.r + (bottom.r - top.r) * t));
        line.g = clamp_channel(static_cast<int>(top.g + (bottom.g - top.g) * t));
        line.b = clamp_channel(static_cast<int>(top.b + (bottom.b - top.b) * t));
        line.a = clamp_channel(static_cast<int>(top.a + (bottom.a - top.a) * t));
        for (int x = 0; x < w; ++x) {
            if (!inside_corner(x, y, w, h, radius)) {
                pixels[y * pitch + x] = pack_color(fmt, SDL_Color{0,0,0,0});
                continue;
            }
            SDL_Color current = unpack_color(pixels[y * pitch + x]);
            float alpha = line.a / 255.0f;
            SDL_Color blended;
            blended.r = clamp_channel(static_cast<int>(current.r * (1.0f - alpha) + line.r * alpha));
            blended.g = clamp_channel(static_cast<int>(current.g * (1.0f - alpha) + line.g * alpha));
            blended.b = clamp_channel(static_cast<int>(current.b * (1.0f - alpha) + line.b * alpha));
            blended.a = std::max(current.a, line.a);
            pixels[y * pitch + x] = pack_color(fmt, blended);
        }
    }
    SDL_UnlockSurface(surface);
}

void blend_border(SDL_Surface* surface, const SDL_Color& color, bool top, bool left, bool bottom, bool right) {
    if (!surface) return;
    SDL_LockSurface(surface);
    const int w = surface->w;
    const int h = surface->h;
    SDL_PixelFormat* fmt = surface->format;
    Uint32* pixels = static_cast<Uint32*>(surface->pixels);
    const int pitch = surface->pitch / 4;
    auto blend_pixel = [&](int px, int py) {
        if (px < 0 || px >= w || py < 0 || py >= h) return;
        SDL_Color src = unpack_color(pixels[py * pitch + px]);
        float a = color.a / 255.0f;
        SDL_Color out;
        out.r = clamp_channel(static_cast<int>(src.r * (1.0f - a) + color.r * a));
        out.g = clamp_channel(static_cast<int>(src.g * (1.0f - a) + color.g * a));
        out.b = clamp_channel(static_cast<int>(src.b * (1.0f - a) + color.b * a));
        out.a = std::max(src.a, color.a);
        pixels[py * pitch + px] = pack_color(fmt, out);
    };
    if (top)    for (int x = 0; x < w; ++x) blend_pixel(x, 0);
    if (bottom) for (int x = 0; x < w; ++x) blend_pixel(x, h - 1);
    if (left)   for (int y = 0; y < h; ++y) blend_pixel(0, y);
    if (right)  for (int y = 0; y < h; ++y) blend_pixel(w - 1, y);
    SDL_UnlockSurface(surface);
}

SDL_Texture* surface_to_texture(SDL_Renderer* renderer, SDL_Surface* surface) {
    if (!surface) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

} // namespace

bool Button::glass_blur_enabled_ = true;

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
        const SDL_Color text_col = (override_col.a ? override_col : style.color);
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
                                        SDL_SetTextureAlphaMod(tex_shadow, 130);
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

Button Button::get_main_button(const std::string& text) {
	return Button(text, &Styles::MainDecoButton(), width(), height());
}

Button Button::get_exit_button(const std::string& text) {
	return Button(text, &Styles::ExitDecoButton(), width(), height());
}

Button::Button() = default;

Button::Button(const std::string& text, const ButtonStyle* style, int w, int h)
: rect_{0,0,w,h}, label_(text), style_(style), glass_style_(default_glass_style()) {}

void Button::set_position(SDL_Point p) {
        rect_.x = p.x; rect_.y = p.y;
}

void Button::set_rect(const SDL_Rect& r) {
	rect_ = r;
}

const SDL_Rect& Button::rect() const {
	return rect_;
}

void Button::set_text(const std::string& text) {
	label_ = text;
}

const std::string& Button::text() const {
	return label_;
}

bool Button::handle_event(const SDL_Event& e) {
	bool clicked = false;
	if (e.type == SDL_MOUSEMOTION) {
		SDL_Point p{ e.motion.x, e.motion.y };
		hovered_ = SDL_PointInRect(&p, &rect_);
	}
	else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
		SDL_Point p{ e.button.x, e.button.y };
		if (SDL_PointInRect(&p, &rect_)) {
			pressed_ = true;
		}
	}
	else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
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
        draw_deco(renderer, rect_, hovered_);
        const SDL_Color text_normal = style_->text_normal;
        const SDL_Color text_hover  = style_->text_hover;
        const SDL_Color chosen = hovered_ ? text_hover : text_normal;
        blit_text_center(renderer, style_->label, label_, rect_, true, chosen);
}

bool Button::is_hovered() const { return hovered_; }
bool Button::is_pressed() const { return pressed_; }

int Button::width()  { return 520; }
int Button::height() { return 64; }

void Button::draw_deco(SDL_Renderer* r, const SDL_Rect& b, bool hovered) const {
        if (!style_) return;
        const SDL_Color fill_base   = style_->fill_base;
        SDL_Color fill_top          = style_->fill_top;
        const SDL_Color outline_on  = style_->outline;
	const SDL_Color outline_off = style_->outline_dim;
	const SDL_Color accent      = style_->accent;
	const SDL_Color glow        = style_->glow;
	SDL_SetRenderDrawColor(r, fill_base.r, fill_base.g, fill_base.b, fill_base.a);
	SDL_RenderFillRect(r, &b);
	SDL_Rect topHalf{ b.x, b.y, b.w, b.h/2 };
	SDL_SetRenderDrawColor(r, fill_top.r, fill_top.g, fill_top.b, fill_top.a ? fill_top.a : 200);
	SDL_RenderFillRect(r, &topHalf);
	const SDL_Color frame = hovered ? outline_on : outline_off;
	SDL_SetRenderDrawColor(r, frame.r, frame.g, frame.b, 255);
	SDL_Rect outer{ b.x, b.y, b.w, b.h };
	SDL_RenderDrawRect(r, &outer);
	SDL_Rect inner{ b.x+1, b.y+1, b.w-2, b.h-2 };
	SDL_RenderDrawRect(r, &inner);
	SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 255);
	SDL_RenderDrawLine(r, b.x + 10, b.y + 10,     b.x + b.w - 10, b.y + 10);
	SDL_RenderDrawLine(r, b.x + 10, b.y + b.h-11, b.x + b.w - 10, b.y + b.h - 11);
	for (int i = 0; i < 3; ++i) {
		SDL_RenderDrawLine(r, b.x + 10 + i,       b.y + 10,        b.x + 10 + i,       b.y + 20);
		SDL_RenderDrawLine(r, b.x + b.w - 11 - i, b.y + 10,        b.x + b.w - 11 - i, b.y + 20);
		SDL_RenderDrawLine(r, b.x + 10 + i,       b.y + b.h - 21,  b.x + 10 + i,       b.y + b.h - 11);
		SDL_RenderDrawLine(r, b.x + b.w - 11 - i, b.y + b.h - 21,  b.x + b.w - 11 - i, b.y + b.h - 11);
	}
	if (hovered) {
		SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
		SDL_SetRenderDrawColor(r, glow.r, glow.g, glow.b, glow.a ? glow.a : 45);
		SDL_Rect g{ b.x - 6, b.y - 6, b.w + 12, b.h + 12 };
		SDL_RenderFillRect(r, &g);
		SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        }
}

void Button::set_glass_blur_enabled(bool enabled) {
        glass_blur_enabled_ = enabled;
}

const GlassButtonStyle& Button::default_glass_style() {
        static const GlassButtonStyle kDefault{
                SDL_Color{18, 26, 39, static_cast<Uint8>(0.28f * 255)},
                SDL_Color{18, 26, 39, static_cast<Uint8>(0.34f * 255)},
                SDL_Color{18, 26, 39, static_cast<Uint8>(0.40f * 255)},
                0.06f,
                0.10f,
                12,
                SDL_Color{255, 255, 255, static_cast<Uint8>(0.20f * 255)},
                SDL_Color{0, 0, 0, static_cast<Uint8>(0.35f * 255)},
                SDL_Color{0, 0, 0, static_cast<Uint8>(0.25f * 255)},
                SDL_Color{0, 0, 0, static_cast<Uint8>(0.35f * 255)},
                8,
                10,
                6,
                SDL_Color{0, 0, 0, static_cast<Uint8>(0.70f * 255)},
                SDL_Color{255, 255, 255, static_cast<Uint8>(0.12f * 255)},
                SDL_Color{198, 208, 224, static_cast<Uint8>(0.60f * 255)},
                SDL_Color{212, 175, 55, static_cast<Uint8>(0.30f * 255)},
                SDL_Color{0, 0, 0, static_cast<Uint8>(0.55f * 255)}
        };
        return kDefault;
}

void Button::enable_glass_style(bool enabled) {
        glass_enabled_ = enabled;
}

void Button::set_glass_style(const GlassButtonStyle& style) {
        glass_style_ = style;
}

void Button::draw_glass(SDL_Renderer* renderer, const SDL_Rect& rect) const {
        SDL_Rect glass_rect = adjusted_rect_for_state(rect, hovered_, pressed_);
        SDL_Rect capture_rect{ glass_rect.x - kGlassBleed, glass_rect.y - kGlassBleed, glass_rect.w + kGlassBleed * 2, glass_rect.h + kGlassBleed * 2 };
        capture_rect = clamp_rect(renderer, capture_rect);

        SurfacePtr background(nullptr);
        if (glass_blur_enabled_) {
                background = capture_region(renderer, capture_rect);
                if (background) {
                        apply_kawase(background.get(), pressed_ ? glass_style_.blur_px_pressed : (hovered_ ? glass_style_.blur_px_hover : glass_style_.blur_px));
                }
        }

        SurfacePtr composite(SDL_CreateRGBSurfaceWithFormat(0, glass_rect.w, glass_rect.h, 32, SDL_PIXELFORMAT_RGBA32));
        if (!composite) return;

        SDL_LockSurface(composite.get());
        SDL_PixelFormat* fmt = composite->format;
        Uint32* pixels = static_cast<Uint32*>(composite->pixels);
        const int pitch = composite->pitch / 4;
        const int w = glass_rect.w;
        const int h = glass_rect.h;

        SDL_Color tint = hovered_ ? (pressed_ ? glass_style_.tint_pressed : glass_style_.tint_hover) : glass_style_.tint;
        if (pressed_) tint = glass_style_.tint_pressed;

        int offset_x = glass_rect.x - capture_rect.x;
        int offset_y = glass_rect.y - capture_rect.y;
        Uint32* src_pixels = background ? static_cast<Uint32*>(background->pixels) : nullptr;
        int src_pitch = background ? background->pitch / 4 : 0;

        double luminance_accumulator = 0.0;
        int luminance_samples = 0;
        for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                        if (!inside_corner(x, y, w, h, glass_style_.radius)) {
                                pixels[y * pitch + x] = 0;
                                continue;
                        }
                        SDL_Color base{};
                        if (src_pixels) {
                                int sx = std::clamp(x + offset_x, 0, capture_rect.w - 1);
                                int sy = std::clamp(y + offset_y, 0, capture_rect.h - 1);
                                base = unpack_color(src_pixels[sy * src_pitch + sx]);
                        } else {
                                base = SDL_Color{255,255,255,255};
                        }

                        float tint_alpha = tint.a / 255.0f;
                        SDL_Color tinted{};
                        tinted.r = clamp_channel(static_cast<int>(base.r * (1.0f - tint_alpha) + tint.r * tint_alpha));
                        tinted.g = clamp_channel(static_cast<int>(base.g * (1.0f - tint_alpha) + tint.g * tint_alpha));
                        tinted.b = clamp_channel(static_cast<int>(base.b * (1.0f - tint_alpha) + tint.b * tint_alpha));
                        tinted.a = clamp_channel(220);

                        float noise_value = hash_noise(glass_rect.x + x, glass_rect.y + y) - 0.5f;
                        float noise_alpha = std::clamp(glass_style_.noise_opacity, 0.0f, 1.0f);
                        tinted.r = clamp_channel(static_cast<int>(tinted.r + noise_value * 255.0f * noise_alpha));
                        tinted.g = clamp_channel(static_cast<int>(tinted.g + noise_value * 255.0f * noise_alpha));
                        tinted.b = clamp_channel(static_cast<int>(tinted.b + noise_value * 255.0f * noise_alpha));

                        const int inset = 4;
                        int dist_edge = std::min({x, y, w - 1 - x, h - 1 - y});
                        float smudge_factor = 0.0f;
                        if (dist_edge < inset) {
                                smudge_factor = (1.0f - static_cast<float>(dist_edge) / static_cast<float>(inset));
                                smudge_factor = std::clamp(smudge_factor, 0.0f, 1.0f);
                        }
                        float smudge_alpha = glass_style_.smudge_opacity * smudge_factor;
                        if (smudge_alpha > 0.0f) {
                                tinted.r = clamp_channel(static_cast<int>(tinted.r * (1.0f - smudge_alpha)));
                                tinted.g = clamp_channel(static_cast<int>(tinted.g * (1.0f - smudge_alpha)));
                                tinted.b = clamp_channel(static_cast<int>(tinted.b * (1.0f - smudge_alpha)));
                        }

                        float luminance = (0.2126f * tinted.r + 0.7152f * tinted.g + 0.0722f * tinted.b) / 255.0f * 100.0f;
                        luminance_accumulator += luminance;
                        ++luminance_samples;

                        pixels[y * pitch + x] = pack_color(fmt, tinted);
                }
        }
        SDL_UnlockSurface(composite.get());

        if (luminance_samples > 0) {
                glass_luminance_ = static_cast<float>(luminance_accumulator / luminance_samples);
                glass_has_luminance_ = true;
        } else {
                glass_has_luminance_ = false;
        }

        if (!background) {
                SDL_Color grad_top{255,255,255, static_cast<Uint8>(0.10f * 255)};
                SDL_Color grad_bottom{0,0,0, static_cast<Uint8>(0.06f * 255)};
                apply_gradient(composite.get(), grad_top, grad_bottom, glass_style_.radius);
        }

        blend_border(composite.get(), glass_style_.border_light, true, true, false, false);
        blend_border(composite.get(), glass_style_.border_dark, false, false, true, true);

        SDL_Texture* tex = surface_to_texture(renderer, composite.get());
        if (!tex) return;

        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        const SDL_Color& outer = glass_style_.outer_shadow;
        Uint8 shadow_alpha = static_cast<Uint8>((pressed_ ? 0.15f : 0.30f) * outer.a);
        if (shadow_alpha) {
                SDL_SetRenderDrawColor(renderer, outer.r, outer.g, outer.b, shadow_alpha);
                SDL_Rect shadow_rect{ glass_rect.x, glass_rect.y + 2, glass_rect.w, glass_rect.h };
                SDL_RenderFillRect(renderer, &shadow_rect);
        }

        const SDL_Color& inner = glass_style_.inner_shadow;
        SDL_SetRenderDrawColor(renderer, inner.r, inner.g, inner.b, inner.a);
        SDL_Rect inner_rect{ glass_rect.x, glass_rect.y, glass_rect.w, glass_rect.h };
        SDL_RenderDrawRect(renderer, &inner_rect);

        SDL_RenderCopy(renderer, tex, nullptr, &glass_rect);
        SDL_DestroyTexture(tex);
}

void Button::draw_glass_text(SDL_Renderer* renderer, const SDL_Rect& rect) const {
        if (label_.empty()) return;
        TTF_Font* font = style_->label.open_font();
        if (!font) return;

        SDL_Rect glass_rect = adjusted_rect_for_state(rect, hovered_, pressed_);

        int tw = 0, th = 0;
        TTF_SizeText(font, label_.c_str(), &tw, &th);
        const int x = glass_rect.x + (glass_rect.w - tw) / 2;
        const int y = glass_rect.y + (glass_rect.h - th) / 2;

        SDL_Color text_color = glass_style_.text_color;
        SDL_Color stroke_color = glass_style_.text_stroke;

        if (glass_has_luminance_ && glass_luminance_ < 20.0f) {
                text_color = SDL_Color{20, 20, 20, static_cast<Uint8>(0.85f * 255)};
        } else if (glass_has_luminance_) {
                Uint8 min_alpha = static_cast<Uint8>(0.70f * 255);
                if (text_color.a < min_alpha) text_color.a = min_alpha;
        }

        SDL_Surface* text_surface = TTF_RenderText_Blended(font, label_.c_str(), text_color);
        SDL_Surface* stroke_surface = TTF_RenderText_Blended(font, label_.c_str(), stroke_color);
        if (!text_surface || !stroke_surface) {
                if (text_surface) SDL_FreeSurface(text_surface);
                if (stroke_surface) SDL_FreeSurface(stroke_surface);
                TTF_CloseFont(font);
                return;
        }

        SDL_Texture* text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
        SDL_Texture* stroke_texture = SDL_CreateTextureFromSurface(renderer, stroke_surface);

        if (stroke_texture) {
                SDL_SetTextureBlendMode(stroke_texture, SDL_BLENDMODE_BLEND);
                static const std::array<SDL_Point, 8> offsets{ {
                        { -1, -1 }, { 0, -1 }, { 1, -1 },
                        { -1,  0 },             { 1,  0 },
                        { -1,  1 }, { 0,  1 }, { 1,  1 }
                } };
                for (const auto& off : offsets) {
                        SDL_Rect dst{ x + off.x, y + off.y, stroke_surface->w, stroke_surface->h };
                        SDL_RenderCopy(renderer, stroke_texture, nullptr, &dst);
                }
        }

        if (text_texture) {
                SDL_SetTextureBlendMode(text_texture, SDL_BLENDMODE_BLEND);
                SDL_Rect dst{ x, y, text_surface->w, text_surface->h };
                SDL_RenderCopy(renderer, text_texture, nullptr, &dst);
        }

        if (text_texture) SDL_DestroyTexture(text_texture);
        if (stroke_texture) SDL_DestroyTexture(stroke_texture);
        SDL_FreeSurface(text_surface);
        SDL_FreeSurface(stroke_surface);
        TTF_CloseFont(font);
}
