#include "MapLightPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <SDL_ttf.h>

#include "dev_mode/dev_ui_settings.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "utils/input.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

using nlohmann::json;

namespace {

constexpr std::string_view kUpdateMapLightSettingKey = "dev_ui.lighting.map_panel.update_map_light";
constexpr std::string_view kReactiveSettingsKeyPrefix = "dev_ui.lighting.map_panel.reactive";

} // namespace

using render_pipeline::shading::ReactiveShadowSettings;

namespace {

std::string reactive_settings_key(std::string_view suffix) {
    std::string key(kReactiveSettingsKeyPrefix);
    if (!suffix.empty()) {
        key.push_back('.');
        key.append(suffix);
    }
    return key;
}

float slider_value_scaled(const std::unique_ptr<DMSlider>& slider, float fallback, int scale) {
    if (!slider) {
        return fallback;
    }
    return static_cast<float>(slider->displayed_value()) / static_cast<float>(scale);
}

void set_slider_scaled(const std::unique_ptr<DMSlider>& slider, float value, int scale) {
    if (!slider) {
        return;
    }
    slider->set_value(static_cast<int>(std::round(value * static_cast<float>(scale))));
}

constexpr float kLensSizeBaseMin = 140.0f;
constexpr float kLensSizeBaseMax = 420.0f;
constexpr float kLensSizeScaleMin = 0.5f;
constexpr float kLensSizeScaleMax = 2.5f;
constexpr int   kLensFadeFramesMin = 30;
constexpr int   kLensFadeFramesMax = 300;

float lens_size_scale_from_slider(int slider_value) {
    int clamped = std::clamp(slider_value, 0, 100);
    float t = static_cast<float>(clamped) / 100.0f;
    return kLensSizeScaleMin + t * (kLensSizeScaleMax - kLensSizeScaleMin);
}

int lens_size_slider_from_settings(const LensFlareRenderer::Settings& settings) {
    float base = settings.ghost_size_min > 0.0f ? settings.ghost_size_min / kLensSizeBaseMin : 1.0f;
    float t = (base - kLensSizeScaleMin) / (kLensSizeScaleMax - kLensSizeScaleMin);
    int slider = static_cast<int>(std::round(t * 100.0f));
    return std::clamp(slider, 0, 100);
}

int lens_fade_slider_from_settings(const LensFlareRenderer::Settings& settings) {
    float rise = std::max(settings.ghost_alpha_rise, 1e-4f);
    int frames = static_cast<int>(std::round(1.0f / rise));
    return std::clamp(frames, kLensFadeFramesMin, kLensFadeFramesMax);
}

void apply_lens_size_slider(LensFlareRenderer::Settings& settings, int slider_value) {
    float scale = lens_size_scale_from_slider(slider_value);
    settings.ghost_size_min = kLensSizeBaseMin * scale;
    settings.ghost_size_max = kLensSizeBaseMax * scale;
}

void apply_lens_fade_slider(LensFlareRenderer::Settings& settings, int frames) {
    int clamped = std::clamp(frames, kLensFadeFramesMin, kLensFadeFramesMax);
    settings.ghost_alpha_rise = 1.0f / static_cast<float>(clamped);
    settings.ghost_alpha_fall = 1.0f / static_cast<float>(std::max(clamped, 1) * 1.6f);
}

}  // namespace

class MapLightPanel::WarningLabel : public Widget {
public:
    WarningLabel() = default;

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

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

    void render(SDL_Renderer* r) const override {
        if (text_.empty() || !r) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, rect_.w));
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surface);
            if (tex) {
                SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

    void set_color(SDL_Color color) { color_ = color; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 120, 120, 255};
};

int MapLightPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::clamp_float(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::wrap_angle(float a) {

    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

MapLightPanel::MapLightPanel(int x, int y)
: DockableCollapsible("Map Lighting", true, x, y) {
    set_expanded(true);
    build_ui();
    update_save_status(true);
}

MapLightPanel::~MapLightPanel() = default;

void MapLightPanel::set_reactive_settings(ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (reactive_settings_shared_ && reactive_settings_initialized_) {
        *reactive_settings_shared_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_reactive_);
    }
}

void MapLightPanel::set_map_info(json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    current_key_index_ = 0;
    editing_light_ = json::object();
    reactive_settings_initialized_ = false;
    if (map_info_ && map_info_->contains("map_light_data") && (*map_info_)["map_light_data"].is_object()) {
        editing_light_ = (*map_info_)["map_light_data"];
    }
    ensure_light();
    update_save_status(true);
    load_update_map_light_setting();
    sync_ui_from_json();
}

void MapLightPanel::open()   {
    set_visible(true);
    set_expanded(true);
}
void MapLightPanel::close()  { set_visible(false); }
void MapLightPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}
bool MapLightPanel::is_visible() const { return visible_; }

void MapLightPanel::build_ui() {

    update_map_light_checkbox_ = std::make_unique<DMCheckbox>("Update Map Light", false);
    update_btn_ = std::make_unique<DMButton>("Update Light", &DMStyles::AccentButton(), 160, DMButton::height());
    orbit_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    screen_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    texture_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    reactive_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    lens_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    update_section_header_labels();

    auto make_float_slider = [&](const std::string& label,
                                 float min,
                                 float max,
                                 float initial,
                                 int scale,
                                 int decimals = 2) {
        int min_i = static_cast<int>(std::round(min * static_cast<float>(scale)));
        int max_i = static_cast<int>(std::round(max * static_cast<float>(scale)));
        int init_i = static_cast<int>(std::round(initial * static_cast<float>(scale)));
        auto slider = std::make_unique<DMSlider>(label, min_i, max_i, init_i);
        slider->set_value_formatter([scale, decimals](int value, auto& buffer) -> std::string_view {
            const float actual = static_cast<float>(value) / static_cast<float>(scale);
            const char* fmt = (decimals == 3) ? "%.3f" : "%.2f";
            std::snprintf(buffer.data(), buffer.size(), fmt, actual);
            return std::string_view(buffer.data());
        });
        slider->set_value_parser([scale, min, max](const std::string& text) -> std::optional<int> {
            try {
                float parsed = std::stof(text);
                parsed = std::clamp(parsed, min, max);
                return static_cast<int>(std::round(parsed * static_cast<float>(scale)));
            } catch (...) {
                return std::nullopt;
            }
        });
        slider->set_defer_commit_until_unfocus(true);
        return slider;
    };

    radius_         = std::make_unique<DMSlider>("Radius",          0, 20000, 0);
    intensity_      = std::make_unique<DMSlider>("Intensity",       0,   255, 255);
    orbit_x_        = std::make_unique<DMSlider>("Orbit X Radius",  0, 20000, 0);
    orbit_y_        = std::make_unique<DMSlider>("Orbit Y Radius",  0, 20000, 0);
    update_interval_= std::make_unique<DMSlider>("Update Interval", 1,   120, 10);
    mult_x100_      = std::make_unique<DMSlider>("Mult x100",       0,   100, 0);
    falloff_        = std::make_unique<DMSlider>("Fall-off",        0,   100, 100);
    min_opacity_    = std::make_unique<DMSlider>("Min Opacity",     0,   255, 0);
    max_opacity_    = std::make_unique<DMSlider>("Max Opacity",     0,   255, 255);

    screen_r_          = std::make_unique<DMSlider>("Screen Base R",       0, 255, 255);
    screen_g_          = std::make_unique<DMSlider>("Screen Base G",       0, 255, 255);
    screen_b_          = std::make_unique<DMSlider>("Screen Base B",       0, 255, 255);
    screen_min_opacity_= std::make_unique<DMSlider>("Screen Min Opacity",  0, 255, 0);
    screen_max_opacity_= std::make_unique<DMSlider>("Screen Max Opacity",  0, 255, 255);

    if (update_interval_) update_interval_->set_defer_commit_until_unfocus(true);
    if (orbit_x_)        orbit_x_->set_defer_commit_until_unfocus(true);
    if (orbit_y_)        orbit_y_->set_defer_commit_until_unfocus(true);
    if (min_opacity_)    min_opacity_->set_defer_commit_until_unfocus(true);
    if (max_opacity_)    max_opacity_->set_defer_commit_until_unfocus(true);
    if (screen_r_)       screen_r_->set_defer_commit_until_unfocus(true);
    if (screen_g_)       screen_g_->set_defer_commit_until_unfocus(true);
    if (screen_b_)       screen_b_->set_defer_commit_until_unfocus(true);
    if (screen_min_opacity_) screen_min_opacity_->set_defer_commit_until_unfocus(true);
    if (screen_max_opacity_) screen_max_opacity_->set_defer_commit_until_unfocus(true);

    LensFlareRenderer::Settings default_lens = LensFlareRenderer::default_settings();
    lens_enabled_ = std::make_unique<DMCheckbox>("Enable Lens Flares", default_lens.enabled);
    lens_seed_threshold_ = make_float_slider("Sensitivity", 0.0f, 1.0f, default_lens.seed_threshold_norm, 100);
    lens_seed_ema_ = make_float_slider("Tracking Smoothness", 0.0f, 1.0f, default_lens.seed_pos_ema, 100);
    lens_size_scalar_ = std::make_unique<DMSlider>("Flare Size", 0, 100, lens_size_slider_from_settings(default_lens));
    lens_fade_frames_ = std::make_unique<DMSlider>("Fade Duration (frames)", kLensFadeFramesMin, kLensFadeFramesMax, lens_fade_slider_from_settings(default_lens));
    lens_intensity_gain_ = make_float_slider("Strength", 0.0f, 2.5f, default_lens.ghost_intensity_gain, 100);
    lens_alpha_cap_ = make_float_slider("Max Brightness", 0.0f, 1.0f, default_lens.ghost_alpha_cap, 100);

    if (lens_size_scalar_) lens_size_scalar_->set_defer_commit_until_unfocus(true);
    if (lens_fade_frames_) lens_fade_frames_->set_defer_commit_until_unfocus(true);

    base_r_ = std::make_unique<DMSlider>("Base R", 0, 255, 255);
    base_g_ = std::make_unique<DMSlider>("Base G", 0, 255, 255);
    base_b_ = std::make_unique<DMSlider>("Base B", 0, 255, 255);
    base_a_ = std::make_unique<DMSlider>("Base A", 0, 255, 255);

    prev_key_btn_ = std::make_unique<DMButton>("< Prev", &DMStyles::HeaderButton(), 120, DMButton::height());
    next_key_btn_ = std::make_unique<DMButton>("Next >", &DMStyles::HeaderButton(), 120, DMButton::height());
    add_pair_btn_ = std::make_unique<DMButton>("+ Pair @Angle", &DMStyles::HeaderButton(), 180, DMButton::height());
    delete_btn_   = std::make_unique<DMButton>("Delete Key", &DMStyles::HeaderButton(), 140, DMButton::height());

    key_angle_ = std::make_unique<DMSlider>("Key Angle (deg)", 0, 360, 0);
    key_r_     = std::make_unique<DMSlider>("Key R", 0, 255, 255);
    key_g_     = std::make_unique<DMSlider>("Key G", 0, 255, 255);
    key_b_     = std::make_unique<DMSlider>("Key B", 0, 255, 255);
    key_a_     = std::make_unique<DMSlider>("Key A", 0, 255, 255);

    reactive_offsets_enabled_ = std::make_unique<DMCheckbox>("Enable Offsets", last_applied_reactive_.directionality.enable_offsets);
    reactive_scale_enabled_   = std::make_unique<DMCheckbox>("Enable Scale", last_applied_reactive_.response.enable_scale);
    reactive_opacity_enabled_ = std::make_unique<DMCheckbox>("Enable Opacity", last_applied_reactive_.response.enable_opacity);
    reactive_temporal_enabled_= std::make_unique<DMCheckbox>("Enable Temporal", last_applied_reactive_.stability.enable_temporal_smoothing);

    reactive_kernel_radius_      = std::make_unique<DMSlider>("Kernel Radius", 1, 16, last_applied_reactive_.sampling.kernel_radius);
    reactive_outer_ring_weight_  = make_float_slider("Outer Ring Weight", 0.0f, 3.0f, last_applied_reactive_.sampling.outer_ring_weight, 100);
    reactive_diagonal_weight_    = make_float_slider("Diagonal Weight", 0.0f, 2.0f, last_applied_reactive_.sampling.diagonal_weight, 100);
    reactive_gradient_deadzone_  = make_float_slider("Gradient Deadzone", 0.0f, 1.0f, last_applied_reactive_.directionality.gradient_deadzone, 100);
    reactive_gradient_max_       = make_float_slider("Gradient Max", 0.1f, 2.5f, last_applied_reactive_.directionality.gradient_max, 100);
    reactive_offset_ratio_x_     = make_float_slider("Offset Ratio X", 0.0f, 1.0f, last_applied_reactive_.directionality.offset_ratio_x, 100);
    reactive_offset_ratio_y_     = make_float_slider("Offset Ratio Y", 0.0f, 1.0f, last_applied_reactive_.directionality.offset_ratio_y, 100);
    reactive_offset_bias_x_      = make_float_slider("Offset Bias X", 0.0f, 3.0f, last_applied_reactive_.directionality.offset_x_bias, 100);
    reactive_offset_bias_y_      = make_float_slider("Offset Bias Y", 0.0f, 3.0f, last_applied_reactive_.directionality.offset_y_bias, 100);
    reactive_offset_limit_x_     = make_float_slider("Offset Limit X", 0.0f, 1.0f, last_applied_reactive_.directionality.offset_max_ratio_x, 100);
    reactive_offset_limit_y_     = make_float_slider("Offset Limit Y", 0.0f, 1.0f, last_applied_reactive_.directionality.offset_max_ratio_y, 100);
    reactive_scale_strength_     = make_float_slider("Scale Strength", 0.0f, 1.0f, last_applied_reactive_.response.scale_strength, 100);
    reactive_scale_front_limit_  = make_float_slider("Scale Front Limit", 0.0f, 1.0f, last_applied_reactive_.response.scale_front_limit, 100);
    reactive_scale_back_limit_   = make_float_slider("Scale Back Limit", 0.0f, 1.0f, last_applied_reactive_.response.scale_back_limit, 100);
    reactive_scale_min_          = make_float_slider("Scale Min", 0.1f, 4.0f, last_applied_reactive_.response.scale_min, 100);
    reactive_scale_max_          = make_float_slider("Scale Max", 0.1f, 4.0f, last_applied_reactive_.response.scale_max, 100);
    reactive_opacity_gamma_      = make_float_slider("Opacity Gamma", 0.0f, 4.0f, last_applied_reactive_.response.opacity_gamma, 100);
    reactive_opacity_min_factor_ = make_float_slider("Opacity Min Factor", 0.1f, 3.0f, last_applied_reactive_.response.opacity_min_factor, 100);
    reactive_opacity_max_factor_ = make_float_slider("Opacity Max Factor", 0.1f, 3.0f, last_applied_reactive_.response.opacity_max_factor, 100);
    reactive_opacity_floor_      = make_float_slider("Opacity Floor", 0.0f, 1.0f, last_applied_reactive_.response.absolute_opacity_min, 100);
    reactive_opacity_ceiling_    = make_float_slider("Opacity Ceiling", 0.0f, 1.0f, last_applied_reactive_.response.absolute_opacity_max, 100);
    reactive_brightness_floor_   = make_float_slider("Brightness Floor", 0.0f, 1.0f, last_applied_reactive_.response.brightness_floor, 100);
    reactive_temporal_smoothing_ = make_float_slider("Temporal Smoothing", 0.0f, 0.999f, last_applied_reactive_.stability.temporal_smoothing, 1000, 3);

    if (reactive_kernel_radius_) reactive_kernel_radius_->set_defer_commit_until_unfocus(true);

    rebuild_rows();
}

void MapLightPanel::update_section_header_labels() {
    auto label_for = [](const std::string& title, bool collapsed) {
        return std::string(collapsed ? "[\xE2\x86\x93] " : "[\xE2\x86\x91] ") + title;
};
    if (orbit_section_btn_) {
        orbit_section_btn_->set_text(label_for("Orbit Settings", orbit_section_collapsed_));
    }
    if (screen_section_btn_) {
        screen_section_btn_->set_text(label_for("Screen Light", screen_section_collapsed_));
    }
    if (texture_section_btn_) {
        texture_section_btn_->set_text(label_for("Map Light Texture", texture_section_collapsed_));
    }
    if (reactive_section_btn_) {
        reactive_section_btn_->set_text(label_for("Reactive Shadows", reactive_section_collapsed_));
    }
    if (lens_section_btn_) {
        lens_section_btn_->set_text(label_for("Lens Flares", lens_section_collapsed_));
    }
}

void MapLightPanel::rebuild_rows() {
    update_section_header_labels();

    widget_wrappers_.clear();
    widget_wrappers_.reserve(128);

    auto add_widget = [this](std::unique_ptr<Widget> w) -> Widget* {
        Widget* raw = w.get();
        widget_wrappers_.push_back(std::move(w));
        return raw;
};

    Rows rows;

    auto warning_label = std::make_unique<WarningLabel>();
    warning_label_ = warning_label.get();
    warning_label_->set_color(SDL_Color{255, 120, 120, 255});
    if (!persistence_warning_text_.empty()) {
        warning_label_->set_text(persistence_warning_text_);
    }
    rows.push_back({ add_widget(std::move(warning_label)) });

    load_update_map_light_setting();
    if (update_map_light_checkbox_) {
        rows.push_back({ add_widget(std::make_unique<CheckboxWidget>(update_map_light_checkbox_.get())) });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(orbit_section_btn_.get(), [this]() { toggle_orbit_section(); })) });
    if (!orbit_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(update_interval_.get())),
            add_widget(std::make_unique<SliderWidget>(orbit_x_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(orbit_y_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(min_opacity_.get())),
            add_widget(std::make_unique<SliderWidget>(max_opacity_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(screen_section_btn_.get(), [this]() { toggle_screen_section(); })) });
    if (!screen_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(screen_r_.get())),
            add_widget(std::make_unique<SliderWidget>(screen_g_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(screen_b_.get())),
            add_widget(std::make_unique<SliderWidget>(screen_min_opacity_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(screen_max_opacity_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(lens_section_btn_.get(), [this]() { toggle_lens_section(); })) });
    if (!lens_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<CheckboxWidget>(lens_enabled_.get())),
            add_widget(std::make_unique<SliderWidget>(lens_seed_threshold_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(lens_seed_ema_.get())),
            add_widget(std::make_unique<SliderWidget>(lens_fade_frames_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(lens_size_scalar_.get())),
            add_widget(std::make_unique<SliderWidget>(lens_intensity_gain_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(lens_alpha_cap_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(reactive_section_btn_.get(), [this]() { toggle_reactive_section(); })) });
    if (!reactive_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<CheckboxWidget>(reactive_offsets_enabled_.get())),
            add_widget(std::make_unique<CheckboxWidget>(reactive_scale_enabled_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<CheckboxWidget>(reactive_opacity_enabled_.get())),
            add_widget(std::make_unique<CheckboxWidget>(reactive_temporal_enabled_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_kernel_radius_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_outer_ring_weight_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_diagonal_weight_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_gradient_deadzone_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_gradient_max_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_offset_ratio_x_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_offset_ratio_y_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_offset_bias_x_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_offset_bias_y_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_offset_limit_x_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_offset_limit_y_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_scale_strength_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_scale_front_limit_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_scale_back_limit_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_scale_min_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_scale_max_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_opacity_gamma_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_opacity_min_factor_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_opacity_max_factor_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_opacity_floor_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_opacity_ceiling_.get())),
            add_widget(std::make_unique<SliderWidget>(reactive_brightness_floor_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(reactive_temporal_smoothing_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(texture_section_btn_.get(), [this]() { toggle_texture_section(); })) });
    if (!texture_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(radius_.get())),
            add_widget(std::make_unique<SliderWidget>(intensity_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(mult_x100_.get())),
            add_widget(std::make_unique<SliderWidget>(falloff_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(base_r_.get())),
            add_widget(std::make_unique<SliderWidget>(base_g_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(base_b_.get())),
            add_widget(std::make_unique<SliderWidget>(base_a_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<ButtonWidget>(prev_key_btn_.get(), [this]() { select_prev_key(); })),
            add_widget(std::make_unique<ButtonWidget>(next_key_btn_.get(), [this]() { select_next_key(); })),
            add_widget(std::make_unique<ButtonWidget>(add_pair_btn_.get(), [this]() { add_key_pair_at_current_angle(); })),
            add_widget(std::make_unique<ButtonWidget>(delete_btn_.get(), [this]() { delete_current_key(); }))
        });
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(key_angle_.get())) });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(key_r_.get())),
            add_widget(std::make_unique<SliderWidget>(key_g_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(key_b_.get())),
            add_widget(std::make_unique<SliderWidget>(key_a_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<ButtonWidget>(update_btn_.get(), [this]() { apply_changes(); }))
        });
    }

    set_rows(rows);
}

void MapLightPanel::toggle_orbit_section() {
    orbit_section_collapsed_ = !orbit_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::toggle_screen_section() {
    screen_section_collapsed_ = !screen_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::toggle_texture_section() {
    texture_section_collapsed_ = !texture_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::toggle_reactive_section() {
    reactive_section_collapsed_ = !reactive_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::toggle_lens_section() {
    lens_section_collapsed_ = !lens_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::apply_changes() {
    if (!map_info_) {
        return;
    }

    sync_json_from_ui();
    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    ScreenLightSettings screen = sanitize_screen_settings(current_screen_settings_from_ui(), orbit);
    LensFlareRenderer::Settings lens = current_lens_settings_from_ui();

    bool ok = commit_light_changes();
    if (ok) {
        last_applied_orbit_ = orbit;
        last_applied_screen_ = screen;
        last_applied_lens_ = lens;
    }
}

nlohmann::json& MapLightPanel::ensure_light() {

    if (!editing_light_.is_object()) {
        editing_light_ = json::object();
    }
    json& L = editing_light_;

    auto parse_int = [](const json& value, int fallback) -> std::optional<int> {
        try {
            if (value.is_number_integer()) {
                return value.get<int>();
            }
            if (value.is_number_float()) {
                return static_cast<int>(std::lround(value.get<double>()));
            }
            if (value.is_string()) {
                const std::string text = value.get<std::string>();
                size_t idx = 0;
                int parsed = std::stoi(text, &idx);
                if (idx == text.size()) {
                    return parsed;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
};

    auto read_int = [&](const char* key, int fallback, int lo, int hi) {
        int value = fallback;
        auto it = L.find(key);
        if (it != L.end()) {
            if (auto parsed = parse_int(*it, fallback)) {
                value = *parsed;
            }
        }
        return clamp_int(value, lo, hi);
};

    auto parse_double = [](const json& value, double fallback) -> std::optional<double> {
        try {
            if (value.is_number_float()) {
                return value.get<double>();
            }
            if (value.is_number_integer()) {
                return static_cast<double>(value.get<int>());
            }
            if (value.is_string()) {
                const std::string text = value.get<std::string>();
                size_t idx = 0;
                double parsed = std::stod(text, &idx);
                if (idx == text.size()) {
                    return parsed;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
};

    auto read_double = [&](const char* key, double fallback, double lo, double hi) {
        double value = fallback;
        auto it = L.find(key);
        if (it != L.end()) {
            if (auto parsed = parse_double(*it, fallback)) {
                value = *parsed;
            }
        }
        return std::clamp(value, lo, hi);
};

    L["radius"] = read_int("radius", 0, 0, 20000);
    L["intensity"] = read_int("intensity", 255, 0, 255);
    L["fall_off"] = read_int("fall_off", 100, 0, 100);
    L["update_interval"] = read_int("update_interval", 10, 1, 120);

    double mult = read_double("mult", 0.0, 0.0, 1.0);
    L["mult"] = mult;

    int min_opacity = read_int("min_opacity", 0, 0, 255);
    int max_opacity = read_int("max_opacity", 255, 0, 255);
    if (min_opacity > max_opacity) {
        std::swap(min_opacity, max_opacity);
    }
    L["min_opacity"] = min_opacity;
    L["max_opacity"] = max_opacity;

    auto read_radius = [&](const char* key) -> std::optional<int> {
        auto it = L.find(key);
        if (it == L.end()) {
            return std::nullopt;
        }
        if (auto parsed = parse_int(*it, 0)) {
            return clamp_int(*parsed, 0, 20000);
        }
        return std::nullopt;
};

    const int fallback_orbit = read_radius("orbit_radius").value_or(0);
    int orbit_x = read_radius("orbit_x").value_or(fallback_orbit);
    int orbit_y = read_radius("orbit_y").value_or(orbit_x);
    orbit_x = clamp_int(orbit_x, 0, 20000);
    orbit_y = clamp_int(orbit_y, 0, 20000);
    L["orbit_x"] = orbit_x;
    L["orbit_y"] = orbit_y;
    L["orbit_radius"] = std::max(orbit_x, orbit_y);

    if (!L.contains("base_color") || !L["base_color"].is_array() || L["base_color"].size() < 4) {
        L["base_color"] = {255,255,255,255};
    }

    ensure_screen_light(L);
    ensure_reactive_settings(L);
    ensure_lens_flare_settings(L);

    if (!L.contains("keys") || !L["keys"].is_array()) {

        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    }
    return L;
}

nlohmann::json& MapLightPanel::ensure_screen_light(nlohmann::json& light) {
    if (!light.contains("screen_light") || !light["screen_light"].is_object()) {
        light["screen_light"] = json::object();
    }
    json& screen = light["screen_light"];
    if (!screen.contains("color") || !screen["color"].is_array() || screen["color"].size() < 3) {
        screen["color"] = json::array({255, 255, 255});
    }
    auto clamp_component = [](const json& v) -> int {
        try {
            return clamp_int(v.get<int>(), 0, 255);
        } catch (...) {
            return 255;
        }
};
    auto& color = screen["color"];
    if (color.is_array()) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (i >= color.size()) {
                color.push_back(255);
            } else {
                color[i] = clamp_component(color[i]);
            }
        }
        while (color.size() > 3) {
            color.erase(color.size() - 1);
        }
    }

    int map_min = 0;
    int map_max = 255;
    try { map_min = light.at("min_opacity").get<int>(); } catch (...) {}
    try { map_max = light.at("max_opacity").get<int>(); } catch (...) {}
    map_min = clamp_int(map_min, 0, 255);
    map_max = clamp_int(map_max, 0, 255);
    if (map_min > map_max) std::swap(map_min, map_max);

    if (!screen.contains("min_opacity")) {
        screen["min_opacity"] = map_min;
    }
    if (!screen.contains("max_opacity")) {
        screen["max_opacity"] = map_max;
    }
    int scr_min = map_min;
    int scr_max = map_max;
    try { scr_min = clamp_int(screen.at("min_opacity").get<int>(), map_min, map_max); } catch (...) { scr_min = map_min; }
    try { scr_max = clamp_int(screen.at("max_opacity").get<int>(), map_min, map_max); } catch (...) { scr_max = map_max; }
    if (scr_min > scr_max) std::swap(scr_min, scr_max);
    screen["min_opacity"] = scr_min;
    screen["max_opacity"] = scr_max;
    return screen;
}

nlohmann::json& MapLightPanel::ensure_reactive_settings(nlohmann::json& light) {
    if (!light.contains("reactive_shadows") || !light["reactive_shadows"].is_object()) {
        light["reactive_shadows"] = json::object();
    }
    json& reactive = light["reactive_shadows"];
    ReactiveShadowSettings defaults = load_reactive_settings_from_dev_settings();
    ReactiveShadowSettings parsed = render_pipeline::shading::reactive_shadow_settings_from_json(reactive, defaults);
    render_pipeline::shading::assign_reactive_shadow_settings(reactive, parsed);
    return reactive;
}

nlohmann::json& MapLightPanel::ensure_lens_flare_settings(nlohmann::json& light) {
    if (!light.contains("lens_flare") || !light["lens_flare"].is_object()) {
        light["lens_flare"] = json::object();
    }
    json& lens = light["lens_flare"];
    LensFlareRenderer::Settings parsed = LensFlareRenderer::settings_from_json(lens, LensFlareRenderer::default_settings());
    LensFlareRenderer::settings_to_json(lens, parsed);
    return lens;
}

void MapLightPanel::sync_ui_from_json() {
    json& L = ensure_light();

    radius_       ->set_value(clamp_int(L.value("radius", 0), 0, 20000));
    intensity_    ->set_value(clamp_int(L.value("intensity", 255), 0, 255));

    {
        double m = 0.0;
        try { m = L.at("mult").get<double>(); } catch(...) {}
        m = clamp_float((float)m, 0.0f, 1.0f);
        mult_x100_->set_value((int)std::round(m * 100.0));
    }
    falloff_->set_value(clamp_int(L.value("fall_off", 100), 0, 100));

    OrbitSettings orbit{};
    orbit.update_interval = clamp_int(L.value("update_interval", 10), 1, 120);
    const int fallback_orbit = clamp_int(L.value("orbit_radius", 0), 0, 20000);
    orbit.orbit_x = clamp_int(L.value("orbit_x", fallback_orbit), 0, 20000);
    orbit.orbit_y = clamp_int(L.value("orbit_y", orbit.orbit_x), 0, 20000);
    orbit.min_opacity = clamp_int(L.value("min_opacity", 0), 0, 255);
    orbit.max_opacity = clamp_int(L.value("max_opacity", 255), 0, 255);
    orbit = sanitize_orbit_settings(orbit);
    set_orbit_sliders(orbit);
    last_applied_orbit_ = orbit;

    auto bc = L["base_color"];
    int br = 255, bg = 255, bb = 255, ba = 255;
    try {
        if (bc.is_array() && bc.size() >= 4) {
            br = clamp_int(bc[0].get<int>(), 0, 255);
            bg = clamp_int(bc[1].get<int>(), 0, 255);
            bb = clamp_int(bc[2].get<int>(), 0, 255);
            ba = clamp_int(bc[3].get<int>(), 0, 255);
        }
    } catch(...) {}
    base_r_->set_value(br);
    base_g_->set_value(bg);
    base_b_->set_value(bb);
    base_a_->set_value(ba);

    json& screen_json = ensure_screen_light(L);
    ScreenLightSettings screen{};
    try {
        if (screen_json["color"].is_array()) {
            auto color = screen_json["color"];
            if (color.size() >= 3) {
                screen.r = clamp_int(color[0].get<int>(), 0, 255);
                screen.g = clamp_int(color[1].get<int>(), 0, 255);
                screen.b = clamp_int(color[2].get<int>(), 0, 255);
            }
        }
    } catch (...) {}
    screen.min_opacity = screen_json.value("min_opacity", orbit.min_opacity);
    screen.max_opacity = screen_json.value("max_opacity", orbit.max_opacity);
    screen = sanitize_screen_settings(screen, orbit);
    set_screen_sliders(screen);
    last_applied_screen_ = screen;

    json& reactive_json = ensure_reactive_settings(L);
    ReactiveShadowSettings reactive = render_pipeline::shading::reactive_shadow_settings_from_json(
        reactive_json, load_reactive_settings_from_dev_settings());
    reactive = render_pipeline::shading::sanitize_reactive_shadow_settings(reactive);
    set_reactive_checkboxes(reactive);
    set_reactive_sliders(reactive);
    persist_reactive_settings_to_dev_settings(reactive);
    last_applied_reactive_ = reactive;
    reactive_settings_initialized_ = true;
    if (reactive_settings_shared_) {
        *reactive_settings_shared_ = reactive;
    }

    json& lens_json = ensure_lens_flare_settings(L);
    LensFlareRenderer::Settings lens_settings = LensFlareRenderer::settings_from_json(lens_json, LensFlareRenderer::default_settings());
    set_lens_sliders(lens_settings);
    last_applied_lens_ = lens_settings;

    ensure_keys_array();
    clamp_key_index();

    const auto& keys = L["keys"];
    if (!keys.empty() && keys[current_key_index_].is_array() && keys[current_key_index_].size() >= 2) {
        float ang = 0.0f;
        int r=255,g=255,b=255,a=255;
        try {
            ang = (float)keys[current_key_index_][0].get<double>();
            auto kc = keys[current_key_index_][1];
            if (kc.is_array() && kc.size() >= 4) {
                r = clamp_int(kc[0].get<int>(), 0, 255);
                g = clamp_int(kc[1].get<int>(), 0, 255);
                b = clamp_int(kc[2].get<int>(), 0, 255);
                a = clamp_int(kc[3].get<int>(), 0, 255);
            }
        } catch(...) {}
        key_angle_->set_value((int)std::round(wrap_angle(ang)));
        key_r_->set_value(r);
        key_g_->set_value(g);
        key_b_->set_value(b);
        key_a_->set_value(a);
    } else {
        key_angle_->set_value(0);
        key_r_->set_value(br);
        key_g_->set_value(bg);
        key_b_->set_value(bb);
        key_a_->set_value(ba);
    }

    needs_sync_to_json_ = false;
}

void MapLightPanel::sync_json_from_ui() {
    json& L = ensure_light();

    auto slider_value = [](const std::unique_ptr<DMSlider>& slider, int fallback) {
        return slider ? slider->displayed_value() : fallback;
    };

    L["radius"]         = slider_value(radius_, 0);
    L["intensity"]      = slider_value(intensity_, 255);
    L["mult"]           = static_cast<double>(slider_value(mult_x100_, 0)) / 100.0;
    L["fall_off"]       = slider_value(falloff_, 100);

    L["base_color"]     = json::array({
        slider_value(base_r_, 255),
        slider_value(base_g_, 255),
        slider_value(base_b_, 255),
        slider_value(base_a_, 255)
    });

    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    write_orbit_settings_to_json(orbit);

    ScreenLightSettings screen = sanitize_screen_settings(current_screen_settings_from_ui(), orbit);
    write_screen_settings_to_json(screen);

    set_orbit_sliders(orbit);
    set_screen_sliders(screen);

    ReactiveShadowSettings reactive = render_pipeline::shading::sanitize_reactive_shadow_settings(
        current_reactive_settings_from_ui());
    write_reactive_settings_to_json(reactive);
    set_reactive_sliders(reactive);
    set_reactive_checkboxes(reactive);
    persist_reactive_settings_to_dev_settings(reactive);

    LensFlareRenderer::Settings lens = current_lens_settings_from_ui();
    write_lens_settings_to_json(lens);
    set_lens_sliders(lens);

    ensure_keys_array();
    clamp_key_index();

    auto& keys = L["keys"];
    if (!keys.empty() && current_key_index_ >= 0 && current_key_index_ < (int)keys.size()) {
        const int ang = clamp_int(key_angle_->value(), 0, 360);
        const int r   = clamp_int(key_r_->value(),   0, 255);
        const int g   = clamp_int(key_g_->value(),   0, 255);
        const int b   = clamp_int(key_b_->value(),   0, 255);
        const int a   = clamp_int(key_a_->value(),   0, 255);
        keys[current_key_index_] = json::array({ (double)ang, json::array({ r, g, b, a }) });
    }

    needs_sync_to_json_ = false;
}

void MapLightPanel::load_update_map_light_setting() {
    update_map_light_enabled_ = devmode::ui_settings::load_bool(kUpdateMapLightSettingKey, false);
    if (update_map_light_checkbox_) {
        update_map_light_checkbox_->set_value(update_map_light_enabled_);
    }
}

void MapLightPanel::ensure_keys_array() {
    json& L = ensure_light();
    if (!L.contains("keys") || !L["keys"].is_array()) {
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    }
}

MapLightPanel::OrbitSettings MapLightPanel::sanitize_orbit_settings(const OrbitSettings& raw) const {
    OrbitSettings out = raw;
    out.update_interval = clamp_int(out.update_interval, 1, 120);
    out.orbit_x = clamp_int(out.orbit_x, 0, 20000);
    out.orbit_y = clamp_int(out.orbit_y, 0, 20000);
    out.min_opacity = clamp_int(out.min_opacity, 0, 255);
    out.max_opacity = clamp_int(out.max_opacity, 0, 255);
    if (out.min_opacity > out.max_opacity) {
        std::swap(out.min_opacity, out.max_opacity);
    }
    return out;
}

MapLightPanel::ScreenLightSettings MapLightPanel::sanitize_screen_settings(const ScreenLightSettings& raw,
                                                                          const OrbitSettings& orbit) const {
    ScreenLightSettings out = raw;
    out.r = clamp_int(out.r, 0, 255);
    out.g = clamp_int(out.g, 0, 255);
    out.b = clamp_int(out.b, 0, 255);
    int lo = orbit.min_opacity;
    int hi = orbit.max_opacity;
    if (lo > hi) std::swap(lo, hi);
    out.min_opacity = clamp_int(out.min_opacity, lo, hi);
    out.max_opacity = clamp_int(out.max_opacity, lo, hi);
    if (out.min_opacity > out.max_opacity) {
        std::swap(out.min_opacity, out.max_opacity);
    }
    return out;
}

MapLightPanel::OrbitSettings MapLightPanel::current_orbit_settings_from_ui() const {
    OrbitSettings current;
    current.update_interval = update_interval_ ? update_interval_->displayed_value() : 10;
    current.orbit_x = orbit_x_ ? orbit_x_->displayed_value() : 0;
    current.orbit_y = orbit_y_ ? orbit_y_->displayed_value() : current.orbit_x;
    current.min_opacity = min_opacity_ ? min_opacity_->displayed_value() : 0;
    current.max_opacity = max_opacity_ ? max_opacity_->displayed_value() : 255;
    return current;
}

MapLightPanel::ScreenLightSettings MapLightPanel::current_screen_settings_from_ui() const {
    ScreenLightSettings current;
    current.r = screen_r_ ? screen_r_->displayed_value() : 255;
    current.g = screen_g_ ? screen_g_->displayed_value() : 255;
    current.b = screen_b_ ? screen_b_->displayed_value() : 255;
    current.min_opacity = screen_min_opacity_ ? screen_min_opacity_->displayed_value() : 0;
    current.max_opacity = screen_max_opacity_ ? screen_max_opacity_->displayed_value() : 255;
    return current;
}

LensFlareRenderer::Settings MapLightPanel::current_lens_settings_from_ui() const {
    LensFlareRenderer::Settings settings = last_applied_lens_;
    if (lens_enabled_) settings.enabled = lens_enabled_->value();
    settings.seed_threshold_norm = slider_value_scaled(lens_seed_threshold_, settings.seed_threshold_norm, 100);
    settings.seed_pos_ema = slider_value_scaled(lens_seed_ema_, settings.seed_pos_ema, 100);
    if (lens_size_scalar_) apply_lens_size_slider(settings, lens_size_scalar_->displayed_value());
    if (lens_fade_frames_) apply_lens_fade_slider(settings, lens_fade_frames_->displayed_value());
    settings.ghost_intensity_gain = slider_value_scaled(lens_intensity_gain_, settings.ghost_intensity_gain, 100);
    settings.ghost_alpha_cap = slider_value_scaled(lens_alpha_cap_, settings.ghost_alpha_cap, 100);

    LensFlareRenderer::Settings defaults = LensFlareRenderer::default_settings();
    settings.seed_stride_px = defaults.seed_stride_px;
    settings.max_new_per_frame = defaults.max_new_per_frame;
    settings.ghost_follow_ema = std::clamp(settings.seed_pos_ema * 0.5f + 0.04f, 0.04f, 0.25f);
    settings.ghost_spawn_speed = defaults.ghost_spawn_speed;
    settings.ghost_drift = defaults.ghost_drift;
    settings.offscreen_spawn_bias = defaults.offscreen_spawn_bias;
    settings.streak_angle_lean = defaults.streak_angle_lean;
    settings.axis_factors = defaults.axis_factors;

    return LensFlareRenderer::sanitize_settings(settings);
}

void MapLightPanel::set_orbit_sliders(const OrbitSettings& orbit) {
    if (update_interval_) update_interval_->set_value(orbit.update_interval);
    if (orbit_x_)         orbit_x_->set_value(orbit.orbit_x);
    if (orbit_y_)         orbit_y_->set_value(orbit.orbit_y);
    if (min_opacity_)     min_opacity_->set_value(orbit.min_opacity);
    if (max_opacity_)     max_opacity_->set_value(orbit.max_opacity);
}

void MapLightPanel::set_screen_sliders(const ScreenLightSettings& screen) {
    if (screen_r_)          screen_r_->set_value(screen.r);
    if (screen_g_)          screen_g_->set_value(screen.g);
    if (screen_b_)          screen_b_->set_value(screen.b);
    if (screen_min_opacity_)screen_min_opacity_->set_value(screen.min_opacity);
    if (screen_max_opacity_)screen_max_opacity_->set_value(screen.max_opacity);
}

void MapLightPanel::set_lens_sliders(const LensFlareRenderer::Settings& settings) {
    LensFlareRenderer::Settings sanitized = LensFlareRenderer::sanitize_settings(settings);
    if (lens_enabled_) lens_enabled_->set_value(sanitized.enabled);
    set_slider_scaled(lens_seed_threshold_, sanitized.seed_threshold_norm, 100);
    set_slider_scaled(lens_seed_ema_, sanitized.seed_pos_ema, 100);
    if (lens_size_scalar_) lens_size_scalar_->set_value(lens_size_slider_from_settings(sanitized));
    if (lens_fade_frames_) lens_fade_frames_->set_value(lens_fade_slider_from_settings(sanitized));
    set_slider_scaled(lens_intensity_gain_, sanitized.ghost_intensity_gain, 100);
    set_slider_scaled(lens_alpha_cap_, sanitized.ghost_alpha_cap, 100);
}

ReactiveShadowSettings MapLightPanel::current_reactive_settings_from_ui() const {
    ReactiveShadowSettings settings = last_applied_reactive_;
    if (reactive_kernel_radius_) {
        settings.sampling.kernel_radius = clamp_int(reactive_kernel_radius_->displayed_value(), 1, 16);
    }
    settings.sampling.outer_ring_weight = slider_value_scaled(reactive_outer_ring_weight_, settings.sampling.outer_ring_weight, 100);
    settings.sampling.diagonal_weight   = slider_value_scaled(reactive_diagonal_weight_, settings.sampling.diagonal_weight, 100);

    if (reactive_offsets_enabled_) {
        settings.directionality.enable_offsets = reactive_offsets_enabled_->value();
    }
    settings.directionality.gradient_deadzone  = slider_value_scaled(reactive_gradient_deadzone_, settings.directionality.gradient_deadzone, 100);
    settings.directionality.gradient_max       = slider_value_scaled(reactive_gradient_max_, settings.directionality.gradient_max, 100);
    settings.directionality.offset_ratio_x     = slider_value_scaled(reactive_offset_ratio_x_, settings.directionality.offset_ratio_x, 100);
    settings.directionality.offset_ratio_y     = slider_value_scaled(reactive_offset_ratio_y_, settings.directionality.offset_ratio_y, 100);
    settings.directionality.offset_x_bias      = slider_value_scaled(reactive_offset_bias_x_, settings.directionality.offset_x_bias, 100);
    settings.directionality.offset_y_bias      = slider_value_scaled(reactive_offset_bias_y_, settings.directionality.offset_y_bias, 100);
    settings.directionality.offset_max_ratio_x = slider_value_scaled(reactive_offset_limit_x_, settings.directionality.offset_max_ratio_x, 100);
    settings.directionality.offset_max_ratio_y = slider_value_scaled(reactive_offset_limit_y_, settings.directionality.offset_max_ratio_y, 100);

    if (reactive_scale_enabled_) {
        settings.response.enable_scale = reactive_scale_enabled_->value();
    }
    if (reactive_opacity_enabled_) {
        settings.response.enable_opacity = reactive_opacity_enabled_->value();
    }
    settings.response.scale_strength       = slider_value_scaled(reactive_scale_strength_, settings.response.scale_strength, 100);
    settings.response.scale_front_limit    = slider_value_scaled(reactive_scale_front_limit_, settings.response.scale_front_limit, 100);
    settings.response.scale_back_limit     = slider_value_scaled(reactive_scale_back_limit_, settings.response.scale_back_limit, 100);
    settings.response.scale_min            = slider_value_scaled(reactive_scale_min_, settings.response.scale_min, 100);
    settings.response.scale_max            = slider_value_scaled(reactive_scale_max_, settings.response.scale_max, 100);
    settings.response.opacity_gamma        = slider_value_scaled(reactive_opacity_gamma_, settings.response.opacity_gamma, 100);
    settings.response.opacity_min_factor   = slider_value_scaled(reactive_opacity_min_factor_, settings.response.opacity_min_factor, 100);
    settings.response.opacity_max_factor   = slider_value_scaled(reactive_opacity_max_factor_, settings.response.opacity_max_factor, 100);
    settings.response.absolute_opacity_min = slider_value_scaled(reactive_opacity_floor_, settings.response.absolute_opacity_min, 100);
    settings.response.absolute_opacity_max = slider_value_scaled(reactive_opacity_ceiling_, settings.response.absolute_opacity_max, 100);
    settings.response.brightness_floor     = slider_value_scaled(reactive_brightness_floor_, settings.response.brightness_floor, 100);

    if (reactive_temporal_enabled_) {
        settings.stability.enable_temporal_smoothing = reactive_temporal_enabled_->value();
    }
    settings.stability.temporal_smoothing = slider_value_scaled(reactive_temporal_smoothing_, settings.stability.temporal_smoothing, 1000);

    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapLightPanel::set_reactive_sliders(const ReactiveShadowSettings& settings) {
    if (reactive_kernel_radius_) reactive_kernel_radius_->set_value(settings.sampling.kernel_radius);
    set_slider_scaled(reactive_outer_ring_weight_, settings.sampling.outer_ring_weight, 100);
    set_slider_scaled(reactive_diagonal_weight_, settings.sampling.diagonal_weight, 100);
    set_slider_scaled(reactive_gradient_deadzone_, settings.directionality.gradient_deadzone, 100);
    set_slider_scaled(reactive_gradient_max_, settings.directionality.gradient_max, 100);
    set_slider_scaled(reactive_offset_ratio_x_, settings.directionality.offset_ratio_x, 100);
    set_slider_scaled(reactive_offset_ratio_y_, settings.directionality.offset_ratio_y, 100);
    set_slider_scaled(reactive_offset_bias_x_, settings.directionality.offset_x_bias, 100);
    set_slider_scaled(reactive_offset_bias_y_, settings.directionality.offset_y_bias, 100);
    set_slider_scaled(reactive_offset_limit_x_, settings.directionality.offset_max_ratio_x, 100);
    set_slider_scaled(reactive_offset_limit_y_, settings.directionality.offset_max_ratio_y, 100);
    set_slider_scaled(reactive_scale_strength_, settings.response.scale_strength, 100);
    set_slider_scaled(reactive_scale_front_limit_, settings.response.scale_front_limit, 100);
    set_slider_scaled(reactive_scale_back_limit_, settings.response.scale_back_limit, 100);
    set_slider_scaled(reactive_scale_min_, settings.response.scale_min, 100);
    set_slider_scaled(reactive_scale_max_, settings.response.scale_max, 100);
    set_slider_scaled(reactive_opacity_gamma_, settings.response.opacity_gamma, 100);
    set_slider_scaled(reactive_opacity_min_factor_, settings.response.opacity_min_factor, 100);
    set_slider_scaled(reactive_opacity_max_factor_, settings.response.opacity_max_factor, 100);
    set_slider_scaled(reactive_opacity_floor_, settings.response.absolute_opacity_min, 100);
    set_slider_scaled(reactive_opacity_ceiling_, settings.response.absolute_opacity_max, 100);
    set_slider_scaled(reactive_brightness_floor_, settings.response.brightness_floor, 100);
    set_slider_scaled(reactive_temporal_smoothing_, settings.stability.temporal_smoothing, 1000);
}

void MapLightPanel::set_reactive_checkboxes(const ReactiveShadowSettings& settings) {
    if (reactive_offsets_enabled_) reactive_offsets_enabled_->set_value(settings.directionality.enable_offsets);
    if (reactive_scale_enabled_) reactive_scale_enabled_->set_value(settings.response.enable_scale);
    if (reactive_opacity_enabled_) reactive_opacity_enabled_->set_value(settings.response.enable_opacity);
    if (reactive_temporal_enabled_) reactive_temporal_enabled_->set_value(settings.stability.enable_temporal_smoothing);
}

ReactiveShadowSettings MapLightPanel::load_reactive_settings_from_dev_settings() const {
    using devmode::ui_settings::load_bool;
    using devmode::ui_settings::load_number;
    ReactiveShadowSettings settings = render_pipeline::shading::sanitize_reactive_shadow_settings({});
    settings.directionality.enable_offsets = load_bool(reactive_settings_key("directionality.enable_offsets"), settings.directionality.enable_offsets);
    settings.directionality.gradient_deadzone = static_cast<float>(load_number(reactive_settings_key("directionality.gradient_deadzone"), settings.directionality.gradient_deadzone));
    settings.directionality.gradient_max = static_cast<float>(load_number(reactive_settings_key("directionality.gradient_max"), settings.directionality.gradient_max));
    settings.directionality.offset_ratio_x = static_cast<float>(load_number(reactive_settings_key("directionality.offset_ratio_x"), settings.directionality.offset_ratio_x));
    settings.directionality.offset_ratio_y = static_cast<float>(load_number(reactive_settings_key("directionality.offset_ratio_y"), settings.directionality.offset_ratio_y));
    settings.directionality.offset_x_bias = static_cast<float>(load_number(reactive_settings_key("directionality.offset_x_bias"), settings.directionality.offset_x_bias));
    settings.directionality.offset_y_bias = static_cast<float>(load_number(reactive_settings_key("directionality.offset_y_bias"), settings.directionality.offset_y_bias));
    settings.directionality.offset_max_ratio_x = static_cast<float>(load_number(reactive_settings_key("directionality.offset_max_ratio_x"), settings.directionality.offset_max_ratio_x));
    settings.directionality.offset_max_ratio_y = static_cast<float>(load_number(reactive_settings_key("directionality.offset_max_ratio_y"), settings.directionality.offset_max_ratio_y));

    settings.response.enable_scale = load_bool(reactive_settings_key("response.enable_scale"), settings.response.enable_scale);
    settings.response.enable_opacity = load_bool(reactive_settings_key("response.enable_opacity"), settings.response.enable_opacity);
    settings.response.scale_strength = static_cast<float>(load_number(reactive_settings_key("response.scale_strength"), settings.response.scale_strength));
    settings.response.scale_front_limit = static_cast<float>(load_number(reactive_settings_key("response.scale_front_limit"), settings.response.scale_front_limit));
    settings.response.scale_back_limit = static_cast<float>(load_number(reactive_settings_key("response.scale_back_limit"), settings.response.scale_back_limit));
    settings.response.scale_min = static_cast<float>(load_number(reactive_settings_key("response.scale_min"), settings.response.scale_min));
    settings.response.scale_max = static_cast<float>(load_number(reactive_settings_key("response.scale_max"), settings.response.scale_max));
    settings.response.opacity_gamma = static_cast<float>(load_number(reactive_settings_key("response.opacity_gamma"), settings.response.opacity_gamma));
    settings.response.opacity_min_factor = static_cast<float>(load_number(reactive_settings_key("response.opacity_min_factor"), settings.response.opacity_min_factor));
    settings.response.opacity_max_factor = static_cast<float>(load_number(reactive_settings_key("response.opacity_max_factor"), settings.response.opacity_max_factor));
    settings.response.absolute_opacity_min = static_cast<float>(load_number(reactive_settings_key("response.absolute_opacity_min"), settings.response.absolute_opacity_min));
    settings.response.absolute_opacity_max = static_cast<float>(load_number(reactive_settings_key("response.absolute_opacity_max"), settings.response.absolute_opacity_max));
    settings.response.brightness_floor = static_cast<float>(load_number(reactive_settings_key("response.brightness_floor"), settings.response.brightness_floor));

    settings.stability.enable_temporal_smoothing = load_bool(reactive_settings_key("stability.enable_temporal_smoothing"), settings.stability.enable_temporal_smoothing);
    settings.stability.temporal_smoothing = static_cast<float>(load_number(reactive_settings_key("stability.temporal_smoothing"), settings.stability.temporal_smoothing));

    settings.sampling.kernel_radius = clamp_int(static_cast<int>(std::round(load_number(reactive_settings_key("sampling.kernel_radius"), settings.sampling.kernel_radius))), 1, 16);
    settings.sampling.outer_ring_weight = static_cast<float>(load_number(reactive_settings_key("sampling.outer_ring_weight"), settings.sampling.outer_ring_weight));
    settings.sampling.diagonal_weight = static_cast<float>(load_number(reactive_settings_key("sampling.diagonal_weight"), settings.sampling.diagonal_weight));

    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapLightPanel::persist_reactive_settings_to_dev_settings(const ReactiveShadowSettings& settings) const {
    using devmode::ui_settings::save_bool;
    using devmode::ui_settings::save_number;
    save_bool(reactive_settings_key("directionality.enable_offsets"), settings.directionality.enable_offsets);
    save_number(reactive_settings_key("directionality.gradient_deadzone"), settings.directionality.gradient_deadzone);
    save_number(reactive_settings_key("directionality.gradient_max"), settings.directionality.gradient_max);
    save_number(reactive_settings_key("directionality.offset_ratio_x"), settings.directionality.offset_ratio_x);
    save_number(reactive_settings_key("directionality.offset_ratio_y"), settings.directionality.offset_ratio_y);
    save_number(reactive_settings_key("directionality.offset_x_bias"), settings.directionality.offset_x_bias);
    save_number(reactive_settings_key("directionality.offset_y_bias"), settings.directionality.offset_y_bias);
    save_number(reactive_settings_key("directionality.offset_max_ratio_x"), settings.directionality.offset_max_ratio_x);
    save_number(reactive_settings_key("directionality.offset_max_ratio_y"), settings.directionality.offset_max_ratio_y);

    save_bool(reactive_settings_key("response.enable_scale"), settings.response.enable_scale);
    save_bool(reactive_settings_key("response.enable_opacity"), settings.response.enable_opacity);
    save_number(reactive_settings_key("response.scale_strength"), settings.response.scale_strength);
    save_number(reactive_settings_key("response.scale_front_limit"), settings.response.scale_front_limit);
    save_number(reactive_settings_key("response.scale_back_limit"), settings.response.scale_back_limit);
    save_number(reactive_settings_key("response.scale_min"), settings.response.scale_min);
    save_number(reactive_settings_key("response.scale_max"), settings.response.scale_max);
    save_number(reactive_settings_key("response.opacity_gamma"), settings.response.opacity_gamma);
    save_number(reactive_settings_key("response.opacity_min_factor"), settings.response.opacity_min_factor);
    save_number(reactive_settings_key("response.opacity_max_factor"), settings.response.opacity_max_factor);
    save_number(reactive_settings_key("response.absolute_opacity_min"), settings.response.absolute_opacity_min);
    save_number(reactive_settings_key("response.absolute_opacity_max"), settings.response.absolute_opacity_max);
    save_number(reactive_settings_key("response.brightness_floor"), settings.response.brightness_floor);

    save_bool(reactive_settings_key("stability.enable_temporal_smoothing"), settings.stability.enable_temporal_smoothing);
    save_number(reactive_settings_key("stability.temporal_smoothing"), settings.stability.temporal_smoothing);

    save_number(reactive_settings_key("sampling.kernel_radius"), static_cast<double>(settings.sampling.kernel_radius));
    save_number(reactive_settings_key("sampling.outer_ring_weight"), settings.sampling.outer_ring_weight);
    save_number(reactive_settings_key("sampling.diagonal_weight"), settings.sampling.diagonal_weight);
}

void MapLightPanel::write_reactive_settings_to_json(const ReactiveShadowSettings& settings) {
    json& L = ensure_light();
    json& reactive_json = ensure_reactive_settings(L);
    render_pipeline::shading::assign_reactive_shadow_settings(reactive_json, settings);
}

void MapLightPanel::write_orbit_settings_to_json(const OrbitSettings& orbit) {
    json& L = ensure_light();
    L["update_interval"] = orbit.update_interval;
    L["orbit_x"] = orbit.orbit_x;
    L["orbit_y"] = orbit.orbit_y;
    L["orbit_radius"] = std::max(orbit.orbit_x, orbit.orbit_y);
    L["min_opacity"] = orbit.min_opacity;
    L["max_opacity"] = orbit.max_opacity;
}

void MapLightPanel::write_screen_settings_to_json(const ScreenLightSettings& screen) {
    json& L = ensure_light();
    json& screen_json = ensure_screen_light(L);
    screen_json["color"] = json::array({ screen.r, screen.g, screen.b });
    screen_json["min_opacity"] = screen.min_opacity;
    screen_json["max_opacity"] = screen.max_opacity;
}

void MapLightPanel::write_lens_settings_to_json(const LensFlareRenderer::Settings& settings) {
    json& L = ensure_light();
    json& lens_json = ensure_lens_flare_settings(L);
    LensFlareRenderer::settings_to_json(lens_json, settings);
}

void MapLightPanel::apply_immediate_settings() {
    if (!map_info_) {
        return;
    }

    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    ScreenLightSettings screen = sanitize_screen_settings(current_screen_settings_from_ui(), orbit);
    ReactiveShadowSettings reactive = render_pipeline::shading::sanitize_reactive_shadow_settings(
        current_reactive_settings_from_ui());

    LensFlareRenderer::Settings lens = current_lens_settings_from_ui();
    bool orbit_changed = !(orbit == last_applied_orbit_);
    bool screen_changed = !(screen == last_applied_screen_);
    bool reactive_changed = !(reactive == last_applied_reactive_);
    bool lens_changed = !(lens == last_applied_lens_);
    if (!orbit_changed && !screen_changed && !reactive_changed && !lens_changed) {
        return;
    }

    if (orbit_changed) {
        write_orbit_settings_to_json(orbit);
        set_orbit_sliders(orbit);
    }
    if (screen_changed) {
        write_screen_settings_to_json(screen);
        set_screen_sliders(screen);
    }
    if (reactive_changed) {
        write_reactive_settings_to_json(reactive);
        set_reactive_sliders(reactive);
        set_reactive_checkboxes(reactive);
        persist_reactive_settings_to_dev_settings(reactive);
        if (reactive_settings_shared_) {
            *reactive_settings_shared_ = reactive;
        }
    }
    if (lens_changed) {
        write_lens_settings_to_json(lens);
        set_lens_sliders(lens);
    }

    bool ok = commit_light_changes();
    if (ok) {
        if (orbit_changed) {
            last_applied_orbit_ = orbit;
        }
        if (screen_changed) {
            last_applied_screen_ = screen;
        }
        if (reactive_changed) {
            last_applied_reactive_ = reactive;
            reactive_settings_initialized_ = true;
        }
        if (lens_changed) {
            last_applied_lens_ = lens;
        }
    }
}

bool MapLightPanel::commit_light_changes() {
    if (!map_info_) {
        return false;
    }
    if (!map_info_->is_object()) {
        *map_info_ = json::object();
    }
    (*map_info_)["map_light_data"] = ensure_light();

    bool ok = true;
    if (on_save_) {
        ok = on_save_();
    }
    update_save_status(ok);
    return ok;
}

void MapLightPanel::clamp_key_index() {
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) {
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
        n = 1;
    }
    current_key_index_ = clamp_int(current_key_index_, 0, std::max(0, n-1));

    std::ostringstream oss;
    oss << "Key " << (current_key_index_ + 1) << " / " << n;
    current_key_label_ = oss.str();
}

void MapLightPanel::select_prev_key() {
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) return;
    current_key_index_ = (current_key_index_ - 1 + n) % n;
    sync_ui_from_json();
}

void MapLightPanel::select_next_key() {
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) return;
    current_key_index_ = (current_key_index_ + 1) % n;
    sync_ui_from_json();
}

void MapLightPanel::add_key_pair_at_current_angle() {
    json& L = ensure_light();

    const int ang = clamp_int(key_angle_->value(), 0, 360);
    const int r   = clamp_int(key_r_->value(), 0, 255);
    const int g   = clamp_int(key_g_->value(), 0, 255);
    const int b   = clamp_int(key_b_->value(), 0, 255);
    const int a   = clamp_int(key_a_->value(), 0, 255);

    const int ang2 = (ang + 180) % 360;

    auto key1 = json::array({ (double)ang,  json::array({ r,g,b,a }) });
    auto key2 = json::array({ (double)ang2, json::array({ r,g,b,a }) });

    auto& keys = L["keys"];
    keys.push_back(key1);
    keys.push_back(key2);

    std::sort(keys.begin(), keys.end(), [](const json& A, const json& B){
        double a0 = 0.0, b0 = 0.0;
        try { a0 = A[0].get<double>(); } catch(...) {}
        try { b0 = B[0].get<double>(); } catch(...) {}
        return a0 < b0;
    });

    for (int i=0;i<(int)keys.size();++i) {
        try {
            if ((int)std::round(keys[i][0].get<double>()) == ang) {
                current_key_index_ = i;
                break;
            }
        } catch(...) {}
    }

    needs_sync_to_json_ = true;
    sync_ui_from_json();
}

void MapLightPanel::delete_current_key() {
    json& L = ensure_light();
    auto& keys = L["keys"];
    if (keys.size() <= 1) return;
    if (current_key_index_ < 0 || current_key_index_ >= (int)keys.size()) return;
    keys.erase(keys.begin() + current_key_index_);
    if (current_key_index_ >= (int)keys.size()) current_key_index_ = (int)keys.size() - 1;

    needs_sync_to_json_ = true;
    sync_ui_from_json();
}

void MapLightPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;

    DockableCollapsible::update(input, screen_w, screen_h);

    apply_immediate_settings();

}

bool MapLightPanel::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    bool used = DockableCollapsible::handle_event(e);

    if (used) {
        needs_sync_to_json_ = true;
        if (update_map_light_checkbox_) {
            bool current = update_map_light_checkbox_->value();
            if (current != update_map_light_enabled_) {
                update_map_light_enabled_ = current;
                devmode::ui_settings::save_bool(kUpdateMapLightSettingKey, update_map_light_enabled_);
            }
        }
    }

    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }

    return used;
}

void MapLightPanel::render(SDL_Renderer* r) const {
    if (!visible_) return;
    DockableCollapsible::render(r);
}

bool MapLightPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void MapLightPanel::update_save_status(bool success) const {
    if (!warning_label_) {
        return;
    }
    const std::string failure_message = "Failed to save map lighting changes. Check logs.";
    if (success) {
        if (!persistence_warning_text_.empty()) {
            persistence_warning_text_.clear();
            warning_label_->set_text({});
            const_cast<MapLightPanel*>(this)->layout();
        }
        return;
    }
    if (persistence_warning_text_ != failure_message) {
        persistence_warning_text_ = failure_message;
        warning_label_->set_text(persistence_warning_text_);
        const_cast<MapLightPanel*>(this)->layout();
    }
}

void MapLightPanel::render_content(SDL_Renderer* r) const {

    if (!r) return;

    if (!editing_light_.is_object()) return;
    const json& L = editing_light_;
    auto keys_it = L.find("keys");
    if (keys_it == L.end() || !keys_it->is_array()) return;
    const auto& keys = *keys_it;
    if (keys.empty()) return;

    int r_out=255,g_out=255,b_out=255,a_out=255;
    double ang = 0.0;
    try {
        const auto& K = keys.at(std::min<int>(current_key_index_, (int)keys.size()-1));
        if (K.is_array() && K.size() >= 2) {
            ang = K[0].get<double>();
            const auto& kc = K[1];
            if (kc.is_array() && kc.size() >= 4) {
                r_out = clamp_int(kc[0].get<int>(), 0, 255);
                g_out = clamp_int(kc[1].get<int>(), 0, 255);
                b_out = clamp_int(kc[2].get<int>(), 0, 255);
                a_out = clamp_int(kc[3].get<int>(), 0, 255);
            }
        }
    } catch(...) {}

    SDL_Rect swatch = body_viewport_;
    swatch.y += std::max(0, swatch.h - 24);
    swatch.h = 16;
    swatch.w = std::min(120, swatch.w);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const SDL_Color fill_color{static_cast<Uint8>(r_out), static_cast<Uint8>(g_out), static_cast<Uint8>(b_out), static_cast<Uint8>(a_out)};
    const int radius = std::min(DMStyles::CornerRadius(), std::min(swatch.w, swatch.h) / 2);
    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(swatch.w, swatch.h) / 2));
    dm_draw::DrawBeveledRect(
        r,
        swatch,
        radius,
        bevel,
        fill_color,
        fill_color,
        fill_color,
        false,
        0.0f,
        0.0f);

    const SDL_Color border = DMStyles::Border();
    dm_draw::DrawRoundedOutline(
        r,
        swatch,
        radius,
        1,
        border);

}

