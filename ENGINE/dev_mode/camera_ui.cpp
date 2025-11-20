#include "camera_ui.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>
#include <sstream>
#include <SDL_image.h>

#include "core/AssetsManager.hpp"
#include "dev_mode/depth_cue_settings.hpp"
#include "dev_mode/dm_icons.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/float_slider_widget.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "dev_mode/widgets.hpp"
#include "render/camera.hpp"
#include "utils/input.hpp"

namespace {
    constexpr float kMinTau = 1e-4f;
    constexpr float kPi     = 3.14159265358979323846f;

    float rate_from_tau(float tau) {
        if (!std::isfinite(tau) || tau <= kMinTau) {
            return 0.0f;
        }
        return 1.0f / tau;
    }

    float tau_from_rate(float rate) {
        if (!std::isfinite(rate) || rate <= kMinTau) {
            return 0.0f;
        }
        return 1.0f / rate;
    }

    float interpolate_height_for_ui(const camera::RealismSettings& settings, float zoom) {
        const float low_zoom  = std::max(0.0001f, settings.zoom_low);
        const float high_zoom = std::max(low_zoom + 0.0001f, settings.zoom_high);
        const float low_h     = std::max(1.0f, settings.height_low_px);
        const float high_h    = std::max(low_h, settings.height_high_px);
        float t = (zoom - low_zoom) / std::max(0.0001f, high_zoom - low_zoom);
        t = std::clamp(t, 0.0f, 1.0f);
        return low_h + (high_h - low_h) * t;
    }

    int method_to_index(TransformSmoothingMethod method) {
        switch (method) {
        case TransformSmoothingMethod::Lerp:
            return 0;
        case TransformSmoothingMethod::CriticallyDampedSpring:
        default:
            return 1;
        }
    }

    TransformSmoothingMethod method_from_index(int idx) {
        return (idx == 0) ? TransformSmoothingMethod::Lerp : TransformSmoothingMethod::CriticallyDampedSpring;
    }
}

class SpacerWidget : public Widget {
public:
    explicit SpacerWidget(int height)
        : height_(std::max(0, height)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return height_; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer*) const override {}
    bool wants_full_row() const override { return true; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    int height_ = 0;
};

class GroupLabelWidget : public Widget {
public:
    explicit GroupLabelWidget(std::string text)
        : text_(std::move(text)) {
        style_ = DMStyles::Label();
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return DMCheckbox::height(); }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const int text_y = rect_.y + std::max(0, (DMCheckbox::height() - style_.font_size) / 2);
        DrawLabelText(renderer, text_, rect_.x, text_y, style_);
    }
    bool wants_full_row() const override { return true; }
private:
    std::string text_{};
    DMLabelStyle style_{};
    SDL_Rect rect_{0,0,0,DMCheckbox::height()};
};

struct CameraDepthViewValues {
    float zoom_low         = 1.0f;
    float zoom_high        = 2.0f;
    float height_low_px    = 320.0f;
    float height_high_px   = 960.0f;
    float current_zoom     = 1.0f;
    float current_height   = 320.0f;
    float pitch_degrees    = 0.0f;
    float depth_offset_px  = 240.0f;
};

class CameraSideViewWidget : public Widget {
public:
    using ChangeCallback = std::function<void(const CameraDepthViewValues&)>;
    explicit CameraSideViewWidget(ChangeCallback cb) : on_change_(std::move(cb)) {}

    ~CameraSideViewWidget() override {
        if (subject_texture_) SDL_DestroyTexture(subject_texture_);
        if (camera_texture_) SDL_DestroyTexture(camera_texture_);
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return preferred_height_; }
    bool handle_event(const SDL_Event& e) override {
        update_layout_cache();
        if (!render_rect_valid_) {
            hover_target_ = DragTarget::None;
            return false;
        }

        const SDL_Point p = event_point(e);
        if (dragging_ == DragTarget::None) {
            hover_target_ = detect_hover_target(p);
        }
        const bool mouse_inside = (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN ||
                                   e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL) &&
                                  point_in_rect(p, rect_);

        if (low_height_slider_ && slider_event_allowed(*low_height_slider_, e, p)) {
            const int before = low_height_slider_->displayed_value();
            if (low_height_slider_->handle_event(e)) {
                const int after = low_height_slider_->displayed_value();
                if (after != before) {
                    apply_height_from_slider(*low_height_slider_, true);
                }
                return true;
            }
        }
        if (high_height_slider_ && slider_event_allowed(*high_height_slider_, e, p)) {
            const int before = high_height_slider_->displayed_value();
            if (high_height_slider_->handle_event(e)) {
                const int after = high_height_slider_->displayed_value();
                if (after != before) {
                    apply_height_from_slider(*high_height_slider_, false);
                }
                return true;
            }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (hit_line(low_line_hitbox_, e.button.x, e.button.y)) {
                dragging_ = DragTarget::LowLine;
                hover_target_ = DragTarget::None;
                update_anchor_from_pointer(e.button.y, true);
                return true;
            }
            if (hit_line(high_line_hitbox_, e.button.x, e.button.y)) {
                dragging_ = DragTarget::HighLine;
                hover_target_ = DragTarget::None;
                update_anchor_from_pointer(e.button.y, false);
                return true;
            }
            if (hit_depth_handle(p)) {
                dragging_ = DragTarget::DepthOffset;
                hover_target_ = DragTarget::None;
                update_depth_offset_from_pointer(e.button.x);
                return true;
            }
        }
        if (e.type == SDL_MOUSEMOTION && dragging_ != DragTarget::None) {
            if (dragging_ == DragTarget::DepthOffset) {
                update_depth_offset_from_pointer(e.motion.x);
            } else if (dragging_ == DragTarget::LowLine) {
                update_anchor_from_pointer(e.motion.y, true);
            } else if (dragging_ == DragTarget::HighLine) {
                update_anchor_from_pointer(e.motion.y, false);
            }
            return true;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            dragging_ = DragTarget::None;
            hover_target_ = detect_hover_target(p);
            if (mouse_inside) return true;
        }

        if (mouse_inside) {
            return true;
        }
        return false;
    }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        if (rect_.w <= 0 || rect_.h <= 0) return;
        render_rect_valid_ = false;
        update_layout_cache(renderer);
        if (!render_rect_valid_) return;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 18, 22, 30, 255);
        SDL_RenderFillRect(renderer, &rect_);
        SDL_SetRenderDrawColor(renderer, 34, 49, 68, 120);
        SDL_Rect top_glow{rect_.x, rect_.y, rect_.w, std::max(12, rect_.h / 4)};
        SDL_RenderFillRect(renderer, &top_glow);
        SDL_SetRenderDrawColor(renderer, 10, 12, 18, 160);
        SDL_Rect bottom_shade{rect_.x, rect_.y + rect_.h / 2, rect_.w, rect_.h / 2};
        SDL_RenderFillRect(renderer, &bottom_shade);
        SDL_SetRenderDrawColor(renderer, 64, 82, 102, 230);
        SDL_RenderDrawRect(renderer, &rect_);
        SDL_Rect inset{rect_.x + 1, rect_.y + 1, rect_.w - 2, rect_.h - 2};
        SDL_SetRenderDrawColor(renderer, 18, 26, 34, 120);
        SDL_RenderDrawRect(renderer, &inset);

        SDL_Rect content_bg{layout_.content.x, layout_.content.y, layout_.content.w, layout_.content.h};
        SDL_SetRenderDrawColor(renderer, 24, 32, 42, 80);
        SDL_RenderFillRect(renderer, &content_bg);

        // Draw the subject first so the sliders and guides sit over it.
        draw_icon(renderer, subject_texture_, subject_texture_failed_, layout_.subject_rect, "Subject");

        SDL_SetRenderDrawColor(renderer, 78, 94, 116, 220);
        SDL_RenderDrawLine(renderer, layout_.content.x, layout_.ground_y, layout_.content.x + layout_.content.w, layout_.ground_y);
        SDL_Rect ground_thick{layout_.content.x, layout_.ground_y, layout_.content.w, 2};
        SDL_RenderFillRect(renderer, &ground_thick);

        SDL_SetRenderDrawColor(renderer, 112, 186, 255, 235);
        SDL_RenderDrawLine(renderer, layout_.content.x, layout_.low_y, layout_.content.x + layout_.content.w, layout_.low_y);
        SDL_SetRenderDrawColor(renderer, 112, 186, 255, 48);
        SDL_RenderFillRect(renderer, &low_line_hitbox_);
        if (should_highlight(DragTarget::LowLine)) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 190);
            for (int dy = -1; dy <= 1; ++dy) {
                SDL_RenderDrawLine(renderer, layout_.content.x, layout_.low_y + dy,
                                   layout_.content.x + layout_.content.w, layout_.low_y + dy);
            }
        }

        SDL_SetRenderDrawColor(renderer, 196, 148, 255, 235);
        SDL_RenderDrawLine(renderer, layout_.content.x, layout_.high_y, layout_.content.x + layout_.content.w, layout_.high_y);
        SDL_SetRenderDrawColor(renderer, 196, 148, 255, 40);
        SDL_RenderFillRect(renderer, &high_line_hitbox_);
        if (should_highlight(DragTarget::HighLine)) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 190);
            for (int dy = -1; dy <= 1; ++dy) {
                SDL_RenderDrawLine(renderer, layout_.content.x, layout_.high_y + dy,
                                   layout_.content.x + layout_.content.w, layout_.high_y + dy);
            }
        }

        SDL_SetRenderDrawColor(renderer, 255, 210, 120, 200);
        for (int y = layout_.camera_y; y <= layout_.ground_y; y += 6) {
            SDL_RenderDrawLine(renderer, layout_.camera_x, y, layout_.camera_x, std::min(y + 3, layout_.ground_y));
        }
        if (should_highlight(DragTarget::DepthOffset)) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 190);
            for (int dx = -1; dx <= 1; ++dx) {
                SDL_RenderDrawLine(renderer, layout_.camera_x + dx, layout_.camera_y,
                                   layout_.camera_x + dx, layout_.ground_y);
            }
        }

        DMLabelStyle label_style = DMStyles::Label();
        label_style.font_size = std::max(10, label_style.font_size - 2);
        char buffer[128] = {0};
        std::snprintf(buffer, sizeof(buffer), "Height %.0f px", values_.current_height);
        DrawLabelText(renderer, buffer, layout_.camera_x + 8, (layout_.camera_y + layout_.ground_y) / 2 - label_style.font_size / 2, label_style);
        std::snprintf(buffer, sizeof(buffer), "Offset %.0f px", values_.depth_offset_px);
        DrawLabelText(renderer, buffer, layout_.camera_x - 24, layout_.ground_y - label_style.font_size - 6, label_style);

        const float clamped_pitch = std::clamp(values_.pitch_degrees,
                                              camera::kMinPitchDegrees,
                                              camera::kMaxPitchDegrees);
        std::snprintf(buffer, sizeof(buffer), "Pitch %.1f deg (%.0f-%.0f)",
                      clamped_pitch,
                      camera::kMinPitchDegrees,
                      camera::kMaxPitchDegrees);
        DrawLabelText(renderer, buffer, layout_.camera_x + 8, layout_.camera_y - layout_.icon_size - 6, label_style);

        DrawLabelText(renderer, "Low anchor", layout_.content.x + 6, layout_.low_y - label_style.font_size - 2, label_style);
        std::snprintf(buffer, sizeof(buffer), "Zoom %.2f", values_.zoom_low);
        DrawLabelText(renderer, buffer, layout_.content.x + 6, layout_.low_y + 6, label_style);
        DrawLabelText(renderer, "High anchor", layout_.content.x + 6, layout_.high_y - label_style.font_size - 2, label_style);
        std::snprintf(buffer, sizeof(buffer), "Zoom %.2f", values_.zoom_high);
        DrawLabelText(renderer, buffer, layout_.content.x + 6, layout_.high_y + 6, label_style);

        SDL_SetRenderDrawColor(renderer, 255, 210, 120, 180);
        SDL_RenderFillRect(renderer, &depth_handle_rect_);
        SDL_SetRenderDrawColor(renderer, 60, 50, 30, 255);
        SDL_RenderDrawRect(renderer, &depth_handle_rect_);
        if (should_highlight(DragTarget::DepthOffset)) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
            SDL_Rect handle_highlight = depth_handle_rect_;
            handle_highlight.x -= 2;
            handle_highlight.y -= 2;
            handle_highlight.w += 4;
            handle_highlight.h += 4;
            SDL_RenderDrawRect(renderer, &handle_highlight);
        }

        const float pitch_radians = clamped_pitch * (kPi / 180.0f);
        const int line_length = std::max(layout_.icon_size * 2, layout_.content.w / 4);
        const int pitch_dx = line_length;
        const int pitch_dy = static_cast<int>(std::round(std::tan(pitch_radians) * static_cast<float>(line_length) * 0.45f));
        SDL_SetRenderDrawColor(renderer, 154, 203, 255, 200);
        SDL_RenderDrawLine(renderer, layout_.camera_x, layout_.camera_y, layout_.camera_x + pitch_dx, layout_.camera_y - pitch_dy);

        draw_icon(renderer, camera_texture_, camera_texture_failed_, layout_.camera_rect, "Camera", clamped_pitch);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 26);
        SDL_RenderDrawLine(renderer, layout_.content.x, layout_.camera_y, layout_.camera_x, layout_.camera_y);

        if (low_height_slider_) low_height_slider_->render(renderer);
        if (high_height_slider_) high_height_slider_->render(renderer);
    }
    bool wants_full_row() const override { return true; }

    void set_values(const CameraDepthViewValues& v) {
        values_ = v;
        enforce_height_constraints();
        enforce_zoom_constraints();
        enforce_pitch_constraints();
        sync_sliders();
    }
    CameraDepthViewValues values() const { return values_; }
    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }
    void set_preferred_height(int h) { preferred_height_ = std::max(160, h); }

private:
    struct LayoutState {
        SDL_Rect content{0,0,0,0};
        int ground_y = 0;
        int zoom_axis_top = 0;
        int zoom_axis_bottom = 0;
        int low_y = 0;
        int high_y = 0;
        int camera_x = 0;
        int camera_y = 0;
        int icon_size = 0;
        SDL_Rect subject_rect{0,0,0,0};
        SDL_Rect camera_rect{0,0,0,0};
        float pixels_per_unit = 1.0f;
        int offset_range_x = 0;
        int offset_range_w = 1;
    };

    enum class DragTarget { None, LowLine, HighLine, DepthOffset };

    void notify() { if (on_change_) on_change_(values_); }

    bool hit_line(const SDL_Rect& rect, int px, int py) const {
        return px >= rect.x && px <= rect.x + rect.w && py >= rect.y && py <= rect.y + rect.h;
    }

    static SDL_Point event_point(const SDL_Event& e) {
        SDL_Point p{0,0};
        switch (e.type) {
        case SDL_MOUSEMOTION: p = SDL_Point{e.motion.x, e.motion.y}; break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: p = SDL_Point{e.button.x, e.button.y}; break;
        case SDL_MOUSEWHEEL: SDL_GetMouseState(&p.x, &p.y); break;
        default: break;
        }
        return p;
    }

    static bool point_in_rect(SDL_Point p, const SDL_Rect& r) {
        return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
    }

    static bool is_pointer_event(const SDL_Event& e) {
        switch (e.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            return true;
        default:
            return false;
        }
    }

    bool slider_event_allowed(const DMSlider& slider, const SDL_Event& e, SDL_Point point) const {
        if (!is_pointer_event(e)) {
            return true;
        }
        if (slider.is_dragging()) {
            return true;
        }
        const SDL_Rect hit = slider.interaction_rect();
        if (hit.w <= 0 || hit.h <= 0) {
            return false;
        }
        return point_in_rect(point, hit);
    }

    void apply_height_from_slider(const DMSlider& slider, bool low_line) {
        const float parsed = static_cast<float>(slider.displayed_value());
        if (low_line) {
            values_.height_low_px = std::clamp(parsed, 0.0f, values_.height_high_px);
        } else {
            values_.height_high_px = std::max(parsed, values_.height_low_px);
        }
        enforce_height_constraints();
        sync_sliders();
        recompute_current_height();
        notify();
    }

    int compute_height_slider_max() const {
        const float max_height = std::max({values_.height_low_px, values_.height_high_px, values_.current_height, 600.0f});
        const float padded = std::max(600.0f, max_height * 1.35f);
        const int quantized = static_cast<int>(std::ceil(padded / 100.0f) * 100.0f);
        return std::clamp(quantized, 800, kMaxHeightSliderPx);
    }

    void ensure_height_sliders() const {
        const int desired_max = compute_height_slider_max();
        const bool needs_rebuild = desired_max != height_slider_range_px_ || !low_height_slider_ || !high_height_slider_;
        if (needs_rebuild) {
            height_slider_range_px_ = desired_max;
            low_height_slider_ = std::make_unique<DMSlider>("Low Height (px)", 0, height_slider_range_px_, static_cast<int>(std::lround(values_.height_low_px)));
            high_height_slider_ = std::make_unique<DMSlider>("High Height (px)", 0, height_slider_range_px_, static_cast<int>(std::lround(values_.height_high_px)));
            if (low_height_slider_) low_height_slider_->set_defer_commit_until_unfocus(false);
            if (high_height_slider_) high_height_slider_->set_defer_commit_until_unfocus(false);
        }
        if (low_height_slider_) {
            low_height_slider_->set_value(static_cast<int>(std::lround(values_.height_low_px)));
        }
        if (high_height_slider_) {
            high_height_slider_->set_value(static_cast<int>(std::lround(values_.height_high_px)));
        }
    }

    void sync_sliders() const {
        ensure_height_sliders();
    }

    SDL_Rect align_slider_to_line(DMSlider& slider, int line_y, int slider_w) const {
        const int slider_h = slider.preferred_height(slider_w);
        SDL_Rect rect{layout_.content.x + layout_.content.w - slider_w - padding_, line_y - slider_h / 2, slider_w, slider_h};
        slider.set_rect(rect);
        int delta = slider.track_center_y() - line_y;
        if (delta != 0) {
            rect.y = std::clamp(rect.y - delta, layout_.content.y, layout_.content.y + layout_.content.h - slider_h);
            slider.set_rect(rect);
            delta = slider.track_center_y() - line_y;
            if (delta != 0) {
                rect.y = std::clamp(rect.y - delta, layout_.content.y, layout_.content.y + layout_.content.h - slider_h);
                slider.set_rect(rect);
            }
        }
        return rect;
    }

    void update_anchor_from_pointer(int py, bool low_line) {
        if (!render_rect_valid_) return;
        const float target_zoom = pointer_y_to_zoom(py);
        if (low_line) {
            values_.zoom_low = target_zoom;
        } else {
            values_.zoom_high = target_zoom;
        }
        enforce_zoom_constraints();
        recompute_current_height();
        notify();
    }

    int zoom_axis_span() const {
        return std::max(1, layout_.zoom_axis_bottom - layout_.zoom_axis_top);
    }

    int zoom_to_y(float zoom) const {
        const float min_zoom = std::min(values_.zoom_low, values_.zoom_high);
        const float max_zoom = std::max(values_.zoom_low, values_.zoom_high);
        const float zoom_span = std::max(0.0001f, max_zoom - min_zoom);
        const float normalized = std::clamp((zoom - min_zoom) / zoom_span, 0.0f, 1.0f);
        const float offset = normalized * static_cast<float>(zoom_axis_span());
        const int y = layout_.zoom_axis_bottom - static_cast<int>(std::lround(offset));
        return std::clamp(y, layout_.zoom_axis_top, layout_.zoom_axis_bottom);
    }

    float pointer_y_to_zoom(int py) const {
        const int top = layout_.zoom_axis_top;
        const int bottom = layout_.zoom_axis_bottom;
        if (bottom <= top) {
            return values_.zoom_low;
        }
        const int clamped_py = std::clamp(py, top, bottom);
        const float span = static_cast<float>(bottom - top);
        const float normalized = std::clamp((bottom - clamped_py) / span, 0.0f, 1.0f);
        const float min_zoom = std::min(values_.zoom_low, values_.zoom_high);
        const float max_zoom = std::max(values_.zoom_low, values_.zoom_high);
        const float zoom_span = std::max(0.0001f, max_zoom - min_zoom);
        return min_zoom + normalized * zoom_span;
    }

    void update_depth_offset_from_pointer(int px) {
        if (!render_rect_valid_) return;
        const int clamped_px = std::clamp(px, layout_.offset_range_x, layout_.offset_range_x + layout_.offset_range_w);
        const float t = static_cast<float>(clamped_px - layout_.offset_range_x) / std::max(1, layout_.offset_range_w);
        const float offset_px = std::clamp(t * kMaxDepthOffsetPx, 0.0f, kMaxDepthOffsetPx);
        values_.depth_offset_px = offset_px;
        notify();
    }

    bool hit_depth_handle(SDL_Point p) const {
        if (!render_rect_valid_) return false;
        if (hit_line(camera_drag_hitbox_, p.x, p.y)) return true;
        return hit_line(depth_handle_rect_, p.x, p.y);
    }

    DragTarget detect_hover_target(SDL_Point p) const {
        if (!render_rect_valid_) return DragTarget::None;
        if (hit_line(low_line_hitbox_, p.x, p.y)) return DragTarget::LowLine;
        if (hit_line(high_line_hitbox_, p.x, p.y)) return DragTarget::HighLine;
        if (hit_depth_handle(p)) return DragTarget::DepthOffset;
        return DragTarget::None;
    }

    bool should_highlight(DragTarget target) const {
        if (dragging_ != DragTarget::None) {
            return dragging_ == target;
        }
        return hover_target_ == target;
    }

    void enforce_height_constraints() {
        values_.height_low_px = std::max(0.0f, values_.height_low_px);
        values_.height_high_px = std::max(values_.height_low_px, values_.height_high_px);
    }

    void enforce_pitch_constraints() {
        values_.pitch_degrees = std::clamp(values_.pitch_degrees,
                                           camera::kMinPitchDegrees,
                                           camera::kMaxPitchDegrees);
    }

    void enforce_zoom_constraints() {
        constexpr float kMinZoom      = 0.05f;
        constexpr float kMinZoomDelta = 0.0001f;
        values_.zoom_low = std::max(kMinZoom, values_.zoom_low);
        values_.zoom_high = std::max(values_.zoom_low + kMinZoomDelta, values_.zoom_high);
    }

    void recompute_current_height() {
        camera::RealismSettings settings{};
        settings.zoom_low = values_.zoom_low;
        settings.zoom_high = values_.zoom_high;
        settings.height_low_px = values_.height_low_px;
        settings.height_high_px = values_.height_high_px;
        values_.current_height = interpolate_height_for_ui(settings, values_.current_zoom);
    }


    void draw_icon(SDL_Renderer* renderer,
                   SDL_Texture*& tex,
                   bool& failed_flag,
                   const SDL_Rect& rect,
                   const char* fallback_label,
                   float angle_degrees = 0.0f) const {
        if (!renderer) return;
        if (!tex && !failed_flag) {
            const bool is_camera = std::string(fallback_label) == "Camera";
            tex = IMG_LoadTexture(renderer, is_camera ? "SRC/icons/camera.png" : "SRC/icons/subject.png");
            if (!tex) {
                failed_flag = true;
            }
        }
        if (tex) {
            SDL_RenderCopyEx(renderer, tex, nullptr, &rect, angle_degrees, nullptr, SDL_FLIP_NONE);
            return;
        }
        SDL_SetRenderDrawColor(renderer, 180, 110, 120, 230);
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
        SDL_RenderDrawRect(renderer, &rect);
        const DMLabelStyle ls = DMStyles::Label();
        DrawLabelText(renderer, fallback_label, rect.x + 4, rect.y + rect.h / 2 - ls.font_size / 2, ls);
    }

    void update_layout_cache(SDL_Renderer* renderer = nullptr) const {
        const int inner_pad = padding_;
        layout_.content = SDL_Rect{
            rect_.x + inner_pad,
            rect_.y + inner_pad,
            rect_.w - inner_pad * 2,
            rect_.h - inner_pad * 2
        };
        render_rect_valid_ = layout_.content.w > 0 && layout_.content.h > 0;
        if (!render_rect_valid_) {
            camera_drag_hitbox_ = SDL_Rect{0,0,0,0};
            return;
        }

        layout_.icon_size = std::max(28, static_cast<int>(std::round(std::min(layout_.content.w, layout_.content.h) * 0.18f)));

        const float max_height = std::max({values_.height_low_px, values_.height_high_px, values_.current_height, 1.0f});
        const float span_px = std::max(max_height, 120.0f);
        const int top_padding = layout_.icon_size;
        const int usable_height = std::max(1, layout_.content.h - top_padding);
        layout_.pixels_per_unit = static_cast<float>(usable_height) / span_px;
        layout_.ground_y = layout_.content.y + layout_.content.h - 1;
        layout_.zoom_axis_top = layout_.content.y + 2;
        layout_.zoom_axis_bottom = std::max(layout_.zoom_axis_top, layout_.ground_y - 2);
        auto height_to_y = [&](float h) {
            const int y = layout_.ground_y - static_cast<int>(std::lround(std::max(0.0f, h) * layout_.pixels_per_unit));
            return std::clamp(y, layout_.content.y, layout_.ground_y - 2);
        };
        layout_.low_y  = zoom_to_y(values_.zoom_low);
        layout_.high_y = zoom_to_y(values_.zoom_high);
        if (layout_.high_y > layout_.low_y) layout_.high_y = layout_.low_y;

        const int drag_pad = std::max(10, layout_.icon_size / 3);
        low_line_hitbox_ = SDL_Rect{layout_.content.x, layout_.low_y - drag_pad, layout_.content.w, drag_pad * 2};
        high_line_hitbox_ = SDL_Rect{layout_.content.x, layout_.high_y - drag_pad, layout_.content.w, drag_pad * 2};

        layout_.offset_range_x = layout_.content.x + layout_.icon_size * 2;
        layout_.offset_range_w = std::max(layout_.content.w - layout_.icon_size * 3, layout_.icon_size);
        const float t = std::clamp(values_.depth_offset_px / kMaxDepthOffsetPx, 0.0f, 1.0f);
        layout_.camera_x = layout_.offset_range_x + static_cast<int>(std::round(t * layout_.offset_range_w));
        layout_.camera_y = height_to_y(values_.current_height);

        layout_.subject_rect = SDL_Rect{layout_.content.x + layout_.icon_size / 2, layout_.ground_y - layout_.icon_size, layout_.icon_size, layout_.icon_size};
        layout_.camera_rect = SDL_Rect{layout_.camera_x - layout_.icon_size / 2, layout_.camera_y - layout_.icon_size / 2, layout_.icon_size, layout_.icon_size};

        const int handle_w = std::max(14, layout_.icon_size / 2);
        const int handle_h = std::max(14, layout_.icon_size / 2);
        int handle_y = std::min(layout_.content.y + layout_.content.h - handle_h, layout_.ground_y + 6);
        handle_y = std::max(layout_.content.y, handle_y);
        depth_handle_rect_ = SDL_Rect{layout_.camera_x - handle_w / 2, handle_y, handle_w, handle_h};
        const int drag_w = std::max(handle_w * 2, layout_.icon_size + 12);
        camera_drag_hitbox_ = SDL_Rect{
            layout_.camera_x - drag_w / 2,
            layout_.content.y,
            drag_w,
            layout_.content.h
        };

        const int slider_w_max = std::max(180, layout_.content.w - padding_ * 2);
        const int slider_w = std::clamp(layout_.content.w / 2, 180, slider_w_max);
        ensure_height_sliders();
        if (low_height_slider_) {
            align_slider_to_line(*low_height_slider_, layout_.low_y, slider_w);
        }
        if (high_height_slider_) {
            align_slider_to_line(*high_height_slider_, layout_.high_y, slider_w);
        }

        (void)renderer;
        render_rect_valid_ = true;
    }

    SDL_Rect rect_{0,0,0,0};
    int preferred_height_ = 420;
    int padding_ = 14;
    CameraDepthViewValues values_{};
    ChangeCallback on_change_{};
    mutable SDL_Rect depth_handle_rect_{0,0,0,0};
    mutable SDL_Rect low_line_hitbox_{0,0,0,0};
    mutable SDL_Rect high_line_hitbox_{0,0,0,0};
    mutable SDL_Rect camera_drag_hitbox_{0,0,0,0};
    mutable bool render_rect_valid_ = false;
    mutable LayoutState layout_{};
    mutable std::unique_ptr<DMSlider> low_height_slider_{};
    mutable std::unique_ptr<DMSlider> high_height_slider_{};
    mutable int height_slider_range_px_ = 6000;
    mutable SDL_Texture* subject_texture_ = nullptr;
    mutable SDL_Texture* camera_texture_ = nullptr;
    mutable bool subject_texture_failed_ = false;
    mutable bool camera_texture_failed_ = false;
    DragTarget dragging_ = DragTarget::None;
    DragTarget hover_target_ = DragTarget::None;
    inline static constexpr float kMaxDepthOffsetPx = 4000.0f;
    inline static constexpr int kMaxHeightSliderPx = 40000;
};

class CameraSideViewPanel : public DockableCollapsible {
public:
    CameraSideViewPanel()
        : DockableCollapsible("Depth Side View", true, 0, 0) {
        widget_ = std::make_unique<CameraSideViewWidget>(
            [this](const CameraDepthViewValues& vals) {
                values_ = vals;
                if (on_change_) {
                    on_change_(vals);
                }
            });
        if (widget_) {
            widget_->set_preferred_height(420);
        }
        set_rows({ { widget_.get() } });
        set_padding(DMSpacing::panel_padding());
        set_row_gap(DMSpacing::item_gap());
        set_col_gap(DMSpacing::small_gap());
        set_close_button_enabled(true);
        set_scroll_enabled(false);
        set_floating_content_width(560);
        set_visible_height(420);
        set_cell_width(520);
        set_expanded(true);
        set_visible(false);
    }

    void set_values(const CameraDepthViewValues& values) {
        values_ = values;
        if (widget_) widget_->set_values(values);
    }

    CameraDepthViewValues values() const {
        if (widget_) return widget_->values();
        return values_;
    }

    void set_on_change(CameraSideViewWidget::ChangeCallback cb) {
        on_change_ = std::move(cb);
        if (widget_) widget_->set_on_change(on_change_);
    }

    bool handle_event(const SDL_Event& e) override {
        bool handled = DockableCollapsible::handle_event(e);
        if (handled) {
            return true;
        }
        if (!is_visible()) {
            return false;
        }
        int px = 0;
        int py = 0;
        bool have_point = false;
        switch (e.type) {
        case SDL_MOUSEMOTION:
            px = e.motion.x; py = e.motion.y; have_point = true; break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            px = e.button.x; py = e.button.y; have_point = true; break;
        case SDL_MOUSEWHEEL:
            SDL_GetMouseState(&px, &py);
            have_point = true;
            break;
        default:
            break;
        }
        if (have_point) {
            return is_point_inside(px, py);
        }
        return false;
    }

private:
    CameraDepthViewValues values_{};
    std::unique_ptr<CameraSideViewWidget> widget_;
    CameraSideViewWidget::ChangeCallback on_change_{};
};

class PanelBannerWidget  : public Widget {
public:
    PanelBannerWidget(std::string heading, std::string detail)
        : heading_(std::move(heading)),
          detail_(std::move(detail)) {
        heading_style_ = DMStyles::Label();
        heading_style_.font_size = std::max(heading_style_.font_size + 2, 18);
        heading_style_.color = DMStyles::AccentButton().text;

        body_style_ = DMStyles::Label();
        body_style_.font_size = std::max(12, body_style_.font_size - 2);
        body_style_.color = dm::rgba(255, 255, 255, 230);
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        const int inner = std::max(1, w - 2 * padding());
        ensure_lines(inner);
        const int heading_h = heading_style_.font_size + kHeadingGap;
        const int body_lines = std::max(1, static_cast<int>(lines_.size()));
        const int line_h = body_style_.font_size + kLineGap;
        return padding() * 2 + heading_h + body_lines * line_h;
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        SDL_Color accent = DMStyles::AccentButton().bg;
        SDL_Color background{ accent.r, accent.g, accent.b, static_cast<Uint8>(220) };
        SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
        SDL_RenderFillRect(renderer, &rect_);

        SDL_Color border = DMStyles::AccentButton().border;
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &rect_);

        const int pad = padding();
        SDL_Rect content{ rect_.x + pad, rect_.y + pad, rect_.w - 2 * pad, rect_.h - 2 * pad };
        DrawLabelText(renderer, heading_, content.x, content.y, heading_style_);
        int text_y = content.y + heading_style_.font_size + kHeadingGap;

        ensure_lines(content.w);
        for (const auto& line : lines_) {
            DrawLabelText(renderer, line, content.x, text_y, body_style_);
            text_y += body_style_.font_size + kLineGap;
        }
    }

    bool wants_full_row() const override { return true; }

private:
    static std::vector<std::string> wrap_lines(const std::string& text, int max_width, const DMLabelStyle& style) {
        std::vector<std::string> lines;
        if (text.empty() || max_width <= 0) {
            if (!text.empty()) lines.push_back(text);
            return lines;
        }
        std::istringstream stream(text);
        std::string word;
        std::string current;
        while (stream >> word) {
            std::string candidate = current.empty() ? word : current + " " + word;
            SDL_Point dims = MeasureLabelText(style, candidate);
            if (!current.empty() && dims.x > max_width) {
                lines.push_back(current);
                current = word;
                continue;
            }
            current = candidate;
        }
        if (!current.empty()) {
            lines.push_back(current);
        }
        if (lines.empty()) {
            lines.push_back(text);
        }
        return lines;
    }

    void ensure_lines(int inner_width) const {
        int width = std::max(1, inner_width);
        if (width == cached_width_) {
            return;
        }
        cached_width_ = width;
        lines_ = wrap_lines(detail_, cached_width_, body_style_);
    }

    static int padding() { return DMSpacing::item_gap(); }

private:
    static constexpr int kHeadingGap = 6;
    static constexpr int kLineGap = 4;
    SDL_Rect rect_{0, 0, 0, 0};
    std::string heading_;
    std::string detail_;
    DMLabelStyle heading_style_;
    DMLabelStyle body_style_;
    mutable std::vector<std::string> lines_;
    mutable int cached_width_ = -1;
};

class SectionToggleWidget : public Widget {
public:
    using ToggleCallback = std::function<void(bool)>;

    SectionToggleWidget(std::string label, bool expanded)
        : label_(std::move(label)),
          expanded_(expanded) {
        button_ = std::make_unique<DMButton>(
            "",
            &DMStyles::HeaderButton(),
            DockableCollapsible::kDefaultFloatingContentWidth,
            DMButton::height());
        if (button_) {
            button_->set_tooltip_state(this->tooltip_state());
        }
        update_button_text();
    }

    ~SectionToggleWidget() override {
        if (button_) {
            button_->set_tooltip_state(nullptr);
        }
    }

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (button_) {
            button_->set_rect(r);
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override { return DMButton::height(); }

    bool handle_event(const SDL_Event& e) override {
        if (!button_) return false;
        bool used = button_->handle_event(e);
        if (used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            set_expanded(!expanded_);
            if (on_toggle_) {
                on_toggle_(expanded_);
            }
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (button_) button_->render(renderer);
    }

    bool wants_full_row() const override { return true; }

    void set_on_toggle(ToggleCallback cb) { on_toggle_ = std::move(cb); }

    void set_label(std::string label) {
        label_ = std::move(label);
        update_button_text();
    }

    void set_expanded(bool expanded) {
        if (expanded_ == expanded) {
            return;
        }
        expanded_ = expanded;
        update_button_text();
    }

    bool expanded() const { return expanded_; }

private:
    void update_button_text() {
        if (!button_) return;
        const std::string indicator = expanded_
            ? std::string(DMIcons::CollapseExpanded())
            : std::string(DMIcons::CollapseCollapsed());
        button_->set_text(indicator + " " + label_);
        const DMButtonStyle* style = expanded_ ? &DMStyles::HeaderButton() : &DMStyles::FooterToggleButton();
        button_->set_style(style);
    }

    std::unique_ptr<DMButton> button_;
    SDL_Rect rect_{0, 0, 0, DMButton::height()};
    std::string label_;
    bool expanded_ = true;
    ToggleCallback on_toggle_{};
};


class DiscreteSliderWidget : public Widget {
public:
    using ChangeCallback = std::function<void(int)>;

    DiscreteSliderWidget(std::string label,
                         std::vector<int> values,
                         int value)
        : values_(std::move(values)) {
        if (values_.empty()) {
            values_.push_back(100);
        }
        slider_min_units_ = 0;
        slider_max_units_ = static_cast<int>(values_.size() - 1);
        slider_ = std::make_unique<DMSlider>(std::move(label), slider_min_units_, slider_max_units_, value_to_slider(value));
        slider_->set_defer_commit_until_unfocus(false);
        slider_->set_value_formatter([this](int units, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            const int idx = clamp_index(units);
            std::snprintf(buffer.data(), buffer.size(), "%d%%", values_[idx]);
            return std::string_view(buffer.data());
        });
        slider_->set_value_parser([this](const std::string& text) -> std::optional<int> {
            try {
                const int parsed = std::stoi(text);
                return value_to_slider(parsed);
            } catch (...) {
                return std::nullopt;
            }
        });
        slider_widget_ = std::make_unique<SliderWidget>(slider_.get());
        current_index_ = clamp_index(slider_->value());
    }

    void set_on_value_changed(ChangeCallback cb) { on_change_ = std::move(cb); }

    void set_value(int v) {
        if (!slider_) return;
        slider_->set_value(value_to_slider(v));
        current_index_ = clamp_index(slider_->value());
    }

    int value() const {
        if (values_.empty()) return 0;
        const int idx = clamp_index(current_index_);
        return values_[idx];
    }

    void set_rect(const SDL_Rect& r) override {
        if (slider_widget_) slider_widget_->set_rect(r);
    }

    const SDL_Rect& rect() const override {
        if (slider_widget_) {
            return slider_widget_->rect();
        }
        static SDL_Rect empty{0, 0, 0, 0};
        return empty;
    }

    int height_for_width(int w) const override {
        return slider_widget_ ? slider_widget_->height_for_width(w) : DMSlider::height();
    }

    bool wants_full_row() const override { return true; }

    bool handle_event(const SDL_Event& e) override {
        if (!slider_widget_) return false;
        const int previous_value = value();
        bool handled = slider_widget_->handle_event(e);
        if (slider_) {
            current_index_ = clamp_index(slider_->value());
            const int new_value = value();
            if (handled && on_change_ && new_value != previous_value) {
                on_change_(new_value);
            }
        }
        return handled;
    }

    void render(SDL_Renderer* renderer) const override {
        if (slider_widget_) slider_widget_->render(renderer);
    }

    void set_tooltip(std::string text) {
        if (slider_widget_) slider_widget_->set_tooltip(std::move(text));
    }

private:
    int clamp_index(int index) const {
        if (values_.empty()) return 0;
        return std::clamp(index, slider_min_units_, slider_max_units_);
    }

    int value_to_slider(int value) const {
        if (values_.empty()) return slider_min_units_;
        int best_index = slider_min_units_;
        int best_diff = std::abs(value - values_[best_index]);
        for (std::size_t i = 1; i < values_.size(); ++i) {
            const int diff = std::abs(value - values_[i]);
            if (diff < best_diff) {
                best_diff = diff;
                best_index = static_cast<int>(i);
            }
        }
        return clamp_index(best_index);
    }

    std::unique_ptr<DMSlider> slider_;
    std::unique_ptr<SliderWidget> slider_widget_;
    std::vector<int> values_;
    int slider_min_units_ = 0;
    int slider_max_units_ = 0;
    int current_index_ = 0;
    ChangeCallback on_change_{};
};

CameraUIPanel::CameraUIPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Camera Settings", true, x, y),
      assets_(assets) {
    last_depthcue_enabled_ = devmode::camera_prefs::load_depthcue_enabled();
    set_expanded(true);
    set_visible(false);
    set_padding(16);
    set_close_button_enabled(true);
    set_close_button_on_left(false);
    set_floatable(true);
    build_ui();
    sync_from_camera();
}

CameraUIPanel::~CameraUIPanel() = default;

void CameraUIPanel::set_assets(Assets* assets) {
    assets_ = assets;
    sync_from_camera();
}

void CameraUIPanel::set_image_effects_panel_callback(std::function<void()> cb) {
    open_image_effects_cb_ = std::move(cb);
}

void CameraUIPanel::open() {
    set_visible(true);
    suppress_apply_once_ = true;
    // Collapse all sections by default when opened
    visibility_section_expanded_ = false;
    depth_section_expanded_ = false;
    depthcue_section_expanded_ = false;
    smoothing_section_expanded_ = false;
    if (visibility_section_header_) visibility_section_header_->set_expanded(false);
    if (depth_section_header_)      depth_section_header_->set_expanded(false);
    if (depthcue_section_header_)   depthcue_section_header_->set_expanded(false);
    if (smoothing_section_header_)  smoothing_section_header_->set_expanded(false);
    rebuild_rows();
    sync_from_camera();
}

void CameraUIPanel::close() {
    set_visible(false);
    if (side_view_panel_) {
        side_view_panel_->close();
    }
}

void CameraUIPanel::toggle() {
    set_visible(!is_visible());
    if (is_visible()) {
        suppress_apply_once_ = true;
        sync_from_camera();
    } else if (side_view_panel_) {
        side_view_panel_->close();
    }
}

bool CameraUIPanel::is_point_inside(int x, int y) const {
    if (side_view_panel_ && side_view_panel_->is_visible() && side_view_panel_->is_point_inside(x, y)) {
        return true;
    }
    return DockableCollapsible::is_point_inside(x, y);
}

void CameraUIPanel::update(const Input& input, int screen_w, int screen_h) {
    last_screen_w_ = screen_w;
    last_screen_h_ = screen_h;
    const bool previously_visible = was_visible_;
    DockableCollapsible::update(input, screen_w, screen_h);
    update_side_view_visibility(screen_w, screen_h);
    if (side_view_panel_ && side_view_panel_->is_visible()) {
        side_view_panel_->update(input, screen_w, screen_h);
    }
    const bool currently_visible = is_visible();
    if (currently_visible && !previously_visible) {
        // Panel might be shown via base-class helpers; always resync when this happens.
        suppress_apply_once_ = true;
        // Collapse all sections by default when the panel becomes visible
        visibility_section_expanded_ = false;
        depth_section_expanded_ = false;
        depthcue_section_expanded_ = false;
        smoothing_section_expanded_ = false;
        if (visibility_section_header_) visibility_section_header_->set_expanded(false);
        if (depth_section_header_)      depth_section_header_->set_expanded(false);
        if (depthcue_section_header_)   depthcue_section_header_->set_expanded(false);
        if (smoothing_section_header_)  smoothing_section_header_->set_expanded(false);
        rebuild_rows();
        sync_from_camera();
    }
    was_visible_ = currently_visible;

    if (!currently_visible) return;
    if (!assets_) return;
    if (suppress_apply_once_) {
        suppress_apply_once_ = false;
        refresh_side_view_preview();
        return;
    }
    refresh_side_view_preview();
    apply_settings_if_needed();
}

bool CameraUIPanel::handle_event(const SDL_Event& e) {
    if (side_view_panel_ && side_view_panel_->is_visible()) {
        bool handled = side_view_panel_->handle_event(e);
        if (!handled) {
            int px = 0;
            int py = 0;
            bool pointer_event = false;
            switch (e.type) {
            case SDL_MOUSEMOTION:
                px = e.motion.x; py = e.motion.y; pointer_event = true; break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                px = e.button.x; py = e.button.y; pointer_event = true; break;
            case SDL_MOUSEWHEEL:
                SDL_GetMouseState(&px, &py);
                pointer_event = true;
                break;
            default:
                break;
            }
            if (pointer_event && side_view_panel_->is_point_inside(px, py)) {
                handled = true;
            }
        }
        if (handled) {
            return true;
        }
    }
    if (!is_visible()) return false;
    bool used = DockableCollapsible::handle_event(e);
    if (used) {
        refresh_side_view_preview();
        apply_settings_if_needed();
    }
    return used;
}

void CameraUIPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (is_visible()) {
        DockableCollapsible::render(renderer);
    }
    if (side_view_panel_ && side_view_panel_->is_visible()) {
        side_view_panel_->render(renderer);
    }
    // Ensure expanded dropdown options render above the panel
    DMDropdown::render_active_options(renderer);
}

void CameraUIPanel::layout_custom_content(int screen_w, int screen_h) const {
    // Allow dragging the panel by clicking the banner area as well as the header
    if (hero_banner_widget_) {
        set_drag_handle_rect(hero_banner_widget_->rect());
    } else {
        set_drag_handle_rect(SDL_Rect{0,0,0,0});
    }
}

void CameraUIPanel::sync_from_camera() {
    if (!assets_) return;
    camera& cam = assets_->getView();
    last_settings_ = cam.realism_settings();
    bool effects_enabled = cam.realism_enabled() && cam.parallax_enabled();
    last_realism_enabled_ = effects_enabled;

    if (min_render_size_slider_) min_render_size_slider_->set_value(last_settings_.min_visible_screen_ratio);
    if (foreshorten_strength_slider_) foreshorten_strength_slider_->set_value(last_settings_.foreshorten_strength);
    if (distance_strength_slider_) distance_strength_slider_->set_value(last_settings_.distance_scale_strength);
    if (render_quality_slider_) render_quality_slider_->set_value(last_settings_.render_quality_percent);
    if (smoothing_checkbox_) smoothing_checkbox_->set_value(last_settings_.smooth_motion_zoom);
    if (smoothing_method_dropdown_) smoothing_method_dropdown_->set_selected(method_to_index(last_settings_.motion_smoothing_method));
    if (motion_tau_slider_) motion_tau_slider_->set_value(last_settings_.motion_smoothing_tau);
    if (motion_stiffness_slider_) motion_stiffness_slider_->set_value(last_settings_.motion_smoothing_spring_frequency);
    if (motion_max_step_slider_) motion_max_step_slider_->set_value(last_settings_.motion_smoothing_max_step);
    if (motion_snap_slider_) motion_snap_slider_->set_value(last_settings_.motion_smoothing_snap_threshold);
    if (parallax_smoothing_slider_) {
        const float slider_value = (last_settings_.parallax_smoothing.method == TransformSmoothingMethod::Lerp)
            ? tau_from_rate(last_settings_.parallax_smoothing.lerp_rate)
            : last_settings_.parallax_smoothing.spring_frequency;
        parallax_smoothing_slider_->set_value(slider_value);
    }
    if (hysteresis_margin_slider_) hysteresis_margin_slider_->set_value(last_settings_.scale_variant_hysteresis_margin);

    if (foreground_texture_opacity_slider_) {
        foreground_texture_opacity_slider_->set_value(static_cast<float>(last_settings_.foreground_texture_max_opacity));
    }
    if (background_texture_opacity_slider_) {
        background_texture_opacity_slider_->set_value(static_cast<float>(last_settings_.background_texture_max_opacity));
    }
    if (texture_opacity_interp_dropdown_) {
        texture_opacity_interp_dropdown_->set_selected(static_cast<int>(last_settings_.texture_opacity_falloff_method));
    }

    refresh_side_view_preview();
}

void CameraUIPanel::build_ui() {
    set_header_button_style(&DMStyles::AccentButton());
    set_header_highlight_color(DMStyles::AccentButton().bg);
    set_padding(DMSpacing::panel_padding());
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::item_gap());
    set_floating_content_width(460);

    header_spacer_ = std::make_unique<SpacerWidget>(DMSpacing::header_gap());
    hero_banner_widget_ = std::make_unique<PanelBannerWidget>(
        "Camera realism",
        "Dial in render buffers, parallax depth, and smoothing without leaving the editor.");
    controls_spacer_ = std::make_unique<SpacerWidget>(DMSpacing::small_gap());



    camera::RealismSettings defaults;

    auto configure_section = [this](std::unique_ptr<SectionToggleWidget>& target,
                                    const std::string& label,
                                    bool* expanded_flag) {
        target = std::make_unique<SectionToggleWidget>(label, *expanded_flag);
        target->set_on_toggle([this, expanded_flag](bool expanded) {
            *expanded_flag = expanded;
            rebuild_rows();
        });
        target->set_tooltip("Click to collapse or expand this section.");
    };

    configure_section(visibility_section_header_, "Visibility & Performance", &visibility_section_expanded_);
    configure_section(depth_section_header_,      "Depth & Perspective",      &depth_section_expanded_);
    configure_section(depthcue_section_header_,   "Depth Cue",               &depthcue_section_expanded_);
    configure_section(smoothing_section_header_,  "Motion & Smoothing",       &smoothing_section_expanded_);
    if (depth_section_header_) {
        depth_section_header_->set_on_toggle([this](bool expanded) {
            depth_section_expanded_ = expanded;
            rebuild_rows();
            update_side_view_visibility(last_screen_w_, last_screen_h_);
        });
    }

    min_render_size_slider_ = std::make_unique<FloatSliderWidget>("Min On-Screen Size", 0.0f, 0.05f, 0.001f, defaults.min_visible_screen_ratio, 3);
    min_render_size_slider_->set_tooltip("Cull sprites once their height drops below this fraction of the screen (0.01 = 1%).");
    min_render_size_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    foreshorten_strength_slider_ = std::make_unique<FloatSliderWidget>("Vertical Stretch", 0.0f, 2.0f, 0.01f, defaults.foreshorten_strength, 2);
    foreshorten_strength_slider_->set_tooltip("Controls how much tall sprites stretch or compress with depth.");
    foreshorten_strength_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    distance_strength_slider_ = std::make_unique<FloatSliderWidget>("Distance Scale", 0.0f, 1.0f, 0.01f, defaults.distance_scale_strength, 2);
    distance_strength_slider_->set_tooltip("Higher values shrink faraway sprites more aggressively.");
    distance_strength_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    render_quality_slider_ = std::make_unique<DiscreteSliderWidget>("Render Quality (%)", std::vector<int>{100, 75, 50, 25, 10}, defaults.render_quality_percent);
    render_quality_slider_->set_tooltip("Trade fidelity for speed; lowers the number of sprites drawn each frame.");
    render_quality_slider_->set_on_value_changed([this](int) { on_control_value_changed(); });

    side_view_panel_ = std::make_unique<CameraSideViewPanel>();
    if (side_view_panel_) {
        side_view_panel_->set_on_change([this](const CameraDepthViewValues&) {
            this->on_control_value_changed();
        });
        side_view_panel_->set_work_area(SDL_Rect{0, 0, last_screen_w_, last_screen_h_});
    }



    const int stored_fg_opacity = devmode::camera_prefs::load_foreground_texture_max_opacity();
    const int stored_bg_opacity = devmode::camera_prefs::load_background_texture_max_opacity();
    foreground_texture_opacity_slider_ = std::make_unique<FloatSliderWidget>(
        "Foreground Texture Max Opacity", 0.0f, 255.0f, 1.0f, static_cast<float>(stored_fg_opacity), 0);
    foreground_texture_opacity_slider_->set_tooltip("Maximum opacity when blending the foreground texture.");
    foreground_texture_opacity_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    background_texture_opacity_slider_ = std::make_unique<FloatSliderWidget>(
        "Background Texture Max Opacity", 0.0f, 255.0f, 1.0f, static_cast<float>(stored_bg_opacity), 0);
    background_texture_opacity_slider_->set_tooltip("Maximum opacity when blending the background texture.");
    background_texture_opacity_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    {
        const int default_interp_index = std::clamp(
            static_cast<int>(defaults.texture_opacity_falloff_method), 0, 4);
        std::vector<std::string> options{ "Linear", "Quadratic", "Cubic", "Logarithmic", "Exponential" };
        texture_opacity_interp_dropdown_ = std::make_unique<DMDropdown>("Depth Cue Opacity Interpolation", options, default_interp_index);
        texture_opacity_interp_widget_   = std::make_unique<DropdownWidget>(texture_opacity_interp_dropdown_.get());
        texture_opacity_interp_widget_->set_tooltip("Curve used when blending precomputed textures by depth.");
        texture_opacity_interp_dropdown_->set_on_selection_changed([this](int) { on_control_value_changed(); });
    }

    image_effect_button_ = std::make_unique<DMButton>("Configure Image Effects", &DMStyles::AccentButton(), DockableCollapsible::kDefaultFloatingContentWidth, DMButton::height());
    image_effect_widget_ = std::make_unique<ButtonWidget>(image_effect_button_.get(), [this]() {
        if (open_image_effects_cb_) {
            open_image_effects_cb_();
        }
    });
    if (image_effect_widget_) {
        image_effect_widget_->set_tooltip("Open the global image effect editor to regenerate depth cue textures.");
    }


    smoothing_checkbox_ = std::make_unique<DMCheckbox>("Smooth Motion", defaults.smooth_motion_zoom);
    smoothing_widget_   = std::make_unique<CheckboxWidget>(smoothing_checkbox_.get());
    smoothing_widget_->set_tooltip("Blend camera motion and zoom instead of stepping directly to the target.");

    const std::vector<std::string> method_options{"Smooth Lerp", "Spring"};
    smoothing_method_dropdown_ = std::make_unique<DMDropdown>(
        "Smoothing Type",
        method_options,
        method_to_index(defaults.motion_smoothing_method));
    smoothing_method_widget_ = std::make_unique<DropdownWidget>(smoothing_method_dropdown_.get());
    smoothing_method_widget_->set_tooltip("Pick between a simple lerp or a spring-like response for smoothing.");
    if (smoothing_method_dropdown_) {
        smoothing_method_dropdown_->set_on_selection_changed([this](int) {
            // Rebuild visible rows to reflect method-specific widgets
            rebuild_rows();
            // Apply updated method immediately
            on_control_value_changed();
        });
    }

    motion_tau_slider_ = std::make_unique<FloatSliderWidget>("Lerp Response (s)", 0.0f, 1.0f, 0.01f, defaults.motion_smoothing_tau, 3);
    motion_tau_slider_->set_tooltip("When using lerp smoothing, this is how long it takes to settle (smaller reacts faster).");
    motion_tau_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    motion_stiffness_slider_ = std::make_unique<FloatSliderWidget>(
        "Spring Frequency (Hz)", 0.0f, 10.0f, 0.05f, defaults.motion_smoothing_spring_frequency, 2);
    motion_stiffness_slider_->set_tooltip("When using the spring method, higher values track the target faster.");
    motion_stiffness_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    motion_max_step_slider_ = std::make_unique<FloatSliderWidget>(
        "Max Catch-Up Speed", 0.0f, 12000.0f, 25.0f, defaults.motion_smoothing_max_step, 0);
    motion_max_step_slider_->set_tooltip("Largest distance the smoothing can cover per second while chasing the target.");
    motion_max_step_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });
    motion_snap_slider_ = std::make_unique<FloatSliderWidget>("Snap Distance", 0.0f, 5.0f, 0.01f, defaults.motion_smoothing_snap_threshold, 2);
    motion_snap_slider_->set_tooltip("When closer than this amount, skip smoothing and snap immediately.");
    motion_snap_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    // Make parallax smoothing snappy by default.
    if (defaults.parallax_smoothing.method == TransformSmoothingMethod::None) {
        defaults.parallax_smoothing.method = TransformSmoothingMethod::Lerp;
        defaults.parallax_smoothing.lerp_rate = rate_from_tau(0.08f);
    } else if (defaults.parallax_smoothing.method == TransformSmoothingMethod::Lerp &&
               defaults.parallax_smoothing.lerp_rate <= 0.0f) {
        defaults.parallax_smoothing.lerp_rate = rate_from_tau(0.08f);
    } else if (defaults.parallax_smoothing.method == TransformSmoothingMethod::CriticallyDampedSpring &&
               defaults.parallax_smoothing.spring_frequency <= 0.0f) {
        defaults.parallax_smoothing.spring_frequency = 10.0f;
    }
    const float default_parallax_value = (defaults.parallax_smoothing.method == TransformSmoothingMethod::Lerp)
        ? tau_from_rate(defaults.parallax_smoothing.lerp_rate)
        : defaults.parallax_smoothing.spring_frequency;
    parallax_smoothing_slider_ = std::make_unique<FloatSliderWidget>(
        "Parallax Ease", 0.0f, 4.0f, 0.02f, default_parallax_value, 2);
    parallax_smoothing_slider_->set_tooltip(
        "Extra smoothing just for parallax offsets (seconds for lerp, Hz for spring).");
    parallax_smoothing_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    hysteresis_margin_slider_ = std::make_unique<FloatSliderWidget>(
        "Texture Switch Cushion", 0.0f, 0.5f, 0.005f, defaults.scale_variant_hysteresis_margin, 3);
    hysteresis_margin_slider_->set_tooltip(
        "Padding before swapping between pre-scaled sprite variants to avoid flicker.");
    hysteresis_margin_slider_->set_on_value_changed([this](float) { on_control_value_changed(); });

    rebuild_rows();
}

void CameraUIPanel::on_control_value_changed() {
    if (!assets_ || !is_visible()) {
        return;
    }
    apply_settings_if_needed();
    refresh_side_view_preview();
}

void CameraUIPanel::rebuild_rows() {
    Rows rows;
    if (header_spacer_) rows.push_back({ header_spacer_.get() });
    if (hero_banner_widget_) rows.push_back({ hero_banner_widget_.get() });
    if (controls_spacer_) rows.push_back({ controls_spacer_.get() });

    if (visibility_section_header_) rows.push_back({ visibility_section_header_.get() });
    if (visibility_section_expanded_) {
        if (min_render_size_slider_) rows.push_back({ min_render_size_slider_.get() });
        if (render_quality_slider_) rows.push_back({ render_quality_slider_.get() });
    }

    if (depth_section_header_) rows.push_back({ depth_section_header_.get() });
    if (depth_section_expanded_) {
        if (foreshorten_strength_slider_) rows.push_back({ foreshorten_strength_slider_.get() });
        if (distance_strength_slider_) rows.push_back({ distance_strength_slider_.get() });
    }

    if (depthcue_section_header_) rows.push_back({ depthcue_section_header_.get() });
    if (depthcue_section_expanded_) {
        if (foreground_texture_opacity_slider_) rows.push_back({ foreground_texture_opacity_slider_.get() });
        if (background_texture_opacity_slider_) rows.push_back({ background_texture_opacity_slider_.get() });
        if (texture_opacity_interp_widget_) rows.push_back({ texture_opacity_interp_widget_.get() });
        if (image_effect_widget_) rows.push_back({ image_effect_widget_.get() });
    }

    if (smoothing_section_header_) rows.push_back({ smoothing_section_header_.get() });
    if (smoothing_section_expanded_) {
        if (smoothing_widget_) rows.push_back({ smoothing_widget_.get() });
        if (smoothing_method_widget_) rows.push_back({ smoothing_method_widget_.get() });
        // Show only the controls relevant to the selected smoothing method
        TransformSmoothingMethod ui_method = last_settings_.motion_smoothing_method;
        if (smoothing_method_dropdown_) {
            ui_method = method_from_index(smoothing_method_dropdown_->selected());
        }
        if (ui_method == TransformSmoothingMethod::Lerp) {
            if (motion_tau_slider_) rows.push_back({ motion_tau_slider_.get() });
        } else {
            if (motion_stiffness_slider_) rows.push_back({ motion_stiffness_slider_.get() });
        }
        if (motion_max_step_slider_) rows.push_back({ motion_max_step_slider_.get() });
        if (motion_snap_slider_) rows.push_back({ motion_snap_slider_.get() });
        if (parallax_smoothing_slider_) rows.push_back({ parallax_smoothing_slider_.get() });
        if (hysteresis_margin_slider_) rows.push_back({ hysteresis_margin_slider_.get() });
    }
    set_rows(rows);
}

void CameraUIPanel::refresh_side_view_preview() {
    if (!side_view_panel_) {
        return;
    }
    CameraDepthViewValues vals{};
    camera::RealismSettings settings = last_settings_;
    vals.zoom_low = settings.zoom_low;
    vals.zoom_high = settings.zoom_high;
    vals.height_low_px = settings.height_low_px;
    vals.height_high_px = settings.height_high_px;
    vals.depth_offset_px = settings.grid_depth_offset_px;
    vals.pitch_degrees = settings.grid_pitch_degrees;
    if (assets_) {
        camera& cam = assets_->getView();
        vals.current_zoom = cam.get_scale();
        vals.pitch_degrees = cam.current_pitch_degrees();
    } else {
        vals.current_zoom = settings.zoom_low;
    }
    vals.current_height = interpolate_height_for_ui(settings, vals.current_zoom);
    side_view_panel_->set_values(vals);
}

void CameraUIPanel::update_side_view_visibility(int screen_w, int screen_h) {
    if (!side_view_panel_) {
        return;
    }
    side_view_panel_->set_work_area(SDL_Rect{0, 0, screen_w, screen_h});
    const bool should_show = is_visible() && depth_section_expanded_;
    if (should_show) {
        SDL_Rect bounds = side_view_panel_->rect();
        const bool needs_reposition =
            !side_view_panel_->is_visible() ||
            bounds.w <= 0 || bounds.h <= 0 ||
            bounds.x + bounds.w > screen_w ||
            bounds.y + bounds.h > screen_h;
        if (needs_reposition && screen_w > 0 && screen_h > 0) {
            position_side_view_panel(screen_w, screen_h);
        }
        if (!side_view_panel_->is_visible()) {
            side_view_panel_->open();
        }
    } else if (side_view_panel_->is_visible()) {
        side_view_panel_->close();
    }
}

void CameraUIPanel::position_side_view_panel(int screen_w, int screen_h) {
    if (!side_view_panel_) {
        return;
    }
    SDL_Rect anchor = rect();
    SDL_Rect target = side_view_panel_->rect();
    const int gap = DMSpacing::item_gap();
    target.x = anchor.x + anchor.w + gap;
    target.y = anchor.y;
    const int max_x = std::max(0, screen_w - target.w);
    const int max_y = std::max(0, screen_h - target.h);
    target.x = std::clamp(target.x, 0, max_x);
    target.y = std::clamp(target.y, 0, max_y);
    side_view_panel_->set_rect(target);
}

void CameraUIPanel::apply_settings_if_needed() {
    if (!assets_) return;
    camera::RealismSettings settings = read_settings_from_ui();
    const bool effects_enabled = assets_->getView().realism_enabled();
    const bool depthcue_enabled = last_depthcue_enabled_;

    auto differs = [](float a, float b) {
        return std::fabs(a - b) > 0.0001f;
    };

    bool changed = (effects_enabled != last_realism_enabled_) || (depthcue_enabled != last_depthcue_enabled_);
    const camera::RealismSettings& prev = last_settings_;
    changed = changed || differs(settings.zoom_low, prev.zoom_low) || differs(settings.zoom_high, prev.zoom_high);
    changed = changed || differs(settings.height_low_px, prev.height_low_px) || differs(settings.height_high_px, prev.height_high_px);
    changed = changed || differs(settings.foreshorten_strength, prev.foreshorten_strength) || differs(settings.distance_scale_strength, prev.distance_scale_strength) || differs(settings.min_visible_screen_ratio, prev.min_visible_screen_ratio);
    if (render_quality_slider_) {
        changed = changed || settings.render_quality_percent != prev.render_quality_percent;
    }
    changed = changed || differs(settings.grid_depth_offset_px, prev.grid_depth_offset_px);
    changed = changed || settings.smooth_motion_zoom != prev.smooth_motion_zoom;
    changed = changed || settings.motion_smoothing_method != prev.motion_smoothing_method;
    changed = changed || differs(settings.motion_smoothing_tau, prev.motion_smoothing_tau);
    changed = changed || differs(settings.motion_smoothing_spring_frequency, prev.motion_smoothing_spring_frequency);
    changed = changed || differs(settings.motion_smoothing_max_step, prev.motion_smoothing_max_step);
    changed = changed || differs(settings.motion_smoothing_snap_threshold, prev.motion_smoothing_snap_threshold);
    changed = changed || differs(settings.scale_variant_hysteresis_margin, prev.scale_variant_hysteresis_margin);
    changed = changed || settings.parallax_smoothing.method != prev.parallax_smoothing.method ||
        differs(settings.parallax_smoothing.lerp_rate, prev.parallax_smoothing.lerp_rate) ||
        differs(settings.parallax_smoothing.spring_frequency, prev.parallax_smoothing.spring_frequency);

    // Depth cue texture parameters
    changed = changed || (settings.foreground_texture_max_opacity != prev.foreground_texture_max_opacity);
    changed = changed || (settings.background_texture_max_opacity != prev.background_texture_max_opacity);
    changed = changed || differs(settings.foreground_plane_screen_y, prev.foreground_plane_screen_y);
    changed = changed || differs(settings.background_plane_screen_y, prev.background_plane_screen_y);
    changed = changed || static_cast<int>(settings.texture_opacity_falloff_method) != static_cast<int>(prev.texture_opacity_falloff_method);

    if (changed) {
        apply_settings_to_camera(settings, effects_enabled, depthcue_enabled);

        assets_->on_camera_settings_changed();
    }
}



void CameraUIPanel::apply_settings_to_camera(const camera::RealismSettings& settings,
                                             bool effects_enabled,
                                             bool depthcue_enabled) {
    if (!assets_) return;
    camera& cam = assets_->getView();
    camera::RealismSettings effective = settings;
    if (!depthcue_enabled) {
        effective.foreground_texture_max_opacity = 0;
        effective.background_texture_max_opacity = 0;
    }
    cam.set_realism_settings(effective);
    cam.set_realism_enabled(effects_enabled);
    cam.set_parallax_enabled(effects_enabled);
    if (assets_) {
        assets_->set_depth_effects_enabled(depthcue_enabled);
        assets_->apply_camera_runtime_settings();
    } else if (depthcue_enabled != last_depthcue_enabled_) {
        devmode::camera_prefs::save_depthcue_enabled(depthcue_enabled);
    }
    last_settings_ = settings;
    last_realism_enabled_ = effects_enabled;
    devmode::camera_prefs::save_foreground_texture_max_opacity(settings.foreground_texture_max_opacity);
    devmode::camera_prefs::save_background_texture_max_opacity(settings.background_texture_max_opacity);
    last_depthcue_enabled_ = depthcue_enabled;
    refresh_side_view_preview();
}

camera::RealismSettings CameraUIPanel::read_settings_from_ui() const {
    camera::RealismSettings settings = last_settings_;
    if (min_render_size_slider_) settings.min_visible_screen_ratio = std::clamp(min_render_size_slider_->value(), 0.0f, 0.5f);
    if (foreshorten_strength_slider_) settings.foreshorten_strength = std::max(0.0f, foreshorten_strength_slider_->value());
    if (distance_strength_slider_) settings.distance_scale_strength = std::max(0.0f, distance_strength_slider_->value());
    if (render_quality_slider_) settings.render_quality_percent = render_quality_slider_->value();
    if (smoothing_checkbox_) settings.smooth_motion_zoom = smoothing_checkbox_->value();

    TransformSmoothingMethod method = settings.motion_smoothing_method;
    if (smoothing_method_dropdown_) {
        method = method_from_index(smoothing_method_dropdown_->selected());
    }
    settings.motion_smoothing_method = method;

    if (motion_tau_slider_) settings.motion_smoothing_tau = std::max(0.0f, motion_tau_slider_->value());
    if (motion_stiffness_slider_) settings.motion_smoothing_spring_frequency = std::max(0.0f, motion_stiffness_slider_->value());
    if (motion_max_step_slider_) settings.motion_smoothing_max_step = std::max(0.0f, motion_max_step_slider_->value());
    if (motion_snap_slider_) settings.motion_smoothing_snap_threshold = std::max(0.0f, motion_snap_slider_->value());

    if (parallax_smoothing_slider_) {
        const float slider_value = std::max(0.0f, parallax_smoothing_slider_->value());
        if (method == TransformSmoothingMethod::Lerp) {
            settings.parallax_smoothing.lerp_rate = rate_from_tau(slider_value);
            settings.parallax_smoothing.spring_frequency = 0.0f;
        } else {
            settings.parallax_smoothing.spring_frequency = slider_value;
            settings.parallax_smoothing.lerp_rate = 0.0f;
        }
    }
    settings.parallax_smoothing.method = method;
    if (hysteresis_margin_slider_) {
        settings.scale_variant_hysteresis_margin = std::max(0.0f, hysteresis_margin_slider_->value());
    }
    if (side_view_panel_) {
        const CameraDepthViewValues vals = side_view_panel_->values();
        settings.zoom_low        = vals.zoom_low;
        settings.zoom_high       = vals.zoom_high;
        settings.height_low_px   = vals.height_low_px;
        settings.height_high_px  = vals.height_high_px;
        settings.grid_pitch_degrees   = std::clamp(vals.pitch_degrees,
                                                   camera::kMinPitchDegrees,
                                                   camera::kMaxPitchDegrees);
        settings.grid_depth_offset_px = vals.depth_offset_px;
    }
    // Depth cue texture settings
    auto slider_to_opacity = [](const FloatSliderWidget* slider) -> int {
        if (!slider) return 0;
        const float clamped = std::clamp(slider->value(), 0.0f, 255.0f);
        return static_cast<int>(std::round(clamped));
    };
    settings.foreground_texture_max_opacity = slider_to_opacity(foreground_texture_opacity_slider_.get());
    settings.background_texture_max_opacity = slider_to_opacity(background_texture_opacity_slider_.get());
    auto clamp_curve_selection = [](DMDropdown* dropdown) -> camera::BlurFalloffMethod {
        if (!dropdown) return camera::BlurFalloffMethod::Linear;
        int sel = dropdown->selected();
        sel = std::clamp(sel, 0, 4);
        return static_cast<camera::BlurFalloffMethod>(sel);
    };
    settings.texture_opacity_falloff_method = clamp_curve_selection(texture_opacity_interp_dropdown_.get());
    return settings;
}
