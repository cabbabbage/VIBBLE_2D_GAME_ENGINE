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
constexpr int kMinLumaMin = 0;
constexpr int kMinLumaMax = 100;
constexpr int kBrightPercentileMin = 0;
constexpr int kBrightPercentileMax = 100;
constexpr int kDensityMin = 0;
constexpr int kDensityMax = 400;      // 0.00 .. 4.00
constexpr int kDecayMin = 0;
constexpr int kDecayMax = 100;        // 0.00 .. 1.00
constexpr int kWeightMin = 0;
constexpr int kWeightMax = 2000;      // 0.00 .. 20.00
constexpr int kExposureMin = 0;
constexpr int kExposureMax = 2000;    // 0.00 .. 20.00
constexpr int kSamplesMin = 1;
constexpr int kSamplesMax = 256;
constexpr int kDownsampleMin = 0;
constexpr int kDownsampleMax = 4;

constexpr double clamp_double(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}
}

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
    : DockableCollapsible("Light Rays", true, x, y) {
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
    enabled_checkbox_ = std::make_unique<DMCheckbox>("Enable Light Rays", true);

    min_luma_slider_ = std::make_unique<DMSlider>("Min Luma Threshold", kMinLumaMin, kMinLumaMax, 10);
    configure_float_slider(min_luma_slider_.get(), kFloatScale, 2);

    bright_percentile_slider_ = std::make_unique<DMSlider>("Bright Percentile", kBrightPercentileMin, kBrightPercentileMax, 94);
    configure_float_slider(bright_percentile_slider_.get(), kFloatScale, 2);

    density_slider_ = std::make_unique<DMSlider>("Density", kDensityMin, kDensityMax, 135);
    configure_float_slider(density_slider_.get(), kFloatScale, 2);

    decay_slider_ = std::make_unique<DMSlider>("Decay", kDecayMin, kDecayMax, 94);
    configure_float_slider(decay_slider_.get(), kFloatScale, 2);

    weight_slider_ = std::make_unique<DMSlider>("Weight", kWeightMin, kWeightMax, 675);
    configure_float_slider(weight_slider_.get(), kFloatScale, 2);

    exposure_slider_ = std::make_unique<DMSlider>("Exposure", kExposureMin, kExposureMax, 860);
    configure_float_slider(exposure_slider_.get(), kFloatScale, 2);

    samples_slider_ = std::make_unique<DMSlider>("Samples", kSamplesMin, kSamplesMax, 112);
    downsample_slider_ = std::make_unique<DMSlider>("Downsample log2", kDownsampleMin, kDownsampleMax, 1);

    widget_wrappers_.clear();
    widget_wrappers_.reserve(16);

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
        add_widget(std::make_unique<SliderWidget>(min_luma_slider_.get())),
        add_widget(std::make_unique<SliderWidget>(bright_percentile_slider_.get()))
    });

    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(density_slider_.get())),
        add_widget(std::make_unique<SliderWidget>(decay_slider_.get()))
    });

    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(weight_slider_.get())),
        add_widget(std::make_unique<SliderWidget>(exposure_slider_.get()))
    });

    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(samples_slider_.get())),
        add_widget(std::make_unique<SliderWidget>(downsample_slider_.get()))
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

nlohmann::json& LightRaysUIPanel::ensure_params() {
    static json dummy = json::object();
    if (!map_info_) {
        return dummy;
    }
    if (!map_info_->is_object()) {
        *map_info_ = json::object();
    }
    json& root = (*map_info_)["light_rays_params"];
    if (!root.is_object()) {
        root = json::object();
    }

    bool enabled = root.value("enabled", true);
    root["enabled"] = enabled;

    double min_luma = clamp_double(root.value("min_luma_threshold", 0.1), 0.0, 1.0);
    root["min_luma_threshold"] = min_luma;

    double bright = clamp_double(root.value("bright_percentile", 0.94), 0.0, 1.0);
    root["bright_percentile"] = bright;

    double density = clamp_double(root.value("density", 1.35), 0.0, 4.0);
    root["density"] = density;

    double decay = clamp_double(root.value("decay", 0.94), 0.0, 1.0);
    root["decay"] = decay;

    double weight = clamp_double(root.value("weight", 6.75), 0.0, 20.0);
    root["weight"] = weight;

    double exposure = clamp_double(root.value("exposure", 8.6), 0.0, 20.0);
    root["exposure"] = exposure;

    int samples = clamp_int(root.value("samples", 112), kSamplesMin, kSamplesMax);
    root["samples"] = samples;

    int downsample = clamp_int(root.value("downsample_log2", 1), kDownsampleMin, kDownsampleMax);
    root["downsample_log2"] = downsample;

    return root;
}

void LightRaysUIPanel::sync_ui_from_json() {
    json& params = ensure_params();

    if (enabled_checkbox_) {
        enabled_checkbox_->set_value(params.value("enabled", true));
    }

    if (min_luma_slider_) {
        int units = double_to_slider_units(params.value("min_luma_threshold", 0.1), kFloatScale, kMinLumaMin, kMinLumaMax);
        min_luma_slider_->set_value(units);
    }
    if (bright_percentile_slider_) {
        int units = double_to_slider_units(params.value("bright_percentile", 0.94), kFloatScale, kBrightPercentileMin, kBrightPercentileMax);
        bright_percentile_slider_->set_value(units);
    }
    if (density_slider_) {
        int units = double_to_slider_units(params.value("density", 1.35), kFloatScale, kDensityMin, kDensityMax);
        density_slider_->set_value(units);
    }
    if (decay_slider_) {
        int units = double_to_slider_units(params.value("decay", 0.94), kFloatScale, kDecayMin, kDecayMax);
        decay_slider_->set_value(units);
    }
    if (weight_slider_) {
        int units = double_to_slider_units(params.value("weight", 6.75), kFloatScale, kWeightMin, kWeightMax);
        weight_slider_->set_value(units);
    }
    if (exposure_slider_) {
        int units = double_to_slider_units(params.value("exposure", 8.6), kFloatScale, kExposureMin, kExposureMax);
        exposure_slider_->set_value(units);
    }
    if (samples_slider_) {
        samples_slider_->set_value(clamp_int(params.value("samples", 112), kSamplesMin, kSamplesMax));
    }
    if (downsample_slider_) {
        downsample_slider_->set_value(clamp_int(params.value("downsample_log2", 1), kDownsampleMin, kDownsampleMax));
    }

    needs_sync_ = false;
}

void LightRaysUIPanel::sync_json_from_ui() {
    if (!map_info_) return;

    json& params = ensure_params();

    if (enabled_checkbox_) {
        params["enabled"] = enabled_checkbox_->value();
    }
    if (min_luma_slider_) {
        params["min_luma_threshold"] = slider_units_to_double(min_luma_slider_->value(), kFloatScale);
    }
    if (bright_percentile_slider_) {
        params["bright_percentile"] = slider_units_to_double(bright_percentile_slider_->value(), kFloatScale);
    }
    if (density_slider_) {
        params["density"] = slider_units_to_double(density_slider_->value(), kFloatScale);
    }
    if (decay_slider_) {
        params["decay"] = slider_units_to_double(decay_slider_->value(), kFloatScale);
    }
    if (weight_slider_) {
        params["weight"] = slider_units_to_double(weight_slider_->value(), kFloatScale);
    }
    if (exposure_slider_) {
        params["exposure"] = slider_units_to_double(exposure_slider_->value(), kFloatScale);
    }
    if (samples_slider_) {
        params["samples"] = clamp_int(samples_slider_->value(), kSamplesMin, kSamplesMax);
    }
    if (downsample_slider_) {
        params["downsample_log2"] = clamp_int(downsample_slider_->value(), kDownsampleMin, kDownsampleMax);
    }

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
    const std::string failure_message = "Failed to save light rays settings. Check logs.";
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

