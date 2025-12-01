#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <utility>

#include "dm_styles.hpp"
#include "shared/formatting.hpp"

struct DMWidgetTooltipState {
    bool enabled = false;
    std::string text{};
    bool icon_hovered = false;
    Uint32 hover_start_ms = 0;
};

SDL_Rect DMWidgetTooltipIconRect(const SDL_Rect& bounds);
bool DMWidgetTooltipHandleEvent(const SDL_Event& e, const SDL_Rect& bounds, DMWidgetTooltipState& state);
bool DMWidgetTooltipEnabled(const DMWidgetTooltipState& state);
bool DMWidgetTooltipShouldDisplay(const DMWidgetTooltipState& state, Uint32 now_ticks);
void DMWidgetTooltipRender(SDL_Renderer* renderer, const SDL_Rect& bounds, const DMWidgetTooltipState& state);
void DMWidgetTooltipResetHover(DMWidgetTooltipState& state);

bool DMWidgetsSliderScrollCaptured();
void DMWidgetsSetSliderScrollCapture(const void* owner, bool capture);

class DMButton {
public:
    DMButton(const std::string& text, const DMButtonStyle* style, int w, int h);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_text(const std::string& t);
    const std::string& text() const { return text_; }
    void set_style(const DMButtonStyle* style);
    void set_tooltip_state(DMWidgetTooltipState* state);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_hovered() const { return hovered_; }
    static int height() { return 28; }
    int preferred_width() const { return preferred_width_; }

private:
    void draw_label(SDL_Renderer* r, SDL_Color col) const;
    void update_preferred_width();
    SDL_Rect rect_{0,0,200,28};
    std::string text_;
    bool hovered_ = false;
    bool pressed_ = false;
    const DMButtonStyle* style_ = nullptr;
    int preferred_width_ = 0;
    DMWidgetTooltipState* tooltip_state_ = nullptr;
};

class DMTextBox {
public:
    DMTextBox(const std::string& label, const std::string& value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(const std::string& v);
    const std::string& value() const { return text_; }
    void set_label_text(const std::string& label);
    void reset_label_text();
    void set_label_color_override(const SDL_Color& color);
    void clear_label_color_override();
    void set_tooltip_state(DMWidgetTooltipState* state);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    bool is_editing() const { return editing_; }
    void start_editing();
    void stop_editing();

    int preferred_height(int width) const;
    static int height() { return 32; }
    int height_for_width(int w) const;
    void set_on_height_changed(std::function<void()> cb);
private:
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y, int max_width, const DMLabelStyle& ls) const;
    std::vector<std::string> wrap_lines(TTF_Font* f, const std::string& s, int max_width) const;
    int compute_label_height(int width) const;
    int compute_text_height(TTF_Font* f, int width) const;
    int compute_box_height(int width) const;
    bool update_geometry(bool notify_change);
    SDL_Rect box_rect() const;
    SDL_Rect label_rect() const;
    SDL_Rect rect_{0,0,200,32};
    SDL_Rect box_rect_{0,0,200,32};
    SDL_Rect label_rect_{0,0,0,0};
    int label_height_ = 0;
    std::string label_;
    std::string default_label_;
    std::string text_;
    std::optional<SDL_Color> label_color_override_;
    bool hovered_ = false;
    bool editing_ = false;
    size_t caret_pos_ = 0;
    std::function<void()> on_height_changed_{};
    DMWidgetTooltipState* tooltip_state_ = nullptr;
};

class DMCheckbox {
public:
    DMCheckbox(const std::string& label, bool value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(bool v) { value_ = v; }
    bool value() const { return value_; }
    void set_tooltip_state(DMWidgetTooltipState* state);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    int preferred_width() const;
    static int height() { return 28; }
private:
    void draw_label(SDL_Renderer* r) const;
    SDL_Rect rect_{0,0,200,28};
    std::string label_;
    bool value_ = false;
    bool hovered_ = false;
    DMWidgetTooltipState* tooltip_state_ = nullptr;
};

class DMNumericStepper {
public:
    DMNumericStepper(const std::string& label, int min_value, int max_value, int value);
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(int v);
    int value() const { return value_; }
    void set_range(int min_value, int max_value);
    void set_step(int step);
    void set_on_change(std::function<void(int)> cb) { on_change_ = std::move(cb); }
    void set_tooltip_state(DMWidgetTooltipState* state);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    int preferred_height(int width) const;
    static int height();

private:
    int clamp_value(int v) const;
    void update_layout();
    void update_hover(SDL_Point p);
    bool apply_delta(int delta);
    void commit_value(int new_value);
    int compute_label_height(int width) const;
    SDL_Rect decrement_rect() const { return dec_rect_; }
    SDL_Rect increment_rect() const { return inc_rect_; }
    SDL_Rect value_rect() const { return value_rect_; }

    SDL_Rect rect_{0,0,200,32};
    SDL_Rect label_rect_{0,0,0,0};
    SDL_Rect control_rect_{0,0,0,0};
    SDL_Rect dec_rect_{0,0,0,0};
    SDL_Rect inc_rect_{0,0,0,0};
    SDL_Rect value_rect_{0,0,0,0};
    int label_height_ = 0;
    std::string label_;
    int min_value_ = 0;
    int max_value_ = 0;
    int value_ = 0;
    int step_ = 1;
    bool hovered_dec_ = false;
    bool hovered_inc_ = false;
    bool hovered_value_ = false;
    bool pressed_dec_ = false;
    bool pressed_inc_ = false;
    std::function<void(int)> on_change_{};
    DMWidgetTooltipState* tooltip_state_ = nullptr;
};

class DMTextBox;

class DMSlider {
public:
    DMSlider(const std::string& label, int min_val, int max_val, int value);
    ~DMSlider();
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_value(int v);
    int value() const { return value_; }
    int displayed_value() const;
    void set_defer_commit_until_unfocus(bool defer) {
        if (defer_commit_until_unfocus_ == defer) {
            return;
        }
        defer_commit_until_unfocus_ = defer;
        if (!defer_commit_until_unfocus_) {
            commit_pending_value();
        }
        pending_value_ = value_;
        has_pending_value_ = false;
    }
    bool defer_commit_until_unfocus() const { return defer_commit_until_unfocus_; }
    using SliderValueFormatter = std::function<std::string_view(int, std::array<char, dev_mode::kSliderFormatBufferSize>&)>;
    void set_value_formatter(SliderValueFormatter formatter);
    void set_value_parser(std::function<std::optional<int>(const std::string&)> parser);
    void set_tooltip_state(DMWidgetTooltipState* state);
    void set_on_value_changed(std::function<void(int)> callback);
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }
    int track_center_y() const;
    SDL_Rect interaction_rect() const;
    bool is_dragging() const { return dragging_; }
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    int preferred_height(int width) const;
    static int height();
private:
    int clamp_value(int v) const;
    bool apply_interaction_value(int v);
    bool commit_pending_value();
    void notify_value_changed();
    int display_value() const;
    int label_space() const;
    SDL_Rect content_rect() const;
    SDL_Rect value_rect() const;
    SDL_Rect track_rect() const;
    SDL_Rect knob_rect() const;
    int value_for_x(int x) const;
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const;
    int compute_label_height(int width) const;
    const std::string& format_value(int v) const;
    std::optional<int> parse_value(const std::string& text) const;
    SDL_Rect rect_{0,0,200,40};
    SDL_Rect content_rect_{0,0,200,40};
    SDL_Rect label_rect_{0,0,0,0};
    SDL_Rect value_rect_{0,0,0,0};
    int label_height_ = 0;
    std::string label_;
    int min_ = 0;
    int max_ = 100;
    int value_ = 0;
    int pending_value_ = 0;
    bool has_pending_value_ = false;
    bool defer_commit_until_unfocus_ = false;
    bool knob_hovered_ = false;
    bool hovered_ = false;
    bool focused_ = false;
    bool dragging_ = false;
    std::unique_ptr<DMTextBox> edit_box_;
    mutable std::array<char, dev_mode::kSliderFormatBufferSize> value_buffer_{};
    mutable std::string formatted_value_cache_{};
    SliderValueFormatter value_formatter_{};
    std::function<std::optional<int>(const std::string&)> value_parser_{};
    DMWidgetTooltipState* tooltip_state_ = nullptr;
    bool enabled_ = true;
    std::function<void(int)> value_changed_callback_{};
    int last_notified_value_ = 0;
};

class DMRangeSlider {
public:
    DMRangeSlider(int min_val, int max_val, int min_value, int max_value);
    ~DMRangeSlider();
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    void set_min_value(int v);
    void set_max_value(int v);
    int min_value() const { return min_value_; }
    int max_value() const { return max_value_; }
    void set_defer_commit_until_unfocus(bool defer) {
        if (defer_commit_until_unfocus_ == defer) {
            return;
        }
        defer_commit_until_unfocus_ = defer;
        if (!defer_commit_until_unfocus_) {
            commit_pending_values();
        }
        pending_min_value_ = min_value_;
        pending_max_value_ = max_value_;
        pending_dirty_ = false;
    }
    bool defer_commit_until_unfocus() const { return defer_commit_until_unfocus_; }
    bool has_pending_values() const { return pending_dirty_; }
    void set_tooltip_state(DMWidgetTooltipState* state);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    static int height();
private:
    int clamp_min_value(int v) const;
    int clamp_max_value(int v) const;
    bool apply_min_interaction(int v);
    bool apply_max_interaction(int v);
    bool commit_pending_values();
    int display_min_value() const;
    int display_max_value() const;
    SDL_Rect track_rect() const;
    SDL_Rect min_knob_rect() const;
    SDL_Rect max_knob_rect() const;
    int value_for_x(int x) const;
    void draw_text(SDL_Renderer* r, const std::string& s, int x, int y) const;
    SDL_Rect content_rect() const;
    SDL_Rect rect_{0,0,200,40};
    SDL_Rect content_rect_{0,0,200,40};
    SDL_Rect min_value_rect_{0,0,0,0};
    SDL_Rect max_value_rect_{0,0,0,0};
    int min_ = 0;
    int max_ = 100;
    int min_value_ = 0;
    int max_value_ = 100;
    int pending_min_value_ = 0;
    int pending_max_value_ = 0;
    bool pending_dirty_ = false;
    bool defer_commit_until_unfocus_ = false;
    bool min_hovered_ = false;
    bool max_hovered_ = false;
    bool hovered_ = false;
    bool focused_ = false;
    bool dragging_min_ = false;
    bool dragging_max_ = false;
    bool wheel_target_max_ = false;
    std::unique_ptr<DMTextBox> edit_min_;
    std::unique_ptr<DMTextBox> edit_max_;
    DMWidgetTooltipState* tooltip_state_ = nullptr;
};

class DMDropdown {
public:
    DMDropdown(const std::string& label, const std::vector<std::string>& options, int idx = 0);
    ~DMDropdown();
    void set_rect(const SDL_Rect& r);
    const SDL_Rect& rect() const { return rect_; }
    int selected() const { return index_; }
    void set_selected(int idx);
    void set_on_selection_changed(std::function<void(int)> cb) { on_selection_changed_ = std::move(cb); }
    bool focused() const { return focused_; }
    int pending_index() const { return has_pending_index_ ? pending_index_ : index_; }
    void set_tooltip_state(DMWidgetTooltipState* state);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;
    void render_options(SDL_Renderer* r) const;
    bool expanded() const { return focused_; }
    int preferred_height(int width) const;
    static int height();

    static DMDropdown* active_dropdown();

    static void render_active_options(SDL_Renderer* r);
private:
    struct OptionEntry;

    bool build_option_entries(std::vector<OptionEntry>& entries) const;
    int clamp_index(int idx) const;
    bool commit_pending_selection();
    void begin_focus();
    int label_space() const;
    int compute_label_height(int width) const;
    SDL_Rect box_rect() const;
    SDL_Rect label_rect() const;
    SDL_Rect rect_{0,0,200,32};
    SDL_Rect box_rect_{0,0,200,32};
    SDL_Rect label_rect_{0,0,0,0};
    int label_height_ = 0;
    std::string label_;
    std::vector<std::string> options_;
    int index_ = 0;
    bool hovered_ = false;
    bool focused_ = false;
    int pending_index_ = 0;
    bool has_pending_index_ = false;
    int hovered_option_index_ = -1;
    static DMDropdown* active_;
    DMWidgetTooltipState* tooltip_state_ = nullptr;
    std::function<void(int)> on_selection_changed_{};
};

class Widget {
public:
    virtual ~Widget() = default;
    virtual void set_rect(const SDL_Rect& r) = 0;
    virtual const SDL_Rect& rect() const = 0;
    virtual int height_for_width(int w) const = 0;
    virtual bool handle_event(const SDL_Event& e) = 0;
    virtual void render(SDL_Renderer* r) const = 0;
    virtual bool wants_full_row() const { return false; }
    void set_layout_dirty_callback(std::function<void()> cb) { layout_dirty_callback_ = std::move(cb); }
    void clear_layout_dirty_flags() const { layout_dirty_ = false; geometry_dirty_ = false; }
    bool needs_layout() const { return layout_dirty_; }
    bool needs_geometry() const { return geometry_dirty_; }
    void set_tooltip_text(std::string text);
    void set_tooltip_enabled(bool enabled);
    void set_tooltip(std::string text) {
        set_tooltip_text(std::move(text));
        set_tooltip_enabled(true);
    }
    bool tooltip_enabled() const { return DMWidgetTooltipEnabled(tooltip_state_); }
    const std::string& tooltip_text() const { return tooltip_state_.text; }
protected:
    void request_layout() const {
        layout_dirty_ = true;
        geometry_dirty_ = true;
        if (layout_dirty_callback_) layout_dirty_callback_();
    }
    void request_geometry_update() const {
        geometry_dirty_ = true;
        if (layout_dirty_callback_) layout_dirty_callback_();
    }
    DMWidgetTooltipState* tooltip_state() { return &tooltip_state_; }
    const DMWidgetTooltipState* tooltip_state() const { return &tooltip_state_; }
private:
    mutable std::function<void()> layout_dirty_callback_{};
    mutable bool layout_dirty_ = false;
    mutable bool geometry_dirty_ = false;
    DMWidgetTooltipState tooltip_state_{};
};

class ButtonWidget : public Widget {
public:
    explicit ButtonWidget(DMButton* b, std::function<void()> on_click = {})
        : b_(b), on_click_(std::move(on_click)) {
        if (b_) b_->set_tooltip_state(this->tooltip_state());
    }
    ~ButtonWidget() override {
        if (b_) b_->set_tooltip_state(nullptr);
    }
    void set_rect(const SDL_Rect& r) override { if (b_) b_->set_rect(r); }
    const SDL_Rect& rect() const override { return b_->rect(); }
    int height_for_width(int ) const override { return DMButton::height(); }
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
    DMButton* b_ = nullptr;
    std::function<void()> on_click_{};
};

class TextBoxWidget : public Widget {
public:
    explicit TextBoxWidget(DMTextBox* t, bool full_row = false)
        : t_(t), full_row_(full_row) {
        if (t_) {
            t_->set_on_height_changed([this]() { this->request_layout(); });
            t_->set_tooltip_state(this->tooltip_state());
        }
    }
    ~TextBoxWidget() override {
        if (t_) {
            t_->set_on_height_changed(nullptr);
            t_->set_tooltip_state(nullptr);
        }
    }
    void set_rect(const SDL_Rect& r) override { if (t_) t_->set_rect(r); }
    const SDL_Rect& rect() const override { return t_->rect(); }
    int height_for_width(int w) const override { return t_ ? t_->preferred_height(w) : DMTextBox::height(); }
    bool handle_event(const SDL_Event& e) override { return t_ ? t_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (t_) t_->render(r); }
    bool wants_full_row() const override { return full_row_; }
private:
    DMTextBox* t_ = nullptr;
    bool full_row_ = false;
};

class ReadOnlyTextBoxWidget : public Widget {
public:
    ReadOnlyTextBoxWidget(const std::string& label,
                          const std::string& value,
                          bool full_row = true)
        : box_(std::make_unique<DMTextBox>(label, value)),
          full_row_(full_row) {}

    void set_value(const std::string& value) {
        if (box_) {
            box_->set_value(value);
        }
    }

    void set_rect(const SDL_Rect& r) override {
        if (box_) {
            box_->set_rect(r);
        } else {
            rect_cache_ = r;
        }
    }

    const SDL_Rect& rect() const override {
        if (box_) {
            return box_->rect();
        }
        return rect_cache_;
    }

    int height_for_width(int w) const override {
        return box_ ? box_->preferred_height(w) : DMTextBox::height();
    }

    bool handle_event(const SDL_Event&) override {
        return false;
    }

    void render(SDL_Renderer* r) const override {
        if (box_) {
            box_->render(r);
        }
    }

    bool wants_full_row() const override { return full_row_; }

private:
    std::unique_ptr<DMTextBox> box_;
    mutable SDL_Rect rect_cache_{0, 0, 0, DMTextBox::height()};
    bool full_row_ = true;
};

class CheckboxWidget : public Widget {
public:
    explicit CheckboxWidget(DMCheckbox* c) : c_(c) {
        if (c_) c_->set_tooltip_state(this->tooltip_state());
    }
    ~CheckboxWidget() override {
        if (c_) c_->set_tooltip_state(nullptr);
    }
    void set_rect(const SDL_Rect& r) override { if (c_) c_->set_rect(r); }
    const SDL_Rect& rect() const override { return c_->rect(); }
    int height_for_width(int ) const override { return DMCheckbox::height(); }
    bool handle_event(const SDL_Event& e) override { return c_ ? c_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (c_) c_->render(r); }
private:
    DMCheckbox* c_ = nullptr;
};

class StepperWidget : public Widget {
public:
    explicit StepperWidget(DMNumericStepper* s) : s_(s) {
        if (s_) s_->set_tooltip_state(this->tooltip_state());
    }
    ~StepperWidget() override {
        if (s_) s_->set_tooltip_state(nullptr);
    }
    void set_rect(const SDL_Rect& r) override {
        rect_cache_ = r;
        if (s_) s_->set_rect(r);
    }
    const SDL_Rect& rect() const override {
        if (s_) {
            return s_->rect();
        }
        return rect_cache_;
    }
    int height_for_width(int w) const override { return s_ ? s_->preferred_height(w) : DMNumericStepper::height(); }
    bool handle_event(const SDL_Event& e) override { return s_ ? s_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (s_) s_->render(r); }
    bool wants_full_row() const override { return true; }
private:
    DMNumericStepper* s_ = nullptr;
    SDL_Rect rect_cache_{0, 0, 0, DMNumericStepper::height()};
};

class SliderWidget : public Widget {
public:
    explicit SliderWidget(DMSlider* s) : s_(s) {
        if (s_) s_->set_tooltip_state(this->tooltip_state());
    }
    ~SliderWidget() override {
        if (s_) s_->set_tooltip_state(nullptr);
    }
    void set_rect(const SDL_Rect& r) override { if (s_) s_->set_rect(r); }
    const SDL_Rect& rect() const override { return s_->rect(); }
    int height_for_width(int w) const override { return s_ ? s_->preferred_height(w) : DMSlider::height(); }
    bool handle_event(const SDL_Event& e) override { return s_ ? s_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (s_) s_->render(r); }
    bool wants_full_row() const override { return true; }
private:
    DMSlider* s_ = nullptr;
};

class RangeSliderWidget : public Widget {
public:
    explicit RangeSliderWidget(DMRangeSlider* s) : s_(s) {
        if (s_) s_->set_tooltip_state(this->tooltip_state());
    }
    ~RangeSliderWidget() override {
        if (s_) s_->set_tooltip_state(nullptr);
    }
    void set_rect(const SDL_Rect& r) override { if (s_) s_->set_rect(r); }
    const SDL_Rect& rect() const override { return s_->rect(); }
    int height_for_width(int ) const override { return DMRangeSlider::height(); }
    bool handle_event(const SDL_Event& e) override { return s_ ? s_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (s_) s_->render(r); }
    bool wants_full_row() const override { return true; }
private:
    DMRangeSlider* s_ = nullptr;
};

class DropdownWidget : public Widget {
public:
    explicit DropdownWidget(DMDropdown* d) : d_(d) {
        if (d_) d_->set_tooltip_state(this->tooltip_state());
    }
    ~DropdownWidget() override {
        if (d_) d_->set_tooltip_state(nullptr);
    }
    void set_rect(const SDL_Rect& r) override {
        rect_cache_ = r;
        if (d_) d_->set_rect(r);
    }
    const SDL_Rect& rect() const override {
        if (d_) {
            return d_->rect();
        }
        return rect_cache_;
    }
    int height_for_width(int w) const override { return d_ ? d_->preferred_height(w) : DMDropdown::height(); }
    bool handle_event(const SDL_Event& e) override { return d_ ? d_->handle_event(e) : false; }
    void render(SDL_Renderer* r) const override { if (d_) d_->render(r); }
private:
    DMDropdown* d_ = nullptr;
    SDL_Rect rect_cache_{0, 0, 0, 0};
};
