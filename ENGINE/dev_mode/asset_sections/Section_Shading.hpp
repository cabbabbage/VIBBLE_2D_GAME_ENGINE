#pragma once

#include "../DockableCollapsible.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "asset/asset_info.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/shared/formatting.hpp"

class AssetInfoUI;

class Section_Shading : public DockableCollapsible {
public:
    Section_Shading() : DockableCollapsible("Shading", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Shading() override = default;

    void build() override {
        widgets_.clear();
        shading_enabled_checkbox_.reset();
        Rows rows;

        if (!info_) {
            auto placeholder = std::make_unique<ReadOnlyTextBoxWidget>( "", "No asset selected. Select an asset from the library or scene to view and edit its information.");
            rows.push_back({ placeholder.get() });
            widgets_.push_back(std::move(placeholder));
            set_rows(rows);
            return;
        }

        const ShadowMaskSettings settings = SanitizeShadowMaskSettings(info_->shadow_mask_settings);

        shading_enabled_checkbox_ = std::make_unique<DMCheckbox>("Enable Shading", info_->is_shaded);
        auto shading_checkbox_widget = std::make_unique<CheckboxWidget>(shading_enabled_checkbox_.get());
        rows.push_back({ shading_checkbox_widget.get() });
        widgets_.push_back(std::move(shading_checkbox_widget));

        auto make_scaled_slider = [](const std::string& label,
                                     float                min_value,
                                     float                max_value,
                                     float                current,
                                     int                  scale) {
            int min_i = static_cast<int>(std::round(min_value * static_cast<float>(scale)));
            int max_i = static_cast<int>(std::round(max_value * static_cast<float>(scale)));
            int cur_i = static_cast<int>(std::round(current * static_cast<float>(scale)));
            if (cur_i < min_i) {
                min_i = cur_i;
            }
            if (cur_i > max_i) {
                max_i = cur_i;
            }
            auto slider = std::make_unique<DMSlider>(label, min_i, max_i, std::clamp(cur_i, min_i, max_i));
            slider->set_defer_commit_until_unfocus(false);
            slider->set_value_formatter([scale](int value,
                                               std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
                const float scaled = static_cast<float>(value) / static_cast<float>(scale);
                return dev_mode::FormatSliderValue(scaled, 2, buffer);
            });
            slider->set_value_parser([scale](const std::string& text) -> std::optional<int> {
                try {
                    float parsed = std::stof(text);
                    return static_cast<int>(std::round(parsed * static_cast<float>(scale)));
                } catch (...) {
                    return std::nullopt;
                }
            });
            return slider;
};

        expansion_ratio_slider_ = make_scaled_slider("Expansion Ratio", 0.0f, 4.0f, settings.expansion_ratio, 100);
        blur_scale_slider_      = make_scaled_slider("Blur Scale", 0.0f, 8.0f, settings.blur_scale, 100);
        falloff_start_slider_   = make_scaled_slider("Falloff Start", 0.0f, 0.99f, settings.falloff_start, 100);
        falloff_exponent_slider_ = make_scaled_slider("Falloff Exponent", 0.01f, 20.0f, settings.falloff_exponent, 100);
        alpha_multiplier_slider_ = make_scaled_slider("Alpha Multiplier", 0.0f, 4.0f, settings.alpha_multiplier, 100);

        auto add_slider_row = [&](std::unique_ptr<DMSlider>& slider) {
            auto widget = std::make_unique<SliderWidget>(slider.get());
            rows.push_back({ widget.get() });
            widgets_.push_back(std::move(widget));
};

        add_slider_row(expansion_ratio_slider_);
        add_slider_row(blur_scale_slider_);
        add_slider_row(falloff_start_slider_);
        add_slider_row(falloff_exponent_slider_);
        add_slider_row(alpha_multiplier_slider_);

        auto preview_widget = std::make_unique<PreviewWidget>(this);
        rows.push_back({ preview_widget.get() });
        widgets_.push_back(std::move(preview_widget));

        generate_button_ = std::make_unique<DMButton>("Generate All", &DMStyles::AccentButton(), 200, DMButton::height());
        auto generate_widget = std::make_unique<ButtonWidget>(generate_button_.get(), [this]() { this->on_generate_all(); });
        rows.push_back({ generate_widget.get() });
        widgets_.push_back(std::move(generate_widget));

        set_rows(rows);
    }

    bool handle_event(const SDL_Event& e) override {
        if (!info_) {
            return DockableCollapsible::handle_event(e);
        }

        const bool was_shaded = info_->is_shaded;
        bool       used       = DockableCollapsible::handle_event(e);
        bool       shading_changed = false;

        if (shading_enabled_checkbox_) {
            const bool wants_shading = shading_enabled_checkbox_->value();
            if (wants_shading != was_shaded) {
                info_->set_shading_enabled(wants_shading);
                (void)info_->commit_manifest();
                shading_changed = true;
                if (ui_) {
                    ui_->sync_target_shading_settings();
                    ui_->regenerate_shadow_masks();
                }
            }
        }

        if (!expanded_) {
            return used || shading_changed;
        }

        if (expansion_ratio_slider_ && expansion_ratio_slider_->handle_event(e)) used = true;
        if (blur_scale_slider_ && blur_scale_slider_->handle_event(e)) used = true;
        if (falloff_start_slider_ && falloff_start_slider_->handle_event(e)) used = true;
        if (falloff_exponent_slider_ && falloff_exponent_slider_->handle_event(e)) used = true;
        if (alpha_multiplier_slider_ && alpha_multiplier_slider_->handle_event(e)) used = true;

        const ShadowMaskSettings previous = info_->shadow_mask_settings;
        ShadowMaskSettings       updated  = previous;

        auto read_slider = [](const std::unique_ptr<DMSlider>& slider, float fallback, int scale) {
            if (!slider) return fallback;
            return static_cast<float>(slider->displayed_value()) / static_cast<float>(scale);
};

        updated.expansion_ratio  = read_slider(expansion_ratio_slider_, updated.expansion_ratio, 100);
        updated.blur_scale       = read_slider(blur_scale_slider_, updated.blur_scale, 100);
        updated.falloff_start    = read_slider(falloff_start_slider_, updated.falloff_start, 100);
        updated.falloff_exponent = read_slider(falloff_exponent_slider_, updated.falloff_exponent, 100);
        updated.alpha_multiplier = read_slider(alpha_multiplier_slider_, updated.alpha_multiplier, 100);

        auto nearly_equal = [](float a, float b) {
            return std::fabs(a - b) <= 0.0005f;
};

        const bool changed = !nearly_equal(previous.expansion_ratio, updated.expansion_ratio) || !nearly_equal(previous.blur_scale, updated.blur_scale) || !nearly_equal(previous.falloff_start, updated.falloff_start) || !nearly_equal(previous.falloff_exponent, updated.falloff_exponent) || !nearly_equal(previous.alpha_multiplier, updated.alpha_multiplier);

        if (changed) {
            info_->set_shadow_mask_settings(updated);
            (void)info_->commit_manifest();
            if (ui_) {
                ui_->sync_target_shading_settings();
                ui_->regenerate_shadow_masks();
            }
        }

        return used || changed || shading_changed;
    }

    void render_content(SDL_Renderer* r) const override {
        DockableCollapsible::render_content(r);
    }

    bool shading_enabled() const { return info_ && info_->is_shaded; }

private:
    class PreviewWidget : public Widget {
    public:
        explicit PreviewWidget(Section_Shading* owner) : owner_(owner) {}

        void set_rect(const SDL_Rect& r) override { rect_ = r; }
        const SDL_Rect& rect() const override { return rect_; }
        int height_for_width(int) const override { return 200; }
        bool handle_event(const SDL_Event&) override { return false; }
        void render(SDL_Renderer* renderer) const override {
            if (owner_) {
                owner_->render_preview(renderer, rect_);
            }
        }
        bool wants_full_row() const override { return true; }

    private:
        Section_Shading* owner_ = nullptr;
        SDL_Rect         rect_{0, 0, 0, 0};
};

    void on_generate_all();
    void render_preview(SDL_Renderer* renderer, const SDL_Rect& bounds) const;
    const Animation* find_preview_animation() const;
    int preview_frame_index(const Animation& animation) const;
    SDL_Texture* resolve_preview_sprite() const;
    SDL_Texture* resolve_preview_mask() const;

    std::unique_ptr<DMCheckbox> shading_enabled_checkbox_;
    std::unique_ptr<DMSlider> expansion_ratio_slider_;
    std::unique_ptr<DMSlider> blur_scale_slider_;
    std::unique_ptr<DMSlider> falloff_start_slider_;
    std::unique_ptr<DMSlider> falloff_exponent_slider_;
    std::unique_ptr<DMSlider> alpha_multiplier_slider_;

    std::unique_ptr<DMButton> generate_button_;
    std::vector<std::unique_ptr<Widget>> widgets_{};
    AssetInfoUI* ui_ = nullptr;
};

inline void Section_Shading::on_generate_all() {
    if (!info_) {
        return;
    }
    (void)info_->commit_manifest();
    if (ui_) {
        ui_->regenerate_shadow_masks();
    }
}

inline const Animation* Section_Shading::find_preview_animation() const {
    if (!info_) {
        return nullptr;
    }
    if (info_->animations.empty()) {
        return nullptr;
    }

    const std::string& start = info_->start_animation.empty() ? std::string{"default"} : info_->start_animation;
    auto                it    = info_->animations.find(start);
    if (it == info_->animations.end()) {
        it = info_->animations.find("default");
    }
    if (it == info_->animations.end()) {
        it = info_->animations.begin();
    }
    if (it == info_->animations.end()) {
        return nullptr;
    }
    return &it->second;
}

inline int Section_Shading::preview_frame_index(const Animation& animation) const {
    if (auto* frame = animation.get_first_frame()) {
        if (frame->frame_index >= 0 && frame->frame_index < static_cast<int>(animation.frames.size())) {
            return frame->frame_index;
        }
    }
    if (!animation.frames.empty()) {
        return 0;
    }
    return -1;
}

inline SDL_Texture* Section_Shading::resolve_preview_sprite() const {
    const Animation* animation = find_preview_animation();
    if (!animation) {
        return nullptr;
    }
    const int index = preview_frame_index(*animation);
    if (index < 0 || index >= static_cast<int>(animation->frames.size())) {
        return nullptr;
    }
    if (animation->frames[index] && !animation->frames[index]->variants.empty()) {
        return animation->frames[index]->variants[0].base_texture;
    }
    return nullptr;
}

inline SDL_Texture* Section_Shading::resolve_preview_mask() const {
    if (ui_) {
        if (SDL_Texture* preview = ui_->mask_preview_texture()) {
            return preview;
        }
    }
    const Animation* animation = find_preview_animation();
    if (!animation) {
        return nullptr;
    }
    const int index = preview_frame_index(*animation);
    if (index < 0 || index >= static_cast<int>(animation->frames.size())) {
        return nullptr;
    }
    if (animation->frames[index] && !animation->frames[index]->variants.empty()) {
        return animation->frames[index]->variants[0].shadow_mask_texture;
    }
    return nullptr;
}

inline void Section_Shading::render_preview(SDL_Renderer* renderer, const SDL_Rect& bounds) const {
    if (!renderer) {
        return;
    }

    const SDL_Color& bg     = DMStyles::PanelBG();
    const SDL_Color& border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bounds);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bounds);

    SDL_Texture* sprite = resolve_preview_sprite();
    SDL_Texture* mask   = resolve_preview_mask();
    if (!sprite && !mask) {
        return;
    }

    int tex_w = 0;
    int tex_h = 0;
    if (sprite) {
        SDL_QueryTexture(sprite, nullptr, nullptr, &tex_w, &tex_h);
    }
    if ((tex_w <= 0 || tex_h <= 0) && mask) {
        SDL_QueryTexture(mask, nullptr, nullptr, &tex_w, &tex_h);
    }
    if (tex_w <= 0 || tex_h <= 0) {
        return;
    }

    const int padding = 8;
    const int avail_w = std::max(1, bounds.w - padding * 2);
    const int avail_h = std::max(1, bounds.h - padding * 2);
    const float scale_x = static_cast<float>(avail_w) / static_cast<float>(tex_w);
    const float scale_y = static_cast<float>(avail_h) / static_cast<float>(tex_h);
    const float scale   = std::max(0.0f, std::min(scale_x, scale_y));
    if (!(scale > 0.0f && std::isfinite(scale))) {
        return;
    }

    const int draw_w = std::max(1, static_cast<int>(std::round(static_cast<float>(tex_w) * scale)));
    const int draw_h = std::max(1, static_cast<int>(std::round(static_cast<float>(tex_h) * scale)));
    SDL_Rect   dest{ bounds.x + (bounds.w - draw_w) / 2, bounds.y + (bounds.h - draw_h) / 2, draw_w, draw_h };

    if (sprite) {
        SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
        SDL_RenderCopy(renderer, sprite, nullptr, &dest);
    }

    if (mask) {
        SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
        Uint8 prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
        SDL_GetTextureColorMod(mask, &prev_r, &prev_g, &prev_b);
        SDL_GetTextureAlphaMod(mask, &prev_a);
        SDL_SetTextureColorMod(mask, 40, 40, 40);
        SDL_SetTextureAlphaMod(mask, 220);
        SDL_RenderCopy(renderer, mask, nullptr, &dest);
        SDL_SetTextureColorMod(mask, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(mask, prev_a);
    }
}
