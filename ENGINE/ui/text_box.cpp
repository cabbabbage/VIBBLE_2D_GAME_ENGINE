#include "text_box.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <cstdlib>
#include "utils/text_style.hpp"
#include "ui/styles.hpp"
TextBox::TextBox(const std::string& label, const std::string& value)
: label_(label), text_(value), caret_pos_(value.size()) {}

void TextBox::set_position(SDL_Point p) { rect_.x = p.x; rect_.y = p.y; }
void TextBox::set_rect(const SDL_Rect& r) { rect_ = r; }
const SDL_Rect& TextBox::rect() const { return rect_; }

void TextBox::set_label(const std::string& s) { label_ = s; }
const std::string& TextBox::label() const { return label_; }

void TextBox::set_value(const std::string& v) {
        text_ = v;
        caret_pos_ = std::min(caret_pos_, text_.size());
}
const std::string& TextBox::value() const { return text_; }

void TextBox::set_editing(bool e) {
        if (editing_ == e) return;
        editing_ = e;
        if (editing_) {
                SDL_StartTextInput();
                caret_pos_ = text_.size();
        }
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
                if (editing_) caret_pos_ = caret_index_from_x(e.button.x);
        }
        else if (editing_ && e.type == SDL_TEXTINPUT) {
                text_.insert(caret_pos_, e.text.text);
                caret_pos_ += std::strlen(e.text.text);
                changed = true;
        }
        else if (editing_ && e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (caret_pos_ > 0 && !text_.empty()) {
                                size_t erase_pos = caret_pos_ - 1;
                                text_.erase(erase_pos, 1);
                                caret_pos_ = erase_pos;
                                changed = true;
                        }
                }
                else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        set_editing(false);
                }
                else if (e.key.keysym.sym == SDLK_DELETE) {
                        if (caret_pos_ < text_.size()) {
                                text_.erase(caret_pos_, 1);
                                changed = true;
                        }
                }
                else if (e.key.keysym.sym == SDLK_LEFT) {
                        if (caret_pos_ > 0) --caret_pos_;
                }
                else if (e.key.keysym.sym == SDLK_RIGHT) {
                        if (caret_pos_ < text_.size()) ++caret_pos_;
                }
                else if (e.key.keysym.sym == SDLK_HOME) {
                        caret_pos_ = 0;
                }
                else if (e.key.keysym.sym == SDLK_END) {
                        caret_pos_ = text_.size();
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
        if (editing_) render_caret(r);
}

int TextBox::width()  { return 420; }
int TextBox::height() { return 36;  }

void TextBox::render_caret(SDL_Renderer* r) const {
        const TextStyle style{ TextStyles::SmallMain().font_path, TextStyles::SmallMain().font_size, Styles::Ivory() };
        TTF_Font* f = style.open_font();
        if (!f) return;
        size_t caret_index = std::min(caret_pos_, text_.size());
        std::string prefix = text_.substr(0, caret_index);
        int w = 0, h = 0;
        if (!prefix.empty()) TTF_SizeUTF8(f, prefix.c_str(), &w, &h);
        else { TTF_SizeUTF8(f, " ", &w, &h); w = 0; }
        int font_height = TTF_FontHeight(f);
        int text_y = rect_.y + (rect_.h/2 - 8);
        int caret_x = rect_.x + 6 + w;
        SDL_SetRenderDrawColor(r, style.color.r, style.color.g, style.color.b, style.color.a);
        SDL_RenderDrawLine(r, caret_x, text_y, caret_x, text_y + font_height);
        TTF_CloseFont(f);
}

size_t TextBox::caret_index_from_x(int mouse_x) const {
        const TextStyle style{ TextStyles::SmallMain().font_path, TextStyles::SmallMain().font_size, Styles::Ivory() };
        TTF_Font* f = style.open_font();
        if (!f) return std::min(caret_pos_, text_.size());
        int text_start = rect_.x + 6;
        int relative = mouse_x - text_start;
        if (relative <= 0) { TTF_CloseFont(f); return 0; }
        size_t best_index = text_.size();
        int best_diff = std::numeric_limits<int>::max();
        for (size_t i = 0; i <= text_.size(); ++i) {
                std::string prefix = text_.substr(0, i);
                int w = 0, h = 0;
                if (!prefix.empty()) TTF_SizeUTF8(f, prefix.c_str(), &w, &h);
                else { w = 0; h = TTF_FontHeight(f); }
                int diff = std::abs(w - relative);
                if (diff < best_diff) { best_diff = diff; best_index = i; }
                if (w >= relative) break;
        }
        TTF_CloseFont(f);
        return best_index;
}
