#pragma once

#include <functional>
#include <memory>
#include <string>

#include <SDL.h>

#include "widgets.hpp"
#include "utils/ranged_color.hpp"

class DMColorRangeWidget : public Widget {
public:
    using RangedColor = utils::color::RangedColor;
    using ValueChangedCallback = std::function<void(const RangedColor&)>;

    explicit DMColorRangeWidget(std::string label);
    ~DMColorRangeWidget() override;

    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;
    bool wants_full_row() const override { return true; }

    void set_value(const RangedColor& value);
    const RangedColor& value() const { return value_; }

    void set_label(std::string label);

    void set_on_value_changed(ValueChangedCallback cb);

    bool handle_overlay_event(const SDL_Event& e);
    void render_overlay(SDL_Renderer* r) const;
    bool overlay_visible() const;
    void close_overlay();

private:
    class Picker;

    void update_layout();
    void open_picker();
    void ensure_picker();
    void on_picker_value_changed(const RangedColor& value);

    std::string label_;
    SDL_Rect rect_{0, 0, 0, 0};
    SDL_Rect label_rect_{0, 0, 0, 0};
    SDL_Rect swatch_rect_{0, 0, 0, 0};
    RangedColor value_{};
    SDL_Color resolved_color_{255, 255, 255, 255};
    ValueChangedCallback on_value_changed_{};
    std::unique_ptr<Picker> picker_;
};

