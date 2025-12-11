#include "color_range_widget.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "DockableCollapsible.hpp"
#include "FloatingDockableManager.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "dm_styles.hpp"
#include "draw_utils.hpp"
#include "font_cache.hpp"
#include "utils/input.hpp"
#include "utils/ranged_color.hpp"

namespace {
SDL_Rect make_rect(int x, int y, int w, int h) {
    return SDL_Rect{x, y, w, h};
}

int clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

}

class DMColorRangeWidget::Picker : public DockableCollapsible {
public:
    class ChannelWidget : public Widget {
    public:
        ChannelWidget(std::string label, DMRangeSlider* slider)
            : label_(std::move(label)), slider_(slider) {}

        void set_rect(const SDL_Rect& r) override {
            rect_ = r;
            layout();
        }

        const SDL_Rect& rect() const override { return rect_; }

        int height_for_width(int) const override {
            const int pad = DMSpacing::item_gap();
            const DMLabelStyle label_style = DMStyles::Label();
            SDL_Point label_size = DMFontCache::instance().measure_text(label_style, label_);
            const int slider_height = DMRangeSlider::height();
            const int content_height = std::max(slider_height, label_size.y);
            return pad * 2 + content_height;
        }

        bool handle_event(const SDL_Event& e) override {
            return slider_ ? slider_->handle_event(e) : false;
        }

        void render(SDL_Renderer* r) const override {
            if (!r) return;
            const DMLabelStyle label_style = DMStyles::Label();
            DMFontCache::instance().draw_text(r, label_style, label_, label_rect_.x, label_rect_.y);
            if (slider_) {
                slider_->render(r);
            }
        }

        bool wants_full_row() const override { return true; }

        void refresh_layout() { layout(); }

    private:
        void layout() {
            const int pad = DMSpacing::item_gap();
            const int gap = DMSpacing::small_gap();
            const DMLabelStyle label_style = DMStyles::Label();
            SDL_Point label_size = DMFontCache::instance().measure_text(label_style, label_);
            const int slider_height = DMRangeSlider::height();
            const int content_height = std::max(slider_height, label_size.y);

            const int label_x = rect_.x + pad;
            const int label_y = rect_.y + pad + std::max(0, (content_height - label_size.y) / 2);
            label_rect_ = make_rect(label_x, label_y, label_size.x, label_size.y);

            const int slider_x = label_rect_.x + label_rect_.w + gap;
            const int slider_y = rect_.y + pad + std::max(0, (content_height - slider_height) / 2);
            const int slider_w = std::max(0, rect_.w - (slider_x - rect_.x) - pad);
            if (slider_) {
                slider_->set_rect(make_rect(slider_x, slider_y, slider_w, slider_height));
            }

            rect_.h = pad * 2 + content_height;
        }

        std::string label_;
        DMRangeSlider* slider_ = nullptr;
        SDL_Rect rect_{0, 0, 0, 0};
        SDL_Rect label_rect_{0, 0, 0, 0};
};

    explicit Picker(DMColorRangeWidget& owner)
        : DockableCollapsible(owner.label() + " Picker", true, 48, 48),
          owner_(owner) {
        initialize_channels();

        sample_button_ = std::make_unique<DMButton>( "Select color from map", &DMStyles::AccentButton(), 0, DMButton::height());
        sample_button_widget_ = std::make_unique<ButtonWidget>(
            sample_button_.get(),
            [this]() { this->handle_sample_button(); });

        rebuild_rows();

        set_close_button_enabled(true);
        set_scroll_enabled(true);
        set_row_gap(DMSpacing::small_gap());
        set_padding(DMSpacing::item_gap());
        set_col_gap(DMSpacing::small_gap());
        set_visible_height(desired_content_height());
        set_floating_content_width(resolve_panel_width());
        set_visible(false);
        set_expanded(true);
    }

    ~Picker() override = default;

    void open(const SDL_Rect& anchor, const RangedColor& value) {
        anchor_ = anchor;
        value_ = utils::color::clamp_ranged_color(value);
        resolved_color_ = utils::color::resolve_ranged_color(value_);
        sync_sliders_from_value();

        reset_scroll();

        set_title(owner_.label() + " Picker");
        set_visible_height(desired_content_height());
        set_floating_content_width(resolve_panel_width());
        set_cell_width(std::max(0, resolve_panel_width() - DMSpacing::item_gap() * 2));

        DockableCollapsible::open();
        force_pointer_ready();
        FloatingDockableManager::instance().open_floating(
            owner_.label() + " Picker",
            this,
            {},
            "light_picker");
        position_near_anchor();
    }

    void close() {
        DockableCollapsible::close();
    }

    void update(const Input& input, int screen_w, int screen_h) override {
        DockableCollapsible::update(input, screen_w, screen_h);
    }

    bool handle_event(const SDL_Event& e) override {
        const RangedColor before = value_;
        bool used = DockableCollapsible::handle_event(e);
        const bool changed = before_changed(before);
        if (changed) {
            notify_parent();
        }
        return used || changed;
    }
    void set_value(const RangedColor& value) {
        value_ = utils::color::clamp_ranged_color(value);
        resolved_color_ = utils::color::resolve_ranged_color(value_);
        sync_sliders_from_value();
    }

    void render(SDL_Renderer* r) const override;

    bool is_visible() const { return DockableCollapsible::is_visible(); }

private:
    void initialize_channels() {
        auto create_slider = []() {
            auto slider = std::make_unique<DMRangeSlider>(0, 255, 0, 255);
            slider->set_defer_commit_until_unfocus(false);
            return slider;
};

        channels_[0].label = "R";
        channels_[1].label = "G";
        channels_[2].label = "B";
        channels_[3].label = "A";

        for (auto& channel : channels_) {
            channel.slider = create_slider();
            channel.widget = std::make_unique<ChannelWidget>(channel.label, channel.slider.get());
        }
    }

    void rebuild_rows() {
        DockableCollapsible::Rows rows;
        rows.reserve(channels_.size());
        for (auto& channel : channels_) {
            rows.push_back({ channel.widget.get() });
        }
        if (sample_button_widget_) {
            rows.push_back({ sample_button_widget_.get() });
        }
        set_rows(rows);
    }

    int desired_content_height() const {
        const int pad = DMSpacing::item_gap();
        const int gap = DMSpacing::small_gap();
        const int slider_height = DMRangeSlider::height();
        const int slider_count = static_cast<int>(channels_.size());
        int content_height = 0;
        if (slider_count > 0) {
            const int row_height = pad * 2 + slider_height;
            const int slider_area_height = slider_count * row_height + (slider_count - 1) * gap;
            content_height += slider_area_height;
            content_height += gap;
        }
        content_height += DMButton::height();
        return pad + content_height + pad;
    }

    int resolve_panel_width() const {
        const int pad = DMSpacing::item_gap();
        const int gap = DMSpacing::small_gap();
        int max_label_width = 0;
        const DMLabelStyle label_style = DMStyles::Label();
        for (const auto& channel : channels_) {
            SDL_Point size = DMFontCache::instance().measure_text(label_style, channel.label);
            max_label_width = std::max(max_label_width, size.x);
        }
        const int min_slider_width = 240;
        int slider_width = pad * 2 + max_label_width + gap + min_slider_width;
        return std::max(DockableCollapsible::kDefaultFloatingContentWidth, slider_width);
    }

    void position_near_anchor() {
        SDL_Rect usable = FloatingPanelLayoutManager::instance().usableRect();
        if (usable.w <= 0 || usable.h <= 0) {
            int x = anchor_.x;
            int y = anchor_.y + anchor_.h + DMSpacing::small_gap();
            set_position(x, y);
            return;
        }

        FloatingPanelLayoutManager::PanelInfo info;
        info.panel = this;
        info.preferred_width = resolve_panel_width();
        info.preferred_height =
            desired_content_height() + 2 * padding_ + (show_header_ ? DMButton::height() + DMSpacing::header_gap() : 0);

        FloatingPanelLayoutManager::SlidingParentInfo parent;
        parent.bounds = anchor_;
        parent.padding = DMSpacing::item_gap() * 2;
        parent.anchor_left = anchor_.x > usable.x + usable.w / 2;
        parent.align_top = true;

        SDL_Point pos = FloatingPanelLayoutManager::instance().positionFor(info, &parent);
        set_position(pos.x, pos.y);
    }

    bool before_changed(const RangedColor& before) {
        sync_value_from_sliders();
        return before.r.min != value_.r.min || before.r.max != value_.r.max ||
               before.g.min != value_.g.min || before.g.max != value_.g.max ||
               before.b.min != value_.b.min || before.b.max != value_.b.max ||
               before.a.min != value_.a.min || before.a.max != value_.a.max;
    }

    void notify_parent() {
        resolved_color_ = utils::color::resolve_ranged_color(value_);
        owner_.on_picker_value_changed(value_);
    }

    void handle_sample_button() {
        if (!owner_.request_sample_from_map()) {
            return;
        }
    }

    void sync_sliders_from_value() {
        auto set_slider = [](DMRangeSlider* slider, const utils::color::ChannelRange& channel) {
            if (!slider) return;
            slider->set_min_value(clamp_int(channel.min, 0, 255));
            slider->set_max_value(clamp_int(channel.max, 0, 255));
};

        set_slider(channels_[0].slider.get(), value_.r);
        set_slider(channels_[1].slider.get(), value_.g);
        set_slider(channels_[2].slider.get(), value_.b);
        set_slider(channels_[3].slider.get(), value_.a);

        for (auto& channel : channels_) {
            if (channel.widget) {
                channel.widget->refresh_layout();
            }
        }
    }

    void sync_value_from_sliders() {
        auto get_slider_values = [](DMRangeSlider* slider, utils::color::ChannelRange& channel) {
            if (!slider) return;
            channel.min = clamp_int(slider->min_value(), 0, 255);
            channel.max = clamp_int(slider->max_value(), 0, 255);
};

        get_slider_values(channels_[0].slider.get(), value_.r);
        get_slider_values(channels_[1].slider.get(), value_.g);
        get_slider_values(channels_[2].slider.get(), value_.b);
        get_slider_values(channels_[3].slider.get(), value_.a);
        value_ = utils::color::clamp_ranged_color(value_);
    }

    DMColorRangeWidget& owner_;
    SDL_Rect anchor_{0, 0, 0, 0};
    RangedColor value_{};
    SDL_Color resolved_color_{255, 255, 255, 255};
    struct ChannelEntry {
        std::string label;
        std::unique_ptr<DMRangeSlider> slider;
        std::unique_ptr<ChannelWidget> widget;
};
    std::array<ChannelEntry, 4> channels_{};
    std::unique_ptr<DMButton> sample_button_;
    std::unique_ptr<ButtonWidget> sample_button_widget_;
};

void DMColorRangeWidget::Picker::render(SDL_Renderer* r) const {
    if (!r) return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    bg.a = 255;
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &rect_);

    DockableCollapsible::render(r);
}

DMColorRangeWidget::DMColorRangeWidget(std::string label)
    : label_(std::move(label)) {
    value_.r = utils::color::ChannelRange{255, 255};
    value_.g = utils::color::ChannelRange{255, 255};
    value_.b = utils::color::ChannelRange{255, 255};
    value_.a = utils::color::ChannelRange{255, 255};
    resolved_color_ = utils::color::resolve_ranged_color(value_);
}

DMColorRangeWidget::~DMColorRangeWidget() {
    close_overlay();
}

void DMColorRangeWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    update_layout();
}

int DMColorRangeWidget::height_for_width(int) const {
    const DMLabelStyle label_style = DMStyles::Label();
    SDL_Point label_size = DMFontCache::instance().measure_text(label_style, label_);
    const int gap = DMSpacing::small_gap();
    const int content_height = 32;
    return label_size.y + gap + content_height;
}

bool DMColorRangeWidget::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        SDL_Point p{e.button.x, e.button.y};
        if (SDL_PointInRect(&p, &swatch_rect_)) {
            if (e.type == SDL_MOUSEBUTTONUP) {
                open_picker();
            }
            return true;
        }
    }
    return false;
}

void DMColorRangeWidget::render(SDL_Renderer* r) const {
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const DMLabelStyle label_style = DMStyles::Label();
    DMFontCache::instance().draw_text(r, label_style, label_, label_rect_.x, label_rect_.y);

    dm_draw::DrawBeveledRect( r, swatch_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), resolved_color_, DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    SDL_Color border = DMStyles::Border();
    dm_draw::DrawRoundedOutline(r, swatch_rect_, DMStyles::CornerRadius(), 1, border);
}

void DMColorRangeWidget::set_value(const RangedColor& value) {
    const RangedColor clamped = utils::color::clamp_ranged_color(value);
    if (clamped.r.min == value_.r.min && clamped.r.max == value_.r.max &&
        clamped.g.min == value_.g.min && clamped.g.max == value_.g.max &&
        clamped.b.min == value_.b.min && clamped.b.max == value_.b.max &&
        clamped.a.min == value_.a.min && clamped.a.max == value_.a.max) {
        return;
    }
    value_ = clamped;
    resolved_color_ = utils::color::resolve_ranged_color(value_);
    if (picker_) {
        picker_->set_value(value_);
    }
    if (on_value_changed_) {
        on_value_changed_(value_);
    }
}

void DMColorRangeWidget::set_on_value_changed(ValueChangedCallback cb) {
    on_value_changed_ = std::move(cb);
}

void DMColorRangeWidget::set_on_sample_requested(SampleRequestCallback cb) {
    on_sample_requested_ = std::move(cb);
}

void DMColorRangeWidget::set_label(std::string label) {
    label_ = std::move(label);
    update_layout();
}

bool DMColorRangeWidget::handle_overlay_event(const SDL_Event& e) {
    if (!picker_ || !picker_->is_visible()) {
        return false;
    }
    return picker_->handle_event(e);
}

void DMColorRangeWidget::render_overlay(SDL_Renderer* r) const {
    if (picker_ && picker_->is_visible()) {
        picker_->render(r);
    }
}

bool DMColorRangeWidget::overlay_visible() const {
    return picker_ && picker_->is_visible();
}

void DMColorRangeWidget::close_overlay() {
    if (picker_) {
        picker_->close();
    }
}

void DMColorRangeWidget::update_overlay(const Input& input, int screen_w, int screen_h) {
    if (picker_) {
        picker_->update(input, screen_w, screen_h);
    }
}

void DMColorRangeWidget::update_layout() {
    const int gap = DMSpacing::small_gap();
    const DMLabelStyle label_style = DMStyles::Label();
    SDL_Point label_size = DMFontCache::instance().measure_text(label_style, label_);
    label_rect_ = make_rect(rect_.x, rect_.y, rect_.w, label_size.y);
    const int swatch_height = 32;
    swatch_rect_ = make_rect(rect_.x, rect_.y + label_rect_.h + gap, rect_.w, swatch_height);
}

void DMColorRangeWidget::open_picker() {
    ensure_picker();
    if (picker_) {
        picker_->open(swatch_rect_, value_);
    }
}

void DMColorRangeWidget::ensure_picker() {
    if (!picker_) {
        picker_ = std::make_unique<Picker>(*this);
    }
}

void DMColorRangeWidget::on_picker_value_changed(const RangedColor& value) {
    value_ = value;
    resolved_color_ = utils::color::resolve_ranged_color(value_);
    if (on_value_changed_) {
        on_value_changed_(value_);
    }
}

bool DMColorRangeWidget::request_sample_from_map() {
    if (!on_sample_requested_) {
        return false;
    }
    const bool was_open = overlay_visible();
    if (was_open) {
        close_overlay();
    }
    reopen_picker_after_sample_ = was_open;

    auto apply = [this](SDL_Color color) {
        this->apply_sampled_color(color);
        if (reopen_picker_after_sample_) {
            reopen_picker_after_sample_ = false;
            this->open_picker();
        }
};

    auto cancel = [this]() {
        if (reopen_picker_after_sample_) {
            reopen_picker_after_sample_ = false;
            this->open_picker();
        }
};

    on_sample_requested_(value_, std::move(apply), std::move(cancel));
    return true;
}

void DMColorRangeWidget::apply_sampled_color(SDL_Color color) {
    SDL_Color clamped = utils::color::clamp_color(color);
    auto make_channel = [](Uint8 component) {
        utils::color::ChannelRange range{};
        range.min = range.max = static_cast<int>(component);
        return range;
};

    RangedColor ranged{};
    ranged.r = make_channel(clamped.r);
    ranged.g = make_channel(clamped.g);
    ranged.b = make_channel(clamped.b);
    ranged.a = make_channel(clamped.a);
    set_value(ranged);
}
