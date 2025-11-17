#include "dev_mode/foreground_background_effect_panel.hpp"

#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/font_cache.hpp"
#include "dev_mode/widgets.hpp"
#include "render/camera.hpp"
#include "utils/cache_manager.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class LocalSpacerWidget : public Widget {
public:
    explicit LocalSpacerWidget(int h) : height_(h) {}
    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return height_; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer*) const override {}
    bool wants_full_row() const override { return true; }
private:
    SDL_Rect rect_{0,0,0,0};
    int height_ = 0;
};

class SectionLabelWidget : public Widget {
public:
    explicit SectionLabelWidget(std::string text)
        : text_(std::move(text)) {
        style_ = DMStyles::Label();
        style_.font_size = std::max(style_.font_size + 2, 18);
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
    SDL_Rect rect_{0,0,0,0};
    std::string text_;
    DMLabelStyle style_;
};

class ImagePreviewWidget : public Widget {
public:
    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return preferred_height_; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        // Draw background
        SDL_SetRenderDrawColor(renderer, 18, 20, 26, 255);
        SDL_RenderFillRect(renderer, &rect_);
        SDL_SetRenderDrawColor(renderer, 38, 42, 52, 255);
        SDL_RenderDrawRect(renderer, &rect_);
        // Draw the single processed texture centered
        if (processed_texture_) {
            const int padding = 8;
            SDL_Rect display_area{
                rect_.x + padding,
                rect_.y + padding,
                std::max(0, rect_.w - padding * 2),
                std::max(0, rect_.h - padding * 2)
            };
            draw_centered_texture(renderer, display_area, processed_texture_, processed_w_, processed_h_);
        }
    }
    bool wants_full_row() const override { return true; }

    void set_texture(SDL_Texture* texture, int w, int h) {
        processed_texture_ = texture;
        processed_w_ = w;
        processed_h_ = h;
    }

    void clear_texture() {
        processed_texture_ = nullptr;
        processed_w_ = processed_h_ = 0;
    }

private:
    static void draw_centered_texture(SDL_Renderer* renderer, const SDL_Rect& area, SDL_Texture* tex, int tex_w, int tex_h) {
        if (!tex || tex_w <= 0 || tex_h <= 0 || area.w <= 0 || area.h <= 0) {
            return;
        }
        float scale_w = static_cast<float>(area.w) / static_cast<float>(tex_w);
        float scale_h = static_cast<float>(area.h) / static_cast<float>(tex_h);
        float scale = std::min(scale_w, scale_h);
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
        const int draw_w = static_cast<int>(std::round(static_cast<float>(tex_w) * scale));
        const int draw_h = static_cast<int>(std::round(static_cast<float>(tex_h) * scale));
        SDL_Rect dst{
            area.x + (area.w - draw_w) / 2,
            area.y + (area.h - draw_h) / 2,
            draw_w,
            draw_h
        };
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
    }

    SDL_Rect rect_{0,0,0,200};
    int preferred_height_ = 200;
    SDL_Texture* processed_texture_ = nullptr;
    int processed_w_ = 0;
    int processed_h_ = 0;
};

struct PreviewTextureSelection {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

bool query_texture_size(SDL_Texture* texture, int& width, int& height) {
    width = 0;
    height = 0;
    if (!texture) {
        return false;
    }
    if (SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) != 0) {
        width = 0;
        height = 0;
        return false;
    }
    return width > 0 && height > 0;
}

PreviewTextureSelection pick_smallest_cached_variant(Animation& animation) {
    PreviewTextureSelection selection{};
    if (animation.frames.empty()) {
        return selection;
    }

    std::size_t frame_index = 0;
    SDL_Texture* fallback = nullptr;
    for (std::size_t idx = 0; idx < animation.frames.size(); ++idx) {
        if (animation.frames[idx]) {
            fallback = animation.frames[idx];
            frame_index = idx;
            break;
        }
    }

    if (!fallback) {
        return selection;
    }

    const std::size_t variant_count = animation.variant_count();
    if (variant_count > 0) {
        double best_area = std::numeric_limits<double>::max();
        for (std::size_t variant_idx = 0; variant_idx < variant_count; ++variant_idx) {
            SDL_Texture* candidate = animation.frame_variant(frame_index, variant_idx);
            if (!candidate) {
                continue;
            }
            int candidate_w = 0;
            int candidate_h = 0;
            if (!query_texture_size(candidate, candidate_w, candidate_h)) {
                continue;
            }
            const double area = static_cast<double>(candidate_w) * static_cast<double>(candidate_h);
            if (!selection.texture || area < best_area) {
                best_area = area;
                selection.texture = candidate;
                selection.width = candidate_w;
                selection.height = candidate_h;
            }
        }
    }

    if (!selection.texture && query_texture_size(fallback, selection.width, selection.height)) {
        selection.texture = fallback;
    }

    return selection;
}

} // namespace

ForegroundBackgroundEffectPanel::ForegroundBackgroundEffectPanel(Assets* assets, int x, int y)
    : DockableCollapsible("Image Effects", true, x, y),
      assets_(assets) {
    set_padding(DMSpacing::panel_padding());
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::item_gap());
    set_floating_content_width(520);
    set_close_button_enabled(true);
    set_header_button_style(&DMStyles::AccentButton());
    header_spacer_ = std::make_unique<LocalSpacerWidget>(DMSpacing::header_gap());
    build_ui();
    refresh_from_camera();
    rebuild_asset_options();
    load_depth_cue_settings_from_manifest();
}

ForegroundBackgroundEffectPanel::~ForegroundBackgroundEffectPanel() {
    destroy_preview_textures();
}

void ForegroundBackgroundEffectPanel::set_assets(Assets* assets) {
    assets_ = assets;
    destroy_preview_textures();
    rebuild_asset_options();
    refresh_from_camera();
}

void ForegroundBackgroundEffectPanel::open() {
    set_visible(true);
    DockableCollapsible::open();
}

void ForegroundBackgroundEffectPanel::close() {
    DockableCollapsible::close();
    if (close_callback_) {
        close_callback_();
    }
}

bool ForegroundBackgroundEffectPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

void ForegroundBackgroundEffectPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    if (preview_dirty_) {
        rebuild_previews();
    }
}

bool ForegroundBackgroundEffectPanel::handle_event(const SDL_Event& e) {
    return DockableCollapsible::handle_event(e);
}

void ForegroundBackgroundEffectPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
    DMDropdown::render_active_options(renderer);
}

void ForegroundBackgroundEffectPanel::render_content(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (!preview_) return;
    if (preview_rect_.w <= 0 || preview_rect_.h <= 0) return;
    preview_->render(renderer);
}

void ForegroundBackgroundEffectPanel::build_ui() {
    // Mode toggle buttons
    fg_mode_button_ = std::make_unique<DMButton>("Foreground", &DMStyles::AccentButton(), 0, DMButton::height());
    bg_mode_button_ = std::make_unique<DMButton>("Background", &DMStyles::HeaderButton(), 0, DMButton::height());
    fg_mode_button_widget_ = std::make_unique<ButtonWidget>(fg_mode_button_.get(), [this]() { set_mode(EffectMode::Foreground); });
    bg_mode_button_widget_ = std::make_unique<ButtonWidget>(bg_mode_button_.get(), [this]() { set_mode(EffectMode::Background); });

    recreate_asset_dropdown();

    // Single preview widget
    preview_ = std::make_unique<ImagePreviewWidget>();

    // Configure sliders (single set for current mode)
    auto configure_slider = [this](std::unique_ptr<FloatSliderWidget>& target,
                                   const std::string& label,
                                   float min,
                                   float max,
                                   float step,
                                   int decimals) {
        target = std::make_unique<FloatSliderWidget>(label, min, max, step, 0.0f, decimals);
        target->set_on_value_changed([this](float) { on_slider_changed(); });
    };

    configure_slider(rgb_boost_, "RGB Boost", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(contrast_, "Contrast", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(brightness_, "Brightness", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(blur_, "Blur / Sharpen", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(saturation_r_, "Red Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(saturation_g_, "Green Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(saturation_b_, "Blue Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(hue_, "Hue Shift (deg)", -180.0f, 180.0f, 1.0f, 0);

    apply_button_ = std::make_unique<DMButton>("Create Effects", &DMStyles::AccentButton(), 0, DMButton::height());
    apply_button_widget_ = std::make_unique<ButtonWidget>(apply_button_.get(), [this]() { apply_and_regenerate(); });

    restore_defaults_button_ = std::make_unique<DMButton>("Restore Defaults", &DMStyles::WarnButton(), 0, DMButton::height());
    restore_defaults_button_widget_ = std::make_unique<ButtonWidget>(restore_defaults_button_.get(), [this]() { restore_defaults(); });
    restore_defaults_button_widget_->set_tooltip("Reset all current mode settings to zero");

    rebuild_rows();
}

void ForegroundBackgroundEffectPanel::rebuild_rows() {
    Rows rows;
    if (header_spacer_) rows.push_back({ header_spacer_.get() });

    // Mode toggle buttons in header
    if (fg_mode_button_widget_ && bg_mode_button_widget_) {
        rows.push_back({ fg_mode_button_widget_.get(), bg_mode_button_widget_.get() });
    }

    if (asset_dropdown_widget_) rows.push_back({ asset_dropdown_widget_.get() });

    // Single set of sliders for current mode
    rows.push_back({ rgb_boost_.get(), contrast_.get() });
    rows.push_back({ brightness_.get(), blur_.get() });
    rows.push_back({ saturation_r_.get(), saturation_g_.get() });
    rows.push_back({ saturation_b_.get(), hue_.get() });

    // Bottom action buttons row
    if (apply_button_widget_ || restore_defaults_button_widget_) {
        rows.push_back({ apply_button_widget_.get(), restore_defaults_button_widget_.get() });
    }
    set_rows(rows);
}

void ForegroundBackgroundEffectPanel::layout_custom_content(int, int) const {
    preview_rect_ = SDL_Rect{0, 0, 0, 0};
    if (!preview_) {
        return;
    }

    if (!is_visible() || !is_expanded() || body_viewport_h_ <= 0) {
        preview_->set_rect(preview_rect_);
        return;
    }

    const int preview_gap = DMSpacing::section_gap();
    preview_rect_.x = body_viewport_.x + body_viewport_.w + preview_gap;
    preview_rect_.y = body_viewport_.y;
    preview_rect_.w = kPreviewPanelWidth;
    preview_rect_.h = body_viewport_h_;

    preview_->set_rect(preview_rect_);

    body_viewport_.w = (preview_rect_.x + preview_rect_.w) - body_viewport_.x;

    const int preview_right = preview_rect_.x + preview_rect_.w;
    const int desired_panel_right = preview_right + padding_;
    const int current_panel_right = rect_.x + rect_.w;
    if (desired_panel_right > current_panel_right) {
        rect_.w = desired_panel_right - rect_.x;
    }
}

void ForegroundBackgroundEffectPanel::recreate_asset_dropdown() {
    std::vector<std::string> display = asset_names_;
    if (display.empty()) {
        display.emplace_back("No assets available");
    }
    int selected_index = 0;
    if (!asset_names_.empty() && !selected_asset_.empty()) {
        auto it = std::find(asset_names_.begin(), asset_names_.end(), selected_asset_);
        if (it != asset_names_.end()) {
            selected_index = static_cast<int>(std::distance(asset_names_.begin(), it));
        }
    }
    asset_dropdown_ = std::make_unique<DMDropdown>("Preview Asset", display, selected_index);
    asset_dropdown_->set_on_selection_changed([this](int idx) { handle_asset_selection(idx); });
    asset_dropdown_widget_ = std::make_unique<DropdownWidget>(asset_dropdown_.get());
    asset_dropdown_widget_->set_tooltip("Pick a reference asset to visualize the effect adjustments.");
}

void ForegroundBackgroundEffectPanel::rebuild_asset_options() {
    std::string previous = selected_asset_;
    asset_names_.clear();
    if (assets_) {
        const auto& all = assets_->library().all();
        asset_names_.reserve(all.size());
        for (const auto& entry : all) {
            asset_names_.push_back(entry.first);
        }
        std::sort(asset_names_.begin(), asset_names_.end());
    }
    if (!asset_names_.empty()) {
        auto it = std::find(asset_names_.begin(), asset_names_.end(), previous);
        if (it != asset_names_.end()) {
            selected_asset_ = *it;
        } else {
            selected_asset_ = asset_names_.front();
        }
    } else {
        selected_asset_.clear();
    }
    recreate_asset_dropdown();
    rebuild_rows();
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::handle_asset_selection(int index) {
    if (asset_names_.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(asset_names_.size()) - 1);
    selected_asset_ = asset_names_[static_cast<std::size_t>(index)];
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::update_controls_from_settings(const camera_effects::ImageEffectSettings& settings) {
    if (rgb_boost_) rgb_boost_->set_value(settings.rgb_boost);
    if (contrast_) contrast_->set_value(settings.contrast);
    if (brightness_) brightness_->set_value(settings.brightness);
    if (blur_) blur_->set_value(settings.blur);
    if (saturation_r_) saturation_r_->set_value(settings.saturation_red);
    if (saturation_g_) saturation_g_->set_value(settings.saturation_green);
    if (saturation_b_) saturation_b_->set_value(settings.saturation_blue);
    if (hue_) hue_->set_value(settings.hue);
    current_settings_ = settings;
}

camera_effects::ImageEffectSettings ForegroundBackgroundEffectPanel::read_current_settings() const {
    camera_effects::ImageEffectSettings settings{};
    if (rgb_boost_) settings.rgb_boost = rgb_boost_->value();
    if (contrast_) settings.contrast = contrast_->value();
    if (brightness_) settings.brightness = brightness_->value();
    if (blur_) settings.blur = blur_->value();
    if (saturation_r_) settings.saturation_red = saturation_r_->value();
    if (saturation_g_) settings.saturation_green = saturation_g_->value();
    if (saturation_b_) settings.saturation_blue = saturation_b_->value();
    if (hue_) settings.hue = hue_->value();
    camera_effects::ClampImageEffectSettings(settings);
    return settings;
}

void ForegroundBackgroundEffectPanel::save_current_mode_settings() {
    current_settings_ = read_current_settings();
    if (current_mode_ == EffectMode::Foreground) {
        fg_settings_ = current_settings_;
    } else {
        bg_settings_ = current_settings_;
    }
}

void ForegroundBackgroundEffectPanel::save_depth_cue_settings_to_manifest() {
    // Save the global image effect settings to manifest.json top-level image_effects section
    std::string manifest_path = "manifest.json";
    std::error_code ec;
    if (!fs::exists(manifest_path, ec)) {
        std::cerr << "[DepthCuePanel] manifest.json not found: " << manifest_path << "\n";
        return;
    }

    nlohmann::json manifest;
    try {
        std::ifstream manifest_file(manifest_path);
        if (!manifest_file.is_open()) {
            std::cerr << "[DepthCuePanel] Failed to open manifest.json\n";
            return;
        }
        manifest_file >> manifest;
    } catch (const std::exception& e) {
        std::cerr << "[DepthCuePanel] Error reading manifest.json: " << e.what() << "\n";
        return;
    }

    // Build the global image effects structure
    nlohmann::json image_effects = nlohmann::json::object();

    // Convert C++ ImageEffectSettings to JSON for foreground and background
    auto add_image_effects = [&](const std::string& type, const camera_effects::ImageEffectSettings& settings) {
        nlohmann::json effects_json = {
            {"rgb_boost", settings.rgb_boost},
            {"contrast", settings.contrast},
            {"brightness", settings.brightness},
            {"blur", settings.blur},
            {"saturation_red", settings.saturation_red},
            {"saturation_green", settings.saturation_green},
            {"saturation_blue", settings.saturation_blue},
            {"hue", settings.hue}
        };
        image_effects[type] = effects_json;
    };

    add_image_effects("foreground", fg_settings_);
    add_image_effects("background", bg_settings_);

    // Update the top-level image_effects section
    manifest["image_effects"] = image_effects;

    // Save the manifest back
    try {
        std::ofstream manifest_file(manifest_path);
        if (!manifest_file.is_open()) {
            std::cerr << "[DepthCuePanel] Failed to write manifest.json\n";
            return;
        }
        manifest_file << manifest.dump(2);
    } catch (const std::exception& e) {
        std::cerr << "[DepthCuePanel] Error writing manifest.json: " << e.what() << "\n";
        return;
    }

    std::cout << "[DepthCuePanel] Saved global image effect settings\n";
    has_unsaved_changes_ = false;
}

bool ForegroundBackgroundEffectPanel::load_depth_cue_settings_from_manifest() {
    // Load global image effect settings from manifest.json top-level image_effects section
    std::string manifest_path = "manifest.json";
    std::error_code ec;
    if (!fs::exists(manifest_path, ec)) {
        std::cerr << "[DepthCuePanel] manifest.json not found: " << manifest_path << "\n";
        return false;
    }

    nlohmann::json manifest;
    try {
        std::ifstream manifest_file(manifest_path);
        if (!manifest_file.is_open()) {
            std::cerr << "[DepthCuePanel] Failed to open manifest.json\n";
            return false;
        }
        manifest_file >> manifest;
    } catch (const std::exception& e) {
        std::cerr << "[DepthCuePanel] Error reading manifest.json: " << e.what() << "\n";
        return false;
    }

    // Check for the top-level image_effects section
    if (!manifest.contains("image_effects") || !manifest["image_effects"].is_object()) {
        std::cout << "[DepthCuePanel] No image_effects section found in manifest.json, using defaults\n";
        // Reset to defaults
        fg_settings_ = camera_effects::ImageEffectSettings{};
        bg_settings_ = camera_effects::ImageEffectSettings{};
        saved_fg_ = fg_settings_;
        saved_bg_ = bg_settings_;
        load_current_mode_settings();
        return false;
    }

    auto& image_effects = manifest["image_effects"];

    // Load image effects from JSON
    auto load_image_effects = [&](const std::string& type) -> camera_effects::ImageEffectSettings {
        camera_effects::ImageEffectSettings settings{};

        if (!image_effects.contains(type)) {
            std::cout << "[DepthCuePanel] Missing " << type << " effects in image_effects, using defaults\n";
            return settings;
        }

        auto& effects_json = image_effects[type];
        if (!effects_json.is_object()) {
            std::cout << "[DepthCuePanel] Invalid " << type << " effects format, using defaults\n";
            return settings;
        }

        // Load each effect value
        settings.rgb_boost = effects_json.value("rgb_boost", 0.0f);
        settings.contrast = effects_json.value("contrast", 0.0f);
        settings.brightness = effects_json.value("brightness", 0.0f);
        settings.blur = effects_json.value("blur", 0.0f);
        settings.saturation_red = effects_json.value("saturation_red", 0.0f);
        settings.saturation_green = effects_json.value("saturation_green", 0.0f);
        settings.saturation_blue = effects_json.value("saturation_blue", 0.0f);
        settings.hue = effects_json.value("hue", 0.0f);

        camera_effects::ClampImageEffectSettings(settings);
        return settings;
    };

    // Load foreground and background settings
    fg_settings_ = load_image_effects("foreground");
    bg_settings_ = load_image_effects("background");

    saved_fg_ = fg_settings_;
    saved_bg_ = bg_settings_;
    load_current_mode_settings();

    std::cout << "[DepthCuePanel] Loaded global image effect settings\n";
    return true;
}

void ForegroundBackgroundEffectPanel::generate_preview_with_python(const std::string& image_path, const std::string& effect_type) {
    // Call Python script to generate preview using global image effects settings from manifest
    if (image_path.empty() || effect_type.empty()) {
        std::cerr << "[DepthCuePanel] Invalid preview parameters\n";
        return;
    }

    std::string python_cmd = "python tools/asset_tool.py build-texture";
    std::string manifest_arg = "--manifest-path manifest.json";
    std::string preview_image_arg = "--preview-image \"" + image_path + "\"";
    std::string preview_type_arg = "--preview-type " + effect_type;

    std::string full_cmd = python_cmd + " " + preview_image_arg + " " + preview_type_arg + " " + manifest_arg;

    std::cout << "[DepthCuePanel] Executing: " << full_cmd << "\n";

    // Execute the command (simplified for now - would need proper process execution)
    int result = std::system(full_cmd.c_str());

    if (result == 0) {
        std::cout << "[DepthCuePanel] Preview generated successfully\n";
        // Trigger preview reload if needed
        preview_dirty_ = true;
    } else {
        std::cerr << "[DepthCuePanel] Failed to generate preview, exit code: " << result << "\n";
    }
}

void ForegroundBackgroundEffectPanel::load_current_mode_settings() {
    if (current_mode_ == EffectMode::Foreground) {
        current_settings_ = fg_settings_;
    } else {
        current_settings_ = bg_settings_;
    }
    update_controls_from_settings(current_settings_);
}

void ForegroundBackgroundEffectPanel::set_mode(EffectMode mode) {
    if (mode == current_mode_) return;

    // Save current settings before switching
    save_current_mode_settings();

    // Switch mode
    current_mode_ = mode;

    // Update button styles
    if (fg_mode_button_ && bg_mode_button_) {
        fg_mode_button_->set_style(current_mode_ == EffectMode::Foreground ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
        bg_mode_button_->set_style(current_mode_ == EffectMode::Background ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
    }

    // Load settings for new mode
    load_current_mode_settings();

    // Rebuild preview
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::on_slider_changed() {
    save_current_mode_settings();
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::refresh_from_camera() {
    if (!assets_) {
        fg_settings_ = camera_effects::ImageEffectSettings{};
        bg_settings_ = camera_effects::ImageEffectSettings{};
        saved_fg_ = fg_settings_;
        saved_bg_ = bg_settings_;
        load_current_mode_settings();
        return;
    }
    camera& cam = assets_->getView();
    const camera::RealismSettings& settings = cam.realism_settings();
    fg_settings_ = settings.foreground_effects;
    bg_settings_ = settings.background_effects;
    saved_fg_ = fg_settings_;
    saved_bg_ = bg_settings_;
    load_current_mode_settings();
    has_unsaved_changes_ = false;
    preview_dirty_ = true;
}

bool ForegroundBackgroundEffectPanel::ensure_preview_source() {
    if (!assets_) {
        return false;
    }
    base_preview_texture_ = nullptr;
    base_preview_w_ = base_preview_h_ = 0;

    SDL_Renderer* renderer = assets_->renderer();
    if (!renderer) {
        return false;
    }
    if (selected_asset_.empty()) {
        return false;
    }
    auto info = assets_->library().get(selected_asset_);
    if (!info) {
        return false;
    }

    const bool asset_changed = !preview_info_ || preview_info_.get() != info.get();
    if (asset_changed) {
        preview_animation_id_.clear();
    }
    preview_info_ = info;

    auto select_animation = [&]() -> Animation* {
        if (!preview_animation_id_.empty()) {
            auto it = info->animations.find(preview_animation_id_);
            if (it != info->animations.end()) {
                return &it->second;
            }
        }
        if (!info->animations.empty()) {
            preview_animation_id_ = info->animations.begin()->first;
            return &info->animations.begin()->second;
        }
        return nullptr;
    };

    bool reloaded_asset = false;
    if (asset_changed || info->animations.empty()) {
        info->loadAnimations(renderer);
        reloaded_asset = true;
    }

    Animation* anim = select_animation();
    if (!anim && !reloaded_asset) {
        info->loadAnimations(renderer);
        reloaded_asset = true;
        anim = select_animation();
    }
    if (!anim || anim->frames.empty()) {
        return false;
    }

    PreviewTextureSelection selection = pick_smallest_cached_variant(*anim);
    if (!selection.texture && !reloaded_asset) {
        info->loadAnimations(renderer);
        reloaded_asset = true;
        anim = select_animation();
        if (anim) {
            selection = pick_smallest_cached_variant(*anim);
        }
    }

    if (!selection.texture) {
        return false;
    }

    base_preview_texture_ = selection.texture;
    base_preview_w_ = selection.width;
    base_preview_h_ = selection.height;
    return true;
}

void ForegroundBackgroundEffectPanel::destroy_preview_textures() {
    if (current_preview_texture_) {
        SDL_DestroyTexture(current_preview_texture_);
        current_preview_texture_ = nullptr;
    }
    current_preview_w_ = current_preview_h_ = 0;
}

void ForegroundBackgroundEffectPanel::rebuild_previews() {
    preview_dirty_ = false;
    if (auto* image_preview = dynamic_cast<ImagePreviewWidget*>(preview_.get())) {
        image_preview->clear_texture();
    }
    destroy_preview_textures();

    if (!ensure_preview_source()) {
        return;
    }

    SDL_Renderer* renderer = assets_ ? assets_->renderer() : nullptr;
    if (!renderer) {
        return;
    }

    // No longer using C++ image effects for preview - handled by Python tool
    const bool has_adjustments = !camera_effects::ImageEffectSettingsIsIdentity(current_settings_);
    // Preview effects are now generated via Python script, so just show original texture

    // Set texture on the single preview widget (show processed or original)
    if (auto* image_preview = dynamic_cast<ImagePreviewWidget*>(preview_.get())) {
        SDL_Texture* texture = base_preview_texture_;
        int tex_w = base_preview_w_;
        int tex_h = base_preview_h_;
        if (has_adjustments && current_preview_texture_) {
            texture = current_preview_texture_;
            tex_w = current_preview_w_;
            tex_h = current_preview_h_;
        }
        image_preview->set_texture(texture, tex_w, tex_h);
    }
}

void ForegroundBackgroundEffectPanel::apply_and_regenerate() {
    if (!assets_) {
        return;
    }
    SDL_Renderer* renderer = assets_->renderer();
    if (!renderer) {
        return;
    }
    camera& cam = assets_->getView();
    camera::RealismSettings settings = cam.realism_settings();
    settings.foreground_effects = fg_settings_;
    settings.background_effects = bg_settings_;
    cam.set_realism_settings(settings);
    assets_->on_camera_settings_changed();

    const std::uint64_t fg_hash = camera_effects::HashImageEffectSettings(fg_settings_);
    const std::uint64_t bg_hash = camera_effects::HashImageEffectSettings(bg_settings_);
    purge_mismatched_caches(fg_hash, bg_hash, true);

    assets_->library().loadAllAnimations(renderer);
    saved_fg_ = fg_settings_;
    saved_bg_ = bg_settings_;
    has_unsaved_changes_ = false;
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::restore_defaults() {
    // Reset current mode settings to zero
    camera_effects::ImageEffectSettings zero_settings{};
    current_settings_ = zero_settings;
    update_controls_from_settings(current_settings_);

    // Save the reset settings to current mode storage
    save_current_mode_settings();

    // Rebuild preview to show reset appearance
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::purge_mismatched_caches(std::uint64_t fg_hash, std::uint64_t bg_hash, bool force_purge) {
    const fs::path cache_root("cache");
    std::error_code ec;
    if (!fs::exists(cache_root, ec) || !fs::is_directory(cache_root, ec)) {
        return;
    }
    for (const auto& asset_entry : fs::directory_iterator(cache_root, ec)) {
        if (!asset_entry.is_directory()) {
            continue;
        }
        const fs::path animations_dir = asset_entry.path() / "animations";
        if (!fs::exists(animations_dir, ec) || !fs::is_directory(animations_dir, ec)) {
            continue;
        }
        for (const auto& anim_entry : fs::directory_iterator(animations_dir, ec)) {
            if (!anim_entry.is_directory()) {
                continue;
            }
            const fs::path meta_path = anim_entry.path() / "metadata.json";
            nlohmann::json meta;
            if (!CacheManager::load_metadata(meta_path.generic_string(), meta)) {
                continue;
            }
            auto read_hash = [&](const char* key, std::uint64_t& value) -> bool {
                if (!meta.contains(key)) {
                    return false;
                }
                const auto& node = meta.at(key);
                if (!node.is_number_integer() && !node.is_number_unsigned()) {
                    return false;
                }
                try {
                    value = node.get<std::uint64_t>();
                } catch (...) {
                    return false;
                }
                return true;
            };
            std::uint64_t stored_fg = 0;
            std::uint64_t stored_bg = 0;
            const bool has_fg = read_hash("foreground_effect_hash", stored_fg);
            const bool has_bg = read_hash("background_effect_hash", stored_bg);
            const bool hashes_match = has_fg && has_bg && stored_fg == fg_hash && stored_bg == bg_hash;
            if (!force_purge && hashes_match) {
                continue;
            }

            std::error_code scale_ec;
            for (const auto& scale_entry : fs::directory_iterator(anim_entry.path(), scale_ec)) {
                if (!scale_entry.is_directory()) {
                    continue;
                }
                const std::string dir_name = scale_entry.path().filename().string();
                if (dir_name.rfind("scale_", 0) != 0) {
                    continue;
                }
                std::error_code remove_ec;
                fs::remove_all(scale_entry.path() / "foreground", remove_ec);
                remove_ec.clear();
                fs::remove_all(scale_entry.path() / "background", remove_ec);
            }
        }
    }
}
