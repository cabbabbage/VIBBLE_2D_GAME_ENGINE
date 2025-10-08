#include "LightRaysUIPanel.hpp"

#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "utils/input.hpp"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
constexpr int kFloatScale = 100;
constexpr int kFinalBlurRadiusMin = 0;
constexpr int kFinalBlurRadiusMax = 1000;   // 0.00 .. 10.00
constexpr int kFinalBlurMixMin = 0;
constexpr int kFinalBlurMixMax = 100;        // 0.00 .. 1.00

constexpr bool   kEnabledDefault = false;
constexpr double kFinalBlurRadiusDefault = 2.5;
constexpr double kFinalBlurMixDefault = 0.85;

constexpr double clamp_double(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}

json default_light_rays_params() {
    return json{
        {"enabled", kEnabledDefault},
        {"final_blur_radius", kFinalBlurRadiusDefault},
        {"final_blur_mix", kFinalBlurMixDefault},
    };
}

json sanitize_params(const json* candidate) {
    json params = default_light_rays_params();
    if (!candidate || !candidate->is_object()) {
        return params;
    }

    const json& root = *candidate;
    auto safe_bool = [&](const char* key, bool def) {
        try {
            return root.at(key).get<bool>();
        } catch (...) {
            return def;
        }
    };
    auto safe_double = [&](const char* key, double def, double lo, double hi) {
        double value = def;
        try {
            value = root.at(key).get<double>();
        } catch (...) {
        }
        return clamp_double(value, lo, hi);
    };

    params["enabled"] = safe_bool("enabled", kEnabledDefault);
    params["final_blur_radius"] = safe_double("final_blur_radius", kFinalBlurRadiusDefault, 0.0, 10.0);
    params["final_blur_mix"] = safe_double("final_blur_mix", kFinalBlurMixDefault, 0.0, 1.0);

    return params;
}
} // namespace

class LightRaysUIPanel::StatusLabel : public Widget {
public:
    StatusLabel() = default;

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    void set_color(SDL_Color color) { color_ = color; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        if (text_.empty()) {
            return 0;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return style.font_size;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, w));
        int height = surface ? surface->h : style.font_size;
        if (surface) {
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
        return height + DMSpacing::small_gap();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (text_.empty() || !renderer) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, rect_.w));
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
            if (tex) {
                SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 120, 120, 255};
};

LightRaysUIPanel::LightRaysUIPanel(int x, int y)
    : DockableCollapsible("Final Blur", true, x, y) {
    set_expanded(true);
    set_visible(false);
    set_close_button_enabled(true);
    set_padding(16);
    set_cell_width(260);
    set_col_gap(12);
    set_row_gap(8);
    build_ui();
    update_save_status(true);
}

LightRaysUIPanel::~LightRaysUIPanel() = default;

void LightRaysUIPanel::set_map_info(json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    update_save_status(true);
    sync_ui_from_json();
}

void LightRaysUIPanel::open() {
    set_visible(true);
    set_expanded(true);
    force_pointer_ready();
    sync_ui_from_json();
}

void LightRaysUIPanel::close() { set_visible(false); }

void LightRaysUIPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool LightRaysUIPanel::is_visible() const { return visible_; }

void LightRaysUIPanel::build_ui() {
    enabled_checkbox_ = std::make_unique<DMCheckbox>("Enable Final Blur", true);

    final_blur_radius_slider_ = std::make_unique<DMSlider>(
        "Blur Radius", kFinalBlurRadiusMin, kFinalBlurRadiusMax,
        double_to_slider_units(kFinalBlurRadiusDefault, kFloatScale, kFinalBlurRadiusMin, kFinalBlurRadiusMax));
    configure_float_slider(final_blur_radius_slider_.get(), kFloatScale, 2);

    final_blur_mix_slider_ = std::make_unique<DMSlider>(
        "Blur Mix", kFinalBlurMixMin, kFinalBlurMixMax,
        double_to_slider_units(kFinalBlurMixDefault, kFloatScale, kFinalBlurMixMin, kFinalBlurMixMax));
    configure_float_slider(final_blur_mix_slider_.get(), kFloatScale, 2);

    widget_wrappers_.clear();
    widget_wrappers_.reserve(6);

    auto add_widget = [this](std::unique_ptr<Widget> w) -> Widget* {
        Widget* raw = w.get();
        widget_wrappers_.push_back(std::move(w));
        return raw;
    };

    Rows rows;

    auto status = std::make_unique<StatusLabel>();
    status_label_ = status.get();
    status_label_->set_color(SDL_Color{255, 120, 120, 255});
    rows.push_back({ add_widget(std::move(status)) });

    rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(enabled_checkbox_.get())) });

    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(final_blur_radius_slider_.get())),
        add_widget(std::make_unique<SliderWidget>(final_blur_mix_slider_.get()))
    });

    set_rows(rows);
}

void LightRaysUIPanel::configure_float_slider(DMSlider* slider, int scale, int precision) {
    if (!slider) return;
    slider->set_value_formatter([scale, precision](int units) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision)
            << slider_units_to_double(units, scale);
        return oss.str();
    });
    slider->set_value_parser([scale](const std::string& text) -> std::optional<int> {
        try {
            double value = std::stod(text);
            int units = double_to_slider_units(value, scale, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            return units;
        } catch (...) {
            return std::nullopt;
        }
    });
}

int LightRaysUIPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

double LightRaysUIPanel::slider_units_to_double(int units, int scale) {
    return static_cast<double>(units) / static_cast<double>(scale);
}

int LightRaysUIPanel::double_to_slider_units(double value, int scale, int lo, int hi) {
    double scaled = std::round(value * static_cast<double>(scale));
    int as_int = static_cast<int>(scaled);
    if (lo != std::numeric_limits<int>::min() && hi != std::numeric_limits<int>::max()) {
        as_int = clamp_int(as_int, lo, hi);
    }
    return as_int;
}

void LightRaysUIPanel::sync_ui_from_json() {
    json params = default_light_rays_params();
    if (map_info_ && map_info_->is_object()) {
        auto it = map_info_->find("light_rays_params");
        if (it != map_info_->end()) {
            params = sanitize_params(&(*it));
        }
    }

    if (enabled_checkbox_) {
        enabled_checkbox_->set_value(params.value("enabled", kEnabledDefault));
    }
    if (final_blur_radius_slider_) {
        int units = double_to_slider_units(params.value("final_blur_radius", kFinalBlurRadiusDefault),
                                           kFloatScale, kFinalBlurRadiusMin, kFinalBlurRadiusMax);
        final_blur_radius_slider_->set_value(units);
    }
    if (final_blur_mix_slider_) {
        int units = double_to_slider_units(params.value("final_blur_mix", kFinalBlurMixDefault),
                                           kFloatScale, kFinalBlurMixMin, kFinalBlurMixMax);
        final_blur_mix_slider_->set_value(units);
    }

    needs_sync_ = false;
}

void LightRaysUIPanel::sync_json_from_ui() {
    if (!map_info_) return;

    json params = default_light_rays_params();

    if (enabled_checkbox_) {
        params["enabled"] = enabled_checkbox_->value();
    }
    if (final_blur_radius_slider_) {
        params["final_blur_radius"] = slider_units_to_double(final_blur_radius_slider_->value(), kFloatScale);
    }
    if (final_blur_mix_slider_) {
        params["final_blur_mix"] = slider_units_to_double(final_blur_mix_slider_->value(), kFloatScale);
    }

    if (!map_info_->is_object()) {
        *map_info_ = json::object();
    }
    (*map_info_)["light_rays_params"] = std::move(params);

    bool ok = true;
    if (on_save_) {
        ok = on_save_();
    }
    update_save_status(ok);
    needs_sync_ = false;
}

void LightRaysUIPanel::update_save_status(bool success) const {
    if (!status_label_) {
        return;
    }
    const std::string failure_message = "Failed to save blur settings. Check logs.";
    if (success) {
        if (!status_text_.empty()) {
            status_text_.clear();
            status_label_->set_text({});
            const_cast<LightRaysUIPanel*>(this)->layout();
        }
        return;
    }
    if (status_text_ != failure_message) {
        status_text_ = failure_message;
        status_label_->set_text(status_text_);
        const_cast<LightRaysUIPanel*>(this)->layout();
    }
}

void LightRaysUIPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool LightRaysUIPanel::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    bool used = DockableCollapsible::handle_event(e);
    if (used) {
        needs_sync_ = true;
    }
    if (needs_sync_) {
        sync_json_from_ui();
    }
    return used;
}

void LightRaysUIPanel::render(SDL_Renderer* renderer) const {
    if (!visible_) return;
    DockableCollapsible::render(renderer);
}

bool LightRaysUIPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}
