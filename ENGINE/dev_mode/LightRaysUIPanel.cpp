#include "LightRaysUIPanel.hpp"

#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "render/light_rays.hpp"
#include "render/light_rays_config.hpp"
#include "utils/input.hpp"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
constexpr int kGammaScale = 100;             // 0.01 steps
constexpr int kGammaMin = 10;                // 0.10
constexpr int kGammaMax = 400;               // 4.00

constexpr int kMinLumaScale = 1000;          // 0.001 steps
constexpr int kMinLumaMin = 0;
constexpr int kMinLumaMax = 1000;

constexpr int kPercentileScale = 1000;       // 0.001 steps
constexpr int kPercentileMin = 0;
constexpr int kPercentileMax = 1000;

constexpr int kDensityScale = 100;           // 0.01 steps
constexpr int kDensityMin = 1;               // 0.01
constexpr int kDensityMax = 400;             // 4.00

constexpr int kDecayScale = 1000;            // 0.001 steps
constexpr int kDecayMin = 500;               // 0.500
constexpr int kDecayMax = 999;               // 0.999

constexpr int kWeightScale = 100;            // 0.01 steps
constexpr int kWeightMin = 0;
constexpr int kWeightMax = 800;              // 8.00

constexpr int kExposureScale = 100;          // 0.01 steps
constexpr int kExposureMin = 0;
constexpr int kExposureMax = 800;            // 8.00

constexpr int kSamplesMin = 1;
constexpr int kSamplesMax = 512;

constexpr int kDownsampleMin = 0;
constexpr int kDownsampleMax = 4;

constexpr std::array<const char*, 4> kMetricNames = {
    "Luma709", "MaxRGB", "AvgRGB", "EnergyRGB"
};

LightRaysConfig default_light_rays_config() {
    return LightRaysConfig::defaults();
}

int metric_to_index(BrightnessMetric metric) {
    switch (metric) {
        case BrightnessMetric::Luma709:  return 0;
        case BrightnessMetric::MaxRGB:   return 1;
        case BrightnessMetric::AvgRGB:   return 2;
        case BrightnessMetric::EnergyRGB:return 3;
    }
    return 1;
}

BrightnessMetric index_to_metric(int index) {
    switch (index) {
        case 0: return BrightnessMetric::Luma709;
        case 1: return BrightnessMetric::MaxRGB;
        case 2: return BrightnessMetric::AvgRGB;
        case 3: return BrightnessMetric::EnergyRGB;
    }
    return BrightnessMetric::MaxRGB;
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
    const LightRaysConfig defaults = default_light_rays_config();
    std::vector<std::string> metric_options;
    metric_options.reserve(kMetricNames.size());
    for (const char* name : kMetricNames) {
        metric_options.emplace_back(name);
    }

    rays_enabled_checkbox_ = std::make_unique<DMCheckbox>("Enable Light Rays", defaults.enabled);
    per_light_checkbox_ = std::make_unique<DMCheckbox>("Enable Per-Light Rays", defaults.per_light_enabled);
    use_alpha_checkbox_ = std::make_unique<DMCheckbox>("Use Alpha In Mask", defaults.per_light.use_alpha_in_mask);

    metric_dropdown_ = std::make_unique<DMDropdown>(
        "Brightness Metric", metric_options, metric_to_index(defaults.per_light.metric));

    gamma_comp_slider_ = std::make_unique<DMSlider>(
        "Gamma Compensation", kGammaMin, kGammaMax,
        double_to_slider_units(defaults.per_light.gamma_comp, kGammaScale, kGammaMin, kGammaMax));
    configure_float_slider(gamma_comp_slider_.get(), kGammaScale, 2);

    min_luma_slider_ = std::make_unique<DMSlider>(
        "Min Luma Threshold", kMinLumaMin, kMinLumaMax,
        double_to_slider_units(defaults.per_light.min_luma_threshold, kMinLumaScale, kMinLumaMin, kMinLumaMax));
    configure_float_slider(min_luma_slider_.get(), kMinLumaScale, 3);

    bright_percentile_slider_ = std::make_unique<DMSlider>(
        "Bright Percentile", kPercentileMin, kPercentileMax,
        double_to_slider_units(defaults.per_light.bright_percentile, kPercentileScale, kPercentileMin, kPercentileMax));
    configure_float_slider(bright_percentile_slider_.get(), kPercentileScale, 3);

    samples_slider_ = std::make_unique<DMSlider>(
        "Samples", kSamplesMin, kSamplesMax,
        clamp_int(defaults.per_light.samples, kSamplesMin, kSamplesMax));

    density_slider_ = std::make_unique<DMSlider>(
        "Density", kDensityMin, kDensityMax,
        double_to_slider_units(defaults.per_light.density, kDensityScale, kDensityMin, kDensityMax));
    configure_float_slider(density_slider_.get(), kDensityScale, 2);

    decay_slider_ = std::make_unique<DMSlider>(
        "Decay", kDecayMin, kDecayMax,
        double_to_slider_units(defaults.per_light.decay, kDecayScale, kDecayMin, kDecayMax));
    configure_float_slider(decay_slider_.get(), kDecayScale, 3);

    weight_slider_ = std::make_unique<DMSlider>(
        "Weight", kWeightMin, kWeightMax,
        double_to_slider_units(defaults.per_light.weight, kWeightScale, kWeightMin, kWeightMax));
    configure_float_slider(weight_slider_.get(), kWeightScale, 2);

    exposure_slider_ = std::make_unique<DMSlider>(
        "Exposure", kExposureMin, kExposureMax,
        double_to_slider_units(defaults.per_light.exposure, kExposureScale, kExposureMin, kExposureMax));
    configure_float_slider(exposure_slider_.get(), kExposureScale, 2);

    downsample_slider_ = std::make_unique<DMSlider>(
        "Downsample (log2)", kDownsampleMin, kDownsampleMax,
        clamp_int(defaults.per_light.downsample_log2, kDownsampleMin, kDownsampleMax));

    widget_wrappers_.clear();
    widget_wrappers_.reserve(20);

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

    rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(rays_enabled_checkbox_.get())) });
    rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(per_light_checkbox_.get())) });
    rows.push_back({ add_widget(std::make_unique<DropdownWidget>(metric_dropdown_.get())) });
    rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(use_alpha_checkbox_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(gamma_comp_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(min_luma_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(bright_percentile_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(samples_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(density_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(decay_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(weight_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(exposure_slider_.get())) });
    rows.push_back({ add_widget(std::make_unique<SliderWidget>(downsample_slider_.get())) });

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
    LightRaysConfig config = default_light_rays_config();
    if (map_info_ && map_info_->is_object()) {
        auto it = map_info_->find("light_rays_params");
        if (it != map_info_->end()) {
            config = LightRaysConfig::from_json(*it);
        }
    }

    if (rays_enabled_checkbox_) {
        rays_enabled_checkbox_->set_value(config.enabled);
    }
    if (per_light_checkbox_) {
        per_light_checkbox_->set_value(config.per_light_enabled);
    }
    if (use_alpha_checkbox_) {
        use_alpha_checkbox_->set_value(config.per_light.use_alpha_in_mask);
    }
    if (metric_dropdown_) {
        metric_dropdown_->set_selected(metric_to_index(config.per_light.metric));
    }
    if (gamma_comp_slider_) {
        gamma_comp_slider_->set_value(double_to_slider_units(config.per_light.gamma_comp,
                                                             kGammaScale, kGammaMin, kGammaMax));
    }
    if (min_luma_slider_) {
        min_luma_slider_->set_value(double_to_slider_units(config.per_light.min_luma_threshold,
                                                           kMinLumaScale, kMinLumaMin, kMinLumaMax));
    }
    if (bright_percentile_slider_) {
        bright_percentile_slider_->set_value(double_to_slider_units(config.per_light.bright_percentile,
                                                                    kPercentileScale, kPercentileMin, kPercentileMax));
    }
    if (samples_slider_) {
        samples_slider_->set_value(clamp_int(config.per_light.samples, kSamplesMin, kSamplesMax));
    }
    if (density_slider_) {
        density_slider_->set_value(double_to_slider_units(config.per_light.density,
                                                          kDensityScale, kDensityMin, kDensityMax));
    }
    if (decay_slider_) {
        decay_slider_->set_value(double_to_slider_units(config.per_light.decay,
                                                        kDecayScale, kDecayMin, kDecayMax));
    }
    if (weight_slider_) {
        weight_slider_->set_value(double_to_slider_units(config.per_light.weight,
                                                         kWeightScale, kWeightMin, kWeightMax));
    }
    if (exposure_slider_) {
        exposure_slider_->set_value(double_to_slider_units(config.per_light.exposure,
                                                           kExposureScale, kExposureMin, kExposureMax));
    }
    if (downsample_slider_) {
        downsample_slider_->set_value(clamp_int(config.per_light.downsample_log2,
                                                kDownsampleMin, kDownsampleMax));
    }

    needs_sync_ = false;
}

void LightRaysUIPanel::sync_json_from_ui() {
    if (!map_info_) return;

    LightRaysConfig config = default_light_rays_config();

    if (rays_enabled_checkbox_) {
        config.enabled = rays_enabled_checkbox_->value();
    }
    if (per_light_checkbox_) {
        config.per_light_enabled = per_light_checkbox_->value();
    }
    if (use_alpha_checkbox_) {
        config.per_light.use_alpha_in_mask = use_alpha_checkbox_->value();
    }
    if (metric_dropdown_) {
        config.per_light.metric = index_to_metric(metric_dropdown_->selected());
    }
    if (gamma_comp_slider_) {
        config.per_light.gamma_comp = static_cast<float>(
            slider_units_to_double(gamma_comp_slider_->value(), kGammaScale));
    }
    if (min_luma_slider_) {
        config.per_light.min_luma_threshold = static_cast<float>(
            slider_units_to_double(min_luma_slider_->value(), kMinLumaScale));
    }
    if (bright_percentile_slider_) {
        config.per_light.bright_percentile = static_cast<float>(
            slider_units_to_double(bright_percentile_slider_->value(), kPercentileScale));
    }
    if (samples_slider_) {
        config.per_light.samples = clamp_int(samples_slider_->value(), kSamplesMin, kSamplesMax);
    }
    if (density_slider_) {
        config.per_light.density = static_cast<float>(
            slider_units_to_double(density_slider_->value(), kDensityScale));
    }
    if (decay_slider_) {
        config.per_light.decay = static_cast<float>(
            slider_units_to_double(decay_slider_->value(), kDecayScale));
    }
    if (weight_slider_) {
        config.per_light.weight = static_cast<float>(
            slider_units_to_double(weight_slider_->value(), kWeightScale));
    }
    if (exposure_slider_) {
        config.per_light.exposure = static_cast<float>(
            slider_units_to_double(exposure_slider_->value(), kExposureScale));
    }
    if (downsample_slider_) {
        config.per_light.downsample_log2 = clamp_int(downsample_slider_->value(), kDownsampleMin, kDownsampleMax);
    }

    bool changed = false;

    if (config.enabled) {
        if (!map_info_->is_object()) {
            *map_info_ = json::object();
            changed = true;
        }

        json new_config = config.to_json();
        json* dest = nullptr;
        if (map_info_->is_object()) {
            dest = &(*map_info_)["light_rays_params"];
            if (*dest != new_config) {
                *dest = std::move(new_config);
                changed = true;
            }
        }
    } else if (map_info_->is_object()) {
        changed = map_info_->erase("light_rays_params") > 0;
    }

    bool ok = true;
    if (changed && on_save_) {
        ok = on_save_();
    }
    update_save_status(ok);
    needs_sync_ = false;
}

void LightRaysUIPanel::update_save_status(bool success) const {
    if (!status_label_) {
        return;
    }
    const std::string failure_message = "Failed to save light ray settings. Check logs.";
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
