#include "text_box.hpp"
#include <algorithm>
#include "utils/text_style.hpp"
#include "ui/styles.hpp"
TextBox::TextBox(const std::string& label, const std::string& value)
: label_(label), text_(value) {}

void TextBox::set_position(SDL_Point p) { rect_.x = p.x; rect_.y = p.y; }
void TextBox::set_rect(const SDL_Rect& r) { rect_ = r; }
const SDL_Rect& TextBox::rect() const { return rect_; }

void TextBox::set_label(const std::string& s) { label_ = s; }
const std::string& TextBox::label() const { return label_; }

void TextBox::set_value(const std::string& v) { text_ = v; }
const std::string& TextBox::value() const { return text_; }

void TextBox::set_editing(bool e) {
	if (editing_ == e) return;
	editing_ = e;
	if (editing_) SDL_StartTextInput();
	else SDL_StopTextInput();
}

bool TextBox::handle_event(const SDL_Event& e) {
	bool changed = false;
	if (e.type == SDL_MOUSEMOTION) {
		SDL_Point p{ e.motion.x, e.motion.y };
		hovered_ = SDL_PointInRect(&p, &rect_);
	}
	else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
		SDL_Point p{ e.button.x, e.button.y };
		bool inside = SDL_PointInRect(&p, &rect_);
		set_editing(inside);
	}
	else if (editing_ && e.type == SDL_TEXTINPUT) {
		text_ += e.text.text;
		changed = true;
	}
	else if (editing_ && e.type == SDL_KEYDOWN) {
		if (e.key.keysym.sym == SDLK_BACKSPACE) {
			if (!text_.empty()) { text_.pop_back(); changed = true; }
		}
		else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
			set_editing(false);
		}
		else if ((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_v) {
		}
	}
	return changed;
}

void TextBox::draw_text(SDL_Renderer* r, const std::string& s, int x, int y, SDL_Color col) const {
	const TextStyle style{ TextStyles::SmallMain().font_path, TextStyles::SmallMain().font_size, col };
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

void TextBox::render(SDL_Renderer* r) const {
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	if (!label_.empty()) {
		SDL_Color labelCol = Styles::Mist();
		draw_text(r, label_, rect_.x, rect_.y - 18, labelCol);
	}
	SDL_Rect box{ rect_.x, rect_.y, rect_.w, rect_.h };
	SDL_Color bg = Styles::Slate(); bg.a = 160;
	SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
	SDL_RenderFillRect(r, &box);
	SDL_Color border_on  = Styles::Gold();
	SDL_Color border_off = Styles::GoldDim();
	SDL_Color frame = (hovered_ || editing_) ? border_on : border_off;
	SDL_SetRenderDrawColor(r, frame.r, frame.g, frame.b, 255);
	SDL_RenderDrawRect(r, &box);
	SDL_Color textCol = Styles::Ivory();
	draw_text(r, text_, rect_.x + 6, rect_.y + (rect_.h/2 - 8), textCol);
}

int TextBox::width()  { return 420; }
int TextBox::height() { return 36;  }
