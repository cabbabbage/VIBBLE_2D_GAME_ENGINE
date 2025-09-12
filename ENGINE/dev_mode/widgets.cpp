#include "widgets.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cmath>

// -------- DMButton ---------
DMButton::DMButton(const std::string& text, const DMButtonStyle* style, int w, int h)
    : rect_{0,0,w,h}, text_(text), style_(style) {}

void DMButton::set_rect(const SDL_Rect& r) { rect_ = r; }

bool DMButton::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        hovered_ = SDL_PointInRect(&p, &rect_);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &rect_)) { pressed_ = true; return true; }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        bool inside = SDL_PointInRect(&p, &rect_);
        bool was = pressed_;
        pressed_ = false;
        return inside && was;
    }
    return false;
}

void DMButton::draw_label(SDL_Renderer* r, SDL_Color col) const {
    if (!style_) return;
    TTF_Font* f = TTF_OpenFont(style_->label.font_path.c_str(), style_->label.font_size);
    if (!f) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, text_.c_str(), col);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ rect_.x + (rect_.w - surf->w)/2, rect_.y + (rect_.h - surf->h)/2, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
    TTF_CloseFont(f);
}

void DMButton::render(SDL_Renderer* r) const {
    if (!style_) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = pressed_ ? style_->press_bg : (hovered_ ? style_->hover_bg : style_->bg);
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &rect_);
    SDL_SetRenderDrawColor(r, style_->border.r, style_->border.g, style_->border.b, style_->border.a);
    SDL_RenderDrawRect(r, &rect_);
    draw_label(r, style_->text);
}

// -------- DMTextBox ---------
DMTextBox::DMTextBox(const std::string& label, const std::string& value)
    : label_(label), text_(value) {}

void DMTextBox::set_rect(const SDL_Rect& r) { rect_ = r; }

bool DMTextBox::handle_event(const SDL_Event& e) {
    bool changed = false;
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        hovered_ = SDL_PointInRect(&p, &rect_);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        bool inside = SDL_PointInRect(&p, &rect_);
        editing_ = inside;
        if (editing_) SDL_StartTextInput(); else SDL_StopTextInput();
    } else if (editing_ && e.type == SDL_TEXTINPUT) {
        text_ += e.text.text; changed = true;
    } else if (editing_ && e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE) {
            if (!text_.empty()) { text_.pop_back(); changed = true; }
        } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
            editing_ = false; SDL_StopTextInput();
        }
    }
    return changed;
}

void DMTextBox::draw_text(SDL_Renderer* r, const std::string& s, int x, int y, const DMLabelStyle& ls) const {
    TTF_Font* f = TTF_OpenFont(ls.font_path.c_str(), ls.font_size);
    if (!f) return;
    const int content_w = std::max(1, rect_.w - 12);
    auto lines = wrap_lines(f, s, content_w);
    int line_y = y;
    for (const auto& line : lines) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, line.c_str(), ls.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ x, line_y, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            line_y += surf->h;
            SDL_FreeSurface(surf);
        }
    }
    TTF_CloseFont(f);
}

void DMTextBox::render(SDL_Renderer* r) const {
    const DMTextBoxStyle& st = DMStyles::TextBox();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, st.bg.r, st.bg.g, st.bg.b, st.bg.a);
    SDL_RenderFillRect(r, &rect_);
    SDL_Color border = (hovered_ || editing_) ? st.border_hover : st.border;
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &rect_);
    draw_text(r, label_, rect_.x, rect_.y - st.label.font_size - 2, st.label);
    DMLabelStyle valStyle{ st.label.font_path, st.label.font_size, st.text };
    // Multiline draw from top-left padding
    draw_text(r, text_, rect_.x + 6, rect_.y + 6, valStyle);
}

std::vector<std::string> DMTextBox::wrap_lines(TTF_Font* f, const std::string& s, int max_width) const {
    std::vector<std::string> out;
    if (!f) return out;
    // Split on existing newlines first
    size_t start = 0;
    auto push_wrapped = [&](const std::string& para) {
        if (para.empty()) { out.emplace_back(""); return; }
        // Greedy wrap by words, fallback to char wrap
        size_t pos = 0;
        while (pos < para.size()) {
            size_t best_break = pos;
            size_t last_space = std::string::npos;
            std::string line;
            for (size_t i = pos; i <= para.size(); ++i) {
                std::string trial = para.substr(pos, i - pos);
                int w=0,h=0; TTF_SizeUTF8(f, trial.c_str(), &w, &h);
                if (w <= max_width) {
                    best_break = i;
                    if (i < para.size() && std::isspace((unsigned char)para[i])) last_space = i;
                    if (i == para.size()) break;
                } else {
                    break;
                }
            }
            size_t brk = best_break;
            if (brk > pos && last_space != std::string::npos && last_space > pos) {
                brk = last_space; // wrap at last space within bounds
            }
            if (brk == pos) { // cannot fit even one char, force one char
                brk = std::min(para.size(), pos + 1);
            }
            std::string ln = para.substr(pos, brk - pos);
            // trim trailing spaces
            while (!ln.empty() && std::isspace((unsigned char)ln.back())) ln.pop_back();
            out.push_back(ln);
            // advance past any spaces
            pos = brk;
            while (pos < para.size() && std::isspace((unsigned char)para[pos])) ++pos;
        }
    };
    while (true) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) { push_wrapped(s.substr(start)); break; }
        push_wrapped(s.substr(start, nl - start));
        start = nl + 1;
    }
    if (out.empty()) out.emplace_back("");
    return out;
}

int DMTextBox::preferred_height(int width) const {
    const DMTextBoxStyle& st = DMStyles::TextBox();
    TTF_Font* f = TTF_OpenFont(st.label.font_path.c_str(), st.label.font_size);
    if (!f) return DMTextBox::height();
    int content_w = std::max(1, width - 12);
    auto lines = wrap_lines(f, text_, content_w);
    int fh=0; int fw=0; TTF_SizeUTF8(f, "Ag", &fw, &fh);
    TTF_CloseFont(f);
    int text_h = (int)lines.size() * std::max(1, fh);
    // padding top/bottom ~ 12
    return std::max(DMTextBox::height(), text_h + 12);
}

// -------- DMCheckbox ---------
DMCheckbox::DMCheckbox(const std::string& label, bool value)
    : label_(label), value_(value) {}

void DMCheckbox::set_rect(const SDL_Rect& r) { rect_ = r; }

bool DMCheckbox::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        hovered_ = SDL_PointInRect(&p, &rect_);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &rect_)) { value_ = !value_; return true; }
    }
    return false;
}

void DMCheckbox::draw_label(SDL_Renderer* r) const {
    const DMCheckboxStyle& st = DMStyles::Checkbox();
    TTF_Font* f = TTF_OpenFont(st.label.font_path.c_str(), st.label.font_size);
    if (!f) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, label_.c_str(), st.label.color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ rect_.x + rect_.h + 6, rect_.y + (rect_.h - surf->h)/2, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
    TTF_CloseFont(f);
}

void DMCheckbox::render(SDL_Renderer* r) const {
    const DMCheckboxStyle& st = DMStyles::Checkbox();
    SDL_Rect box{ rect_.x, rect_.y, rect_.h, rect_.h };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, st.box_bg.r, st.box_bg.g, st.box_bg.b, st.box_bg.a);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, st.border.r, st.border.g, st.border.b, st.border.a);
    SDL_RenderDrawRect(r, &box);
    if (value_) {
        SDL_SetRenderDrawColor(r, st.check.r, st.check.g, st.check.b, st.check.a);
        SDL_Rect inner{ box.x + 4, box.y + 4, box.w - 8, box.h - 8 };
        SDL_RenderFillRect(r, &inner);
    }
    draw_label(r);
}

// -------- DMSlider ---------
DMSlider::DMSlider(const std::string& label, int min_val, int max_val, int value)
    : label_(label), min_(min_val), max_(max_val), value_(value) {}

void DMSlider::set_rect(const SDL_Rect& r) { rect_ = r; }

void DMSlider::set_value(int v) { value_ = std::max(min_, std::min(max_, v)); }

SDL_Rect DMSlider::track_rect() const {
    return SDL_Rect{ rect_.x, rect_.y + rect_.h/2 - 4, rect_.w - 60, 8 };
}

SDL_Rect DMSlider::knob_rect() const {
    SDL_Rect tr = track_rect();
    int x = tr.x + (int)((value_ - min_) * (tr.w - 12) / (double)(max_ - min_));
    return SDL_Rect{ x, tr.y - 4, 12, 16 };
}

int DMSlider::value_for_x(int x) const {
    SDL_Rect tr = track_rect();
    double t = (x - tr.x) / (double)(tr.w - 12);
    int v = min_ + (int)std::round(t * (max_ - min_));
    return std::max(min_, std::min(max_, v));
}

bool DMSlider::handle_event(const SDL_Event& e) {
    if (edit_box_) {
        if (edit_box_->handle_event(e)) {
            int nv = std::stoi(edit_box_->value());
            set_value(nv); return true;
        }
        if (!edit_box_->is_editing()) edit_box_.reset();
    }
    SDL_Rect krect = knob_rect();
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        knob_hovered_ = SDL_PointInRect(&p, &krect);
        if (dragging_) { set_value(value_for_x(p.x)); return true; }
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &krect)) { dragging_ = true; return true; }
        SDL_Rect vr{ rect_.x + rect_.w - 60, rect_.y, 60, rect_.h };
        if (SDL_PointInRect(&p, &vr)) {
            edit_box_ = std::make_unique<DMTextBox>("", std::to_string(value_));
            edit_box_->set_rect(vr);
            edit_box_->handle_event(e);
            return true;
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (dragging_) { dragging_ = false; return true; }
    }
    return false;
}

void DMSlider::draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const {
    const DMSliderStyle& st = DMStyles::Slider();
    TTF_Font* f = TTF_OpenFont(st.label.font_path.c_str(), st.label.font_size);
    if (!f) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), st.label.color);
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

void DMSlider::render(SDL_Renderer* r) const {
    const DMSliderStyle& st = DMStyles::Slider();
    draw_text(r, label_, rect_.x, rect_.y - st.label.font_size - 2);
    SDL_Rect tr = track_rect();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, st.track_bg.r, st.track_bg.g, st.track_bg.b, st.track_bg.a);
    SDL_RenderFillRect(r, &tr);
    SDL_Rect fill{ tr.x, tr.y, (int)((value_ - min_) * tr.w / (double)(max_ - min_)), tr.h };
    SDL_SetRenderDrawColor(r, st.track_fill.r, st.track_fill.g, st.track_fill.b, st.track_fill.a);
    SDL_RenderFillRect(r, &fill);
    SDL_Rect krect = knob_rect();
    SDL_Color knob_col = knob_hovered_ || dragging_ ? st.knob_hover : st.knob;
    SDL_Color kborder = knob_hovered_ || dragging_ ? st.knob_border_hover : st.knob_border;
    SDL_SetRenderDrawColor(r, knob_col.r, knob_col.g, knob_col.b, knob_col.a);
    SDL_RenderFillRect(r, &krect);
    SDL_SetRenderDrawColor(r, kborder.r, kborder.g, kborder.b, kborder.a);
    SDL_RenderDrawRect(r, &krect);
    if (edit_box_) {
        edit_box_->render(r);
    } else {
        draw_text(r, std::to_string(value_), rect_.x + rect_.w - 50, rect_.y + (rect_.h - st.value.font_size)/2);
    }
}

// -------- DMRangeSlider ---------
DMRangeSlider::DMRangeSlider(int min_val, int max_val, int min_value, int max_value)
    : min_(min_val), max_(max_val), min_value_(min_value), max_value_(max_value) {}

void DMRangeSlider::set_rect(const SDL_Rect& r) { rect_ = r; }

void DMRangeSlider::set_min_value(int v) {
    min_value_ = std::max(min_, std::min(max_, v));
    if (min_value_ > max_value_) min_value_ = max_value_;
}

void DMRangeSlider::set_max_value(int v) {
    max_value_ = std::max(min_, std::min(max_, v));
    if (max_value_ < min_value_) max_value_ = min_value_;
}

SDL_Rect DMRangeSlider::track_rect() const {
    return SDL_Rect{ rect_.x + 40, rect_.y + rect_.h/2 - 4, rect_.w - 80, 8 };
}

SDL_Rect DMRangeSlider::min_knob_rect() const {
    SDL_Rect tr = track_rect();
    int x = tr.x + (int)((min_value_ - min_) * (tr.w - 12) / (double)(max_ - min_));
    return SDL_Rect{ x, tr.y - 4, 12, 16 };
}

SDL_Rect DMRangeSlider::max_knob_rect() const {
    SDL_Rect tr = track_rect();
    int x = tr.x + (int)((max_value_ - min_) * (tr.w - 12) / (double)(max_ - min_));
    return SDL_Rect{ x, tr.y - 4, 12, 16 };
}

int DMRangeSlider::value_for_x(int x) const {
    SDL_Rect tr = track_rect();
    double t = (x - tr.x) / (double)(tr.w - 12);
    int v = min_ + (int)std::round(t * (max_ - min_));
    return std::max(min_, std::min(max_, v));
}

bool DMRangeSlider::handle_event(const SDL_Event& e) {
    if (edit_min_) {
        if (edit_min_->handle_event(e)) {
            int nv = std::stoi(edit_min_->value());
            if (nv <= max_value_) set_min_value(nv);
            return true;
        }
        if (!edit_min_->is_editing()) edit_min_.reset();
    }
    if (edit_max_) {
        if (edit_max_->handle_event(e)) {
            int nv = std::stoi(edit_max_->value());
            if (nv >= min_value_) set_max_value(nv);
            return true;
        }
        if (!edit_max_->is_editing()) edit_max_.reset();
    }
    SDL_Rect kmin = min_knob_rect();
    SDL_Rect kmax = max_knob_rect();
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        min_hovered_ = SDL_PointInRect(&p, &kmin);
        max_hovered_ = SDL_PointInRect(&p, &kmax);
        if (dragging_min_) { int nv = value_for_x(p.x); if (nv <= max_value_) set_min_value(nv); return true; }
        if (dragging_max_) { int nv = value_for_x(p.x); if (nv >= min_value_) set_max_value(nv); return true; }
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &kmin)) { dragging_min_ = true; return true; }
        if (SDL_PointInRect(&p, &kmax)) { dragging_max_ = true; return true; }
        SDL_Rect min_label{ rect_.x, rect_.y, 40, rect_.h };
        SDL_Rect max_label{ rect_.x + rect_.w - 40, rect_.y, 40, rect_.h };
        if (e.button.clicks >= 2) {
            if (SDL_PointInRect(&p, &min_label)) {
                edit_min_ = std::make_unique<DMTextBox>("", std::to_string(min_value_));
                edit_min_->set_rect(min_label);
                edit_min_->handle_event(e);
                return true;
            } else if (SDL_PointInRect(&p, &max_label)) {
                edit_max_ = std::make_unique<DMTextBox>("", std::to_string(max_value_));
                edit_max_->set_rect(max_label);
                edit_max_->handle_event(e);
                return true;
            }
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (dragging_min_) { dragging_min_ = false; return true; }
        if (dragging_max_) { dragging_max_ = false; return true; }
    }
    return false;
}

void DMRangeSlider::draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const {
    const DMSliderStyle& st = DMStyles::Slider();
    TTF_Font* f = TTF_OpenFont(st.label.font_path.c_str(), st.label.font_size);
    if (!f) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), st.label.color);
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

void DMRangeSlider::render(SDL_Renderer* r) const {
    const DMSliderStyle& st = DMStyles::Slider();
    SDL_Rect tr = track_rect();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, st.track_bg.r, st.track_bg.g, st.track_bg.b, st.track_bg.a);
    SDL_RenderFillRect(r, &tr);
    SDL_Rect kmin = min_knob_rect();
    SDL_Rect kmax = max_knob_rect();
    int fill_x = kmin.x + 6;
    int fill_w = (kmax.x + 6) - fill_x;
    SDL_Rect fill{ fill_x, tr.y, std::max(0, fill_w), tr.h };
    SDL_SetRenderDrawColor(r, st.track_fill.r, st.track_fill.g, st.track_fill.b, st.track_fill.a);
    SDL_RenderFillRect(r, &fill);
    SDL_Color col_min = (min_hovered_ || dragging_min_) ? st.knob_hover : st.knob;
    SDL_Color col_max = (max_hovered_ || dragging_max_) ? st.knob_hover : st.knob;
    SDL_Color border_min = (min_hovered_ || dragging_min_) ? st.knob_border_hover : st.knob_border;
    SDL_Color border_max = (max_hovered_ || dragging_max_) ? st.knob_border_hover : st.knob_border;
    SDL_SetRenderDrawColor(r, col_min.r, col_min.g, col_min.b, col_min.a);
    SDL_RenderFillRect(r, &kmin);
    SDL_SetRenderDrawColor(r, border_min.r, border_min.g, border_min.b, border_min.a);
    SDL_RenderDrawRect(r, &kmin);
    SDL_SetRenderDrawColor(r, col_max.r, col_max.g, col_max.b, col_max.a);
    SDL_RenderFillRect(r, &kmax);
    SDL_SetRenderDrawColor(r, border_max.r, border_max.g, border_max.b, border_max.a);
    SDL_RenderDrawRect(r, &kmax);
    if (edit_min_) {
        edit_min_->render(r);
    } else {
        draw_text(r, std::to_string(min_value_), rect_.x, rect_.y + (rect_.h - st.value.font_size)/2);
    }
    if (edit_max_) {
        edit_max_->render(r);
    } else {
        draw_text(r, std::to_string(max_value_), rect_.x + rect_.w - 40, rect_.y + (rect_.h - st.value.font_size)/2);
    }
}

// -------- DMDropdown ---------
DMDropdown::DMDropdown(const std::string& label, const std::vector<std::string>& options, int idx)
    : label_(label), options_(options), index_(idx) {}

DMDropdown* DMDropdown::active_ = nullptr;

DMDropdown* DMDropdown::active_dropdown() { return active_; }

void DMDropdown::render_active_options(SDL_Renderer* r) {
    if (active_) active_->render_options(r);
}

void DMDropdown::set_rect(const SDL_Rect& r) { rect_ = r; }

bool DMDropdown::handle_event(const SDL_Event& e) {
    if (expanded_) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            if (SDL_PointInRect(&p, &rect_)) {
                expanded_ = false;
                if (active_ == this) active_ = nullptr;
                return true;
            }
            for (size_t i = 0; i < options_.size(); ++i) {
                SDL_Rect opt{ rect_.x, rect_.y + rect_.h * (int)(i + 1), rect_.w, rect_.h };
                if (SDL_PointInRect(&p, &opt)) {
                    index_ = (int)i;
                    expanded_ = false;
                    if (active_ == this) active_ = nullptr;
                    return true;
                }
            }
            expanded_ = false;
            if (active_ == this) active_ = nullptr;
            return true;
        }
        return true; // swallow all other events while open
    }

    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        hovered_ = SDL_PointInRect(&p, &rect_);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &rect_)) {
            expanded_ = true;
            active_ = this;
            return true;
        }
    }
    return false;
}

void DMDropdown::render(SDL_Renderer* r) const {
    const DMTextBoxStyle& st = DMStyles::TextBox();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, st.bg.r, st.bg.g, st.bg.b, st.bg.a);
    SDL_RenderFillRect(r, &rect_);
    SDL_Color border = hovered_ ? st.border_hover : st.border;
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &rect_);
    DMLabelStyle labelStyle{ st.label.font_path, st.label.font_size, st.text };
    TTF_Font* f = TTF_OpenFont(labelStyle.font_path.c_str(), labelStyle.font_size);
    if (f) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, options_.empty()?"":options_[index_].c_str(), labelStyle.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ rect_.x + 6, rect_.y + (rect_.h - surf->h)/2, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(f);
    }
}

void DMDropdown::render_options(SDL_Renderer* r) const {
    const DMTextBoxStyle& st = DMStyles::TextBox();
    SDL_Color border = hovered_ ? st.border_hover : st.border;
    DMLabelStyle labelStyle{ st.label.font_path, st.label.font_size, st.text };
    for (size_t i=0;i<options_.size();++i) {
        SDL_Rect opt{ rect_.x, rect_.y + rect_.h*(int)(i+1), rect_.w, rect_.h };
        SDL_SetRenderDrawColor(r, st.bg.r, st.bg.g, st.bg.b, st.bg.a);
        SDL_RenderFillRect(r, &opt);
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(r, &opt);
        TTF_Font* f2 = TTF_OpenFont(labelStyle.font_path.c_str(), labelStyle.font_size);
        if (f2) {
            SDL_Surface* s2 = TTF_RenderUTF8_Blended(f2, options_[i].c_str(), labelStyle.color);
            if (s2) {
                SDL_Texture* t2 = SDL_CreateTextureFromSurface(r, s2);
                if (t2) {
                    SDL_Rect dst{ opt.x + 6, opt.y + (opt.h - s2->h)/2, s2->w, s2->h };
                    SDL_RenderCopy(r, t2, nullptr, &dst);
                    SDL_DestroyTexture(t2);
                }
                SDL_FreeSurface(s2);
            }
            TTF_CloseFont(f2);
        }
    }
}

