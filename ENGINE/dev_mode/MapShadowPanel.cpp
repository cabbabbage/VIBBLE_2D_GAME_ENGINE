#include "MapShadowPanel.hpp"

#include "MapLightPanel.hpp"
#include "asset/Asset.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dev_ui_settings.hpp"
#include "input.hpp"
#include "shared/formatting.hpp"
#include "render/camera.hpp"
#include "render_pipeline/render_asset/shading/ReactiveShadowSettingsJSON.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <SDL_ttf.h>

namespace {
constexpr std::string_view kReactiveSettingsKey = "dev_ui.lighting.map_panel.reactive";

float clamp_float(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

std::unique_ptr<DMSlider> make_float_slider(const std::string& label,
                                            float min_value,
                                            float max_value,
                                            float current,
                                            int scale) {
    const int min_i = static_cast<int>(std::round(min_value * static_cast<float>(scale)));
    const int max_i = static_cast<int>(std::round(max_value * static_cast<float>(scale)));
    const int cur_i = static_cast<int>(std::round(current * static_cast<float>(scale)));
    auto       slider = std::make_unique<DMSlider>(label, min_i, max_i, cur_i);
    slider->set_defer_commit_until_unfocus(true);
    slider->set_value_formatter([scale](int value, std::array<char, dev_mode::kSliderFormatBufferSize>& buffer) -> std::string_view {
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
    const int scaled = static_cast<int>(std::round(value * static_cast<float>(scale)));
    slider->set_value(scaled);
}

std::string make_setting_key(std::string_view suffix) {
    std::string key{kReactiveSettingsKey};
    key.push_back('.');
    key.append(suffix);
    return key;
}
}  // namespace

class MapShadowPanel::NoteLabel : public Widget {
public:
    explicit NoteLabel(const std::string* text) : text_(text) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        if (!text_ || text_->empty()) {
            return 0;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return style.font_size;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_->c_str(), style.color, std::max(10, w));
        int height = surface ? surface->h : style.font_size;
        if (surface) {
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
        return height + DMSpacing::small_gap();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer || !text_ || text_->empty() || rect_.w <= 0) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(style.open_font(), &TTF_CloseFont);
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font.get(), text_->c_str(), style.color, std::max(10, rect_.w));
        if (!surface) {
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    }

    bool wants_full_row() const override { return true; }

private:
    const std::string* text_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

MapShadowPanel::MapShadowPanel(MapLightPanel* light_panel, Assets* assets, int x, int y)
    : DockableCollapsible("Virtual Light Map Shadows (Quadrant-based)", true, x, y),
      light_panel_(light_panel),
      assets_(assets) {
    set_floating_content_width(360);
    set_visible_height(540);
    build_ui();
    rebuild_rows();
}

MapShadowPanel::~MapShadowPanel() = default;

void MapShadowPanel::set_map_info(nlohmann::json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    if (!reactive_settings_initialized_) {
        last_applied_settings_ = load_reactive_settings_from_dev_settings();
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings();
        reactive_settings_initialized_ = true;
    }
    sync_ui_from_json();
}

void MapShadowPanel::set_reactive_settings(render_pipeline::shading::ReactiveShadowSettings* settings) {
    reactive_settings_shared_ = settings;
    if (settings) {
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(*settings);
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings();
    }
}

void MapShadowPanel::open() {
    DockableCollapsible::open();
    sync_ui_from_json();
}

void MapShadowPanel::close() { DockableCollapsible::close(); }

void MapShadowPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}

bool MapShadowPanel::is_visible() const { return DockableCollapsible::is_visible(); }

void MapShadowPanel::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    if (!is_visible()) {
        return;
    }
    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }
}

bool MapShadowPanel::handle_event(const SDL_Event& e) {
    if (!is_visible()) {
        return false;
    }

    if (DockableCollapsible::handle_event(e)) {
        needs_sync_to_json_ = true;
        return true;
    }

    const bool pointer_event = (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION);
    if (!pointer_event) {
        return false;
    }

    SDL_Point point{0, 0};
    if (e.type == SDL_MOUSEBUTTONDOWN) {
        point = SDL_Point{e.button.x, e.button.y};
    } else {
        point = SDL_Point{e.motion.x, e.motion.y};
    }

    const int quadrant = quadrant_index_from_point(point.x, point.y);
    if (quadrant < 0) {
        return false;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        selected_quadrant_ = quadrant;
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        selected_quadrant_ = quadrant;
    }

    return false;
}

void MapShadowPanel::render(SDL_Renderer* renderer) const {
    if (!is_visible()) {
        return;
    }
    DockableCollapsible::render(renderer);
    render_light_map_preview(renderer);
}

bool MapShadowPanel::is_point_inside(int x, int y) const { return DockableCollapsible::is_point_inside(x, y); }

void MapShadowPanel::render_content(SDL_Renderer* renderer) const {
    DockableCollapsible::render_content(renderer);
}

void MapShadowPanel::layout_custom_content(int, int) const {}

void MapShadowPanel::update_save_status(bool) const {}

void MapShadowPanel::build_ui() {
    horizontal_falloff_ = make_float_slider("Horizontal Falloff", 0.0f, 10.0f,
                                            last_applied_settings_.virtual_light_map.horizontal_falloff, 100);
    vertical_falloff_   = make_float_slider("Vertical Falloff", 0.0f, 10.0f,
                                          last_applied_settings_.virtual_light_map.vertical_falloff, 100);
    max_offset_x_       = make_float_slider("Max Offset X", 0.0f, 500.0f,
                                            last_applied_settings_.virtual_light_map.max_offset_x, 100);
    max_offset_y_       = make_float_slider("Max Offset Y", 0.0f, 500.0f,
                                            last_applied_settings_.virtual_light_map.max_offset_y, 100);
    shadow_scale_       = make_float_slider("Shadow Scale", 0.0f, 10.0f,
                                            last_applied_settings_.virtual_light_map.shadow_scale, 100);
    size_scale_factor_  = make_float_slider("Size Scale Factor", 0.0f, 10.0f,
                                            last_applied_settings_.virtual_light_map.size_scale_factor, 100);
    map_light_factor_   = make_float_slider("Map Light Factor", 0.0f, 1.0f,
                                            last_applied_settings_.virtual_light_map.map_light_factor, 100);

    quadrant_note_text_ =
        "Note: Quadrant preview values are shared—tuning a cell updates all virtual light map cells.";
}

void MapShadowPanel::rebuild_rows() {
    widget_wrappers_.clear();
    widget_wrappers_.reserve(16);

    auto add_widget = [this](std::unique_ptr<Widget> widget) -> Widget* {
        Widget* raw = widget.get();
        widget_wrappers_.push_back(std::move(widget));
        return raw;
    };

    Rows rows;

    if (horizontal_falloff_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(horizontal_falloff_.get())) });
    }
    if (vertical_falloff_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(vertical_falloff_.get())) });
    }
    if (max_offset_x_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(max_offset_x_.get())) });
    }
    if (max_offset_y_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(max_offset_y_.get())) });
    }
    if (shadow_scale_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(shadow_scale_.get())) });
    }
    if (size_scale_factor_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(size_scale_factor_.get())) });
    }
    if (map_light_factor_) {
        rows.push_back({ add_widget(std::make_unique<SliderWidget>(map_light_factor_.get())) });
    }
    if (!quadrant_note_text_.empty()) {
        rows.push_back({ add_widget(std::make_unique<NoteLabel>(&quadrant_note_text_)) });
    }

    set_rows(rows);
}

void MapShadowPanel::sync_ui_from_json() {
    if (!map_info_) {
        return;
    }
    auto it = map_info_->find("reactive_shadows");
    if (it != map_info_->end() && it->is_object()) {
        last_applied_settings_ = render_pipeline::shading::reactive_shadow_settings_from_json(*it, last_applied_settings_);
        last_applied_settings_ = render_pipeline::shading::sanitize_reactive_shadow_settings(last_applied_settings_);
        set_reactive_sliders(last_applied_settings_);
        apply_immediate_settings();
    }
    needs_sync_to_json_ = false;
}

void MapShadowPanel::sync_json_from_ui() {
    if (!map_info_) {
        return;
    }
    render_pipeline::shading::ReactiveShadowSettings settings = current_settings_from_ui();
    write_reactive_settings_to_json(settings);
    last_applied_settings_ = settings;
    apply_immediate_settings();
    needs_sync_to_json_ = false;
}

void MapShadowPanel::apply_immediate_settings() {
    if (reactive_settings_shared_) {
        *reactive_settings_shared_ = last_applied_settings_;
    }
    persist_reactive_settings_to_dev_settings(last_applied_settings_);
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::current_settings_from_ui() const {
    render_pipeline::shading::ReactiveShadowSettings settings = last_applied_settings_;
    settings.virtual_light_map.map_light_factor = slider_value_scaled(map_light_factor_, settings.virtual_light_map.map_light_factor, 100);
    settings.virtual_light_map.horizontal_falloff = slider_value_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, 100);
    settings.virtual_light_map.vertical_falloff = slider_value_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, 100);
    settings.virtual_light_map.max_offset_x = slider_value_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 100);
    settings.virtual_light_map.max_offset_y = slider_value_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 100);
    settings.virtual_light_map.shadow_scale = slider_value_scaled(shadow_scale_, settings.virtual_light_map.shadow_scale, 100);
    settings.virtual_light_map.size_scale_factor = slider_value_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor, 100);
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapShadowPanel::set_reactive_sliders(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    set_slider_scaled(map_light_factor_, settings.virtual_light_map.map_light_factor, 100);
    set_slider_scaled(horizontal_falloff_, settings.virtual_light_map.horizontal_falloff, 100);
    set_slider_scaled(vertical_falloff_, settings.virtual_light_map.vertical_falloff, 100);
    set_slider_scaled(max_offset_x_, settings.virtual_light_map.max_offset_x, 100);
    set_slider_scaled(max_offset_y_, settings.virtual_light_map.max_offset_y, 100);
    set_slider_scaled(shadow_scale_, settings.virtual_light_map.shadow_scale, 100);
    set_slider_scaled(size_scale_factor_, settings.virtual_light_map.size_scale_factor, 100);
}

render_pipeline::shading::ReactiveShadowSettings MapShadowPanel::load_reactive_settings_from_dev_settings() const {
    using devmode::ui_settings::load_number;
    render_pipeline::shading::ReactiveShadowSettings settings = render_pipeline::shading::sanitize_reactive_shadow_settings({});
    settings.virtual_light_map.map_light_factor = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.map_light_factor"), settings.virtual_light_map.map_light_factor));
    settings.virtual_light_map.horizontal_falloff = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.horizontal_falloff"), settings.virtual_light_map.horizontal_falloff));
    settings.virtual_light_map.vertical_falloff = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.vertical_falloff"), settings.virtual_light_map.vertical_falloff));
    settings.virtual_light_map.max_offset_x = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.max_offset_x"), settings.virtual_light_map.max_offset_x));
    settings.virtual_light_map.max_offset_y = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.max_offset_y"), settings.virtual_light_map.max_offset_y));
    settings.virtual_light_map.shadow_scale = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.shadow_scale"), settings.virtual_light_map.shadow_scale));
    settings.virtual_light_map.size_scale_factor = static_cast<float>(
        load_number(make_setting_key("virtual_light_map.size_scale_factor"), settings.virtual_light_map.size_scale_factor));
    return render_pipeline::shading::sanitize_reactive_shadow_settings(settings);
}

void MapShadowPanel::persist_reactive_settings_to_dev_settings(const render_pipeline::shading::ReactiveShadowSettings& settings) const {
    using devmode::ui_settings::save_number;
    save_number(make_setting_key("virtual_light_map.map_light_factor"), settings.virtual_light_map.map_light_factor);
    save_number(make_setting_key("virtual_light_map.horizontal_falloff"), settings.virtual_light_map.horizontal_falloff);
    save_number(make_setting_key("virtual_light_map.vertical_falloff"), settings.virtual_light_map.vertical_falloff);
    save_number(make_setting_key("virtual_light_map.max_offset_x"), settings.virtual_light_map.max_offset_x);
    save_number(make_setting_key("virtual_light_map.max_offset_y"), settings.virtual_light_map.max_offset_y);
    save_number(make_setting_key("virtual_light_map.shadow_scale"), settings.virtual_light_map.shadow_scale);
    save_number(make_setting_key("virtual_light_map.size_scale_factor"), settings.virtual_light_map.size_scale_factor);
}

void MapShadowPanel::write_reactive_settings_to_json(const render_pipeline::shading::ReactiveShadowSettings& settings) {
    if (!map_info_) {
        return;
    }
    nlohmann::json& json = (*map_info_)["reactive_shadows"];
    render_pipeline::shading::assign_reactive_shadow_settings(json, settings);
}

nlohmann::json& MapShadowPanel::ensure_reactive_settings_json() {
    if (!map_info_) {
        static nlohmann::json dummy = nlohmann::json::object();
        return dummy;
    }
    return (*map_info_)["reactive_shadows"];
}

void MapShadowPanel::render_light_map_preview(SDL_Renderer* renderer) const {
    preview_rect_ = SDL_Rect{0, 0, 0, 0};
    preview_grid_rect_ = SDL_Rect{0, 0, 0, 0};
    quadrant_preview_rects_.fill(SDL_Rect{0, 0, 0, 0});

    if (!renderer || !is_visible()) {
        return;
    }

    const VirtualLightMap* map = current_virtual_light_map();
    if (!map || map->screen_width <= 0 || map->screen_height <= 0) {
        return;
    }

    const std::array<const std::unique_ptr<DMSlider>*, 7> sliders = {
        &horizontal_falloff_,
        &vertical_falloff_,
        &max_offset_x_,
        &max_offset_y_,
        &shadow_scale_,
        &size_scale_factor_,
        &map_light_factor_
    };

    SDL_Rect last_slider_rect{rect().x, rect().y, kPreviewWidth, 0};
    bool have_slider = false;
    for (const auto* slider_ptr : sliders) {
        const auto* slider = slider_ptr ? slider_ptr->get() : nullptr;
        if (slider) {
            last_slider_rect = slider->rect();
            have_slider = true;
        }
    }
    if (!have_slider) {
        return;
    }

    const int available_width = std::max(last_slider_rect.w, kPreviewWidth);
    const float aspect = (map->screen_width > 0)
                             ? static_cast<float>(map->screen_height) / static_cast<float>(map->screen_width)
                             : 1.0f;

    int grid_width = std::min(available_width, kPreviewWidth);
    grid_width = std::max(grid_width, 40);
    int grid_height = static_cast<int>(std::lround(static_cast<double>(grid_width) * static_cast<double>(aspect)));
    grid_height = std::max(grid_height, 40);

    const int preview_x = last_slider_rect.x;
    const int preview_y = last_slider_rect.y + last_slider_rect.h + DMSpacing::item_gap();

    int detail_gap = DMSpacing::item_gap();
    int detail_width = available_width - grid_width - detail_gap;
    bool detail_below = (detail_width < 160);

    SDL_Rect detail_rect{0, 0, 0, 0};
    if (detail_below) {
        detail_width = available_width;
        detail_gap = DMSpacing::small_gap();
        detail_rect = SDL_Rect{preview_x, preview_y + grid_height + detail_gap, detail_width, 0};
    } else {
        detail_rect = SDL_Rect{preview_x + grid_width + detail_gap, preview_y, detail_width, grid_height};
    }

    preview_grid_rect_ = SDL_Rect{preview_x, preview_y, grid_width, grid_height};
    preview_rect_ = SDL_Rect{preview_x, preview_y, available_width, grid_height};

    screen_width_px_ = map->screen_width;
    screen_height_px_ = map->screen_height;

    SDL_Color bg{30, 30, 30, 255};
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &preview_grid_rect_);

    auto cell_left = [&](int gx) {
        return preview_grid_rect_.x +
               static_cast<int>(std::lround(static_cast<double>(gx) * preview_grid_rect_.w /
                                            static_cast<double>(VirtualLightMap::kGridWidth)));
    };
    auto cell_top = [&](int gy) {
        return preview_grid_rect_.y +
               static_cast<int>(std::lround(static_cast<double>(gy) * preview_grid_rect_.h /
                                            static_cast<double>(VirtualLightMap::kGridHeight)));
    };

    for (int gy = 0; gy < VirtualLightMap::kGridHeight; ++gy) {
        for (int gx = 0; gx < VirtualLightMap::kGridWidth; ++gx) {
            const int left = cell_left(gx);
            const int right = cell_left(gx + 1);
            const int top = cell_top(gy);
            const int bottom = cell_top(gy + 1);

            SDL_Rect cell_rect{left,
                               top,
                               std::max(1, right - left),
                               std::max(1, bottom - top)};

            const std::size_t index = VirtualLightMap::index_of(gx, gy);
            quadrant_preview_rects_[index] = cell_rect;

            const VirtualLightMap::ShadowCell& cell = map->cell(gx, gy);
            const Uint8 brightness = static_cast<Uint8>(
                std::clamp(cell.brightness, 0.0f, 1.0f) * 255.0f);
            SDL_SetRenderDrawColor(renderer, brightness, brightness, brightness, 255);
            SDL_RenderFillRect(renderer, &cell_rect);
        }
    }

    SDL_Color grid_line{70, 70, 70, 255};
    SDL_SetRenderDrawColor(renderer, grid_line.r, grid_line.g, grid_line.b, grid_line.a);
    SDL_RenderDrawRect(renderer, &preview_grid_rect_);
    for (int gx = 1; gx < VirtualLightMap::kGridWidth; ++gx) {
        const int x = cell_left(gx);
        SDL_RenderDrawLine(renderer, x, preview_grid_rect_.y, x, preview_grid_rect_.y + preview_grid_rect_.h);
    }
    for (int gy = 1; gy < VirtualLightMap::kGridHeight; ++gy) {
        const int y = cell_top(gy);
        SDL_RenderDrawLine(renderer, preview_grid_rect_.x, y,
                           preview_grid_rect_.x + preview_grid_rect_.w, y);
    }

    const int detail_quadrant = (selected_quadrant_ >= 0 &&
                                 selected_quadrant_ < VirtualLightMap::kQuadrantCount)
                                    ? selected_quadrant_
                                    : -1;
    if (detail_quadrant >= 0) {
        const SDL_Rect& selected_rect = quadrant_preview_rects_[detail_quadrant];
        if (selected_rect.w > 0 && selected_rect.h > 0) {
            const SDL_Color accent = DMStyles::HighlightColor();
            SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, accent.a);
            SDL_RenderDrawRect(renderer, &selected_rect);
        }
    }

    std::vector<std::string> detail_lines;
    if (detail_quadrant >= 0) {
        const int gx = detail_quadrant % VirtualLightMap::kGridWidth;
        const int gy = detail_quadrant / VirtualLightMap::kGridWidth;
        const VirtualLightMap::ShadowCell& cell = map->cell_for_index(detail_quadrant);

        auto format_value = [](float value, int precision = 3) {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(precision) << value;
            return stream.str();
        };

        detail_lines.push_back("Cell [" + std::to_string(gx) + ", " + std::to_string(gy) + "] #" +
                               std::to_string(detail_quadrant));
        detail_lines.push_back("Brightness: " + format_value(cell.brightness));
        detail_lines.push_back("Opacity: " + format_value(cell.opacity));
        detail_lines.push_back("Offset X: " + format_value(cell.offset_x, 2));
        detail_lines.push_back("Offset Y: " + format_value(cell.offset_y, 2));
        detail_lines.push_back("Scale: " + format_value(cell.scale));
        detail_lines.push_back("");
        detail_lines.push_back("Assets:");

        auto assets = assets_in_quadrant(detail_quadrant);
        if (assets.empty()) {
            detail_lines.push_back("  (none)");
        } else {
            for (const auto& name : assets) {
                detail_lines.push_back("  - " + name);
            }
        }
    } else {
        detail_lines.push_back("No quadrant selected.");
    }

    std::string detail_text;
    for (std::size_t i = 0; i < detail_lines.size(); ++i) {
        if (i > 0) {
            detail_text.push_back('\n');
        }
        detail_text.append(detail_lines[i]);
    }

    int detail_text_height = 0;
    if (!detail_text.empty() && detail_width > 0) {
        const DMLabelStyle& style = DMStyles::Label();
        std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(style.open_font(), &TTF_CloseFont);
        if (font) {
            SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font.get(), detail_text.c_str(), style.color,
                                                                   std::max(10, detail_width));
            if (surface) {
                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                if (texture) {
                    SDL_Rect dst{detail_rect.x, detail_rect.y, surface->w, surface->h};
                    if (!detail_below) {
                        dst.w = std::min(dst.w, detail_rect.w);
                        dst.h = std::min(dst.h, detail_rect.h);
                    }
                    SDL_Color panel_bg{20, 20, 20, 180};
                    SDL_Rect bg_rect = detail_rect;
                    bg_rect.w = detail_width;
                    bg_rect.h = surface->h;
                    SDL_SetRenderDrawColor(renderer, panel_bg.r, panel_bg.g, panel_bg.b, panel_bg.a);
                    SDL_RenderFillRect(renderer, &bg_rect);

                    SDL_RenderCopy(renderer, texture, nullptr, &dst);
                    SDL_DestroyTexture(texture);
                }
                detail_text_height = surface->h;
                SDL_FreeSurface(surface);
            }
        }
    }

    if (detail_below) {
        detail_rect.h = detail_text_height;
        preview_rect_.h = grid_height + detail_gap + detail_text_height;
    } else {
        detail_rect.h = grid_height;
        preview_rect_.h = std::max(grid_height, detail_text_height);
    }
}

const VirtualLightMap* MapShadowPanel::current_virtual_light_map() const {
    return assets_ ? assets_->virtual_light_map() : nullptr;
}

std::optional<SDL_Point> MapShadowPanel::player_screen_position() const {
    if (!assets_) {
        return std::nullopt;
    }
    camera& view = assets_->getView();
    SDL_Point center = view.get_screen_center();
    return center;
}

std::vector<std::string> MapShadowPanel::assets_in_quadrant(int quadrant) const {
    std::vector<std::string> names;
    if (!assets_ || quadrant < 0 || quadrant >= VirtualLightMap::kQuadrantCount) {
        return names;
    }

    const VirtualLightMap* map = current_virtual_light_map();
    if (!map) {
        return names;
    }

    const auto& active_assets = assets_->getActive();
    if (active_assets.empty()) {
        return names;
    }

    camera& cam = assets_->getView();
    const float cam_scale = cam.get_scale();
    const float inv_scale = (std::isfinite(cam_scale) && cam_scale > 1e-6f) ? (1.0f / cam_scale) : 1.0f;

    float player_screen_height = 1.0f;
    Asset* player_asset = assets_->player;
    if (player_asset) {
        int ph = player_asset->cached_h;
        if (ph <= 0) {
            if (SDL_Texture* texture = player_asset->get_final_texture()) {
                SDL_QueryTexture(texture, nullptr, nullptr, nullptr, &ph);
            }
        }
        const float base_scale = (player_asset->info && std::isfinite(player_asset->info->scale_factor) &&
                                  player_asset->info->scale_factor >= 0.0f)
                                     ? player_asset->info->scale_factor
                                     : 1.0f;
        if (ph > 0) {
            player_screen_height = static_cast<float>(ph) * base_scale * inv_scale;
        }
    }
    if (!(player_screen_height > 0.0f)) {
        player_screen_height = 1.0f;
    }

    auto screen_rect_for = [&](Asset* asset) -> SDL_Rect {
        SDL_Rect zero{0, 0, 0, 0};
        if (!asset) {
            return zero;
        }
        int fw = asset->cached_w;
        int fh = asset->cached_h;
        if (fw <= 0 || fh <= 0) {
            if (SDL_Texture* texture = asset->get_final_texture()) {
                SDL_QueryTexture(texture, nullptr, nullptr, &fw, &fh);
                asset->cached_w = fw;
                asset->cached_h = fh;
            }
        }
        if (fw <= 0 || fh <= 0) {
            return zero;
        }

        const float base_scale = (asset->info && std::isfinite(asset->info->scale_factor) &&
                                  asset->info->scale_factor >= 0.0f)
                                     ? asset->info->scale_factor
                                     : 1.0f;
        float base_sw = static_cast<float>(fw) * base_scale * inv_scale;
        float base_sh = static_cast<float>(fh) * base_scale * inv_scale;

        camera::RenderEffects effects =
            cam.compute_render_effects(SDL_Point{asset->pos.x, asset->pos.y}, base_sh, player_screen_height);

        float scaled_sw = base_sw * effects.distance_scale;
        float scaled_sh = base_sh * effects.distance_scale;
        float final_h = scaled_sh * effects.vertical_scale;

        const int sw_px = std::max(1, static_cast<int>(std::lround(scaled_sw)));
        const int sh_px = std::max(1, static_cast<int>(std::lround(final_h)));

        return SDL_Rect{effects.screen_position.x - sw_px / 2,
                        effects.screen_position.y - sh_px,
                        sw_px,
                        sh_px};
    };

    names.reserve(active_assets.size());
    for (Asset* asset : active_assets) {
        if (!asset || !asset->info) {
            continue;
        }
        SDL_Rect screen_rect = screen_rect_for(asset);
        if (screen_rect.w <= 0 || screen_rect.h <= 0) {
            continue;
        }
        const int cell_index = map->quadrant_for_rect(screen_rect);
        if (cell_index == quadrant) {
            names.push_back(asset->info->name);
        }
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

int MapShadowPanel::quadrant_index_from_point(int x, int y) const {
    if (preview_grid_rect_.w <= 0 || preview_grid_rect_.h <= 0) {
        return -1;
    }
    SDL_Point point{x, y};
    if (!SDL_PointInRect(&point, &preview_grid_rect_)) {
        return -1;
    }
    const int relative_x = x - preview_grid_rect_.x;
    const int relative_y = y - preview_grid_rect_.y;
    if (relative_x < 0 || relative_y < 0) {
        return -1;
    }
    const float norm_x = static_cast<float>(relative_x) / static_cast<float>(preview_grid_rect_.w);
    const float norm_y = static_cast<float>(relative_y) / static_cast<float>(preview_grid_rect_.h);
    const int gx = clamp_int(static_cast<int>(std::floor(norm_x * static_cast<float>(VirtualLightMap::kGridWidth))),
                             0,
                             VirtualLightMap::kGridWidth - 1);
    const int gy = clamp_int(static_cast<int>(std::floor(norm_y * static_cast<float>(VirtualLightMap::kGridHeight))),
                             0,
                             VirtualLightMap::kGridHeight - 1);
    return gy * VirtualLightMap::kGridWidth + gx;
}

int MapShadowPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}
