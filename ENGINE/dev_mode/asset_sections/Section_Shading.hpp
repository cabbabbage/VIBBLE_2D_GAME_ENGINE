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
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/shared/formatting.hpp"
#include "utils/cache_manager.hpp"
#include "utils/generate_faded_mask.hpp"

class AssetInfoUI;

class Section_Shading : public DockableCollapsible {
public:
    Section_Shading() : DockableCollapsible("Shading", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Shading() override { destroy_preview_texture(); }

    void build() override {
        destroy_preview_texture();
        preview_texture_w_ = 0;
        preview_texture_h_ = 0;
        preview_dirty_ = true;
        preview_rect_ = SDL_Rect{0, 0, 0, 0};
        preview_container_rect_ = SDL_Rect{0, 0, 0, 0};

        c_is_shaded_.reset();
        s_light_map_quadrants_.reset();
        s_extend_.reset();
        s_blur_.reset();
        s_falloff_start_.reset();
        s_falloff_rate_.reset();
        s_alpha_.reset();
        generate_all_btn_.reset();

        last_info_ = info_.get();
        if (!info_) {
            return;
        }

        working_settings_ = info_->shadow_mask_settings;
        assign_slider_values_from_settings();

        light_map_quadrant_value_ = std::clamp(info_->virtual_light_map_quadrants, quadrant_min_, quadrant_max_);
        if (info_->virtual_light_map_quadrants != light_map_quadrant_value_) {
            info_->set_virtual_light_map_quadrants(light_map_quadrant_value_);
            (void)info_->commit_manifest();
        }

        c_is_shaded_ = std::make_unique<DMCheckbox>("Has Shading", info_->is_shaded);

        s_light_map_quadrants_ = std::make_unique<DMSlider>(
            "Virtual Light Map Quadrants",
            quadrant_min_,
            quadrant_max_,
            light_map_quadrant_value_);
        s_light_map_quadrants_->set_value_formatter([](int v, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            return dev_mode::FormatSliderValue(static_cast<double>(v), 0, buffer);
        });
        s_light_map_quadrants_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                int parsed = std::stoi(text);
                return std::clamp(parsed, quadrant_min_, quadrant_max_);
            } catch (...) {
                return std::nullopt;
            }
        });
        s_light_map_quadrants_->set_defer_commit_until_unfocus(true);

        s_extend_ = std::make_unique<DMSlider>("Extend Amount", extend_min_, extend_max_, extend_value_);
        configure_ratio_slider(*s_extend_, 100);

        s_blur_ = std::make_unique<DMSlider>("Blur Scale", blur_min_, blur_max_, blur_value_);
        configure_ratio_slider(*s_blur_, 100);

        s_falloff_start_ = std::make_unique<DMSlider>("Falloff Start (%)", falloff_start_min_, falloff_start_max_, falloff_start_value_);
        s_falloff_start_->set_value_formatter([](int v, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            return dev_mode::FormatSliderValue(static_cast<double>(v), 0, buffer);
        });
        s_falloff_start_->set_value_parser([](const std::string& text) -> std::optional<int> {
            try {
                double parsed = std::stod(text);
                if (!std::isfinite(parsed)) {
                    return std::nullopt;
                }
                parsed = std::clamp(parsed, static_cast<double>(falloff_start_min_), static_cast<double>(falloff_start_max_));
                return static_cast<int>(std::lround(parsed));
            } catch (...) {
                return std::nullopt;
            }
        });
        s_falloff_start_->set_defer_commit_until_unfocus(true);

        s_falloff_rate_ = std::make_unique<DMSlider>("Falloff Rate", falloff_rate_min_, falloff_rate_max_, falloff_rate_value_);
        configure_ratio_slider(*s_falloff_rate_, 100);

        s_alpha_ = std::make_unique<DMSlider>("Alpha Multiplier", alpha_min_, alpha_max_, alpha_value_);
        configure_ratio_slider(*s_alpha_, 100);

        generate_all_btn_ = std::make_unique<DMButton>("Generate All Masks", &DMStyles::CreateButton(), 220, DMButton::height());
    }

    void layout_custom_content(int /*screen_w*/, int /*screen_h*/) const override {
        int padding = DMSpacing::panel_padding();
        int header_gap = DMSpacing::header_gap();
        int base_x = rect_.x + padding;
        int base_y = rect_.y + padding + DMButton::height() + header_gap;
        int available_w = rect_.w - 2 * padding;

        int control_x = base_x;
        int control_y = base_y;
        int control_w = available_w;

        const int gap = DMSpacing::item_gap();
        const int min_control_w = 180;

        bool preview_visible = c_is_shaded_ && c_is_shaded_->value();
        bool preview_side_by_side = false;
        int preview_height = 0;

        if (!preview_visible) {
            preview_rect_ = SDL_Rect{0, 0, 0, 0};
            preview_container_rect_ = SDL_Rect{0, 0, 0, 0};
        }

        if (preview_visible) {
            int preview_width = preview_texture_w_ > 0 ? preview_texture_w_ : std::min(available_w, 220);
            preview_height = preview_texture_h_ > 0 ? preview_texture_h_ : 140;
            if (preview_width > available_w) {
                double scale = static_cast<double>(available_w) / static_cast<double>(preview_width);
                preview_width = available_w;
                preview_height = std::max(1, static_cast<int>(std::lround(static_cast<double>(preview_height) * scale)));
            }

            int max_preview_w_for_control = std::max(0, available_w - min_control_w - gap);
            if (max_preview_w_for_control <= 0) {
                preview_visible = false;
                preview_rect_ = SDL_Rect{0, 0, 0, 0};
                preview_container_rect_ = SDL_Rect{0, 0, 0, 0};
            } else {
                if (preview_width > max_preview_w_for_control) {
                    double scale = static_cast<double>(max_preview_w_for_control) / static_cast<double>(preview_width);
                    preview_width = max_preview_w_for_control;
                    preview_height = std::max(1, static_cast<int>(std::lround(static_cast<double>(preview_height) * scale)));
                }

                control_w = available_w - preview_width - gap;
                if (control_w < 100) {
                    control_w = std::max(100, available_w / 2);
                    preview_width = std::max(0, available_w - control_w - gap);
                }

                if (preview_width <= 0) {
                    preview_visible = false;
                    preview_rect_ = SDL_Rect{0, 0, 0, 0};
                    preview_container_rect_ = SDL_Rect{0, 0, 0, 0};
                } else {
                    preview_side_by_side = true;
                    int preview_x = control_x + control_w + gap;
                    preview_rect_ = SDL_Rect{preview_x, base_y, preview_width, preview_height};
                    preview_container_rect_ = SDL_Rect{preview_x, base_y, preview_width, preview_height};
                }
            }
        }

        auto place = [&](auto& widget, int h) {
            if (!widget) return;
            widget->set_rect(SDL_Rect{control_x, control_y - scroll_, control_w, h});
            control_y += h + gap;
        };

        if (s_light_map_quadrants_) {
            place(s_light_map_quadrants_, DMSlider::height());
        }

        if (c_is_shaded_) {
            place(c_is_shaded_, DMCheckbox::height());
        }

        if (c_is_shaded_ && c_is_shaded_->value()) {
            place(s_extend_, DMSlider::height());
            place(s_blur_, DMSlider::height());
            place(s_falloff_start_, DMSlider::height());
            place(s_falloff_rate_, DMSlider::height());
            place(s_alpha_, DMSlider::height());
            if (!preview_side_by_side) {
                preview_rect_ = SDL_Rect{0, 0, 0, 0};
                preview_container_rect_ = SDL_Rect{0, 0, 0, 0};
            }

            if (generate_all_btn_) {
                int btn_w = std::min(generate_all_btn_->preferred_width(), control_w);
                generate_all_btn_->set_rect(SDL_Rect{control_x + (control_w - btn_w) / 2, control_y - scroll_, btn_w, DMButton::height()});
                control_y += DMButton::height() + gap;
            }
        } else {
            preview_rect_ = SDL_Rect{0, 0, 0, 0};
            preview_container_rect_ = SDL_Rect{0, 0, 0, 0};
        }

        int scrollable_height = std::max(0, control_y - base_y);
        int preview_height_total = preview_side_by_side ? preview_height : 0;
        content_height_ = std::max(scrollable_height, preview_height_total);
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        if (!info_ || !expanded_) {
            return used;
        }

        bool shading_toggled = false;
        bool settings_changed = false;

        if (s_light_map_quadrants_) {
            int previous = light_map_quadrant_value_;
            bool slider_used = s_light_map_quadrants_->handle_event(e);
            int committed = std::clamp(s_light_map_quadrants_->value(), quadrant_min_, quadrant_max_);
            if (committed != previous) {
                light_map_quadrant_value_ = committed;
                info_->set_virtual_light_map_quadrants(committed);
                (void)info_->commit_manifest();
                used = true;
            } else if (slider_used) {
                used = true;
            }
        }

        if (c_is_shaded_ && c_is_shaded_->handle_event(e)) {
            used = true;
            shading_toggled = true;
        }

        if (c_is_shaded_ && c_is_shaded_->value()) {
            auto handle_slider = [&](std::unique_ptr<DMSlider>& slider, int& stored_value, int min_v, int max_v) {
                if (!slider) return;
                int previous = stored_value;
                bool slider_used = slider->handle_event(e);
                int committed = std::clamp(slider->value(), min_v, max_v);
                if (committed != previous) {
                    stored_value = committed;
                    settings_changed = true;
                    used = true;
                } else if (slider_used) {
                    used = true;
                }
            };

            handle_slider(s_extend_, extend_value_, extend_min_, extend_max_);
            handle_slider(s_blur_, blur_value_, blur_min_, blur_max_);
            handle_slider(s_falloff_start_, falloff_start_value_, falloff_start_min_, falloff_start_max_);
            handle_slider(s_falloff_rate_, falloff_rate_value_, falloff_rate_min_, falloff_rate_max_);
            handle_slider(s_alpha_, alpha_value_, alpha_min_, alpha_max_);
        }

        if (generate_all_btn_ && generate_all_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                used = true;
                if (ui_ && info_ && info_->is_shaded) {
                    ui_->regenerate_shadow_masks();
                    preview_dirty_ = true;
                }
            }
        }

        if (shading_toggled) {
            const bool new_state = c_is_shaded_ ? c_is_shaded_->value() : false;
            if (info_->is_shaded != new_state) {
                info_->set_shading_enabled(new_state);
                (void)info_->commit_manifest();
                preview_dirty_ = true;
                if (!new_state) {
                    destroy_preview_texture();
                } else if (!is_expanded()) {
                    set_expanded(true);
                }
                if (ui_) {
                    ui_->notify_light_sources_modified(false);
                }
            }
        }

        if (settings_changed && info_) {
            update_settings_from_slider_values();
            info_->set_shadow_mask_settings(working_settings_);
            (void)info_->commit_manifest();
            preview_dirty_ = true;
        }

        return used || shading_toggled || settings_changed;
    }

    void render_content(SDL_Renderer* r) const override {
        if (s_light_map_quadrants_) s_light_map_quadrants_->render(r);
        if (c_is_shaded_) c_is_shaded_->render(r);
        if (c_is_shaded_ && c_is_shaded_->value()) {
            if (s_extend_) s_extend_->render(r);
            if (s_blur_) s_blur_->render(r);
            if (s_falloff_start_) s_falloff_start_->render(r);
            if (s_falloff_rate_) s_falloff_rate_->render(r);
            if (s_alpha_) s_alpha_->render(r);

            if (preview_rect_.w > 0 && preview_rect_.h > 0) {
                ensure_preview(r);
                SDL_Color border = DMStyles::Border();
                dm_draw::DrawRoundedOutline(r, preview_container_rect_, DMStyles::CornerRadius(), 1, border);
                if (preview_texture_) {
                    SDL_RenderCopy(r, preview_texture_, nullptr, &preview_rect_);
                }
                if (light_map_quadrant_value_ > 1) {
                    SDL_Color grid = DMStyles::Border();
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(r, grid.r, grid.g, grid.b, 120);
                    const int divisions = std::max(1, light_map_quadrant_value_);
                    const float cell_w = static_cast<float>(preview_rect_.w) / static_cast<float>(divisions);
                    const float cell_h = static_cast<float>(preview_rect_.h) / static_cast<float>(divisions);
                    for (int i = 1; i < divisions; ++i) {
                        int x = preview_rect_.x + static_cast<int>(std::lround(cell_w * static_cast<float>(i)));
                        SDL_RenderDrawLine(r, x, preview_rect_.y, x, preview_rect_.y + preview_rect_.h);
                        int y = preview_rect_.y + static_cast<int>(std::lround(cell_h * static_cast<float>(i)));
                        SDL_RenderDrawLine(r, preview_rect_.x, y, preview_rect_.x + preview_rect_.w, y);
                    }
                }
            }

            if (generate_all_btn_) {
                generate_all_btn_->render(r);
            }
        }
    }

    bool shading_enabled() const { return c_is_shaded_ && c_is_shaded_->value(); }

private:
    static void configure_ratio_slider(DMSlider& slider, int scale) {
        slider.set_value_formatter([scale](int v, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) {
            return dev_mode::FormatSliderValue(static_cast<double>(v) / static_cast<double>(scale), 2, buffer);
        });
        slider.set_value_parser([scale](const std::string& text) -> std::optional<int> {
            try {
                double parsed = std::stod(text);
                if (!std::isfinite(parsed)) {
                    return std::nullopt;
                }
                double scaled = parsed * static_cast<double>(scale);
                return static_cast<int>(std::lround(scaled));
            } catch (...) {
                return std::nullopt;
            }
        });
        slider.set_defer_commit_until_unfocus(true);
    }

    void assign_slider_values_from_settings() {
        ShadowMaskSettings sanitized = SanitizeShadowMaskSettings(working_settings_);
        working_settings_ = sanitized;
        extend_value_ = std::clamp(static_cast<int>(std::lround(sanitized.expansion_ratio * 100.0f)), extend_min_, extend_max_);
        blur_value_ = std::clamp(static_cast<int>(std::lround(sanitized.blur_scale * 100.0f)), blur_min_, blur_max_);
        falloff_start_value_ = std::clamp(static_cast<int>(std::lround(sanitized.falloff_start * 100.0f)), falloff_start_min_, falloff_start_max_);
        falloff_rate_value_ = std::clamp(static_cast<int>(std::lround(sanitized.falloff_exponent * 100.0f)), falloff_rate_min_, falloff_rate_max_);
        alpha_value_ = std::clamp(static_cast<int>(std::lround(sanitized.alpha_multiplier * 100.0f)), alpha_min_, alpha_max_);
    }

    void update_settings_from_slider_values() {
        working_settings_.expansion_ratio = static_cast<float>(extend_value_) / 100.0f;
        working_settings_.blur_scale = static_cast<float>(blur_value_) / 100.0f;
        working_settings_.falloff_start = static_cast<float>(falloff_start_value_) / 100.0f;
        working_settings_.falloff_exponent = static_cast<float>(falloff_rate_value_) / 100.0f;
        working_settings_.alpha_multiplier = static_cast<float>(alpha_value_) / 100.0f;
        working_settings_ = SanitizeShadowMaskSettings(working_settings_);
        assign_slider_values_from_settings();
        if (s_extend_) s_extend_->set_value(extend_value_);
        if (s_blur_) s_blur_->set_value(blur_value_);
        if (s_falloff_start_) s_falloff_start_->set_value(falloff_start_value_);
        if (s_falloff_rate_) s_falloff_rate_->set_value(falloff_rate_value_);
        if (s_alpha_) s_alpha_->set_value(alpha_value_);
    }

    void ensure_preview(SDL_Renderer* renderer) const {
        if (!renderer || !info_ || !c_is_shaded_ || !c_is_shaded_->value()) {
            return;
        }
        if (!preview_dirty_) {
            return;
        }

        preview_dirty_ = false;
        destroy_preview_texture();

        SDL_Surface* source = capture_preview_surface(renderer);
        if (!source) {
            return;
        }

        SDL_Surface* mask = GenerateFadedMask::GenerateSingleMask(source, working_settings_);
        SDL_FreeSurface(source);
        if (!mask) {
            return;
        }

        SDL_Surface* scaled = scale_surface(mask, 0.5f);
        SDL_FreeSurface(mask);
        if (!scaled) {
            return;
        }

        SDL_Texture* preview = CacheManager::surface_to_texture(renderer, scaled);
        preview_texture_w_ = scaled->w;
        preview_texture_h_ = scaled->h;
        SDL_FreeSurface(scaled);

        preview_texture_ = preview;
    }

    SDL_Surface* capture_preview_surface(SDL_Renderer* renderer) const {
        if (!info_ || info_->animations.empty()) {
            return nullptr;
        }

        const std::string& preferred = !info_->start_animation.empty() ? info_->start_animation : info_->animations.begin()->first;
        auto it = info_->animations.find(preferred);
        if (it == info_->animations.end()) {
            it = info_->animations.begin();
        }
        if (it == info_->animations.end()) {
            return nullptr;
        }

        const Animation& animation = it->second;
        SDL_Texture* frame = animation.frame_variant(0, 0);
        if (!frame) {
            return nullptr;
        }

        return texture_to_surface(renderer, frame);
    }

    SDL_Surface* texture_to_surface(SDL_Renderer* renderer, SDL_Texture* texture) const {
        if (!renderer || !texture) {
            return nullptr;
        }

        Uint32 format = SDL_PIXELFORMAT_RGBA32;
        int access = 0;
        int width = 0;
        int height = 0;
        if (SDL_QueryTexture(texture, &format, &access, &width, &height) != 0) {
            return nullptr;
        }

        SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!target) {
            return nullptr;
        }

        SDL_Texture* previous = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, target) != 0) {
            SDL_DestroyTexture(target);
            return nullptr;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);

        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        if (!surface) {
            SDL_SetRenderTarget(renderer, previous);
            SDL_DestroyTexture(target);
            return nullptr;
        }

        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch) != 0) {
            SDL_FreeSurface(surface);
            SDL_SetRenderTarget(renderer, previous);
            SDL_DestroyTexture(target);
            return nullptr;
        }

        SDL_SetRenderTarget(renderer, previous);
        SDL_DestroyTexture(target);
        return surface;
    }

    SDL_Surface* scale_surface(SDL_Surface* surface, float factor) const {
        if (!surface) {
            return nullptr;
        }
        if (factor <= 0.0f) {
            factor = 1.0f;
        }
        int target_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(surface->w) * factor)));
        int target_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(surface->h) * factor)));
        SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, target_w, target_h, 32, surface->format->format);
        if (!scaled) {
            return nullptr;
        }
        SDL_Rect dst{0, 0, target_w, target_h};
        if (SDL_BlitScaled(surface, nullptr, scaled, &dst) != 0) {
            SDL_FreeSurface(scaled);
            return nullptr;
        }
        return scaled;
    }

    void destroy_preview_texture() const {
        if (preview_texture_) {
            SDL_DestroyTexture(preview_texture_);
            preview_texture_ = nullptr;
        }
    }

    static constexpr int extend_min_ = 0;
    static constexpr int extend_max_ = 400;
    static constexpr int blur_min_ = 0;
    static constexpr int blur_max_ = 800;
    static constexpr int falloff_start_min_ = 0;
    static constexpr int falloff_start_max_ = 95;
    static constexpr int falloff_rate_min_ = 10;
    static constexpr int falloff_rate_max_ = 400;
    static constexpr int alpha_min_ = 0;
    static constexpr int alpha_max_ = 400;
    static constexpr int quadrant_min_ = 1;
    static constexpr int quadrant_max_ = 100;

    ShadowMaskSettings working_settings_{};
    int extend_value_ = 80;
    int blur_value_ = 100;
    int falloff_start_value_ = 0;
    int falloff_rate_value_ = 105;
    int alpha_value_ = 100;
    int light_map_quadrant_value_ = 50;

    std::unique_ptr<DMCheckbox> c_is_shaded_;
    std::unique_ptr<DMSlider> s_light_map_quadrants_;
    std::unique_ptr<DMSlider> s_extend_;
    std::unique_ptr<DMSlider> s_blur_;
    std::unique_ptr<DMSlider> s_falloff_start_;
    std::unique_ptr<DMSlider> s_falloff_rate_;
    std::unique_ptr<DMSlider> s_alpha_;
    std::unique_ptr<DMButton> generate_all_btn_;

    AssetInfoUI* ui_ = nullptr;
    const AssetInfo* last_info_ = nullptr;

    mutable SDL_Texture* preview_texture_ = nullptr;
    mutable int preview_texture_w_ = 0;
    mutable int preview_texture_h_ = 0;
    mutable bool preview_dirty_ = true;
    mutable SDL_Rect preview_rect_{0, 0, 0, 0};
    mutable SDL_Rect preview_container_rect_{0, 0, 0, 0};

protected:
    std::string_view lock_settings_namespace() const override { return "asset_info"; }
    std::string_view lock_settings_id() const override { return "shading"; }
};

