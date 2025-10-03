#include "checkbox.hpp"
#include "utils/text_style.hpp"
#include "ui/styles.hpp"
Checkbox::Checkbox(const std::string& label, bool value)
: label_(label), value_(value) {}

void Checkbox::set_position(SDL_Point p) { rect_.x = p.x; rect_.y = p.y; }
void Checkbox::set_rect(const SDL_Rect& r) { rect_ = r; }
const SDL_Rect& Checkbox::rect() const { return rect_; }

void Checkbox::set_label(const std::string& s) { label_ = s; }
const std::string& Checkbox::label() const { return label_; }

void Checkbox::set_value(bool v) { value_ = v; }
bool Checkbox::value() const { return value_; }

bool Checkbox::handle_event(const SDL_Event& e) {
	bool toggled = false;
	if (e.type == SDL_MOUSEMOTION) {
		SDL_Point p{ e.motion.x, e.motion.y };
		hovered_ = SDL_PointInRect(&p, &rect_);
	}
	else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
		SDL_Point p{ e.button.x, e.button.y };
		if (SDL_PointInRect(&p, &rect_)) {
			value_ = !value_;
			toggled = true;
		}
	}
	return toggled;
}

void Checkbox::render(SDL_Renderer* r) const {
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	const TextStyle& ls = TextStyles::SmallMain();
	if (!label_.empty()) {
		TTF_Font* f = ls.open_font();
		if (f) {
			SDL_Surface* surf = TTF_RenderText_Blended(f, label_.c_str(), ls.color);
			if (surf) {
					SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
					if (tex) {
								SDL_Rect dst{ rect_.x, rect_.y, surf->w, surf->h };
								SDL_RenderCopy(r, tex, nullptr, &dst);
								SDL_DestroyTexture(tex);
					}
					SDL_FreeSurface(surf);
			}
			TTF_CloseFont(f);
		}
	}
	const int box_size = rect_.h - 6;
	SDL_Rect box{
		rect_.x + rect_.w - box_size - 4,
		rect_.y + 3,
		box_size,
		box_size
};
	SDL_Color bg = Styles::Slate(); bg.a = 160;
	SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(r, &box);
	SDL_Color border_on  = Styles::Gold();
	SDL_Color border_off = Styles::GoldDim();
	SDL_Color frame = hovered_ ? border_on : border_off;
	SDL_SetRenderDrawColor(r, frame.r, frame.g, frame.b, 255);
	SDL_RenderDrawRect(r, &box);
	if (value_) {
		SDL_Rect inner{ box.x + 4, box.y + 4, box.w - 8, box.h - 8 };
		SDL_Color fill = Styles::Ivory();
		SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, fill.a ? fill.a : 200);
		SDL_RenderFillRect(r, &inner);
	}
}

int Checkbox::width()  { return 300; }
int Checkbox::height() { return 28;  }
