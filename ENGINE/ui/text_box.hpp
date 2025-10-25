#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

class TextBox {

	public:
    TextBox(const std::string& label, const std::string& value);
    void set_position(SDL_Point p);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const;
    void set_label(const std::string& s);
    const std::string& label() const;
    void set_value(const std::string& v);
    const std::string& value() const;
    bool is_editing() const { return editing_; }
    bool set_editing(bool e);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int width();
    static int height();

        private:
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, SDL_Color col) const;
    void render_caret(SDL_Renderer* r, int line_height) const;
    size_t caret_index_from_point(int mouse_x, int mouse_y) const;
    void recompute_height();
    int font_height() const;
    struct LineInfo {
        size_t start = 0;
        size_t length = 0;
};
    std::vector<LineInfo> line_info() const;
    size_t line_index_from_position(size_t pos, const std::vector<LineInfo>& lines) const;
    void update_caret_column();

        private:
    SDL_Rect rect_{0,0,420,36};
    int base_height_ = height();
    std::string label_;
    std::string text_;
    bool hovered_ = false;
    bool editing_ = false;
    bool edit_dirty_ = false;
    std::string edit_origin_;
    size_t caret_pos_ = 0;
    size_t caret_desired_col_ = 0;
};
