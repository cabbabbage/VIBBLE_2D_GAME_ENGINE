#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "dm_styles.hpp"

class DMButton {
public:
    DMButton(const std::string& text, const DMButtonStyle* style, int w, int h);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_text(const std::string& t) { text_ = t; }
    const std::string& text() const { return text_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_hovered() const { return hovered_; }
    static int height() { return 28; }

private:
    void draw_label(SDL_Renderer* r, SDL_Color col) const;
    SDL_Rect rect_{0,0,200,28};
    std::string text_;
    bool hovered_ = false;
    bool pressed_ = false;
    const DMButtonStyle* style_ = nullptr;
};

class DMTextBox {
public:
    DMTextBox(const std::string& label, const std::string& value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(const std::string& v) { text_ = v; }
    const std::string& value() const { return text_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_editing() const { return editing_; }
    // Compute preferred height for a given width (auto-wrap)
    int preferred_height(int width) const;
    static int height() { return 32; }
    int height_for_width(int w) const;
private:
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, const DMLabelStyle& ls) const;
    std::vector<std::string> wrap_lines(TTF_Font* f, const std::string& s, int max_width) const;
    SDL_Rect rect_{0,0,200,32};
    std::string label_;
    std::string text_;
    bool hovered_ = false;
    bool editing_ = false;
};

class DMCheckbox {
public:
    DMCheckbox(const std::string& label, bool value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(bool v) { value_ = v; }
    bool value() const { return value_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height() { return 28; }
private:
    void draw_label(SDL_Renderer* r) const;
    SDL_Rect rect_{0,0,200,28};
    std::string label_;
    bool value_ = false;
    bool hovered_ = false;
};

class DMTextBox; // forward

class DMSlider {
public:
    DMSlider(const std::string& label, int min_val, int max_val, int value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(int v);
    int value() const { return value_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height() { return 40; }
private:
    SDL_Rect track_rect() const;
    SDL_Rect knob_rect() const;
    int value_for_x(int x) const;
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const;
    SDL_Rect rect_{0,0,200,40};
    std::string label_;
    int min_ = 0;
    int max_ = 100;
    int value_ = 0;
    bool dragging_ = false;
    bool knob_hovered_ = false;
    std::unique_ptr<DMTextBox> edit_box_;
};

class DMRangeSlider {
public:
    DMRangeSlider(int min_val, int max_val, int min_value, int max_value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_min_value(int v);
    void set_max_value(int v);
    int min_value() const { return min_value_; }
    int max_value() const { return max_value_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height() { return 40; }
private:
    SDL_Rect track_rect() const;
    SDL_Rect min_knob_rect() const;
    SDL_Rect max_knob_rect() const;
    int value_for_x(int x) const;
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const;
    SDL_Rect rect_{0,0,200,40};
    int min_ = 0;
    int max_ = 100;
    int min_value_ = 0;
    int max_value_ = 100;
    bool dragging_min_ = false;
    bool dragging_max_ = false;
    bool min_hovered_ = false;
    bool max_hovered_ = false;
    std::unique_ptr<DMTextBox> edit_min_;
    std::unique_ptr<DMTextBox> edit_max_;
};

class DMDropdown {
public:
    DMDropdown(const std::string& label, const std::vector<std::string>& options, int idx = 0);
    ~DMDropdown();
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    int selected() const { return index_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    void render_options(SDL_Renderer* r) const;
    bool expanded() const { return expanded_; }
    static int height() { return 32; }

    // Returns currently expanded dropdown, or nullptr if none.
    static DMDropdown* active_dropdown();
    // Render options for the currently expanded dropdown (if any).
    static void render_active_options(SDL_Renderer* r);
private:
    SDL_Rect rect_{0,0,200,32};
    std::string label_;
    std::vector<std::string> options_;
    int index_ = 0;
    bool hovered_ = false;
    bool expanded_ = false;
    static DMDropdown* active_;
};

// ------------------------------------------------------------
// Lightweight generic widget interface + adapters (formerly in ui_widget.hpp)
// ------------------------------------------------------------

// Lightweight generic widget interface for layout containers.
// Non-owning wrappers let us use existing DM* widgets inside generic UIs.
class Widget {
public:
    virtual ~Widget() = default;
    virtual void set_rect(const SDL_Rect& r) = 0;
    virtual const SDL_Rect& rect() const = 0;
    virtual int height_for_width(int w) const = 0;
    virtual bool handle_event(const SDL_Event& e) = 0;
    virtual void render(SDL_Renderer* r) const = 0;
};

// ---- Adapters for existing dev-mode widgets ----

class ButtonWidget : public Widget {
public:
    explicit ButtonWidget(DMButton* b, std::function<void()> on_click = {})
        : b_(b), on_click_(std::move(on_click)) {}
    void set_rect(const SDL_Rect& r) override { if (b_) b_->set_rect(r); }
    const SDL_Rect& rect() const override { return b_->rect(); }
    int height_for_width(int /*w*/) const override { return DMButton::height(); }
    bool handle_event(const SDL_Event& e) override {
        if (!b_) return false;
        bool used = b_->handle_event(e);
        if (used && on_click_ && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            on_click_();
        }
        return used;
    }
    void render(SDL_Renderer* r) const override { if (b_) b_->render(r); }
private:
    DMButton* b_ = nullptr; // non-owning
    std::function<void()> on_click_{};
};

class TextBoxWidget : public Widget {
public:
    explicit TextBoxWidget(DMTextBox* t) : t_(t) {}
    void set_rect(const SDL_Rect& r) override { if (t_) t_->set_rect(r); }
    const SDL_Rect& rect() const override { return t_->rect(); }
    int height_for_width(int w) const override { return t_ ? t_->preferred_height(w) : DMTextBox::height(); }
    bool handle_event(const SDL_Event& e) override { return t_ ? t_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (t_) t_->render(r); }
private:
    DMTextBox* t_ = nullptr; // non-owning
};

class CheckboxWidget : public Widget {
public:
    explicit CheckboxWidget(DMCheckbox* c) : c_(c) {}
    void set_rect(const SDL_Rect& r) override { if (c_) c_->set_rect(r); }
    const SDL_Rect& rect() const override { return c_->rect(); }
    int height_for_width(int /*w*/) const override { return DMCheckbox::height(); }
    bool handle_event(const SDL_Event& e) override { return c_ ? c_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (c_) c_->render(r); }
private:
    DMCheckbox* c_ = nullptr; // non-owning
};

class SliderWidget : public Widget {
public:
    explicit SliderWidget(DMSlider* s) : s_(s) {}
    void set_rect(const SDL_Rect& r) override { if (s_) s_->set_rect(r); }
    const SDL_Rect& rect() const override { return s_->rect(); }
    int height_for_width(int /*w*/) const override { return DMSlider::height(); }
    bool handle_event(const SDL_Event& e) override { return s_ ? s_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (s_) s_->render(r); }
private:
    DMSlider* s_ = nullptr; // non-owning
};

class RangeSliderWidget : public Widget {
public:
    explicit RangeSliderWidget(DMRangeSlider* s) : s_(s) {}
    void set_rect(const SDL_Rect& r) override { if (s_) s_->set_rect(r); }
    const SDL_Rect& rect() const override { return s_->rect(); }
    int height_for_width(int /*w*/) const override { return DMRangeSlider::height(); }
    bool handle_event(const SDL_Event& e) override { return s_ ? s_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (s_) s_->render(r); }
private:
    DMRangeSlider* s_ = nullptr; // non-owning
};

class DropdownWidget : public Widget {
public:
    explicit DropdownWidget(DMDropdown* d) : d_(d) {}
    void set_rect(const SDL_Rect& r) override { if (d_) d_->set_rect(r); }
    const SDL_Rect& rect() const override { return d_->rect(); }
    int height_for_width(int /*w*/) const override { return DMDropdown::height(); }
    bool handle_event(const SDL_Event& e) override { return d_ ? d_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (d_) d_->render(r); }
private:
    DMDropdown* d_ = nullptr; // non-owning
};
