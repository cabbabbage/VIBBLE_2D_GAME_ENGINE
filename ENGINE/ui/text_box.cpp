#include "text_box.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <cstdlib>
#include <vector>
#include "utils/text_style.hpp"
#include "ui/styles.hpp"
namespace {
constexpr int kHorizontalPadding = 6;
constexpr int kVerticalPadding   = 8;
}
TextBox::TextBox(const std::string& label, const std::string& value)
: label_(label), text_(value), caret_pos_(value.size()) {
        recompute_height();
        update_caret_column();
}

void TextBox::set_position(SDL_Point p) { rect_.x = p.x; rect_.y = p.y; }
void TextBox::set_rect(const SDL_Rect& r) {
        rect_ = r;
        base_height_ = r.h;
        recompute_height();
}
const SDL_Rect& TextBox::rect() const { return rect_; }

void TextBox::set_label(const std::string& s) { label_ = s; }
const std::string& TextBox::label() const { return label_; }

void TextBox::set_value(const std::string& v) {
        text_ = v;
        caret_pos_ = std::min(caret_pos_, text_.size());
        edit_dirty_ = false;
        if (editing_) edit_origin_ = text_;
        recompute_height();
        update_caret_column();
}
const std::string& TextBox::value() const { return text_; }

bool TextBox::set_editing(bool e) {
        if (editing_ == e) return false;
        bool changed = false;
        if (e) {
                editing_ = true;
                SDL_StartTextInput();
                caret_pos_ = std::min(caret_pos_, text_.size());
                edit_origin_ = text_;
                edit_dirty_ = false;
                update_caret_column();
        }
        else {
                SDL_StopTextInput();
                editing_ = false;
                if (edit_dirty_ && edit_origin_ != text_) changed = true;
                edit_dirty_ = false;
        }
        return changed;
}

bool TextBox::handle_event(const SDL_Event& e) {
        bool changed = false;
        if (e.type == SDL_MOUSEMOTION) {
                SDL_Point p{ e.motion.x, e.motion.y };
                bool inside = SDL_PointInRect(&p, &rect_);
                hovered_ = inside;
                if (editing_ && !inside) {
                        bool blur_changed = set_editing(false);
                        if (blur_changed) changed = true;
                }
        }
        else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point p{ e.button.x, e.button.y };
                bool inside = SDL_PointInRect(&p, &rect_);
                hovered_ = inside;
                bool blur_changed = set_editing(inside);
                if (!inside && blur_changed) changed = true;
                if (editing_) {
                        caret_pos_ = caret_index_from_point(e.button.x, e.button.y);
                        update_caret_column();
                }
        }
        else if (editing_ && e.type == SDL_TEXTINPUT) {
                text_.insert(caret_pos_, e.text.text);
                caret_pos_ += std::strlen(e.text.text);
                edit_dirty_ = true;
                changed = true;
                recompute_height();
                update_caret_column();
        }
        else if (editing_ && e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (caret_pos_ > 0 && !text_.empty()) {
                                size_t erase_pos = caret_pos_ - 1;
                                text_.erase(erase_pos, 1);
                                caret_pos_ = erase_pos;
                                edit_dirty_ = true;
                                changed = true;
                                recompute_height();
                                update_caret_column();
                        }
                }
                else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        text_.insert(caret_pos_, "\n");
                        ++caret_pos_;
                        edit_dirty_ = true;
                        changed = true;
                        recompute_height();
                        update_caret_column();
                }
                else if (e.key.keysym.sym == SDLK_DELETE) {
                        if (caret_pos_ < text_.size()) {
                                text_.erase(caret_pos_, 1);
                                edit_dirty_ = true;
                                changed = true;
                                recompute_height();
                                update_caret_column();
                        }
                }
                else if (e.key.keysym.sym == SDLK_LEFT) {
                        if (caret_pos_ > 0) --caret_pos_;
                        update_caret_column();
                }
                else if (e.key.keysym.sym == SDLK_RIGHT) {
                        if (caret_pos_ < text_.size()) ++caret_pos_;
                        update_caret_column();
                }
                else if (e.key.keysym.sym == SDLK_UP) {
                        auto lines = line_info();
                        if (lines.size() > 1) {
                                size_t pos = std::min(caret_pos_, text_.size());
                                size_t current_line = line_index_from_position(pos, lines);
                                if (current_line > 0) {
                                        size_t desired = caret_desired_col_;
                                        const auto& prev = lines[current_line - 1];
                                        size_t new_col = std::min(desired, prev.length);
                                        caret_pos_ = prev.start + new_col;
                                        update_caret_column();
                                }
                        }
                }
                else if (e.key.keysym.sym == SDLK_DOWN) {
                        auto lines = line_info();
                        if (lines.size() > 1) {
                                size_t pos = std::min(caret_pos_, text_.size());
                                size_t current_line = line_index_from_position(pos, lines);
                                if (current_line + 1 < lines.size()) {
                                        size_t desired = caret_desired_col_;
                                        const auto& next = lines[current_line + 1];
                                        size_t new_col = std::min(desired, next.length);
                                        caret_pos_ = next.start + new_col;
                                        update_caret_column();
                                }
                        }
                }
                else if (e.key.keysym.sym == SDLK_HOME) {
                        auto lines = line_info();
                        if (!lines.empty()) {
                                size_t pos = std::min(caret_pos_, text_.size());
                                size_t current_line = line_index_from_position(pos, lines);
                                caret_pos_ = lines[current_line].start;
                        }
                        update_caret_column();
                }
                else if (e.key.keysym.sym == SDLK_END) {
                        auto lines = line_info();
                        if (!lines.empty()) {
                                size_t pos = std::min(caret_pos_, text_.size());
                                size_t current_line = line_index_from_position(pos, lines);
                                const auto& line = lines[current_line];
                                caret_pos_ = line.start + line.length;
                        }
                        update_caret_column();
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
        auto lines = line_info();
        int line_height = font_height();
        int text_y = rect_.y + kVerticalPadding;
        for (const auto& line : lines) {
                std::string line_text = text_.substr(line.start, line.length);
                if (!line_text.empty()) {
                        draw_text(r, line_text, rect_.x + kHorizontalPadding, text_y, textCol);
                }
                text_y += line_height;
        }
        if (editing_) render_caret(r, line_height);
}

int TextBox::width()  { return 420; }
int TextBox::height() { return 36;  }

void TextBox::render_caret(SDL_Renderer* r, int line_height) const {
        const TextStyle style{ TextStyles::SmallMain().font_path, TextStyles::SmallMain().font_size, Styles::Ivory() };
        TTF_Font* f = style.open_font();
        if (!f) return;
        size_t caret_index = std::min(caret_pos_, text_.size());
        auto lines = line_info();
        size_t line_index = line_index_from_position(caret_index, lines);
        size_t column = 0;
        if (!lines.empty()) {
                const auto& line = lines[line_index];
                size_t start = line.start;
                size_t end = start + line.length;
                column = (caret_index <= end) ? caret_index - start : line.length;
        }
        int w = 0, h = 0;
        std::string prefix;
        if (!lines.empty()) {
                const auto& line = lines[line_index];
                prefix = text_.substr(line.start, std::min(column, line.length));
        }
        if (!prefix.empty()) TTF_SizeUTF8(f, prefix.c_str(), &w, &h);
        else { TTF_SizeUTF8(f, " ", &w, &h); w = 0; }
        int font_height = TTF_FontHeight(f);
        if (line_height <= 0) line_height = font_height;
        int text_y = rect_.y + kVerticalPadding + static_cast<int>(line_index) * line_height;
        int caret_x = rect_.x + kHorizontalPadding + w;
        SDL_SetRenderDrawColor(r, style.color.r, style.color.g, style.color.b, style.color.a);
        SDL_RenderDrawLine(r, caret_x, text_y, caret_x, text_y + font_height);
        TTF_CloseFont(f);
}

size_t TextBox::caret_index_from_point(int mouse_x, int mouse_y) const {
        const TextStyle style{ TextStyles::SmallMain().font_path, TextStyles::SmallMain().font_size, Styles::Ivory() };
        TTF_Font* f = style.open_font();
        if (!f) return std::min(caret_pos_, text_.size());
        auto lines = line_info();
        int line_height = TTF_FontHeight(f);
        if (line_height <= 0) line_height = font_height();
        int relative_y = mouse_y - (rect_.y + kVerticalPadding);
        size_t line_index = 0;
        if (relative_y > 0 && line_height > 0) {
                size_t guess = static_cast<size_t>(relative_y / line_height);
                if (!lines.empty()) line_index = std::min(guess, lines.size() - 1);
        }
        if (lines.empty()) lines.push_back({0, text_.size()});
        const auto& line = lines[line_index];
        std::string line_text = text_.substr(line.start, line.length);
        int text_start = rect_.x + kHorizontalPadding;
        int relative = mouse_x - text_start;
        if (relative <= 0) { TTF_CloseFont(f); return line.start; }
        size_t best_index = line.start + line_text.size();
        int best_diff = std::numeric_limits<int>::max();
        for (size_t i = 0; i <= line_text.size(); ++i) {
                std::string prefix = line_text.substr(0, i);
                int w = 0, h = 0;
                if (!prefix.empty()) TTF_SizeUTF8(f, prefix.c_str(), &w, &h);
                else { w = 0; h = line_height; }
                int diff = std::abs(w - relative);
                if (diff < best_diff) { best_diff = diff; best_index = line.start + i; }
                if (w >= relative) break;
        }
        TTF_CloseFont(f);
        return best_index;
}

void TextBox::recompute_height() {
        int base = base_height_;
        int line_height = font_height();
        auto lines = line_info();
        int total = static_cast<int>(lines.size()) * line_height + 2 * kVerticalPadding;
        rect_.h = std::max(base, total);
}

int TextBox::font_height() const {
        const TextStyle style{ TextStyles::SmallMain().font_path, TextStyles::SmallMain().font_size, Styles::Ivory() };
        TTF_Font* f = style.open_font();
        if (!f) return TextStyles::SmallMain().font_size;
        int h = TTF_FontHeight(f);
        TTF_CloseFont(f);
        if (h <= 0) h = TextStyles::SmallMain().font_size;
        return h;
}

std::vector<TextBox::LineInfo> TextBox::line_info() const {
        std::vector<LineInfo> lines;
        size_t len = text_.size();
        size_t start = 0;
        if (len == 0) {
                lines.push_back({0, 0});
                return lines;
        }
        while (start <= len) {
                size_t newline = text_.find('\n', start);
                if (newline == std::string::npos) {
                        lines.push_back({ start, len - start });
                        break;
                }
                lines.push_back({ start, newline - start });
                start = newline + 1;
                if (start == len) {
                        lines.push_back({ start, 0 });
                        break;
                }
        }
        return lines;
}

void TextBox::update_caret_column() {
        auto lines = line_info();
        size_t pos = std::min(caret_pos_, text_.size());
        if (lines.empty()) { caret_desired_col_ = 0; return; }
        size_t idx = line_index_from_position(pos, lines);
        const auto& line = lines[idx];
        size_t start = line.start;
        size_t end = start + line.length;
        if (pos <= end) caret_desired_col_ = pos - start;
        else caret_desired_col_ = line.length;
}

size_t TextBox::line_index_from_position(size_t pos, const std::vector<LineInfo>& lines) const {
        if (lines.empty()) return 0;
        for (size_t i = 0; i < lines.size(); ++i) {
                size_t start = lines[i].start;
                size_t next_start = (i + 1 < lines.size()) ? lines[i + 1].start : std::numeric_limits<size_t>::max();
                if (pos < next_start) return i;
                if (pos == next_start && i + 1 < lines.size()) return i + 1;
        }
        return lines.size() - 1;
}
