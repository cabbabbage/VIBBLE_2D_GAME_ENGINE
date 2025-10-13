#include "widgets.hpp"
#include "draw_utils.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <optional>
#include <unordered_set>
#include <utility>

namespace {
constexpr int kBoxTopPadding = 8;
constexpr int kBoxBottomPadding = 8;
constexpr int kLabelControlGap = 8;
constexpr int kTextboxHorizontalPadding = 8;
constexpr int kSliderControlHeight = 44;
constexpr int kSliderValueWidth = 60;
constexpr int kDropdownControlHeight = 32;
constexpr int kButtonHorizontalPadding = 28;
constexpr int kCheckboxLabelGap = 8;
constexpr int kSliderValueHorizontalPadding = 8;
constexpr int kSliderTrackThickness = 10;
constexpr int kSliderKnobWidth = 14;
constexpr int kSliderKnobHeight = 18;
constexpr int kSliderKnobVerticalInset = (kSliderKnobHeight - kSliderTrackThickness) / 2;

int slider_value_height() {
    const DMSliderStyle& st = DMStyles::Slider();
    return std::max(DMTextBox::height(), st.value.font_size + DMSpacing::small_gap());
}

int range_value_width(int total_width) {
    int candidate = total_width / 4;
    candidate = std::max(candidate, 64);
    candidate = std::min(candidate, std::max(64, total_width / 2));
    return candidate;
}

std::unordered_set<const void*> g_slider_scroll_captures;

void set_slider_scroll_capture(const void* owner, bool capture) {
    if (capture) {
        g_slider_scroll_captures.insert(owner);
    } else {
        g_slider_scroll_captures.erase(owner);
    }
}

bool slider_scroll_captured() {
    return !g_slider_scroll_captures.empty();
}
}

bool DMWidgetsSliderScrollCaptured() {
    return slider_scroll_captured();
}

void DMWidgetsSetSliderScrollCapture(const void* owner, bool capture) {
    set_slider_scroll_capture(owner, capture);
}

DMButton::DMButton(const std::string& text, const DMButtonStyle* style, int w, int h)
    : rect_{0,0,w,h}, text_(text), style_(style) {
    update_preferred_width();
    rect_.w = std::max(rect_.w, preferred_width_);
}

void DMButton::set_rect(const SDL_Rect& r) {
    rect_ = r;
    rect_.w = std::max(rect_.w, preferred_width_);
}

void DMButton::set_text(const std::string& t) {
    text_ = t;
    update_preferred_width();
    rect_.w = std::max(rect_.w, preferred_width_);
}

void DMButton::set_style(const DMButtonStyle* style) {
    if (style_ == style) {
        return;
    }
    style_ = style;
    update_preferred_width();
    rect_.w = std::max(rect_.w, preferred_width_);
}

void DMButton::update_preferred_width() {
    if (!style_) {
        preferred_width_ = rect_.w;
        return;
    }
    TTF_Font* f = TTF_OpenFont(style_->label.font_path.c_str(), style_->label.font_size);
    if (!f) {
        preferred_width_ = rect_.w;
        return;
    }
    int text_w = 0;
    int text_h = 0;
    if (TTF_SizeUTF8(f, text_.c_str(), &text_w, &text_h) != 0) {
        text_w = 0;
    }
    TTF_CloseFont(f);
    preferred_width_ = std::max(text_w + kButtonHorizontalPadding, kButtonHorizontalPadding);
}

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
    const SDL_Color bg = pressed_ ? style_->press_bg : (hovered_ ? style_->hover_bg : style_->bg);
    const SDL_Color& highlight = DMStyles::HighlightColor();
    const SDL_Color& shadow = DMStyles::ShadowColor();
    dm_draw::DrawBeveledRect(
        r,
        rect_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        bg,
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    SDL_Color border = style_->border;
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &rect_);
    draw_label(r, style_->text);
}

DMTextBox::DMTextBox(const std::string& label, const std::string& value)
    : label_(label), text_(value), caret_pos_(value.size()) {}

void DMTextBox::set_rect(const SDL_Rect& r) {
    rect_ = r;
    label_height_ = compute_label_height(rect_.w);
    int y = rect_.y + kBoxTopPadding;
    label_rect_ = SDL_Rect{ rect_.x, y, rect_.w, label_height_ };
    int control_y = y + label_height_ + (label_height_ > 0 ? kLabelControlGap : 0);
    int available = rect_.h - (control_y - rect_.y) - kBoxBottomPadding;
    int control_h = std::max(DMTextBox::height(), available);
    box_rect_ = SDL_Rect{ rect_.x, control_y, rect_.w, control_h };
    rect_.h = (box_rect_.y - rect_.y) + box_rect_.h + kBoxBottomPadding;
}

void DMTextBox::set_value(const std::string& v) {
    text_ = v;
    caret_pos_ = std::min(caret_pos_, text_.size());
}

int DMTextBox::height_for_width(int w) const {
    return preferred_height(w);
}

bool DMTextBox::handle_event(const SDL_Event& e) {
    bool changed = false;
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        hovered_ = SDL_PointInRect(&p, &box_rect_);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        bool inside = SDL_PointInRect(&p, &box_rect_);
        if (inside) {
            if (!editing_) {
                editing_ = true;
                SDL_StartTextInput();
            }
            caret_pos_ = text_.size();
        } else if (editing_) {
            editing_ = false;
            SDL_StopTextInput();
        }
    } else if (editing_ && e.type == SDL_TEXTINPUT) {
        text_.insert(caret_pos_, e.text.text);
        caret_pos_ += std::strlen(e.text.text);
        changed = true;
    } else if (editing_ && e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_BACKSPACE) {
            if (caret_pos_ > 0 && !text_.empty()) {
                size_t erase_pos = caret_pos_ - 1;
                text_.erase(erase_pos, 1);
                caret_pos_ = erase_pos;
                changed = true;
            }
        } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
            editing_ = false; SDL_StopTextInput();
        } else if (e.key.keysym.sym == SDLK_DELETE) {
            if (caret_pos_ < text_.size()) {
                text_.erase(caret_pos_, 1);
                changed = true;
            }
        } else if (e.key.keysym.sym == SDLK_LEFT) {
            if (caret_pos_ > 0) --caret_pos_;
        } else if (e.key.keysym.sym == SDLK_RIGHT) {
            if (caret_pos_ < text_.size()) ++caret_pos_;
        } else if (e.key.keysym.sym == SDLK_HOME) {
            caret_pos_ = 0;
        } else if (e.key.keysym.sym == SDLK_END) {
            caret_pos_ = text_.size();
        }
    }
    return changed;
}

void DMTextBox::draw_text(SDL_Renderer* r, const std::string& s, int x, int y, int max_width, const DMLabelStyle& ls) const {
    TTF_Font* f = TTF_OpenFont(ls.font_path.c_str(), ls.font_size);
    if (!f) return;
    const int content_w = std::max(1, max_width);
    auto lines = wrap_lines(f, s, content_w);
    int line_y = y;
    const int gap = DMSpacing::small_gap();
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, line.c_str(), ls.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ x, line_y, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            line_y += surf->h;
            if (i + 1 < lines.size()) line_y += gap;
            SDL_FreeSurface(surf);
        }
    }
    TTF_CloseFont(f);
}

void DMTextBox::render(SDL_Renderer* r) const {
    const DMTextBoxStyle& st = DMStyles::TextBox();
    if (!label_.empty() && label_height_ > 0) {
        DMLabelStyle lbl = DMStyles::Label();
        draw_text(r, label_, label_rect_.x, label_rect_.y, label_rect_.w, lbl);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_Color fill = (hovered_ || editing_) ? DMStyles::TextboxHoverFill() : DMStyles::TextboxBaseFill();
    dm_draw::DrawBeveledRect(
        r,
        box_rect_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        fill,
        DMStyles::HighlightColor(),
        DMStyles::ShadowColor(),
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    SDL_Color border = st.border;
    if (hovered_ && !editing_) {
        border = DMStyles::TextboxHoverOutline();
    }
    if (editing_) {
        border = DMStyles::TextboxActiveOutline();
    }
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &box_rect_);
    DMLabelStyle valStyle{ st.label.font_path, st.label.font_size, st.text };
    draw_text(r, text_, box_rect_.x + kTextboxHorizontalPadding, box_rect_.y + kTextboxHorizontalPadding, std::max(1, box_rect_.w - 2 * kTextboxHorizontalPadding), valStyle);
    if (editing_) {
        TTF_Font* f = TTF_OpenFont(valStyle.font_path.c_str(), valStyle.font_size);
        if (f) {
            int max_width = std::max(1, box_rect_.w - 2 * kTextboxHorizontalPadding);
            size_t caret_index = std::min(caret_pos_, text_.size());
            std::string prefix = text_.substr(0, caret_index);
            auto lines = wrap_lines(f, prefix, max_width);
            int caret_x = box_rect_.x + kTextboxHorizontalPadding;
            int caret_y = box_rect_.y + kTextboxHorizontalPadding;
            int caret_height = TTF_FontHeight(f);
            const int gap = DMSpacing::small_gap();
            if (!lines.empty()) {
                for (size_t i = 0; i < lines.size(); ++i) {
                    const std::string& line = lines[i];
                    int w = 0, h = 0;
                    if (!line.empty()) {
                        TTF_SizeUTF8(f, line.c_str(), &w, &h);
                    } else {
                        w = 0; h = TTF_FontHeight(f);
                    }
                    if (i + 1 < lines.size()) {
                        caret_y += h + gap;
                    } else {
                        caret_x += w;
                        caret_height = (h > 0) ? h : TTF_FontHeight(f);
                    }
                }
            }
            const SDL_Color caret = DMStyles::TextCaretColor();
            SDL_SetRenderDrawColor(r, caret.r, caret.g, caret.b, caret.a);
            SDL_RenderDrawLine(r, caret_x, caret_y, caret_x, caret_y + caret_height);
            TTF_CloseFont(f);
        }
    }
}

std::vector<std::string> DMTextBox::wrap_lines(TTF_Font* f, const std::string& s, int max_width) const {
    std::vector<std::string> out;
    if (!f) return out;
    size_t start = 0;
    auto push_wrapped = [&](const std::string& para) {
        if (para.empty()) { out.emplace_back(""); return; }
        size_t pos = 0;
        while (pos < para.size()) {
            size_t best_break = pos;
            size_t last_space = std::string::npos;
            for (size_t i = pos; i <= para.size(); ++i) {
                std::string trial = para.substr(pos, i - pos);
                int w=0,h=0; TTF_SizeUTF8(f, trial.c_str(), &w, &h);
                if (w <= max_width) {
                    best_break = i;
                    if (i < para.size() && std::isspace((unsigned char)para[i])) last_space = i;
                    if (i == para.size()) break;
                } else break;
            }
            size_t brk = best_break;
            if (brk > pos && last_space != std::string::npos && last_space > pos) brk = last_space;
            if (brk == pos) brk = std::min(para.size(), pos + 1);
            std::string ln = para.substr(pos, brk - pos);
            while (!ln.empty() && std::isspace((unsigned char)ln.back())) ln.pop_back();
            out.push_back(ln);
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
    int label_h = compute_label_height(width);
    int box_h   = DMTextBox::height();
    return kBoxTopPadding + label_h + (label_h > 0 ? kLabelControlGap : 0) + box_h + kBoxBottomPadding;
}

int DMTextBox::compute_label_height(int width) const {
    if (label_.empty()) return 0;
    DMLabelStyle lbl = DMStyles::Label();
    TTF_Font* f = TTF_OpenFont(lbl.font_path.c_str(), lbl.font_size);
    if (!f) return lbl.font_size;
    auto lines = wrap_lines(f, label_, std::max(1, width));
    int total = 0;
    const int gap = DMSpacing::small_gap();
    for (size_t i = 0; i < lines.size(); ++i) {
        int w = 0, h = 0;
        TTF_SizeUTF8(f, lines[i].c_str(), &w, &h);
        total += h;
        if (i + 1 < lines.size()) total += gap;
    }
    TTF_CloseFont(f);
    return total;
}

SDL_Rect DMTextBox::box_rect() const {
    return box_rect_;
}

SDL_Rect DMTextBox::label_rect() const {
    return label_rect_;
}

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
            SDL_Rect dst{ rect_.x + rect_.h + kCheckboxLabelGap, rect_.y + (rect_.h - surf->h)/2, surf->w, surf->h };
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
    const SDL_Color fill = hovered_ ? DMStyles::CheckboxHoverFill() : DMStyles::CheckboxBaseFill();
    dm_draw::DrawBeveledRect(
        r,
        box,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        fill,
        DMStyles::HighlightColor(),
        DMStyles::ShadowColor(),
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    SDL_Color border = DMStyles::CheckboxOutlineColor();
    if (hovered_) {
        border = DMStyles::CheckboxHoverOutline();
    }
    if (value_) {
        border = DMStyles::CheckboxActiveOutline();
    }
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &box);
    if (value_) {
        SDL_Color check = DMStyles::CheckboxCheckColor();
        SDL_Rect inner{ box.x + 4, box.y + 4, box.w - 8, box.h - 8 };
        dm_draw::DrawBeveledRect(
            r,
            inner,
            std::min(DMStyles::CornerRadius(), 3),
            std::max(0, DMStyles::BevelDepth() - 1),
            check,
            DMStyles::HighlightColor(),
            DMStyles::ShadowColor(),
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
    }
    draw_label(r);
}

DMSlider::DMSlider(const std::string& label, int min_val, int max_val, int value)
    : label_(label), min_(min_val), max_(max_val), value_(value) {
    if (min_ > max_) {
        std::swap(min_, max_);
    }
    set_value(value_);
}

DMSlider::~DMSlider() {
    commit_pending_value();
    focused_ = false;
    set_slider_scroll_capture(this, false);
}

int DMSlider::clamp_value(int v) const {
    if (min_ <= max_) {
        return std::max(min_, std::min(max_, v));
    }
    return std::min(min_, std::max(max_, v));
}

bool DMSlider::apply_interaction_value(int v) {
    int clamped = clamp_value(v);
    if (!defer_commit_until_unfocus_) {
        int prev = value_;
        value_ = clamped;
        pending_value_ = value_;
        has_pending_value_ = false;
        return value_ != prev;
    }
    int prev_display = pending_value_;
    pending_value_ = clamped;
    has_pending_value_ = (pending_value_ != value_);
    return pending_value_ != prev_display;
}

bool DMSlider::commit_pending_value() {
    if (!defer_commit_until_unfocus_ || !has_pending_value_) {
        return false;
    }
    has_pending_value_ = false;
    if (value_ == pending_value_) {
        return false;
    }
    value_ = pending_value_;
    return true;
}

int DMSlider::display_value() const {
    return defer_commit_until_unfocus_ ? pending_value_ : value_;
}

void DMSlider::set_rect(const SDL_Rect& r) {
    rect_ = r;
    label_height_ = compute_label_height(rect_.w);
    const int header_height = std::max(label_height_, slider_value_height());
    const int header_y = rect_.y + kBoxTopPadding;
    const int value_gap = DMSpacing::small_gap();

    int value_w = std::min(kSliderValueWidth, rect_.w);
    value_w = std::max(0, std::min(value_w, rect_.w));

    int label_w = std::max(0, rect_.w - value_w - value_gap);
    if (label_w <= 0) {
        value_w = std::min(rect_.w, value_w);
        label_w = std::max(0, rect_.w - value_w);
    }
    if (label_height_ <= 0 || label_.empty()) {
        label_w = 0;
    }

    int label_y = header_y + (header_height - label_height_) / 2;
    label_rect_ = SDL_Rect{ rect_.x, label_y, label_w, label_height_ };

    int value_y = header_y + (header_height - slider_value_height()) / 2;
    int value_x = rect_.x + rect_.w - std::max(value_w, 0);
    if (label_height_ > 0 && label_w > 0) {
        value_x = rect_.x + label_w + value_gap;
    }
    value_rect_ = SDL_Rect{ value_x, value_y, std::max(value_w, 0), slider_value_height() };
    if (value_rect_.x + value_rect_.w > rect_.x + rect_.w) {
        value_rect_.x = rect_.x + rect_.w - value_rect_.w;
    }

    int content_y = header_y + header_height + kLabelControlGap;
    int available = rect_.h - (content_y - rect_.y) - kBoxBottomPadding;
    int content_h = std::max(kSliderControlHeight, available);
    content_rect_ = SDL_Rect{ rect_.x, content_y, rect_.w, content_h };

    if (edit_box_) {
        edit_box_->set_rect(value_rect_);
    }

    rect_.h = (content_rect_.y - rect_.y) + content_rect_.h + kBoxBottomPadding;
}

void DMSlider::set_value(int v) {
    int clamped = clamp_value(v);
    value_ = clamped;
    pending_value_ = clamped;
    has_pending_value_ = false;
}

int DMSlider::label_space() const {
    return label_height_;
}

SDL_Rect DMSlider::content_rect() const {
    return content_rect_;
}

SDL_Rect DMSlider::value_rect() const {
    return value_rect_;
}

SDL_Rect DMSlider::track_rect() const {
    int track_width = std::max(0, content_rect_.w);
    return SDL_Rect{ content_rect_.x, content_rect_.y + content_rect_.h/2 - kSliderTrackThickness / 2, track_width, kSliderTrackThickness };
}

SDL_Rect DMSlider::knob_rect() const {
    SDL_Rect tr = track_rect();
    int usable = std::max(1, tr.w - kSliderKnobWidth);
    int x = tr.x + (int)((display_value() - min_) * usable / (double)(std::max(1, max_ - min_)));
    return SDL_Rect{ x, tr.y - kSliderKnobVerticalInset, kSliderKnobWidth, kSliderKnobHeight };
}

int DMSlider::value_for_x(int x) const {
    SDL_Rect tr = track_rect();
    int usable = std::max(1, tr.w - kSliderKnobWidth);
    double t = (x - tr.x) / (double)usable;
    int range = std::max(1, max_ - min_);
    int v = min_ + (int)std::round(t * range);
    return std::max(min_, std::min(max_, v));
}

bool DMSlider::handle_event(const SDL_Event& e) {
    if (edit_box_) {
        if (edit_box_->handle_event(e)) {
            std::optional<int> parsed = parse_value(edit_box_->value());
            if (parsed) {
                set_value(*parsed);
                edit_box_->set_value(format_value(display_value()));
            }
            return true;
        }
        if (!edit_box_->is_editing()) edit_box_.reset();
    }
    auto set_focus = [this](bool focus) {
        if (focused_ == focus) {
            return;
        }
        focused_ = focus;
        set_slider_scroll_capture(this, focused_);
        if (!focused_) {
            commit_pending_value();
        }
};
    auto update_hover = [this, &set_focus](SDL_Point p) {
        bool inside = SDL_PointInRect(&p, &rect_);
        hovered_ = inside || dragging_;
        if (!inside) {
            if (!dragging_) {
                knob_hovered_ = false;
                set_focus(false);
            }
            return inside;
        }
        if (dragging_) {
            knob_hovered_ = true;
        } else {
            SDL_Rect knob = knob_rect();
            knob_hovered_ = SDL_PointInRect(&p, &knob);
        }
        return inside;
};

    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        update_hover(p);
        if (dragging_) {
            apply_interaction_value(value_for_x(p.x));
            return true;
        }
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        bool inside = update_hover(p);
        if (inside) {
            bool was_focused = focused_;
            set_focus(true);
            if (!was_focused) {
                return true;
            }
            SDL_Rect vr = value_rect();
            if (SDL_PointInRect(&p, &vr)) {
                edit_box_ = std::make_unique<DMTextBox>("", format_value(display_value()));
                edit_box_->set_rect(vr);
                edit_box_->handle_event(e);
                return true;
            }
            SDL_Rect tr = track_rect();
            SDL_Rect knob = knob_rect();
            if (SDL_PointInRect(&p, &knob) || SDL_PointInRect(&p, &tr)) {
                dragging_ = true;
                knob_hovered_ = true;
                apply_interaction_value(value_for_x(p.x));
                return true;
            }
        } else if (!dragging_) {
            set_focus(false);
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        bool was_dragging = dragging_;
        dragging_ = false;
        SDL_Point p{ e.button.x, e.button.y };
        update_hover(p);
        if (!SDL_PointInRect(&p, &rect_) && focused_) {
            set_focus(false);
        }
        if (was_dragging) {
            return true;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point mouse{0, 0};
        if (SDL_GetMouseFocus() == nullptr) {
            set_focus(false);
            return false;
        }
        SDL_GetMouseState(&mouse.x, &mouse.y);
        update_hover(mouse);
        if (!hovered_) {
            return false;
        }
        if (!focused_) {
            return false;
        }
        int delta = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            delta = -delta;
        }
#if SDL_VERSION_ATLEAST(2,0,18)
        if (delta == 0) {
            delta = static_cast<int>(std::round(e.wheel.preciseY));
        }
#endif
        if (delta == 0) {
            return false;
        }
        const int prev_display = display_value();
        if (!apply_interaction_value(prev_display + delta)) {
            return false;
        }
        return display_value() != prev_display;
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
    if (!label_.empty() && label_height_ > 0) {
        draw_text(r, label_, label_rect_.x, label_rect_.y);
    }
    const bool active = focused_ || dragging_;
    if (active) {
        const SDL_Color& focus_outline = DMStyles::SliderFocusOutline();
        SDL_SetRenderDrawColor(r, focus_outline.r, focus_outline.g, focus_outline.b, focus_outline.a);
        SDL_RenderDrawRect(r, &rect_);
    } else if (hovered_) {
        const SDL_Color& hover_outline = DMStyles::SliderHoverOutline();
        SDL_SetRenderDrawColor(r, hover_outline.r, hover_outline.g, hover_outline.b, hover_outline.a);
        SDL_RenderDrawRect(r, &rect_);
    }
    SDL_Rect tr = track_rect();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_Color& highlight = DMStyles::HighlightColor();
    const SDL_Color& shadow = DMStyles::ShadowColor();
    const int radius = std::min(DMStyles::CornerRadius(), std::min(tr.w, tr.h) / 2);
    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(tr.w, tr.h) / 2));
    dm_draw::DrawBeveledRect(
        r,
        tr,
        radius,
        bevel,
        DMStyles::SliderTrackBackground(),
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());
    int range = std::max(1, max_ - min_);
    int current_value = display_value();
    SDL_Rect fill{ tr.x, tr.y, (int)((current_value - min_) * tr.w / (double)range), tr.h };
    if (fill.w > 0) {
        SDL_Rect fill_rect = fill;
        const SDL_Color track_fill = active ? st.track_fill_active : st.track_fill;
        dm_draw::DrawBeveledRect(
            r,
            fill_rect,
            radius,
            bevel,
            track_fill,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
    }
    SDL_Rect krect = knob_rect();
    SDL_Color knob_col = st.knob;
    SDL_Color kborder = st.knob_border;
    if (active) {
        knob_col = st.knob_accent;
        kborder = st.knob_accent_border;
    } else if (knob_hovered_) {
        knob_col = st.knob_hover;
        kborder = st.knob_border_hover;
    }
    const int knob_radius = std::min(DMStyles::CornerRadius(), std::min(krect.w, krect.h) / 2);
    const int knob_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(krect.w, krect.h) / 2));
    dm_draw::DrawBeveledRect(
        r,
        krect,
        knob_radius,
        knob_bevel,
        knob_col,
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());
    SDL_SetRenderDrawColor(r, kborder.r, kborder.g, kborder.b, kborder.a);
    SDL_RenderDrawRect(r, &krect);
    if (edit_box_) {
        edit_box_->render(r);
    } else {
        SDL_Rect vr = value_rect();
        draw_text(r, format_value(current_value), vr.x + kSliderValueHorizontalPadding, vr.y + (vr.h - st.value.font_size) / 2);
    }
}

void DMSlider::set_value_formatter(std::function<std::string(int)> formatter) {
    value_formatter_ = std::move(formatter);
    if (edit_box_) {
        edit_box_->set_value(format_value(display_value()));
    }
}

void DMSlider::set_value_parser(std::function<std::optional<int>(const std::string&)> parser) {
    value_parser_ = std::move(parser);
}

std::string DMSlider::format_value(int v) const {
    if (value_formatter_) {
        return value_formatter_(v);
    }
    return std::to_string(v);
}

std::optional<int> DMSlider::parse_value(const std::string& text) const {
    if (value_parser_) {
        return value_parser_(text);
    }
    try {
        return std::stoi(text);
    } catch (...) {
        return std::nullopt;
    }
}

int DMSlider::preferred_height(int width) const {
    int label_h = compute_label_height(width);
    int header_h = std::max(label_h, slider_value_height());
    return kBoxTopPadding + header_h + kLabelControlGap + kSliderControlHeight + kBoxBottomPadding;
}

int DMSlider::compute_label_height(int width) const {
    if (label_.empty()) return 0;
    const DMSliderStyle& st = DMStyles::Slider();
    TTF_Font* f = TTF_OpenFont(st.label.font_path.c_str(), st.label.font_size);
    if (!f) return st.label.font_size;
    int text_w = 0;
    int text_h = 0;
    TTF_SizeUTF8(f, label_.c_str(), &text_w, &text_h);
    TTF_CloseFont(f);
    (void)width;
    return text_h;
}

int DMSlider::height() {
    const DMSliderStyle& st = DMStyles::Slider();
    return kBoxTopPadding + st.label.font_size + kLabelControlGap + kSliderControlHeight + kBoxBottomPadding;
}

DMRangeSlider::DMRangeSlider(int min_val, int max_val, int min_value, int max_value)
    : min_(min_val), max_(max_val) {
    if (min_ > max_) std::swap(min_, max_);

    set_max_value(max_value);
    set_min_value(min_value);
}

DMRangeSlider::~DMRangeSlider() {
    commit_pending_values();
    focused_ = false;
    set_slider_scroll_capture(this, false);
}

int DMRangeSlider::clamp_min_value(int v) const {
    int hi = defer_commit_until_unfocus_ ? pending_max_value_ : max_value_;
    hi = std::max(min_, std::min(max_, hi));
    return std::max(min_, std::min(hi, v));
}

int DMRangeSlider::clamp_max_value(int v) const {
    int lo = defer_commit_until_unfocus_ ? pending_min_value_ : min_value_;
    lo = std::max(min_, std::min(max_, lo));
    return std::max(lo, std::min(max_, v));
}

bool DMRangeSlider::apply_min_interaction(int v) {
    int clamped = clamp_min_value(v);
    if (!defer_commit_until_unfocus_) {
        int prev = min_value_;
        min_value_ = clamped;
        if (min_value_ > max_value_) min_value_ = max_value_;
        pending_min_value_ = min_value_;
        pending_max_value_ = max_value_;
        pending_dirty_ = false;
        return min_value_ != prev;
    }
    int prev_display = pending_min_value_;
    pending_min_value_ = clamped;
    if (pending_min_value_ > pending_max_value_) pending_min_value_ = pending_max_value_;
    bool changed = pending_min_value_ != prev_display;
    pending_dirty_ = pending_dirty_ || (pending_min_value_ != min_value_);
    return changed;
}

bool DMRangeSlider::apply_max_interaction(int v) {
    int clamped = clamp_max_value(v);
    if (!defer_commit_until_unfocus_) {
        int prev = max_value_;
        max_value_ = clamped;
        if (max_value_ < min_value_) max_value_ = min_value_;
        pending_min_value_ = min_value_;
        pending_max_value_ = max_value_;
        pending_dirty_ = false;
        return max_value_ != prev;
    }
    int prev_display = pending_max_value_;
    pending_max_value_ = clamped;
    if (pending_max_value_ < pending_min_value_) pending_max_value_ = pending_min_value_;
    bool changed = pending_max_value_ != prev_display;
    pending_dirty_ = pending_dirty_ || (pending_max_value_ != max_value_);
    return changed;
}

bool DMRangeSlider::commit_pending_values() {
    if (!defer_commit_until_unfocus_) {
        return false;
    }
    if (!pending_dirty_ && pending_min_value_ == min_value_ && pending_max_value_ == max_value_) {
        return false;
    }
    pending_dirty_ = false;
    bool changed = false;
    if (min_value_ != pending_min_value_) {
        min_value_ = pending_min_value_;
        changed = true;
    }
    if (max_value_ != pending_max_value_) {
        max_value_ = pending_max_value_;
        changed = true;
    }
    if (min_value_ > max_value_) {
        max_value_ = min_value_;
    }
    return changed;
}

int DMRangeSlider::display_min_value() const {
    return defer_commit_until_unfocus_ ? pending_min_value_ : min_value_;
}

int DMRangeSlider::display_max_value() const {
    return defer_commit_until_unfocus_ ? pending_max_value_ : max_value_;
}

void DMRangeSlider::set_rect(const SDL_Rect& r) {
    rect_ = r;
    const int header_height = slider_value_height();
    const int header_y = rect_.y + kBoxTopPadding;
    const int gap = DMSpacing::small_gap();

    int total_width = std::max(0, rect_.w);
    int available_each = std::max(0, (total_width - gap) / 2);
    int desired = std::min(range_value_width(rect_.w), available_each);
    int label_w = std::max(available_each / 2, desired);
    label_w = std::min(label_w, available_each);

    min_value_rect_ = SDL_Rect{ rect_.x, header_y, label_w, header_height };
    max_value_rect_ = SDL_Rect{ rect_.x + total_width - label_w, header_y, label_w, header_height };

    int content_y = header_y + header_height + kLabelControlGap;
    int available = rect_.h - (content_y - rect_.y) - kBoxBottomPadding;
    int content_h = std::max(kSliderControlHeight, available);
    content_rect_ = SDL_Rect{ rect_.x, content_y, rect_.w, content_h };

    rect_.h = (content_rect_.y - rect_.y) + content_rect_.h + kBoxBottomPadding;

    if (edit_min_) edit_min_->set_rect(min_value_rect_);
    if (edit_max_) edit_max_->set_rect(max_value_rect_);
}

void DMRangeSlider::set_min_value(int v) {
    min_value_ = std::max(min_, std::min(max_, v));
    if (min_value_ > max_value_) min_value_ = max_value_;
    pending_min_value_ = min_value_;
    if (!defer_commit_until_unfocus_) {
        pending_max_value_ = max_value_;
    }
    pending_dirty_ = false;
}

void DMRangeSlider::set_max_value(int v) {
    max_value_ = std::max(min_, std::min(max_, v));
    if (max_value_ < min_value_) max_value_ = min_value_;
    pending_max_value_ = max_value_;
    if (!defer_commit_until_unfocus_) {
        pending_min_value_ = min_value_;
    }
    pending_dirty_ = false;
}

SDL_Rect DMRangeSlider::content_rect() const {
    return content_rect_;
}

SDL_Rect DMRangeSlider::track_rect() const {
    int width = std::max(0, content_rect_.w);
    return SDL_Rect{ content_rect_.x, content_rect_.y + content_rect_.h/2 - kSliderTrackThickness / 2, width, kSliderTrackThickness };
}

SDL_Rect DMRangeSlider::min_knob_rect() const {
    SDL_Rect tr = track_rect();
    int usable = std::max(1, tr.w - kSliderKnobWidth);
    int range = std::max(1, max_ - min_);
    int x = tr.x + (int)((display_min_value() - min_) * usable / (double)range);
    return SDL_Rect{ x, tr.y - kSliderKnobVerticalInset, kSliderKnobWidth, kSliderKnobHeight };
}

SDL_Rect DMRangeSlider::max_knob_rect() const {
    SDL_Rect tr = track_rect();
    int usable = std::max(1, tr.w - kSliderKnobWidth);
    int range = std::max(1, max_ - min_);
    int x = tr.x + (int)((display_max_value() - min_) * usable / (double)range);
    return SDL_Rect{ x, tr.y - kSliderKnobVerticalInset, kSliderKnobWidth, kSliderKnobHeight };
}

int DMRangeSlider::value_for_x(int x) const {
    SDL_Rect tr = track_rect();
    double t = (x - tr.x) / (double)(std::max(1, tr.w - kSliderKnobWidth));
    int v = min_ + (int)std::round(t * (max_ - min_));
    return std::max(min_, std::min(max_, v));
}

bool DMRangeSlider::handle_event(const SDL_Event& e) {
    if (edit_min_) {
        if (edit_min_->handle_event(e)) {
            try {
                int nv = std::stoi(edit_min_->value());
                set_min_value(nv);
            } catch (...) {

            }
            return true;
        }
        if (!edit_min_->is_editing()) edit_min_.reset();
    }
    if (edit_max_) {
        if (edit_max_->handle_event(e)) {
            try {
                int nv = std::stoi(edit_max_->value());
                set_max_value(nv);
            } catch (...) {

            }
            return true;
        }
        if (!edit_max_->is_editing()) edit_max_.reset();
    }
    auto set_focus = [this](bool focus) {
        if (focused_ == focus) {
            return;
        }
        focused_ = focus;
        set_slider_scroll_capture(this, focused_);
        if (!focused_) {
            commit_pending_values();
        }
};
    auto update_hover = [this, &set_focus](SDL_Point p) {
        if (dragging_min_) {
            min_hovered_ = true;
            max_hovered_ = false;
        } else if (dragging_max_) {
            min_hovered_ = false;
            max_hovered_ = true;
        }
        bool inside = SDL_PointInRect(&p, &rect_);
        hovered_ = inside || dragging_min_ || dragging_max_;
        if (!inside) {
            if (!dragging_min_ && !dragging_max_) {
                min_hovered_ = false;
                max_hovered_ = false;
                set_focus(false);
            }
            return inside;
        }
        SDL_Rect kmin = min_knob_rect();
        SDL_Rect kmax = max_knob_rect();
        bool on_min = SDL_PointInRect(&p, &kmin);
        bool on_max = SDL_PointInRect(&p, &kmax);
        if (dragging_min_) {
            min_hovered_ = true;
            max_hovered_ = false;
            return inside;
        }
        if (dragging_max_) {
            min_hovered_ = false;
            max_hovered_ = true;
            return inside;
        }
        if (on_min || on_max) {
            min_hovered_ = on_min;
            max_hovered_ = on_max;
            return inside;
        }
        SDL_Point min_center{ kmin.x + kmin.w / 2, kmin.y + kmin.h / 2 };
        SDL_Point max_center{ kmax.x + kmax.w / 2, kmax.y + kmax.h / 2 };
        const bool overlap = (min_center.x == max_center.x) && (min_center.y == max_center.y);
        if (overlap) {
            if (p.x >= max_center.x) {
                min_hovered_ = false;
                max_hovered_ = true;
            } else {
                min_hovered_ = true;
                max_hovered_ = false;
            }
            return inside;
        }
        const auto sqr = [](int v) { return v * v; };
        const int min_dist = sqr(p.x - min_center.x) + sqr(p.y - min_center.y);
        const int max_dist = sqr(p.x - max_center.x) + sqr(p.y - max_center.y);
        if (min_dist <= max_dist) {
            min_hovered_ = true;
            max_hovered_ = false;
        } else {
            min_hovered_ = false;
            max_hovered_ = true;
        }
        return inside;
};

    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        update_hover(p);
        bool dragging = false;
        if (dragging_min_) {
            apply_min_interaction(value_for_x(p.x));
            dragging = true;
        }
        if (dragging_max_) {
            apply_max_interaction(value_for_x(p.x));
            dragging = true;
        }
        if (dragging) {
            return true;
        }
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        bool inside = update_hover(p);
        if (inside) {
            bool was_focused = focused_;
            set_focus(true);
            if (!was_focused) {
                return true;
            }
        } else if (!dragging_min_ && !dragging_max_) {
            set_focus(false);
        }
        if (e.button.clicks >= 2) {
            if (inside && SDL_PointInRect(&p, &min_value_rect_)) {
                edit_min_ = std::make_unique<DMTextBox>("", std::to_string(display_min_value()));
                edit_min_->set_rect(min_value_rect_);
                edit_min_->handle_event(e);
                return true;
            } else if (inside && SDL_PointInRect(&p, &max_value_rect_)) {
                edit_max_ = std::make_unique<DMTextBox>("", std::to_string(display_max_value()));
                edit_max_->set_rect(max_value_rect_);
                edit_max_->handle_event(e);
                return true;
            }
        }
        if (inside) {
            SDL_Rect track = track_rect();
            SDL_Rect min_knob = min_knob_rect();
            SDL_Rect max_knob = max_knob_rect();
            bool on_track = SDL_PointInRect(&p, &track);
            bool on_min = SDL_PointInRect(&p, &min_knob);
            bool on_max = SDL_PointInRect(&p, &max_knob);
            if (on_min || (on_track && min_hovered_ && !max_hovered_)) {
                dragging_min_ = true;
                min_hovered_ = true;
                max_hovered_ = false;
                apply_min_interaction(value_for_x(p.x));
                return true;
            }
            if (on_max || (on_track && max_hovered_ && !min_hovered_)) {
                dragging_max_ = true;
                min_hovered_ = false;
                max_hovered_ = true;
                apply_max_interaction(value_for_x(p.x));
                return true;
            }
            if (on_track) {
                int target = value_for_x(p.x);
                int midpoint = (display_min_value() + display_max_value()) / 2;
                if (target <= midpoint) {
                    dragging_min_ = true;
                    min_hovered_ = true;
                    max_hovered_ = false;
                    apply_min_interaction(target);
                } else {
                    dragging_max_ = true;
                    min_hovered_ = false;
                    max_hovered_ = true;
                    apply_max_interaction(target);
                }
                return true;
            }
        }
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        bool was_dragging = dragging_min_ || dragging_max_;
        dragging_min_ = false;
        dragging_max_ = false;
        SDL_Point p{ e.button.x, e.button.y };
        update_hover(p);
        if (!SDL_PointInRect(&p, &rect_) && focused_) {
            set_focus(false);
        }
        if (was_dragging) {
            return true;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point mouse{0, 0};
        if (SDL_GetMouseFocus() == nullptr) {
            set_focus(false);
            return false;
        }
        SDL_GetMouseState(&mouse.x, &mouse.y);
        update_hover(mouse);
        if (!hovered_) {
            return false;
        }
        if (!focused_) {
            return false;
        }
        int delta = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            delta = -delta;
        }
#if SDL_VERSION_ATLEAST(2,0,18)
        if (delta == 0) {
            delta = static_cast<int>(std::round(e.wheel.preciseY));
        }
#endif
        if (delta == 0) {
            return false;
        }
        const int prev_min = display_min_value();
        const int prev_max = display_max_value();
        bool changed = false;
        if (max_hovered_) {
            changed = apply_max_interaction(prev_max + delta);
        } else {
            changed = apply_min_interaction(prev_min + delta);
        }
        if (!changed) {
            changed = display_min_value() != prev_min || display_max_value() != prev_max;
        }
        return changed;
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
    const bool dragging = dragging_min_ || dragging_max_;
    const bool active = focused_ || dragging;
    if (active) {
        const SDL_Color& focus_outline = DMStyles::SliderFocusOutline();
        SDL_SetRenderDrawColor(r, focus_outline.r, focus_outline.g, focus_outline.b, focus_outline.a);
        SDL_RenderDrawRect(r, &rect_);
    } else if (hovered_) {
        const SDL_Color& hover_outline = DMStyles::SliderHoverOutline();
        SDL_SetRenderDrawColor(r, hover_outline.r, hover_outline.g, hover_outline.b, hover_outline.a);
        SDL_RenderDrawRect(r, &rect_);
    }
    SDL_Rect tr = track_rect();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_Color& highlight = DMStyles::HighlightColor();
    const SDL_Color& shadow = DMStyles::ShadowColor();
    const int radius = std::min(DMStyles::CornerRadius(), std::min(tr.w, tr.h) / 2);
    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(tr.w, tr.h) / 2));
    dm_draw::DrawBeveledRect(
        r,
        tr,
        radius,
        bevel,
        DMStyles::SliderTrackBackground(),
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());
    SDL_Rect kmin = min_knob_rect();
    SDL_Rect kmax = max_knob_rect();
    int fill_x = kmin.x + kSliderKnobWidth / 2;
    int fill_w = (kmax.x + kSliderKnobWidth / 2) - fill_x;
    SDL_Rect fill{ fill_x, tr.y, std::max(0, fill_w), tr.h };
    if (fill.w > 0) {
        const SDL_Color track_fill = active ? st.track_fill_active : st.track_fill;
        dm_draw::DrawBeveledRect(
            r,
            fill,
            radius,
            bevel,
            track_fill,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
    }
    const bool min_active = focused_ || dragging_min_;
    const bool max_active = focused_ || dragging_max_;
    SDL_Color col_min = st.knob;
    SDL_Color col_max = st.knob;
    SDL_Color border_min = st.knob_border;
    SDL_Color border_max = st.knob_border;
    if (min_active) {
        col_min = st.knob_accent;
        border_min = st.knob_accent_border;
    } else if (min_hovered_) {
        col_min = st.knob_hover;
        border_min = st.knob_border_hover;
    }
    if (max_active) {
        col_max = st.knob_accent;
        border_max = st.knob_accent_border;
    } else if (max_hovered_) {
        col_max = st.knob_hover;
        border_max = st.knob_border_hover;
    }
    const int knob_radius = std::min(DMStyles::CornerRadius(), std::min(kmin.w, kmin.h) / 2);
    const int knob_bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(kmin.w, kmin.h) / 2));
    dm_draw::DrawBeveledRect(
        r,
        kmin,
        knob_radius,
        knob_bevel,
        col_min,
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());
    SDL_SetRenderDrawColor(r, border_min.r, border_min.g, border_min.b, border_min.a);
    SDL_RenderDrawRect(r, &kmin);
    dm_draw::DrawBeveledRect(
        r,
        kmax,
        knob_radius,
        knob_bevel,
        col_max,
        highlight,
        shadow,
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());
    SDL_SetRenderDrawColor(r, border_max.r, border_max.g, border_max.b, border_max.a);
    SDL_RenderDrawRect(r, &kmax);
    if (edit_min_) {
        edit_min_->render(r);
    } else {
        int text_y = min_value_rect_.y + (min_value_rect_.h - st.value.font_size) / 2;
        draw_text(r, std::to_string(display_min_value()), min_value_rect_.x + kSliderValueHorizontalPadding, text_y);
    }
    if (edit_max_) {
        edit_max_->render(r);
    } else {
        std::string value = std::to_string(display_max_value());
        int text_x = max_value_rect_.x + kSliderValueHorizontalPadding;
        int text_y = max_value_rect_.y + (max_value_rect_.h - st.value.font_size) / 2;
        TTF_Font* f = TTF_OpenFont(st.label.font_path.c_str(), st.label.font_size);
        if (f) {
            int tw = 0;
            int th = 0;
            if (TTF_SizeUTF8(f, value.c_str(), &tw, &th) == 0) {
                text_x = std::max(max_value_rect_.x + kSliderValueHorizontalPadding,
                                   max_value_rect_.x + max_value_rect_.w - tw - kSliderValueHorizontalPadding);
            }
            TTF_CloseFont(f);
        }
        draw_text(r, value, text_x, text_y);
    }
}

int DMRangeSlider::height() {
    return kBoxTopPadding + slider_value_height() + kLabelControlGap + kSliderControlHeight + kBoxBottomPadding;
}

DMDropdown::DMDropdown(const std::string& label, const std::vector<std::string>& options, int idx)
    : label_(label), options_(options), index_(idx) {
    set_selected(idx);
}

DMDropdown::~DMDropdown() {
    if (active_ == this) active_ = nullptr;
}

DMDropdown* DMDropdown::active_ = nullptr;

DMDropdown* DMDropdown::active_dropdown() { return active_; }

void DMDropdown::render_active_options(SDL_Renderer* r) {
    if (active_ && active_->focused_) active_->render_options(r);
}

void DMDropdown::set_rect(const SDL_Rect& r) {
    rect_ = r;
    label_height_ = compute_label_height(rect_.w);
    int y = rect_.y + kBoxTopPadding;
    label_rect_ = SDL_Rect{ rect_.x, y, rect_.w, label_height_ };
    int box_y = y + label_height_ + (label_height_ > 0 ? kLabelControlGap : 0);
    int available = rect_.h - (box_y - rect_.y) - kBoxBottomPadding;
    int box_h = std::max(kDropdownControlHeight, available);
    box_rect_ = SDL_Rect{ rect_.x, box_y, rect_.w, box_h };
    rect_.h = (box_rect_.y - rect_.y) + box_rect_.h + kBoxBottomPadding;
}

void DMDropdown::set_selected(int idx) {
    index_ = clamp_index(idx);
    pending_index_ = index_;
    has_pending_index_ = focused_;
}

bool DMDropdown::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        const bool inside = SDL_PointInRect(&p, &box_rect_);
        hovered_ = inside;
        if (focused_ && active_ == this && !inside) {
            const bool applied = commit_pending_selection();
            focused_ = false;
            has_pending_index_ = false;
            if (active_ == this) active_ = nullptr;
            return applied;
        }
        return false;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        const bool inside = SDL_PointInRect(&p, &box_rect_);
        if (inside) {
            begin_focus();
            return true;
        }
        if (focused_ && active_ == this) {
            const bool applied = commit_pending_selection();
            focused_ = false;
            has_pending_index_ = false;
            if (active_ == this) active_ = nullptr;
            return applied;
        }
        return false;
    }

    if (e.type == SDL_MOUSEWHEEL) {
        if (!(focused_ && active_ == this && !options_.empty())) return false;
        if (!has_pending_index_) {
            pending_index_ = index_;
            has_pending_index_ = true;
        }
        int raw_delta = e.wheel.y;
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            raw_delta = -raw_delta;
        }
        const int delta = -raw_delta;
        if (delta == 0) return false;
        int target = pending_index_ + delta;
        int clamped = clamp_index(target);
        if (clamped == pending_index_) {
            pending_index_ = clamped;
            return false;
        }
        pending_index_ = clamped;
        return true;
    }

    return false;
}

void DMDropdown::render(SDL_Renderer* r) const {
    const DMTextBoxStyle& st = DMStyles::TextBox();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const bool has_focus = focused_ && active_ == this;
    const SDL_Color fill = has_focus ? DMStyles::TextboxHoverFill() : (hovered_ ? DMStyles::TextboxHoverFill() : DMStyles::TextboxBaseFill());
    dm_draw::DrawBeveledRect(
        r,
        box_rect_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        fill,
        DMStyles::HighlightColor(),
        DMStyles::ShadowColor(),
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());
    if (!label_.empty() && label_height_ > 0) {
        DMLabelStyle lbl = DMStyles::Label();
        TTF_Font* f = TTF_OpenFont(lbl.font_path.c_str(), lbl.font_size);
        if (f) {
            SDL_Surface* surf = TTF_RenderUTF8_Blended(f, label_.c_str(), lbl.color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                if (tex) {
                    SDL_Rect dst{ label_rect_.x, label_rect_.y, surf->w, surf->h };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
            TTF_CloseFont(f);
        }
    }
    SDL_Color border = st.border;
    if (hovered_ && !has_focus) {
        border = DMStyles::TextboxHoverOutline();
    }
    if (has_focus) {
        border = DMStyles::TextboxActiveOutline();
    }
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &box_rect_);
    DMLabelStyle labelStyle{ st.label.font_path, st.label.font_size, st.text };
    TTF_Font* f = TTF_OpenFont(labelStyle.font_path.c_str(), labelStyle.font_size);
    if (f) {
        int safe_idx = 0;
        if (!options_.empty()) {
            int display_idx = has_focus && has_pending_index_ ? pending_index_ : index_;
            display_idx = clamp_index(display_idx);
            safe_idx = display_idx;
        }
        const char* display = options_.empty() ? "" : options_[safe_idx].c_str();
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, display, labelStyle.color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ box_rect_.x + (box_rect_.w - surf->w)/2, box_rect_.y + (box_rect_.h - surf->h)/2, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(f);
    }
}

void DMDropdown::render_options(SDL_Renderer* r) const {
    if (!(focused_ && active_ == this)) return;
    if (options_.empty()) return;

    const DMTextBoxStyle& tb = DMStyles::TextBox();
    DMLabelStyle labelStyle{ tb.label.font_path, tb.label.font_size, tb.text };
    const SDL_Color focus_border = DMStyles::TextboxActiveOutline();
    const SDL_Color base_border = DMStyles::TextboxHoverOutline();
    const SDL_Color base_fill = DMStyles::TextboxBaseFill();
    const SDL_Color focus_fill = DMStyles::TextboxHoverFill();
    const SDL_Color highlight = DMStyles::HighlightColor();
    const SDL_Color shadow = DMStyles::ShadowColor();

    const int base_index = clamp_index(has_pending_index_ ? pending_index_ : index_);

    struct Candidate {
        int delta;
        float scale;
        float alpha;
    };
    static constexpr Candidate kCandidates[] = {
        { -2, 0.82f, 0.35f },
        { -1, 0.9f, 0.65f },
        { 0, 1.0f, 1.0f },
        { 1, 0.9f, 0.65f },
        { 2, 0.82f, 0.35f },
    };

    struct Entry {
        int index;
        int delta;
        float scale;
        float alpha;
        SDL_Rect rect{};
    };

    std::vector<Entry> entries;
    entries.reserve(std::size(kCandidates));
    for (const Candidate& c : kCandidates) {
        const int idx = base_index + c.delta;
        if (idx < 0 || idx >= static_cast<int>(options_.size())) {
            continue;
        }
        entries.push_back(Entry{ idx, c.delta, c.scale, c.alpha, {} });
    }
    if (entries.empty()) {
        return;
    }

    const auto apply_alpha = [](SDL_Color col, float alpha) {
        const int scaled = static_cast<int>(std::round(col.a * alpha));
        col.a = static_cast<Uint8>(std::clamp(scaled, 0, 255));
        return col;
    };

    const int spacing = 6;
    const int base_w = box_rect_.w;
    const int base_h = box_rect_.h;
    const int center_x = box_rect_.x + base_w / 2;
    const int center_y = box_rect_.y + base_h / 2;

    const auto compute_size = [&](const Entry& e) {
        const int w = std::max(1, static_cast<int>(std::round(base_w * e.scale)));
        const int h = std::max(1, static_cast<int>(std::round(base_h * e.scale)));
        SDL_Rect rect{ center_x - w / 2, center_y - h / 2, w, h };
        return rect;
    };

    Entry* center_entry = nullptr;
    for (Entry& e : entries) {
        if (e.delta == 0) {
            e.rect = compute_size(e);
            center_entry = &e;
            break;
        }
    }
    if (!center_entry) {
        // Should not happen, but fall back to rendering the first entry centered.
        entries.front().rect = compute_size(entries.front());
        center_entry = &entries.front();
    }

    int current_top = center_entry->rect.y;
    std::vector<Entry*> above;
    std::vector<Entry*> below;
    for (Entry& e : entries) {
        if (&e == center_entry) continue;
        if (e.delta < 0) {
            above.push_back(&e);
        } else {
            below.push_back(&e);
        }
    }
    std::sort(above.begin(), above.end(), [](const Entry* a, const Entry* b) { return a->delta > b->delta; });
    std::sort(below.begin(), below.end(), [](const Entry* a, const Entry* b) { return a->delta < b->delta; });

    for (Entry* e : above) {
        SDL_Rect rect = compute_size(*e);
        rect.y = current_top - spacing - rect.h;
        e->rect = rect;
        current_top = rect.y;
    }

    int current_bottom = center_entry->rect.y + center_entry->rect.h;
    for (Entry* e : below) {
        SDL_Rect rect = compute_size(*e);
        rect.y = current_bottom + spacing;
        e->rect = rect;
        current_bottom = rect.y + rect.h;
    }

    TTF_Font* font = TTF_OpenFont(labelStyle.font_path.c_str(), labelStyle.font_size);
    for (Entry& entry : entries) {
        const bool current = (entry.delta == 0);
        SDL_Rect rect = entry.rect;
        SDL_Color fill = current ? focus_fill : base_fill;
        SDL_Color border = current ? focus_border : base_border;
        SDL_Color hl = highlight;
        SDL_Color sh = shadow;
        if (!current) {
            fill = apply_alpha(fill, entry.alpha);
            border = apply_alpha(border, entry.alpha);
            hl = apply_alpha(hl, entry.alpha);
            sh = apply_alpha(sh, entry.alpha);
        }
        dm_draw::DrawBeveledRect(
            r,
            rect,
            DMStyles::CornerRadius(),
            DMStyles::BevelDepth(),
            fill,
            hl,
            sh,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(r, &rect);
        if (!font) {
            continue;
        }
        SDL_Color text_color = labelStyle.color;
        if (!current) {
            text_color = apply_alpha(text_color, entry.alpha);
        }
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, options_[entry.index].c_str(), text_color);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ rect.x + (rect.w - surf->w) / 2, rect.y + (rect.h - surf->h) / 2, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
    }
    if (font) {
        TTF_CloseFont(font);
    }
}

int DMDropdown::preferred_height(int width) const {
    int label_h = compute_label_height(width);
    return kBoxTopPadding + label_h + (label_h > 0 ? kLabelControlGap : 0) + kDropdownControlHeight + kBoxBottomPadding;
}

int DMDropdown::compute_label_height(int width) const {
    if (label_.empty()) return 0;
    DMLabelStyle lbl = DMStyles::Label();
    TTF_Font* f = TTF_OpenFont(lbl.font_path.c_str(), lbl.font_size);
    if (!f) return lbl.font_size;
    int text_w = 0;
    int text_h = 0;
    TTF_SizeUTF8(f, label_.c_str(), &text_w, &text_h);
    TTF_CloseFont(f);
    (void)width;
    return text_h;
}

int DMDropdown::height() {
    DMLabelStyle lbl = DMStyles::Label();
    return kBoxTopPadding + lbl.font_size + kLabelControlGap + kDropdownControlHeight + kBoxBottomPadding;
}

int DMDropdown::label_space() const { return label_height_; }

SDL_Rect DMDropdown::box_rect() const { return box_rect_; }

SDL_Rect DMDropdown::label_rect() const { return label_rect_; }

int DMDropdown::clamp_index(int idx) const {
    if (options_.empty()) {
        return 0;
    }
    if (idx < 0) return 0;
    int max_index = static_cast<int>(options_.size()) - 1;
    if (idx > max_index) idx = max_index;
    return idx;
}

bool DMDropdown::commit_pending_selection() {
    if (options_.empty()) {
        has_pending_index_ = false;
        return false;
    }
    const int target = clamp_index(has_pending_index_ ? pending_index_ : index_);
    const bool changed = target != index_;
    index_ = target;
    pending_index_ = target;
    has_pending_index_ = false;
    return changed;
}

void DMDropdown::begin_focus() {
    focused_ = true;
    if (active_ && active_ != this) {
        active_->commit_pending_selection();
        active_->focused_ = false;
        active_->has_pending_index_ = false;
    }
    active_ = this;
    pending_index_ = index_;
    has_pending_index_ = true;
}

