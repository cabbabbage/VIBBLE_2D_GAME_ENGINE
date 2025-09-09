#include "slider.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include "utils/text_style.hpp"
#include "ui/styles.hpp"

Slider::Slider(const std::string& label, int min_val, int max_val)
: label_(label), min_(std::min(min_val, max_val)), max_(std::max(min_val, max_val))
{
	value_ = min_;
}

Slider::Slider(const std::string& label, int min_val, int max_val, int current_val)
: label_(label), min_(std::min(min_val, max_val)), max_(std::max(min_val, max_val))
{
        value_ = std::max(min_, std::min(max_, current_val));
}

void Slider::set_position(SDL_Point p) {
        rect_.x = p.x; rect_.y = p.y;
}

void Slider::set_rect(const SDL_Rect& r) { rect_ = r; }
const SDL_Rect& Slider::rect() const { return rect_; }

void Slider::set_label(const std::string& text) { label_ = text; }
const std::string& Slider::label() const { return label_; }

void Slider::set_range(int min_val, int max_val) {
	min_ = std::min(min_val, max_val);
	max_ = std::max(min_val, max_val);
	value_ = std::max(min_, std::min(max_, value_));
}

int Slider::min() const { return min_; }
int Slider::max() const { return max_; }

void Slider::set_value(int v) {
	value_ = std::max(min_, std::min(max_, v));
}

int Slider::value() const { return value_; }

int Slider::width()  { return 520; }
int Slider::height() { return 64;  }

SDL_Rect Slider::track_rect() const {
	const int pad = 14;
	const int track_h = 6;
	const int cy = rect_.y + rect_.h/2;
	SDL_Rect t{ rect_.x + pad, cy - track_h/2, rect_.w - 2*pad, track_h };
	if (t.w < 10) t.w = 10;
	return t;
}

SDL_Rect Slider::knob_rect_for_value(int v) const {
	const SDL_Rect tr = track_rect();
	const int knob_w = 12;
	const int knob_h = 24;
	const int range = std::max(1, max_ - min_);
	const float t = float(v - min_) / float(range);
	const int x = tr.x + int(std::round(t * (tr.w))) - knob_w/2;
	const int y = tr.y + tr.h/2 - knob_h/2;
	return SDL_Rect{ x, y, knob_w, knob_h };
}

int Slider::value_for_x(int mouse_x) const {
	const SDL_Rect tr = track_rect();
	const int clamped_x = std::max(tr.x, std::min(tr.x + tr.w, mouse_x));
	const int range = std::max(1, max_ - min_);
	const float t = float(clamped_x - tr.x) / float(tr.w);
	const int v = min_ + int(std::round(t * range));
	return std::max(min_, std::min(max_, v));
}

bool Slider::handle_event(const SDL_Event& e) {
	bool changed = false;
	const SDL_Rect krect = knob_rect_for_value(value_);
	if (e.type == SDL_MOUSEMOTION) {
		SDL_Point p{ e.motion.x, e.motion.y };
		knob_hovered_ = SDL_PointInRect(&p, &krect);
		if (dragging_) {
			const int new_val = value_for_x(e.motion.x);
			if (new_val != value_) { value_ = new_val; changed = true; }
		}
	}
	else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
		SDL_Point p{ e.button.x, e.button.y };
		const SDL_Rect tr = track_rect();
		if (SDL_PointInRect(&p, &krect) || SDL_PointInRect(&p, &tr)) {
			dragging_ = true;
			const int new_val = value_for_x(e.button.x);
			if (new_val != value_) { value_ = new_val; changed = true; }
		}
	}
	else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
		if (dragging_) {
			dragging_ = false;
		}
	}
	return changed;
}

static void fill_rect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(r, &rc);
}

static void stroke_rect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
	SDL_RenderDrawRect(r, &rc);
}

static void blit_text(SDL_Renderer* r, const TextStyle& style, const std::string& s, int x, int y) {
	if (s.empty()) return;
	TTF_Font* f = style.open_font();
	if (!f) return;
	SDL_Surface* surf = TTF_RenderText_Blended(f, s.c_str(), style.color);
	if (surf) {
		SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
		if (tex) {
			SDL_Rect dst{ x, y, surf->w, surf->h };
			SDL_RenderCopy(r, tex, nullptr, &dst);
			SDL_DestroyTexture(tex);
		}
		SDL_FreeSurface(surf);
	}
	TTF_CloseFont(f);
}

void Slider::draw_track(SDL_Renderer* r) const {
	const SDL_Rect tr = track_rect();
	SDL_Color trackBg = style_ ? style_->track_bg : Styles::Slate();
	fill_rect(r, tr, trackBg);
	SDL_Rect tr_fill = tr;
	const int range = std::max(1, max_ - min_);
	const float t = float(value_ - min_) / float(range);
	tr_fill.w = std::max(0, int(std::round(t * tr.w)));
	SDL_Color trackFill = style_ ? style_->track_fill : Styles::Teal();
	fill_rect(r, tr_fill, trackFill);
	SDL_Color frame = style_ ? style_->frame_normal : Styles::GoldDim();
	stroke_rect(r, tr, frame);
}

void Slider::draw_knob(SDL_Renderer* r, const SDL_Rect& krect, bool hovered) const {
	SDL_Color knobCol = style_ ? (hovered ? style_->knob_fill_hover : style_->knob_fill) : (hovered ? Styles::Fog() : Styles::Ivory());
	fill_rect(r, krect, knobCol);
	SDL_Color frame = style_ ? (hovered ? style_->knob_frame_hover : style_->knob_frame) : (hovered ? Styles::Gold() : Styles::GoldDim());
	stroke_rect(r, krect, frame);
	SDL_SetRenderDrawColor(r, frame.r, frame.g, frame.b, 180);
	const int gx = krect.x + krect.w/2;
	SDL_RenderDrawLine(r, gx, krect.y + 4, gx, krect.y + krect.h - 4);
}

void Slider::draw_text(SDL_Renderer* r) const {
	const TextStyle& labelStyle = style_ ? style_->label_style : TextStyles::SmallMain();
	const TextStyle& valueStyle = style_ ? style_->value_style : TextStyles::SmallSecondary();
	blit_text(r, labelStyle, label_, rect_.x + 8, rect_.y + 6);
	const int right_pad = 60;
	blit_text(r, valueStyle, std::to_string(value_), rect_.x + rect_.w - right_pad, rect_.y + 6);
}

void Slider::render(SDL_Renderer* renderer) const {
	SDL_Color frame = knob_hovered_ || dragging_ ? (style_ ? style_->frame_hover : Styles::Gold()) : (style_ ? style_->frame_normal : Styles::GoldDim());
	stroke_rect(renderer, rect_, frame);
	SDL_Rect inner{ rect_.x+1, rect_.y+1, rect_.w-2, rect_.h-2 };
	stroke_rect(renderer, inner, frame);
	draw_track(renderer);
	const SDL_Rect krect = knob_rect_for_value(value_);
	draw_knob(renderer, krect, knob_hovered_ || dragging_);
	draw_text(renderer);
}
