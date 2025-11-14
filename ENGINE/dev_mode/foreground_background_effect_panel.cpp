#include "dev_mode/foreground_background_effect_panel.hpp"

#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/widgets.hpp"
#include "render/camera.hpp"
#include "utils/cache_manager.hpp"
#include "utils/image_effects.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

class SpacerWidget : public Widget {
public:
    explicit SpacerWidget(int h) : height_(h) {}
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
        SDL_SetRenderDrawColor(renderer, 18, 20, 26, 255);
        SDL_RenderFillRect(renderer, &rect_);
        SDL_SetRenderDrawColor(renderer, 38, 42, 52, 255);
        SDL_RenderDrawRect(renderer, &rect_);
        const int padding = 8;
        const int inner_w = std::max(0, rect_.w - padding * 3);
        const int slot_w = inner_w / 2;
        SDL_Rect original_slot{
            rect_.x + padding,
            rect_.y + padding,
            slot_w,
            std::max(0, rect_.h - padding * 2)
        };
        SDL_Rect processed_slot{
            original_slot.x + slot_w + padding,
            original_slot.y,
            slot_w,
            original_slot.h
        };
        draw_slot(renderer, original_slot, base_texture_, base_w_, base_h_);
        draw_slot(renderer, processed_slot, processed_texture_, processed_w_, processed_h_);
    }
    bool wants_full_row() const override { return true; }

    void set_textures(SDL_Texture* original, int ow, int oh, SDL_Texture* processed, int pw, int ph) {
        base_texture_ = original;
        base_w_ = ow;
        base_h_ = oh;
        processed_texture_ = processed;
        processed_w_ = pw;
        processed_h_ = ph;
    }

    void clear_textures() {
        base_texture_ = nullptr;
        processed_texture_ = nullptr;
        base_w_ = base_h_ = processed_w_ = processed_h_ = 0;
    }

private:
    static void draw_slot(SDL_Renderer* renderer, const SDL_Rect& slot, SDL_Texture* tex, int tex_w, int tex_h) {
        SDL_SetRenderDrawColor(renderer, 24, 26, 34, 255);
        SDL_RenderFillRect(renderer, &slot);
        SDL_SetRenderDrawColor(renderer, 64, 70, 84, 255);
        SDL_RenderDrawRect(renderer, &slot);
        if (!tex || tex_w <= 0 || tex_h <= 0) {
            return;
        }
        float scale_w = static_cast<float>(slot.w) / static_cast<float>(tex_w);
        float scale_h = static_cast<float>(slot.h) / static_cast<float>(tex_h);
        float scale = std::min(scale_w, scale_h);
        if (!std::isfinite(scale) || scale <= 0.0f) {
            scale = 1.0f;
        }
        const int draw_w = static_cast<int>(std::round(static_cast<float>(tex_w) * scale));
        const int draw_h = static_cast<int>(std::round(static_cast<float>(tex_h) * scale));
        SDL_Rect dst{
            slot.x + (slot.w - draw_w) / 2,
            slot.y + (slot.h - draw_h) / 2,
            draw_w,
            draw_h
        };
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
    }

    SDL_Rect rect_{0,0,0,200};
    int preferred_height_ = 200;
    SDL_Texture* base_texture_ = nullptr;
    SDL_Texture* processed_texture_ = nullptr;
    int base_w_ = 0;
    int base_h_ = 0;
    int processed_w_ = 0;
    int processed_h_ = 0;
};

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
    header_spacer_ = std::make_unique<SpacerWidget>(DMSpacing::header_gap());
    build_ui();
    refresh_from_camera();
    rebuild_asset_options();
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
    if (!is_visible()) return;
    DockableCollapsible::render(renderer);
    DMDropdown::render_active_options(renderer);
}

void ForegroundBackgroundEffectPanel::build_ui() {
    recreate_asset_dropdown();
    fg_widgets_.label = std::make_unique<SectionLabelWidget>("Foreground Effects");
    bg_widgets_.label = std::make_unique<SectionLabelWidget>("Background Effects");
    fg_widgets_.preview = std::make_unique<ImagePreviewWidget>();
    bg_widgets_.preview = std::make_unique<ImagePreviewWidget>();

    auto configure_slider = [this](std::unique_ptr<FloatSliderWidget>& target,
                                   const std::string& label,
                                   float min,
                                   float max,
                                   float step,
                                   int decimals) {
        target = std::make_unique<FloatSliderWidget>(label, min, max, step, 0.0f, decimals);
        target->set_on_value_changed([this](float) { on_slider_changed(); });
    };

    configure_slider(fg_widgets_.rgb_boost, "RGB Boost", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.contrast, "Contrast", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.brightness, "Brightness", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.blur, "Blur / Sharpen", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.saturation_r, "Red Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.saturation_g, "Green Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.saturation_b, "Blue Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(fg_widgets_.hue, "Hue Shift (deg)", -180.0f, 180.0f, 1.0f, 0);

    configure_slider(bg_widgets_.rgb_boost, "RGB Boost", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.contrast, "Contrast", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.brightness, "Brightness", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.blur, "Blur / Sharpen", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.saturation_r, "Red Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.saturation_g, "Green Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.saturation_b, "Blue Saturation", -1.0f, 1.0f, 0.02f, 2);
    configure_slider(bg_widgets_.hue, "Hue Shift (deg)", -180.0f, 180.0f, 1.0f, 0);

    apply_button_ = std::make_unique<DMButton>("Create All with These Effects", &DMStyles::AccentButton(), 0, DMButton::height());
    apply_button_widget_ = std::make_unique<ButtonWidget>(apply_button_.get(), [this]() { apply_and_regenerate(); });

    rebuild_rows();
}

void ForegroundBackgroundEffectPanel::rebuild_rows() {
    Rows rows;
    if (header_spacer_) rows.push_back({ header_spacer_.get() });
    if (asset_dropdown_widget_) rows.push_back({ asset_dropdown_widget_.get() });
    if (fg_widgets_.label) rows.push_back({ fg_widgets_.label.get() });
    if (fg_widgets_.preview) rows.push_back({ fg_widgets_.preview.get() });
    rows.push_back({ fg_widgets_.rgb_boost.get(), fg_widgets_.contrast.get() });
    rows.push_back({ fg_widgets_.brightness.get(), fg_widgets_.blur.get() });
    rows.push_back({ fg_widgets_.saturation_r.get(), fg_widgets_.saturation_g.get() });
    rows.push_back({ fg_widgets_.saturation_b.get(), fg_widgets_.hue.get() });

    if (bg_widgets_.label) rows.push_back({ bg_widgets_.label.get() });
    if (bg_widgets_.preview) rows.push_back({ bg_widgets_.preview.get() });
    rows.push_back({ bg_widgets_.rgb_boost.get(), bg_widgets_.contrast.get() });
    rows.push_back({ bg_widgets_.brightness.get(), bg_widgets_.blur.get() });
    rows.push_back({ bg_widgets_.saturation_r.get(), bg_widgets_.saturation_g.get() });
    rows.push_back({ bg_widgets_.saturation_b.get(), bg_widgets_.hue.get() });

    if (apply_button_widget_) rows.push_back({ apply_button_widget_.get() });
    set_rows(rows);
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

void ForegroundBackgroundEffectPanel::update_section_from_settings(const camera_effects::ImageEffectSettings& settings,
                                                                  SectionWidgets& widgets) {
    if (widgets.rgb_boost) widgets.rgb_boost->set_value(settings.rgb_boost);
    if (widgets.contrast) widgets.contrast->set_value(settings.contrast);
    if (widgets.brightness) widgets.brightness->set_value(settings.brightness);
    if (widgets.blur) widgets.blur->set_value(settings.blur);
    if (widgets.saturation_r) widgets.saturation_r->set_value(settings.saturation_red);
    if (widgets.saturation_g) widgets.saturation_g->set_value(settings.saturation_green);
    if (widgets.saturation_b) widgets.saturation_b->set_value(settings.saturation_blue);
    if (widgets.hue) widgets.hue->set_value(settings.hue);
}

camera_effects::ImageEffectSettings ForegroundBackgroundEffectPanel::read_section_settings(const SectionWidgets& widgets) const {
    camera_effects::ImageEffectSettings settings{};
    if (widgets.rgb_boost) settings.rgb_boost = widgets.rgb_boost->value();
    if (widgets.contrast) settings.contrast = widgets.contrast->value();
    if (widgets.brightness) settings.brightness = widgets.brightness->value();
    if (widgets.blur) settings.blur = widgets.blur->value();
    if (widgets.saturation_r) settings.saturation_red = widgets.saturation_r->value();
    if (widgets.saturation_g) settings.saturation_green = widgets.saturation_g->value();
    if (widgets.saturation_b) settings.saturation_blue = widgets.saturation_b->value();
    if (widgets.hue) settings.hue = widgets.hue->value();
    camera_effects::ClampImageEffectSettings(settings);
    return settings;
}

void ForegroundBackgroundEffectPanel::on_slider_changed() {
    fg_settings_ = read_section_settings(fg_widgets_);
    bg_settings_ = read_section_settings(bg_widgets_);
    has_unsaved_changes_ =
        !camera_effects::ImageEffectSettingsEqual(fg_settings_, saved_fg_) ||
        !camera_effects::ImageEffectSettingsEqual(bg_settings_, saved_bg_);
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::refresh_from_camera() {
    if (!assets_) {
        fg_settings_ = camera_effects::ImageEffectSettings{};
        bg_settings_ = camera_effects::ImageEffectSettings{};
        saved_fg_ = fg_settings_;
        saved_bg_ = bg_settings_;
        update_section_from_settings(fg_settings_, fg_widgets_);
        update_section_from_settings(bg_settings_, bg_widgets_);
        return;
    }
    camera& cam = assets_->getView();
    const camera::RealismSettings& settings = cam.realism_settings();
    fg_settings_ = settings.foreground_effects;
    bg_settings_ = settings.background_effects;
    saved_fg_ = fg_settings_;
    saved_bg_ = bg_settings_;
    update_section_from_settings(fg_settings_, fg_widgets_);
    update_section_from_settings(bg_settings_, bg_widgets_);
    has_unsaved_changes_ = false;
    preview_dirty_ = true;
}

bool ForegroundBackgroundEffectPanel::ensure_preview_source() {
    if (!assets_) {
        return false;
    }
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
    preview_info_ = info;
    info->loadAnimations(renderer);
    Animation* anim = nullptr;
    if (!preview_animation_id_.empty()) {
        auto it = info->animations.find(preview_animation_id_);
        if (it != info->animations.end()) {
            anim = &it->second;
        }
    }
    if (!anim && !info->animations.empty()) {
        anim = &info->animations.begin()->second;
        preview_animation_id_ = info->animations.begin()->first;
    }
    if (!anim || anim->frames.empty()) {
        base_preview_texture_ = nullptr;
        return false;
    }
    base_preview_texture_ = anim->frames[0];
    if (!base_preview_texture_) {
        return false;
    }
    if (SDL_QueryTexture(base_preview_texture_, nullptr, nullptr, &base_preview_w_, &base_preview_h_) != 0) {
        base_preview_w_ = base_preview_h_ = 0;
        return false;
    }
    return true;
}

void ForegroundBackgroundEffectPanel::destroy_preview_textures() {
    if (fg_preview_texture_) {
        SDL_DestroyTexture(fg_preview_texture_);
        fg_preview_texture_ = nullptr;
    }
    if (bg_preview_texture_) {
        SDL_DestroyTexture(bg_preview_texture_);
        bg_preview_texture_ = nullptr;
    }
    fg_preview_w_ = fg_preview_h_ = 0;
    bg_preview_w_ = bg_preview_h_ = 0;
}

void ForegroundBackgroundEffectPanel::rebuild_previews() {
    preview_dirty_ = false;
    destroy_preview_textures();
    if (!ensure_preview_source()) {
        if (auto* preview = dynamic_cast<ImagePreviewWidget*>(fg_widgets_.preview.get())) {
            preview->clear_textures();
        }
        if (auto* preview = dynamic_cast<ImagePreviewWidget*>(bg_widgets_.preview.get())) {
            preview->clear_textures();
        }
        return;
    }
    SDL_Renderer* renderer = assets_ ? assets_->renderer() : nullptr;
    if (!renderer) {
        return;
    }
    if (base_preview_texture_) {
        fg_preview_texture_ = image_effects::BakeImageEffectTexture(renderer,
                                                                    base_preview_texture_,
                                                                    base_preview_w_,
                                                                    base_preview_h_,
                                                                    fg_settings_);
        bg_preview_texture_ = image_effects::BakeImageEffectTexture(renderer,
                                                                    base_preview_texture_,
                                                                    base_preview_w_,
                                                                    base_preview_h_,
                                                                    bg_settings_);
        if (fg_preview_texture_) {
            SDL_QueryTexture(fg_preview_texture_, nullptr, nullptr, &fg_preview_w_, &fg_preview_h_);
        }
        if (bg_preview_texture_) {
            SDL_QueryTexture(bg_preview_texture_, nullptr, nullptr, &bg_preview_w_, &bg_preview_h_);
        }
    }
    if (auto* preview = dynamic_cast<ImagePreviewWidget*>(fg_widgets_.preview.get())) {
        preview->set_textures(base_preview_texture_,
                              base_preview_w_,
                              base_preview_h_,
                              fg_preview_texture_ ? fg_preview_texture_ : base_preview_texture_,
                              fg_preview_w_ ? fg_preview_w_ : base_preview_w_,
                              fg_preview_h_ ? fg_preview_h_ : base_preview_h_);
    }
    if (auto* preview = dynamic_cast<ImagePreviewWidget*>(bg_widgets_.preview.get())) {
        preview->set_textures(base_preview_texture_,
                              base_preview_w_,
                              base_preview_h_,
                              bg_preview_texture_ ? bg_preview_texture_ : base_preview_texture_,
                              bg_preview_w_ ? bg_preview_w_ : base_preview_w_,
                              bg_preview_h_ ? bg_preview_h_ : base_preview_h_);
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
    purge_mismatched_caches(fg_hash, bg_hash);

    assets_->library().loadAllAnimations(renderer);
    saved_fg_ = fg_settings_;
    saved_bg_ = bg_settings_;
    has_unsaved_changes_ = false;
    preview_dirty_ = true;
}

void ForegroundBackgroundEffectPanel::purge_mismatched_caches(std::uint64_t fg_hash, std::uint64_t bg_hash) {
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
            const std::uint64_t stored_fg = meta.value("depthcue_foreground_hash", 0ull);
            const std::uint64_t stored_bg = meta.value("depthcue_background_hash", 0ull);
            if (stored_fg == fg_hash && stored_bg == bg_hash) {
                continue;
            }
            std::error_code remove_ec;
            fs::remove_all(anim_entry.path() / "foreground", remove_ec);
            remove_ec.clear();
            fs::remove_all(anim_entry.path() / "background", remove_ec);
        }
    }
}
