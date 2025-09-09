#include "button.hpp"
#include <algorithm>
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
: rect_{0,0,w,h}, label_(text), style_(style) {}

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
