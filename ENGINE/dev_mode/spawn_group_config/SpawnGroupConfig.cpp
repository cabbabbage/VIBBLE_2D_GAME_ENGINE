#include "SpawnGroupConfig.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "spawn_group_utils.hpp"
#include "dm_styles.hpp"
#include "dm_icons.hpp"
#include "widgets.hpp"
#include "widgets/CandidateEditorPieGraphWidget.hpp"
#include "utils/input.hpp"
#include "utils/map_grid_settings.hpp"
#include "utils/grid.hpp"
#include "dev_mode/core/manifest_store.hpp"

class SpawnGroupLabelWidget : public Widget {
public:
    SpawnGroupLabelWidget() = default;
    explicit SpawnGroupLabelWidget(std::string text, SDL_Color color = DMStyles::Label().color, bool subtle = false)
        : text_(std::move(text)), color_(color), subtle_(subtle) {}

    void set_text(const std::string& text) { text_ = text; }
    void set_color(SDL_Color color) { color_ = color; }
    void set_subtle(bool subtle) { subtle_ = subtle; }
    void set_font_size(int size) {
        if (size > 0) font_override_ = size;
        else font_override_.reset();
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override {
        int base = DMCheckbox::height();
        if (text_.empty()) return base;
        int font_size = font_override_.value_or(DMStyles::Label().font_size);
        TTF_Font* font = TTF_OpenFont(DMStyles::Label().font_path.c_str(), font_size);
        if (!font) return base;
        int text_w = 0;
        int text_h = 0;
        if (TTF_SizeUTF8(font, text_.c_str(), &text_w, &text_h) != 0) {
            TTF_CloseFont(font);
            return base;
        }
        TTF_CloseFont(font);
        return std::max(base, text_h);
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        DMLabelStyle style = DMStyles::Label();
        if (auto size = font_override_) {
            style.font_size = *size;
        }
        SDL_Color color = subtle_ ? SDL_Color{static_cast<Uint8>(style.color.r / 2),
                                              static_cast<Uint8>(style.color.g / 2), static_cast<Uint8>(style.color.b / 2), style.color.a} : style.color;
        if (color_.a != 0) color = color_;
        TTF_Font* font = TTF_OpenFont(style.font_path.c_str(), style.font_size);
        if (!font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text_.c_str(), color);
        if (!surface) {
            TTF_CloseFont(font);
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);
    }

private:
    std::string text_{};
    SDL_Color color_{0, 0, 0, 0};
    bool subtle_ = false;
    SDL_Rect rect_{0, 0, 0, 0};
    std::optional<int> font_override_{};
};

namespace {
constexpr const char* kDefaultMethod = "Random";
constexpr int kDefaultMinNumber = 1;
constexpr int kDefaultMaxNumber = 1;
constexpr int kExactDefaultQuantity = 1;
constexpr int kPerimeterRadiusSliderMin = 0;
constexpr int kPerimeterRadiusSliderMax = 20000;
constexpr int kEdgeInsetSliderMin = 0;
constexpr int kEdgeInsetSliderMax = 200;
constexpr int kEdgeInsetDefault = 100;

std::function<std::vector<std::string>()> empty_provider() {
    return []() { return std::vector<std::string>{}; };
}

SDL_Color dim_color(SDL_Color color, float factor) {
    factor = std::clamp(factor, 0.0f, 1.0f);
    auto apply = [factor](Uint8 channel) {
        return static_cast<Uint8>(std::clamp(static_cast<int>(std::lround(channel * factor)), 0, 255));
};
    return SDL_Color{apply(color.r), apply(color.g), apply(color.b), color.a};
}

const DMButtonStyle& disabled_priority_button_style() {
    static const DMButtonStyle style = [] {
        const DMButtonStyle& base = DMStyles::ListButton();
        DMButtonStyle disabled{
            {base.label.font_path, base.label.font_size, dim_color(base.label.color, 0.55f)},
            dim_color(base.bg, 0.45f), dim_color(base.hover_bg, 0.45f), dim_color(base.press_bg, 0.45f), dim_color(base.border, 0.55f), dim_color(base.text, 0.55f)};
        return disabled;
    }();
    return style;
}

class PriorityButtonWidget : public Widget {
public:
    PriorityButtonWidget(DMButton* button, std::function<void()> on_click)
        : button_(button), on_click_(std::move(on_click)) {}

    void set_rect(const SDL_Rect& r) override {
        if (button_) button_->set_rect(r);
    }

    const SDL_Rect& rect() const override {
        static SDL_Rect empty{0, 0, 0, 0};
        return button_ ? button_->rect() : empty;
    }

    int height_for_width(int) const override { return DMButton::height(); }

    bool handle_event(const SDL_Event& e) override {
        if (!button_ || !enabled_) return false;
        bool used = button_->handle_event(e);
        if (used && on_click_ && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            on_click_();
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (button_) button_->render(renderer);
    }

    void set_enabled(bool enabled) {
        if (enabled_ == enabled) return;
        enabled_ = enabled;
        if (!button_) return;
        if (enabled_) {
            button_->set_style(&DMStyles::ListButton());
        } else {
            button_->set_style(&disabled_priority_button_style());
        }
    }

    bool enabled() const { return enabled_; }

private:
    DMButton* button_ = nullptr;
    std::function<void()> on_click_{};
    bool enabled_ = true;
};

int parse_int_or(const std::string& text, int fallback) {
    if (text.empty()) return fallback;
    try {
        size_t idx = 0;
        int value = std::stoi(text, &idx);
        if (idx != text.size()) {
            return fallback;
        }
        return value;
    } catch (...) {
        return fallback;
    }
}

double parse_double_or(const std::string& text, double fallback) {
    if (text.empty()) return fallback;
    try {
        size_t idx = 0;
        double value = std::stod(text, &idx);
        if (idx != text.size()) {
            return fallback;
        }
        return value;
    } catch (...) {
        return fallback;
    }
}

std::string safe_string(const nlohmann::json& obj, const char* key, const std::string& fallback = {}) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_string()) return it->get<std::string>();
    return fallback;
}

int safe_int(const nlohmann::json& obj, const char* key, int fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_number_integer()) return it->get<int>();
    if (it->is_number_float()) return static_cast<int>(std::lround(it->get<double>()));
    if (it->is_string()) return parse_int_or(it->get<std::string>(), fallback);
    return fallback;
}

double safe_double(const nlohmann::json& obj, const char* key, double fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_number_float()) return it->get<double>();
    if (it->is_number_integer()) return static_cast<double>(it->get<int>());
    if (it->is_string()) return parse_double_or(it->get<std::string>(), fallback);
    return fallback;
}

bool safe_bool(const nlohmann::json& obj, const char* key, bool fallback) {
    if (!obj.is_object()) return fallback;
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (it->is_boolean()) return it->get<bool>();
    if (it->is_number_integer()) return it->get<int>() != 0;
    if (it->is_string()) {
        std::string text = it->get<std::string>();
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "1" || text == "yes") return true;
        if (text == "false" || text == "0" || text == "no") return false;
    }
    return fallback;
}

std::string default_display_name_for(const nlohmann::json& entry) {
    if (entry.is_object()) {
        const auto it = entry.find("display_name");
        if (it != entry.end() && it->is_string()) {
            std::string value = it->get<std::string>();
            if (!value.empty()) return value;
        }
    }
    return "New Spawn";
}

class SpawnGroupCallbackTextBoxWidget : public Widget {
public:
    SpawnGroupCallbackTextBoxWidget(std::unique_ptr<DMTextBox> box,
                          std::function<void(const std::string&)> on_change,
                          bool full_row,
                          bool editable)
        : box_(std::move(box)), on_change_(std::move(on_change)), full_row_(full_row), editable_(editable) {
        if (box_) {
            box_->set_on_height_changed([this]() { this->request_layout(); });
        }
    }

    ~SpawnGroupCallbackTextBoxWidget() override {
        if (box_) {
            box_->set_on_height_changed(nullptr);
        }
    }

    void set_rect(const SDL_Rect& r) override {
        if (box_) box_->set_rect(r);
        rect_cache_ = r;
    }

    const SDL_Rect& rect() const override {
        if (box_) return box_->rect();
        return rect_cache_;
    }

    int height_for_width(int w) const override {
        return box_ ? box_->preferred_height(w) : DMTextBox::height();
    }

    bool handle_event(const SDL_Event& e) override {
        if (!box_ || !editable_) return false;
        bool was_editing = box_->is_editing();
        std::string before = box_->value();
        bool used = box_->handle_event(e);
        bool now_editing = box_->is_editing();
        const std::string& after = box_->value();
        if (used && on_change_ && after != before) {
            on_change_(after);
        }
        if (on_change_ && was_editing && !now_editing) {
            if (!used || after == before) {
                on_change_(after);
            }
            used = true;
        }
        return used || was_editing != now_editing;
    }

    void render(SDL_Renderer* renderer) const override {
        if (box_) box_->render(renderer);
        if (!editable_) {
            SDL_Rect r = rect();
            SDL_Color overlay{40, 40, 40, 140};
            SDL_SetRenderDrawColor(renderer, overlay.r, overlay.g, overlay.b, overlay.a);
            SDL_RenderFillRect(renderer, &r);
        }
    }

    bool wants_full_row() const override { return full_row_; }

    void set_value(const std::string& value) {
        if (box_) box_->set_value(value);
    }

    DMTextBox* box() { return box_.get(); }

    void set_editable(bool editable) { editable_ = editable; }

private:
    std::unique_ptr<DMTextBox> box_{};
    std::function<void(const std::string&)> on_change_{};
    bool full_row_ = false;
    bool editable_ = true;
    SDL_Rect rect_cache_{0, 0, 0, 0};
};

class SpawnGroupCallbackSliderWidget : public Widget {
public:
    SpawnGroupCallbackSliderWidget(std::unique_ptr<DMSlider> slider,
                                   std::function<void(int)> on_change,
                                   bool editable)
        : slider_(std::move(slider)), on_change_(std::move(on_change)), editable_(editable) {
        if (slider_) {
            last_value_ = slider_->value();
        }
    }

    void set_rect(const SDL_Rect& r) override {
        if (slider_) slider_->set_rect(r);
        rect_cache_ = slider_ ? slider_->rect() : r;
    }

    const SDL_Rect& rect() const override {
        if (slider_) return slider_->rect();
        return rect_cache_;
    }

    int height_for_width(int w) const override {
        return slider_ ? slider_->preferred_height(w) : DMSlider::height();
    }

    bool handle_event(const SDL_Event& e) override {
        if (!slider_) return false;
        int before = slider_->value();
        bool used = editable_ ? slider_->handle_event(e) : false;
        if (!editable_) {
            return used;
        }
        int after = slider_->value();
        if (after != before) {
            last_value_ = after;
            if (on_change_) on_change_(after);
            return true;
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (slider_) slider_->render(renderer);
        if (!editable_) {
            SDL_Rect r = rect();
            SDL_Color overlay{40, 40, 40, 140};
            SDL_SetRenderDrawColor(renderer, overlay.r, overlay.g, overlay.b, overlay.a);
            SDL_RenderFillRect(renderer, &r);
        }
    }

    bool wants_full_row() const override { return true; }

    void set_value(int value) {
        if (!slider_) return;
        slider_->set_value(value);
        last_value_ = slider_->value();
    }

    int value() const { return slider_ ? slider_->value() : last_value_; }

    void set_editable(bool editable) { editable_ = editable; }

private:
    std::unique_ptr<DMSlider> slider_{};
    std::function<void(int)> on_change_{};
    bool editable_ = true;
    SDL_Rect rect_cache_{0, 0, 0, 0};
    int last_value_ = 0;
};

class CallbackCheckboxWidget : public Widget {
public:
    CallbackCheckboxWidget(std::unique_ptr<DMCheckbox> checkbox,
                           std::function<void(bool)> on_change,
                           bool editable)
        : checkbox_(std::move(checkbox)), on_change_(std::move(on_change)), editable_(editable) {}

    void set_rect(const SDL_Rect& r) override {
        rect_cache_ = r;
        if (!checkbox_) return;
        SDL_Rect applied = r;
        const int checkbox_height = DMCheckbox::height();
        if (applied.h > checkbox_height) {
            applied.y += (applied.h - checkbox_height) / 2;
            applied.h = checkbox_height;
        } else {
            applied.h = std::max(applied.h, checkbox_height);
        }
        checkbox_->set_rect(applied);
        SDL_Rect final_rect = checkbox_->rect();
        int preferred = checkbox_->preferred_width();
        int minimum = final_rect.h > 0 ? final_rect.h : checkbox_height;
        int desired = std::max(minimum, preferred);
        final_rect.w = std::min(desired, r.w);
        checkbox_->set_rect(final_rect);
        rect_cache_ = final_rect;
    }

    const SDL_Rect& rect() const override {
        if (checkbox_) return checkbox_->rect();
        return rect_cache_;
    }

    int height_for_width(int) const override { return DMCheckbox::height(); }

    bool handle_event(const SDL_Event& e) override {
        if (!checkbox_ || !editable_) return false;
        bool before = checkbox_->value();
        bool used = checkbox_->handle_event(e);
        if (used) {
            bool after = checkbox_->value();
            if (after != before && on_change_) on_change_(after);
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (checkbox_) checkbox_->render(renderer);
        if (!editable_) {
            SDL_Rect r = rect();
            SDL_Color overlay{40, 40, 40, 140};
            SDL_SetRenderDrawColor(renderer, overlay.r, overlay.g, overlay.b, overlay.a);
            SDL_RenderFillRect(renderer, &r);
        }
    }

    void set_value(bool value) {
        if (checkbox_) checkbox_->set_value(value);
    }

    void set_editable(bool editable) { editable_ = editable; }

private:
    std::unique_ptr<DMCheckbox> checkbox_{};
    std::function<void(bool)> on_change_{};
    bool editable_ = true;
    SDL_Rect rect_cache_{0, 0, 0, 0};
};

class CallbackDropdownWidget : public Widget {
public:
    CallbackDropdownWidget(std::string label,
                           std::vector<std::string> options,
                           std::function<void(int)> on_change,
                           bool editable)
        : label_(std::move(label)),
          options_(std::move(options)),
          on_change_(std::move(on_change)),
          editable_(editable) {
        rebuild_dropdown(0);
    }

    void set_rect(const SDL_Rect& r) override {
        rect_cache_ = r;
        if (dropdown_) dropdown_->set_rect(r);
    }

    const SDL_Rect& rect() const override {
        if (dropdown_) return dropdown_->rect();
        return rect_cache_;
    }

    int height_for_width(int w) const override {
        return dropdown_ ? dropdown_->preferred_height(w) : DMDropdown::height();
    }

    bool handle_event(const SDL_Event& e) override {
        if (!dropdown_ || !editable_) return false;
        int before = dropdown_->selected();
        bool used = dropdown_->handle_event(e);
        if (used) {
            int after = dropdown_->selected();
            if (after != before && on_change_) on_change_(after);
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (dropdown_) dropdown_->render(renderer);
        if (!editable_) {
            SDL_Rect r = rect();
            SDL_Color overlay{40, 40, 40, 140};
            SDL_SetRenderDrawColor(renderer, overlay.r, overlay.g, overlay.b, overlay.a);
            SDL_RenderFillRect(renderer, &r);
        }
    }

    void set_options(std::vector<std::string> options, int selected) {
        options_ = std::move(options);
        if (selected < 0 || selected >= static_cast<int>(options_.size())) selected = 0;
        rebuild_dropdown(selected);
    }

    const std::vector<std::string>& options() const { return options_; }

    void set_selected(int idx) {
        if (!dropdown_) return;
        if (idx < 0 || idx >= static_cast<int>(options_.size())) idx = 0;
        dropdown_->set_selected(idx);
    }

    int selected() const { return dropdown_ ? dropdown_->selected() : 0; }

    void set_editable(bool editable) { editable_ = editable; }

    std::string option_value(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(options_.size())) return {};
        return options_[idx];
    }

private:
    void rebuild_dropdown(int selected) {
        dropdown_ = std::make_unique<DMDropdown>(label_, options_, selected);
        if (rect_cache_.w > 0 && rect_cache_.h > 0) {
            dropdown_->set_rect(rect_cache_);
        }
    }

    std::string label_{};
    std::vector<std::string> options_{};
    std::unique_ptr<DMDropdown> dropdown_{};
    std::function<void(int)> on_change_{};
    bool editable_ = true;
    SDL_Rect rect_cache_{0, 0, 0, 0};
};

std::vector<std::string> build_method_options(const std::string& method) {
    std::vector<std::string> options{"Random", "Perimeter", "Edge", "Exact"};
    if (!method.empty() && std::find(options.begin(), options.end(), method) == options.end()) {
        options.push_back(method);
    }
    return options;
}

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

}

struct SpawnGroupConfig::Entry {
    friend class SpawnGroupConfig;

    struct CandidateWidgets {
        std::unique_ptr<DMTextBox> name_box;
        std::unique_ptr<SpawnGroupCallbackTextBoxWidget> name_widget;
        std::unique_ptr<DMTextBox> chance_box;
        std::unique_ptr<SpawnGroupCallbackTextBoxWidget> chance_widget;
        std::unique_ptr<DMButton> remove_button;
        std::unique_ptr<ButtonWidget> remove_widget;
};

    explicit Entry(SpawnGroupConfig& owner)
        : owner_(&owner),
          area_provider_(empty_provider()),
          candidate_graph_(std::make_unique<CandidateEditorPieGraphWidget>()) {
        editable_ = (owner_->bound_array_ != nullptr) || (owner_->bound_entry_ != nullptr);
        current_resolution_ = owner_ ? owner_->default_resolution_ : vibble::grid::clamp_resolution(MapGridSettings::defaults().resolution);
        method_options_ = build_method_options(kDefaultMethod);

        if (candidate_graph_) {
            candidate_graph_->set_on_request_layout([this]() {
                if (owner_) owner_->mark_layout_dirty();
            });
            if (owner_) {
                candidate_graph_->set_screen_dimensions(owner_->screen_w_, owner_->screen_h_);
            }
        }

        toggle_button_ = std::make_unique<DMButton>("â–¶", &DMStyles::ListButton(), 28, DMButton::height());
        toggle_widget_ = std::make_unique<ButtonWidget>(toggle_button_.get(), [this]() {
            expanded_state_ = !expanded_state_;
            if (expanded_state_) owner_->expand_group(spawn_id());
            else owner_->collapse_group(spawn_id());
            update_toggle_label();
            owner_->mark_layout_dirty();
        });

        ownership_label_widget_ = std::make_unique<SpawnGroupLabelWidget>();
        ownership_label_widget_->set_font_size(DMStyles::Label().font_size + 2);
        ownership_label_widget_->set_subtle(true);

        delete_button_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 200, DMButton::height());
        delete_widget_ = std::make_unique<ButtonWidget>(delete_button_.get(), [this]() {
            if (!owner_ || !editable_) return;
            std::string id = spawn_id();
            owner_->enqueue_notification([owner = owner_, id]() {
                if (!owner) return;
                if (owner->callbacks_.on_delete) owner->callbacks_.on_delete(id);
            });
        });

        priority_up_button_ = std::make_unique<DMButton>(u8"↑", &DMStyles::ListButton(), DMButton::height(), DMButton::height());
        priority_up_widget_ = std::make_unique<PriorityButtonWidget>(priority_up_button_.get(), [this]() {
            if (!owner_) return;
            owner_->nudge_priority(*this, -1);
        });

        priority_down_button_ = std::make_unique<DMButton>(u8"↓", &DMStyles::ListButton(), DMButton::height(), DMButton::height());
        priority_down_widget_ = std::make_unique<PriorityButtonWidget>(priority_down_button_.get(), [this]() {
            if (!owner_) return;
            owner_->nudge_priority(*this, 1);
        });

        auto name_box = std::make_unique<DMTextBox>("", "");
        name_widget_ = std::make_unique<SpawnGroupCallbackTextBoxWidget>(std::move(name_box),
            [this](const std::string& value) {
                if (!editable_) return;
                if (auto* entry = mutable_entry()) {
                    (*entry)["display_name"] = value;
                    notify_change(false, false, false);
                }
            },
            true,
            editable_);

        method_widget_ = std::make_unique<CallbackDropdownWidget>(
            "Spawn Method", method_options_, [this](int index) { on_method_changed(index); }, editable_);

        auto lock_checkbox = std::make_unique<DMCheckbox>("Locked", false);
        lock_widget_ = std::make_unique<CallbackCheckboxWidget>(
            std::move(lock_checkbox),
            [this](bool value) { on_locked_changed(value); },
            editable_);

        auto enforce_checkbox = std::make_unique<DMCheckbox>("Enforce Spacing", false);
        enforce_widget_ = std::make_unique<CallbackCheckboxWidget>(std::move(enforce_checkbox),
            [this](bool value) {
                if (!editable_) return;
                if (auto* entry = mutable_entry()) {
                    (*entry)["enforce_spacing"] = value;
                    notify_change(false, false, false);
                }
            },
            editable_);

        auto geometry_checkbox = std::make_unique<DMCheckbox>("Resolve geometry to room size", false);
        resolve_geometry_widget_ = std::make_unique<CallbackCheckboxWidget>(
            std::move(geometry_checkbox),
            [this](bool value) { on_resolve_geometry_changed(value); },
            editable_);

        auto quantity_checkbox = std::make_unique<DMCheckbox>("Resolve quantity to room size", false);
        resolve_quantity_widget_ = std::make_unique<CallbackCheckboxWidget>(
            std::move(quantity_checkbox),
            [this](bool value) { on_resolve_quantity_changed(value); },
            editable_);

        candidates_toggle_btn_ = std::make_unique<DMButton>("Candidates", &DMStyles::ListButton(), 140, DMButton::height());
        candidates_toggle_widget_ = std::make_unique<ButtonWidget>(candidates_toggle_btn_.get(), [this]() {
            candidates_expanded_ = !candidates_expanded_;
            update_candidates_toggle_label();
            if (owner_) owner_->mark_layout_dirty();
        });
        update_candidates_toggle_label();

        advanced_toggle_btn_ = std::make_unique<DMButton>("Advanced Options", &DMStyles::ListButton(), 180, DMButton::height());
        advanced_toggle_widget_ = std::make_unique<ButtonWidget>(advanced_toggle_btn_.get(), [this]() {
            advanced_expanded_ = !advanced_expanded_;
            update_advanced_toggle_label();
            if (owner_) owner_->mark_layout_dirty();
        });
        update_advanced_toggle_label();

        auto min_box = std::make_unique<DMTextBox>("Min Number", "");
        min_widget_ = std::make_unique<SpawnGroupCallbackTextBoxWidget>(std::move(min_box),
            [this](const std::string& text) { on_min_changed(text); }, false, editable_);

        auto max_box = std::make_unique<DMTextBox>("Max Number", "");
        max_widget_ = std::make_unique<SpawnGroupCallbackTextBoxWidget>(std::move(max_box),
            [this](const std::string& text) { on_max_changed(text); }, false, editable_);

        auto exact_box = std::make_unique<DMTextBox>("Quantity", "");
        exact_widget_ = std::make_unique<SpawnGroupCallbackTextBoxWidget>(std::move(exact_box),
            [this](const std::string& text) { on_exact_changed(text); }, false, editable_);

        auto resolution_slider = std::make_unique<DMSlider>("Grid Resolution (2^r px)", 0, vibble::grid::kMaxResolution, current_resolution_);
        resolution_slider->set_defer_commit_until_unfocus(false);
        resolution_widget_ = std::make_unique<SpawnGroupCallbackSliderWidget>(
            std::move(resolution_slider),
            [this](int value) { on_resolution_changed(value); },
            editable_);

        auto radius_slider = std::make_unique<DMSlider>("Perimeter Radius (px)", kPerimeterRadiusSliderMin, kPerimeterRadiusSliderMax, kPerimeterRadiusSliderMin);
        perimeter_radius_widget_ = std::make_unique<SpawnGroupCallbackSliderWidget>(
            std::move(radius_slider),
            [this](int value) { on_perimeter_radius_changed(value); },
            editable_);

        auto inset_slider = std::make_unique<DMSlider>("Edge Inset (%)", kEdgeInsetSliderMin, kEdgeInsetSliderMax, kEdgeInsetDefault);
        edge_inset_widget_ = std::make_unique<SpawnGroupCallbackSliderWidget>(
            std::move(inset_slider),
            [this](int value) { on_edge_inset_changed(value); },
            editable_);

        empty_candidates_label_ = std::make_unique<SpawnGroupLabelWidget>("No candidates", DMStyles::Label().color, true);

        auto explicit_flip_checkbox = std::make_unique<DMCheckbox>("Explicit Flip", false);
        explicit_flip_widget_ = std::make_unique<CallbackCheckboxWidget>(
            std::move(explicit_flip_checkbox),
            [this](bool value) { on_explicit_flip_changed(value); },
            editable_);

        auto force_flipped_checkbox = std::make_unique<DMCheckbox>("Always Flipped", false);
        force_flipped_widget_ = std::make_unique<CallbackCheckboxWidget>(
            std::move(force_flipped_checkbox),
            [this](bool value) { on_force_flipped_changed(value); },
            editable_);

        rebuild_candidate_widgets();
        sync_from_json();
        update_toggle_label();
    }

    ~Entry() {
        if (owner_ && owner_->current_entry_ == this) {
            owner_->current_entry_ = nullptr;
        }
    }

    void bind(nlohmann::json* entry, std::optional<size_t> index = std::nullopt) {
        array_index_ = index;
        entry_ = entry;
        editable_ = owner_ && (owner_->bound_array_ != nullptr || owner_->bound_entry_ != nullptr);
        if (!entry_) {
            shadow_entry_ = nlohmann::json::object();
        }
        update_candidate_graph();
    }

    void set_shadow_entry(const nlohmann::json& entry) {
        shadow_entry_ = entry;
        update_candidate_graph();
    }

    nlohmann::json* mutable_entry() {
        if (array_index_ && owner_ && owner_->bound_array_) {
            auto* arr = owner_->bound_array_;
            if (*array_index_ < arr->size()) {
                entry_ = &(*arr)[*array_index_];
            } else {
                entry_ = nullptr;
            }
        }
        return entry_;
    }

    const nlohmann::json* mutable_entry() const {
        return const_cast<Entry*>(this)->mutable_entry();
    }

    const nlohmann::json& entry_view() const {
        if (const auto* current = mutable_entry()) {
            return *current;
        }
        return shadow_entry_;
    }

    std::string spawn_id() const {
        const auto& entry = entry_view();
        if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
            return entry["spawn_id"].get<std::string>();
        }
        return std::string{};
    }

    void set_ownership_label(const std::string& label, SDL_Color color) {
        ownership_label_text_ = label;
        ownership_color_ = color;
    }

    void clear_ownership_label() {
        ownership_label_text_.clear();
        ownership_color_.reset();
    }

    void set_area_names_provider(std::function<std::vector<std::string>()> provider) {
        area_provider_ = provider ? std::move(provider) : empty_provider();
    }

    const std::function<std::vector<std::string>()>& area_names_provider() const {
        return area_provider_;
    }

    void lock_method_to(std::string method) { method_lock_ = std::move(method); }

    const std::optional<std::string>& method_lock() const { return method_lock_; }

    void clear_method_lock() { method_lock_.reset(); }

    void set_quantity_hidden(bool hidden) { quantity_hidden_ = hidden; }

    bool quantity_hidden() const { return quantity_hidden_; }

    CandidateEditorPieGraphWidget* candidate_editor_widget() { return candidate_graph_.get(); }

    const CandidateEditorPieGraphWidget* candidate_editor_widget() const { return candidate_graph_.get(); }

    void sync_from_json() {
        const auto& entry = entry_view();
        const std::string id = spawn_id();
        std::string display = safe_string(entry, "display_name", {});
        name_widget_->set_value(display);

        bool base_editable = (owner_ && (owner_->bound_array_ != nullptr || owner_->bound_entry_ != nullptr));
        locked_ = safe_bool(entry, "locked", false);
        if (lock_widget_) {
            lock_widget_->set_value(locked_);
            lock_widget_->set_editable(base_editable);
        }
        editable_ = base_editable && !locked_;

        std::string method = safe_string(entry, "position", kDefaultMethod);
        if (std::find(method_options_.begin(), method_options_.end(), method) == method_options_.end()) {
            method_options_.push_back(method);
        }
        int method_index = 0;
        for (size_t i = 0; i < method_options_.size(); ++i) {
            if (method_options_[i] == method) {
                method_index = static_cast<int>(i);
                break;
            }
        }
        method_widget_->set_options(method_options_, method_index);
        const bool was_exact_method = (current_method_ == "Exact");
        const bool previous_use_exact_quantity = use_exact_quantity_;
        const bool previous_show_resolve_quantity = show_resolve_quantity_widget_;
        const bool previous_hide_quantity_controls = quantity_hidden() || was_exact_method;
        current_method_ = method;
        use_exact_quantity_ = (method == "Exact" || method == "Exact Position");
        bool previous_show_radius = show_perimeter_radius_widget_;
        bool previous_show_edge = show_edge_inset_widget_;
        const bool is_exact_method = (method == "Exact");
        show_perimeter_radius_widget_ = (method == "Perimeter");
        show_edge_inset_widget_ = (method == "Edge");
        show_resolve_geometry_widget_ = (method == "Exact" || method == "Perimeter");
        show_resolve_quantity_widget_ = !quantity_hidden() && !is_exact_method;
        const bool hide_quantity_controls = quantity_hidden() || is_exact_method;
        if (owner_ && (previous_use_exact_quantity != use_exact_quantity_ ||
                       previous_show_resolve_quantity != show_resolve_quantity_widget_ ||
                       previous_hide_quantity_controls != hide_quantity_controls)) {
            owner_->mark_layout_dirty();
        }

        auto is_zone_asset_name = [store = owner_ ? owner_->manifest_store_ : nullptr](const std::string& name) -> bool {
            if (!store || name.empty()) return false;
            try {
                auto view = store->get_asset(name);
                if (!view || !view.data || !view.data->is_object()) return false;
                std::string t = view.data->value("asset_type", std::string{});
                std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
                return t == std::string{"zone_asset"};
            } catch (...) { return false; }
};
        bool has_zone_asset = false;
        try {
            if (entry.contains("candidates") && entry["candidates"].is_array()) {
                for (const auto& c : entry["candidates"]) {
                    if (!c.is_object()) continue;
                    const std::string name = c.value("name", std::string{});
                    if (!name.empty() && name.front() != '#') {
                        if (is_zone_asset_name(name)) { has_zone_asset = true; break; }
                    }
                }
            }
        } catch (...) {}

        static const char* kAdjustLabel = "Adjust to Room";
        static const char* kResolveLabel = "Resolve geometry to room size";
        if (resolve_geometry_widget_) {

            bool want_adjust = has_zone_asset;
            if (use_adjust_label_ != want_adjust) {
                auto geometry_checkbox = std::make_unique<DMCheckbox>(want_adjust ? kAdjustLabel : kResolveLabel, false);
                resolve_geometry_widget_ = std::make_unique<CallbackCheckboxWidget>(
                    std::move(geometry_checkbox),
                    [this](bool value) { on_resolve_geometry_changed(value); },
                    editable_);
                if (owner_) owner_->mark_layout_dirty();
                use_adjust_label_ = want_adjust;
            }
        }

        auto has_flippable_candidate = [this](const nlohmann::json& e) -> bool {
            if (!owner_ || !owner_->manifest_store_) return false;
            if (!e.is_object()) return false;
            auto it = e.find("candidates");
            if (it == e.end() || !it->is_array()) return false;
            try {
                for (const auto& c : *it) {
                    if (!c.is_object()) continue;
                    std::string nm = c.value("name", std::string{});
                    if (nm.empty() || nm == "null") continue;
                    if (!nm.empty() && nm.front() == '#') continue;
                    auto view = owner_->manifest_store_->get_asset(nm);
                    if (!view || !view.data || !view.data->is_object()) continue;
                    if (view.data->value("can_invert", false)) return true;
                }
            } catch (...) {}
            return false;
};
        const bool prev_show_explicit = show_explicit_flip_widget_;
        const bool prev_show_force    = show_force_flipped_widget_;
        show_explicit_flip_widget_ = has_flippable_candidate(entry);
        const bool explicit_on = safe_bool(entry, "explicit_flip", false) && show_explicit_flip_widget_;
        show_force_flipped_widget_ = explicit_on;
        if (explicit_flip_widget_) explicit_flip_widget_->set_value(explicit_on);
        if (force_flipped_widget_) force_flipped_widget_->set_value(safe_bool(entry, "force_flipped", false));
        if (owner_ && (prev_show_explicit != show_explicit_flip_widget_ || prev_show_force != show_force_flipped_widget_)) {
            owner_->mark_layout_dirty();
        }

        const bool geometry_flag = safe_bool(entry, "resolve_geometry_to_room_size", show_resolve_geometry_widget_);
        const bool quantity_flag = safe_bool(entry, "resolve_quantity_to_room_size", false);

        if (resolve_geometry_widget_) {
            resolve_geometry_widget_->set_value(geometry_flag);
            resolve_geometry_widget_->set_editable(editable_ && show_resolve_geometry_widget_);
        }
        if (resolve_quantity_widget_) {
            resolve_quantity_widget_->set_value(quantity_flag);
            resolve_quantity_widget_->set_editable(editable_ && show_resolve_quantity_widget_);
        }

        int resolution_value = vibble::grid::clamp_resolution(safe_int(entry, "resolution", owner_ ? owner_->default_resolution_ : current_resolution_));
        current_resolution_ = resolution_value;
        if (resolution_widget_) {
            resolution_widget_->set_value(resolution_value);
            resolution_widget_->set_editable(editable_);
        }

        int radius_value = safe_int(entry, "radius", safe_int(entry, "perimeter_radius", kPerimeterRadiusSliderMin));
        if (radius_value < kPerimeterRadiusSliderMin) {
            radius_value = kPerimeterRadiusSliderMin;
        }
        if (perimeter_radius_widget_) {
            perimeter_radius_widget_->set_value(radius_value);
            perimeter_radius_widget_->set_editable(editable_ && show_perimeter_radius_widget_);
        }
        int edge_inset_value = std::clamp(safe_int(entry, "edge_inset_percent", kEdgeInsetDefault), kEdgeInsetSliderMin, kEdgeInsetSliderMax);
        if (edge_inset_widget_) {
            edge_inset_widget_->set_value(edge_inset_value);
            edge_inset_widget_->set_editable(editable_ && show_edge_inset_widget_);
        }
        if ((previous_show_radius != show_perimeter_radius_widget_ ||
             previous_show_edge != show_edge_inset_widget_) && owner_) {
            owner_->mark_layout_dirty();
        }

        int min_number = safe_int(entry, "min_number", kDefaultMinNumber);
        int max_number = safe_int(entry, "max_number", std::max(min_number, kDefaultMaxNumber));
        if (max_number < min_number) max_number = min_number;
        int quantity = safe_int(entry, "quantity", use_exact_quantity_ ? min_number : kExactDefaultQuantity);

        min_widget_->set_value(std::to_string(min_number));
        max_widget_->set_value(std::to_string(max_number));
        exact_widget_->set_value(std::to_string(quantity));

        bool enforce_spacing = entry.is_object() ? entry.value("enforce_spacing", false) : false;
        enforce_widget_->set_value(enforce_spacing);

        update_candidate_graph();
        rebuild_candidate_widgets();
        update_ownership_label();
    }

    void refresh_configuration() {
        update_ownership_label();
        auto lock = method_lock();
        if (lock) {
            if (std::find(method_options_.begin(), method_options_.end(), *lock) == method_options_.end()) {
                method_options_.push_back(*lock);
            }
            int idx = 0;
            for (size_t i = 0; i < method_options_.size(); ++i) {
                if (method_options_[i] == *lock) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            method_widget_->set_options(method_options_, idx);
            method_widget_->set_editable(false);
        } else {
            int idx = 0;
            for (size_t i = 0; i < method_options_.size(); ++i) {
                if (method_options_[i] == current_method_) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            method_widget_->set_options(method_options_, idx);
            method_widget_->set_editable(editable_);
        }
        enforce_widget_->set_editable(editable_);
        name_widget_->set_editable(editable_);
        const bool hide_quantity_controls = quantity_hidden() || current_method_ == "Exact";
        const bool allow_quantity_inputs = editable_ && !hide_quantity_controls;
        if (min_widget_) {
            min_widget_->set_editable(allow_quantity_inputs && !use_exact_quantity_);
        }
        if (max_widget_) {
            max_widget_->set_editable(allow_quantity_inputs && !use_exact_quantity_);
        }
        if (exact_widget_) {
            exact_widget_->set_editable(allow_quantity_inputs && use_exact_quantity_);
        }
        if (resolution_widget_) {
            resolution_widget_->set_editable(editable_);
        }
        if (perimeter_radius_widget_) {
            perimeter_radius_widget_->set_editable(editable_ && show_perimeter_radius_widget_);
        }
        if (edge_inset_widget_) {
            edge_inset_widget_->set_editable(editable_ && show_edge_inset_widget_);
        }
        if (resolve_geometry_widget_) {
            resolve_geometry_widget_->set_editable(editable_ && show_resolve_geometry_widget_);
        }
        if (resolve_quantity_widget_) {
            resolve_quantity_widget_->set_editable(editable_ && show_resolve_quantity_widget_);
        }

        if (lock_widget_) {
            bool base_editable = (owner_ && (owner_->bound_array_ != nullptr || owner_->bound_entry_ != nullptr));
            lock_widget_->set_editable(base_editable);
        }

        update_priority_button_states();
        if (delete_button_) {

            delete_button_->set_style(editable_ ? &DMStyles::DeleteButton() : &disabled_priority_button_style());
        }
    }

    void set_expanded(bool expanded) {
        expanded_state_ = expanded;
        update_toggle_label();
    }

    bool expanded() const { return expanded_state_; }

    void append_layout_rows(DockableCollapsible::Rows& rows) {
        if (!owner_ || owner_->should_render_entry_body(*this)) {
            DockableCollapsible::Row header_row;
            header_row.push_back(name_widget_.get());
            if (ownership_label_widget_) {
                header_row.push_back(ownership_label_widget_.get());
            }
            rows.push_back(header_row);

            if (priority_count_ > 1) {
                DockableCollapsible::Row priority_row;
                if (priority_up_widget_) priority_row.push_back(priority_up_widget_.get());
                if (priority_down_widget_) priority_row.push_back(priority_down_widget_.get());
                if (!priority_row.empty()) rows.push_back(priority_row);
            }

            if (lock_widget_) {
                rows.push_back({lock_widget_.get()});
            }

            if (candidates_toggle_widget_ || advanced_toggle_widget_) {
                DockableCollapsible::Row toggles_row;
                if (candidates_toggle_widget_) toggles_row.push_back(candidates_toggle_widget_.get());
                if (advanced_toggle_widget_)   toggles_row.push_back(advanced_toggle_widget_.get());
                if (!toggles_row.empty()) rows.push_back(toggles_row);
            }
            rows.push_back({method_widget_.get()});

            const bool hide_quantity_controls = quantity_hidden() || current_method_ == "Exact";
            const bool show_quantity_range = !hide_quantity_controls && !use_exact_quantity_;
            const bool show_exact_quantity = !hide_quantity_controls && use_exact_quantity_ && exact_widget_;
            if (show_quantity_range) {
                DockableCollapsible::Row qty_row;
                qty_row.push_back(min_widget_.get());
                qty_row.push_back(max_widget_.get());
                rows.push_back(qty_row);
            } else if (show_exact_quantity) {
                DockableCollapsible::Row qty_row;
                qty_row.push_back(exact_widget_.get());
                rows.push_back(qty_row);
            }

            if (show_perimeter_radius_widget_ && perimeter_radius_widget_) {
                rows.push_back({perimeter_radius_widget_.get()});
            }

            if (show_edge_inset_widget_ && edge_inset_widget_) {
                rows.push_back({edge_inset_widget_.get()});
            }

            if (resolution_widget_) {
                rows.push_back({resolution_widget_.get()});
            }

            if (candidates_expanded_) {
                if (candidate_entries_.empty()) {
                    rows.push_back({empty_candidates_label_.get()});
                }
                if (auto* graph = candidate_editor_widget()) {
                    rows.push_back({graph});
                }
            }

            if (advanced_expanded_) {
                if (show_resolve_geometry_widget_ && resolve_geometry_widget_) {
                    rows.push_back({resolve_geometry_widget_.get()});
                }
                if (show_resolve_quantity_widget_ && resolve_quantity_widget_) {
                    rows.push_back({resolve_quantity_widget_.get()});
                }
                if (show_explicit_flip_widget_ && explicit_flip_widget_) {
                    rows.push_back({explicit_flip_widget_.get()});
                }
                if (show_force_flipped_widget_ && force_flipped_widget_) {
                    rows.push_back({force_flipped_widget_.get()});
                }
                if (enforce_widget_) {
                    rows.push_back({enforce_widget_.get()});
                }
            }

            if (delete_widget_) {
                rows.push_back({delete_widget_.get()});
            }

            return;
        }

    }

    SDL_Rect header_rect() const {
        SDL_Rect rect{0, 0, 0, 0};
        if (name_widget_) {
            rect = name_widget_->rect();
        }
        if (ownership_label_widget_) {
            const SDL_Rect& owner_rect = ownership_label_widget_->rect();
            if (owner_rect.w > 0 && owner_rect.h > 0) {
                if (rect.w <= 0 || rect.h <= 0) {
                    rect = owner_rect;
                } else {
                    int x = std::min(rect.x, owner_rect.x);
                    int y = std::min(rect.y, owner_rect.y);
                    int right = std::max(rect.x + rect.w, owner_rect.x + owner_rect.w);
                    int bottom = std::max(rect.y + rect.h, owner_rect.y + owner_rect.h);
                    rect = SDL_Rect{x, y, right - x, bottom - y};
                }
            }
        }
        return rect;
    }

    bool can_begin_drag_at(const SDL_Point& point) const {
        if (locked_) return false;
        SDL_Rect rect = header_rect();
        if (rect.w <= 0 || rect.h <= 0) return false;
        if (SDL_PointInRect(&point, &rect)) {
            if (name_widget_) {
                const SDL_Rect& name_rect = name_widget_->rect();
                if (name_rect.w > 0 && name_rect.h > 0 && SDL_PointInRect(&point, &name_rect)) {

                    int right_edge = name_rect.x + name_rect.w;
                    if (point.x <= right_edge) {
                        return false;
                    }
                }
            }
            return true;
        }
        return false;
    }

private:
    void notify_change(bool method_changed, bool quantity_changed, bool candidates_changed, bool resolution_changed = false) {
        if (!owner_) return;
        SpawnGroupConfig::ChangeSummary summary;
        summary.method_changed = method_changed;
        summary.quantity_changed = quantity_changed;
        summary.candidates_changed = candidates_changed;
        summary.method = current_method_;
        summary.resolution_changed = resolution_changed;
        summary.resolution = current_resolution_;

        nlohmann::json entry_copy = entry_view();

        owner_->enqueue_notification([owner = owner_, entry = std::move(entry_copy), summary, self = this]() mutable {
            if (!owner) return;
            owner->current_entry_ = self;
            if (owner->on_change_) owner->on_change_();
            if (owner->on_entry_change_) owner->on_entry_change_(entry, summary);
            owner->fire_entry_callbacks(entry, summary);
            if (owner->current_entry_ == self) {
                owner->current_entry_ = nullptr;
            }
        });
    }

    void update_toggle_label() {}

    void update_candidates_toggle_label() {
        if (!candidates_toggle_btn_) return;
        std::string label = "";
        label += candidates_expanded_ ? std::string(DMIcons::CollapseExpanded()) : std::string(DMIcons::CollapseCollapsed());
        label += " Candidates";
        candidates_toggle_btn_->set_text(label);
    }

    void update_advanced_toggle_label() {
        if (!advanced_toggle_btn_) return;
        std::string label = "";
        label += advanced_expanded_ ? std::string(DMIcons::CollapseExpanded()) : std::string(DMIcons::CollapseCollapsed());
        label += " Advanced Options";
        advanced_toggle_btn_->set_text(label);
    }

    void update_ownership_label() {
        if (!ownership_label_widget_) return;
        const std::string& label = ownership_label_text_;
        if (label.empty()) {
            ownership_label_widget_->set_color(SDL_Color{0, 0, 0, 0});
            ownership_label_widget_->set_text("Room Owner: None");
            ownership_label_widget_->set_subtle(true);
        } else {
            ownership_label_widget_->set_text("Room Owner: " + label);
            if (auto color = ownership_color_) {
                ownership_label_widget_->set_color(*color);
            } else {
                ownership_label_widget_->set_color(SDL_Color{0, 0, 0, 0});
            }
            ownership_label_widget_->set_subtle(false);
        }
    }

    void update_candidate_graph() {
        if (auto* graph = candidate_editor_widget()) {
            graph->set_search_extra_results_provider([this]() {
                std::vector<SearchAssets::Result> results;

                {
                    SearchAssets::Result null_res;
                    null_res.label = "null";
                    null_res.value = "null";
                    null_res.is_tag = false;
                    results.push_back(std::move(null_res));
                }

                try {
                    const auto& provider = area_names_provider();
                    auto names = provider ? provider() : std::vector<std::string>{};
                    for (const auto& name : names) {
                        if (name.empty()) continue;
                        SearchAssets::Result res;
                        res.label = name + " (Area)";
                        res.value = name;
                        res.is_tag = false;
                        results.push_back(std::move(res));
                    }
                } catch (...) {
                }
                return results;
            });
            graph->set_candidates_from_json(entry_view());
            graph->set_on_adjust([this](int index, int delta){
                if (!editable_) return;
                if (index < 0) return;
                if (auto* entry = mutable_entry()) {
                    devmode::spawn::sanitize_spawn_group_candidates(*entry);
                    auto& arr = (*entry)["candidates"];
                    if (!arr.is_array() || index >= static_cast<int>(arr.size())) return;
                    double curr = safe_double(arr.at(index), "chance", safe_double(arr.at(index), "weight", 0.0));
                    double next = std::max(0.0, curr + static_cast<double>(delta));
                    arr.at(index)["chance"] = next;
                    update_candidate_graph();
                    notify_change(false, false, true);
                }
            });
            graph->set_on_delete([this](int index){
                this->remove_candidate_at(index);
            });
            if (owner_ && owner_->callbacks_.on_regenerate && editable_) {
                graph->set_on_regenerate([this]() {
                    if (!owner_) return;
                    std::string id = spawn_id();
                    owner_->enqueue_notification([owner = owner_, id]() {
                        if (!owner) return;
                        if (owner->callbacks_.on_regenerate) owner->callbacks_.on_regenerate(id);
                    });
                });
            } else {
                graph->set_on_regenerate({});
            }
            if (editable_) {
                graph->set_on_add_candidate([this](const std::string& value) {
                    this->add_candidate_from_search(value);
                });
            } else {
                graph->set_on_add_candidate({});
            }
        }
    }

    void set_priority_position(size_t index, size_t total) {
        priority_index_ = index;
        priority_count_ = total;
        update_priority_button_states();
    }

    void update_priority_button_states() {
        bool can_move_up = editable_ && priority_index_ > 0;
        bool can_move_down = editable_ && (priority_count_ == 0 ? false : priority_index_ + 1 < priority_count_);
        if (priority_up_widget_) priority_up_widget_->set_enabled(can_move_up);
        if (priority_down_widget_) priority_down_widget_->set_enabled(can_move_down);
    }

    void update_embedded_search(const Input& input, int screen_w, int screen_h) {
        if (auto* graph = candidate_editor_widget()) {
            graph->set_screen_dimensions(screen_w, screen_h);
            graph->update_search(input);
        }
    }

    void rebuild_candidate_widgets() {
        candidate_entries_.clear();
        auto* entry = mutable_entry();
        const nlohmann::json& view = entry_view();
        const nlohmann::json* candidates = nullptr;
        if (entry && entry->is_object() && entry->contains("candidates") && (*entry)["candidates"].is_array()) {
            candidates = &(*entry)["candidates"];
        } else if (view.is_object() && view.contains("candidates") && view["candidates"].is_array()) {
            candidates = &view["candidates"];
        }
        if (!candidates) return;
        for (size_t i = 0; i < candidates->size(); ++i) {
            const auto& cand = (*candidates)[i];
            CandidateWidgets widgets;
            std::string name = safe_string(cand, "name", "");
            double chance = safe_double(cand, "chance", safe_double(cand, "weight", 0.0));

            widgets.name_box = std::make_unique<DMTextBox>("Name", name);
            widgets.name_widget = std::make_unique<SpawnGroupCallbackTextBoxWidget>(std::move(widgets.name_box),
                [this, i](const std::string& text) {
                    if (!editable_) return;
                    if (auto* entry = mutable_entry()) {
                        devmode::spawn::sanitize_spawn_group_candidates(*entry);
                        if (i < entry->at("candidates").size()) {
                            entry->at("candidates").at(i)["name"] = trim(text);
                            update_candidate_graph();
                            notify_change(false, false, true);
                        }
                    }
                },
                false,
                editable_);
            widgets.name_widget->set_value(name);

            widgets.chance_box = std::make_unique<DMTextBox>("Chance", std::to_string(static_cast<int>(chance)));
            widgets.chance_widget = std::make_unique<SpawnGroupCallbackTextBoxWidget>(std::move(widgets.chance_box),
                [this, i](const std::string& text) {
                    if (!editable_) return;
                    if (i < candidate_entries_.size()) {
                        auto* widget = candidate_entries_[i].chance_widget.get();
                        if (widget) {
                            DMTextBox* box = widget->box();
                            if (box && box->is_editing()) {
                                return;
                            }
                        }
                    }
                    if (auto* entry = mutable_entry()) {
                        devmode::spawn::sanitize_spawn_group_candidates(*entry);
                        if (i < entry->at("candidates").size()) {
                            double value = parse_double_or(text, safe_double(entry->at("candidates").at(i), "chance", 0.0));
                            entry->at("candidates").at(i)["chance"] = value;
                            update_candidate_graph();
                            notify_change(false, false, true);
                        }
                    }
                },
                false,
                editable_);
            widgets.chance_widget->set_value(std::to_string(static_cast<int>(std::round(chance))));

            widgets.remove_button = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(), 0, DMButton::height());
            int index_copy = static_cast<int>(i);
            widgets.remove_widget = std::make_unique<ButtonWidget>(widgets.remove_button.get(), [this, index_copy]() {
                this->remove_candidate_at(index_copy);
            });

            candidate_entries_.push_back(std::move(widgets));
        }
    }

    double candidate_weight_at(size_t idx) const {
        const nlohmann::json& view = entry_view();
        if (!view.is_object() || !view.contains("candidates") || !view["candidates"].is_array()) return 0.0;
        const auto& arr = view["candidates"];
        if (idx >= arr.size()) return 0.0;
        const auto& cand = arr[idx];
        return safe_double(cand, "chance", safe_double(cand, "weight", 0.0));
    }

    double max_candidate_weight() const {
        const nlohmann::json& view = entry_view();
        if (!view.is_object() || !view.contains("candidates") || !view["candidates"].is_array()) return 0.0;
        double max_w = 0.0;
        for (const auto& cand : view["candidates"]) {
            double w = safe_double(cand, "chance", safe_double(cand, "weight", 0.0));
            double positive = w < 0.0 ? 0.0 : w;
            max_w = std::max(max_w, positive);
        }
        return max_w;
    }

    void remove_candidate_at(int index) {
        if (!editable_ || index < 0) return;
        if (auto* entry = mutable_entry()) {
            devmode::spawn::sanitize_spawn_group_candidates(*entry);
            auto& arr = (*entry)["candidates"];
            if (!arr.is_array()) return;
            if (index >= static_cast<int>(arr.size())) return;
            arr.erase(arr.begin() + index);
            devmode::spawn::sanitize_spawn_group_candidates(*entry);
            update_candidate_graph();
            rebuild_candidate_widgets();
            notify_change(false, false, true);
            owner_->mark_layout_dirty();
        }
    }

    void add_candidate_from_search(const std::string& label) {
        if (!editable_) return;
        if (label.empty()) return;
        if (auto* entry = mutable_entry()) {
            devmode::spawn::sanitize_spawn_group_candidates(*entry);
            auto& arr = (*entry)["candidates"];
            if (!arr.is_array()) return;
            double max_weight = max_candidate_weight();
            double new_weight = max_weight > 0.0 ? max_weight * 0.05 : 5.0;
            if (new_weight <= 0.0) new_weight = 5.0;

            nlohmann::json candidate = nlohmann::json::object();
            candidate["name"] = label;
            candidate["chance"] = new_weight;
            arr.push_back(candidate);
            update_candidate_graph();
            rebuild_candidate_widgets();
            notify_change(false, false, true);
            owner_->mark_layout_dirty();
        }
    }

    void on_method_changed(int index) {
        if (!editable_) return;
        if (index < 0 || index >= static_cast<int>(method_options_.size())) return;
        std::string method = method_options_[index];
        if (auto* entry = mutable_entry()) {
            std::string previous = safe_string(*entry, "position", kDefaultMethod);
            (*entry)["position"] = method;
            if (method == "Exact" || method == "Exact Position") {
                int quantity = safe_int(*entry, "quantity", safe_int(*entry, "min_number", kExactDefaultQuantity));
                (*entry)["min_number"] = quantity;
                (*entry)["max_number"] = quantity;
                (*entry)["quantity"] = quantity;
                (*entry).erase("edge_inset_percent");
        } else {
            int min_number = safe_int(*entry, "min_number", kDefaultMinNumber);
            int max_number = safe_int(*entry, "max_number", std::max(min_number, kDefaultMaxNumber));
            if (max_number < min_number) max_number = min_number;
            (*entry)["min_number"] = min_number;
            (*entry)["max_number"] = max_number;
            if (method == "Edge") {
                (*entry)["edge_inset_percent"] = kEdgeInsetDefault;
            } else if (method == "Perimeter") {
                (*entry)["radius"] = kPerimeterRadiusSliderMin;
                (*entry)["perimeter_radius"] = kPerimeterRadiusSliderMin;
            } else {
                (*entry).erase("edge_inset_percent");
            }
        }
            current_method_ = method;
            use_exact_quantity_ = (method == "Exact" || method == "Exact Position");
            notify_change(method != previous, true, false);
            owner_->mark_layout_dirty();
            sync_from_json();
        }
    }

    void on_min_changed(const std::string& text) {
        if (!editable_) return;
        if (min_widget_ && min_widget_->box() && min_widget_->box()->is_editing()) {
            return;
        }
        if (auto* entry = mutable_entry()) {
            int min_value = parse_int_or(text, safe_int(*entry, "min_number", kDefaultMinNumber));
            int max_value = safe_int(*entry, "max_number", std::max(min_value, kDefaultMaxNumber));
            if (min_value < 0) min_value = 0;
            if (max_value < min_value) max_value = min_value;
            (*entry)["min_number"] = min_value;
            (*entry)["max_number"] = max_value;
            notify_change(false, true, false);
            sync_from_json();
        }
    }

    void on_max_changed(const std::string& text) {
        if (!editable_) return;
        if (max_widget_ && max_widget_->box() && max_widget_->box()->is_editing()) {
            return;
        }
        if (auto* entry = mutable_entry()) {
            int max_value = parse_int_or(text, safe_int(*entry, "max_number", kDefaultMaxNumber));
            int min_value = safe_int(*entry, "min_number", kDefaultMinNumber);
            if (max_value < min_value) max_value = min_value;
            (*entry)["max_number"] = max_value;
            notify_change(false, true, false);
            sync_from_json();
        }
    }

    void on_exact_changed(const std::string& text) {
        if (!editable_) return;
        if (exact_widget_ && exact_widget_->box() && exact_widget_->box()->is_editing()) {
            return;
        }
        if (auto* entry = mutable_entry()) {
            int value = parse_int_or(text, safe_int(*entry, "quantity", kExactDefaultQuantity));
            if (value < 1) value = 1;
            (*entry)["quantity"] = value;
            (*entry)["min_number"] = value;
            (*entry)["max_number"] = value;
            notify_change(false, true, false);
            sync_from_json();
        }
    }

    void on_resolve_geometry_changed(bool value) {
        if (!editable_) return;
        if (auto* entry = mutable_entry()) {
            (*entry)["resolve_geometry_to_room_size"] = value;
            notify_change(false, false, false);
        }
    }

    void on_resolve_quantity_changed(bool value) {
        if (!editable_) return;
        if (auto* entry = mutable_entry()) {
            (*entry)["resolve_quantity_to_room_size"] = value;
            notify_change(false, true, false);
        }
    }

    void on_resolution_changed(int value) {
        if (!editable_) return;
        int clamped = vibble::grid::clamp_resolution(value);
        if (auto* entry = mutable_entry()) {
            int current = safe_int(*entry, "resolution", current_resolution_);
            if (current == clamped) {
                return;
            }
            (*entry)["resolution"] = clamped;
            current_resolution_ = clamped;
            notify_change(false, false, false, true);
        }
    }

    void on_perimeter_radius_changed(int value) {
        if (!editable_) return;
        int clamped = std::max(kPerimeterRadiusSliderMin, value);
        if (auto* entry = mutable_entry()) {
            int current = safe_int(*entry, "radius", safe_int(*entry, "perimeter_radius", kPerimeterRadiusSliderMin));
            if (current == clamped) {
                return;
            }
            (*entry)["radius"] = clamped;
            (*entry)["perimeter_radius"] = clamped;
            notify_change(true, false, false);
            sync_from_json();
        }
    }

    void on_edge_inset_changed(int value) {
        if (!editable_) return;
        int clamped = std::clamp(value, kEdgeInsetSliderMin, kEdgeInsetSliderMax);
        if (auto* entry = mutable_entry()) {
            int current = safe_int(*entry, "edge_inset_percent", kEdgeInsetDefault);
            if (current == clamped) {
                return;
            }
            (*entry)["edge_inset_percent"] = clamped;

            notify_change(true, false, false);
            sync_from_json();
        }
    }

    void on_explicit_flip_changed(bool value) {
        if (!editable_) return;
        if (auto* entry = mutable_entry()) {
            (*entry)["explicit_flip"] = value;
            notify_change(false, false, false);
            show_force_flipped_widget_ = value && show_explicit_flip_widget_;
            if (owner_) owner_->mark_layout_dirty();
        }
    }

    void on_force_flipped_changed(bool value) {
        if (!editable_) return;
        if (auto* entry = mutable_entry()) {
            (*entry)["force_flipped"] = value;
            notify_change(false, false, false);
        }
    }

    void on_locked_changed(bool value) {

        bool base_editable = (owner_ && (owner_->bound_array_ != nullptr || owner_->bound_entry_ != nullptr));
        if (!base_editable) return;
        if (auto* entry = mutable_entry()) {
            (*entry)["locked"] = value;
            locked_ = value;

            editable_ = base_editable && !locked_;
            notify_change(false, false, false);
            refresh_configuration();
        }
    }

    SpawnGroupConfig* owner_ = nullptr;
    nlohmann::json* entry_ = nullptr;
    nlohmann::json shadow_entry_ = nlohmann::json::object();
    std::string ownership_label_text_{};
    std::optional<SDL_Color> ownership_color_{};
    std::function<std::vector<std::string>()> area_provider_{};
    std::optional<std::string> method_lock_{};
    bool quantity_hidden_ = false;
    std::unique_ptr<CandidateEditorPieGraphWidget> candidate_graph_{};

    std::unique_ptr<DMButton> candidates_toggle_btn_{};
    std::unique_ptr<ButtonWidget> candidates_toggle_widget_{};
    bool candidates_expanded_ = false;
    std::unique_ptr<DMButton> advanced_toggle_btn_{};
    std::unique_ptr<ButtonWidget> advanced_toggle_widget_{};
    bool advanced_expanded_ = false;
    bool editable_ = false;
    bool expanded_state_ = false;
    bool use_exact_quantity_ = false;
    bool use_adjust_label_ = false;
    std::string current_method_ = kDefaultMethod;

    std::unique_ptr<DMButton> toggle_button_{};
    std::unique_ptr<ButtonWidget> toggle_widget_{};
    std::unique_ptr<SpawnGroupLabelWidget> ownership_label_widget_{};
    std::unique_ptr<DMButton> priority_up_button_{};
    std::unique_ptr<PriorityButtonWidget> priority_up_widget_{};
    std::unique_ptr<DMButton> priority_down_button_{};
    std::unique_ptr<PriorityButtonWidget> priority_down_widget_{};
    std::unique_ptr<DMButton> delete_button_{};
    std::unique_ptr<ButtonWidget> delete_widget_{};
    std::unique_ptr<CallbackCheckboxWidget> lock_widget_{};

    std::unique_ptr<SpawnGroupCallbackTextBoxWidget> name_widget_{};

    std::vector<std::string> method_options_{};
    std::unique_ptr<CallbackDropdownWidget> method_widget_{};

    std::unique_ptr<CallbackCheckboxWidget> enforce_widget_{};
    std::unique_ptr<CallbackCheckboxWidget> resolve_geometry_widget_{};
    std::unique_ptr<CallbackCheckboxWidget> resolve_quantity_widget_{};

    std::unique_ptr<SpawnGroupCallbackTextBoxWidget> min_widget_{};
    std::unique_ptr<SpawnGroupCallbackTextBoxWidget> max_widget_{};
    std::unique_ptr<SpawnGroupCallbackTextBoxWidget> exact_widget_{};
    std::unique_ptr<SpawnGroupCallbackSliderWidget> resolution_widget_{};
    std::unique_ptr<SpawnGroupCallbackSliderWidget> perimeter_radius_widget_{};
    std::unique_ptr<SpawnGroupCallbackSliderWidget> edge_inset_widget_{};
    bool show_perimeter_radius_widget_ = false;
    bool show_edge_inset_widget_ = false;
    bool show_resolve_geometry_widget_ = false;
    bool show_resolve_quantity_widget_ = false;
    int current_resolution_ = 0;

    std::unique_ptr<CallbackCheckboxWidget> explicit_flip_widget_{};
    std::unique_ptr<CallbackCheckboxWidget> force_flipped_widget_{};
    bool show_explicit_flip_widget_ = false;
    bool show_force_flipped_widget_ = false;
    bool locked_ = false;

    std::optional<size_t> array_index_{};

    std::vector<CandidateWidgets> candidate_entries_{};
    std::unique_ptr<SpawnGroupLabelWidget> empty_candidates_label_{};
    size_t priority_index_ = 0;
    size_t priority_count_ = 0;
};

SpawnGroupConfig::SpawnGroupConfig(bool floatable)
    : DockableCollapsible("Spawn Groups", floatable),
      default_floatable_mode_(floatable) {
    default_resolution_ = vibble::grid::clamp_resolution(MapGridSettings::defaults().resolution);
    set_scroll_enabled(true);
    set_cell_width(420);
    set_row_gap(8);
    set_col_gap(12);
    set_padding(12);
}

SpawnGroupConfig::~SpawnGroupConfig() = default;

void SpawnGroupConfig::set_default_resolution(int resolution) {
    default_resolution_ = vibble::grid::clamp_resolution(resolution);
    for (auto& entry : entries_) {
        if (!entry) continue;
        entry->sync_from_json();
    }
    mark_layout_dirty();
}

void SpawnGroupConfig::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
    for (auto& entry : entries_) {
        if (!entry) continue;
        if (auto* graph = entry->candidate_editor_widget()) {
            graph->set_screen_dimensions(width, height);
        }
    }
}

void SpawnGroupConfig::load(nlohmann::json& groups,
                          std::function<void()> on_change,
                          std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change,
                          ConfigureEntryCallback configure_entry) {
    load_impl(&groups, nullptr, std::move(on_change), std::move(on_entry_change), std::move(configure_entry));
}

void SpawnGroupConfig::bind_entry(nlohmann::json& entry,
                                  EntryCallbacks callbacks,
                                  ConfigureEntryCallback configure_entry) {
    bind_entry(entry, {}, {}, std::move(callbacks), std::move(configure_entry));
}

void SpawnGroupConfig::bind_entry(nlohmann::json& entry,
                                  std::function<void()> on_change,
                                  std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change,
                                  EntryCallbacks callbacks,
                                  ConfigureEntryCallback configure_entry) {
    entry_callbacks_ = std::move(callbacks);
    auto relay = [this, cb = std::move(on_entry_change)](const nlohmann::json& updated, const ChangeSummary& summary) {
        if (cb) cb(updated, summary);
        fire_entry_callbacks(updated, summary);
};
    load_impl(nullptr, &entry, std::move(on_change), std::move(relay), std::move(configure_entry));
}

void SpawnGroupConfig::load(const nlohmann::json& groups) {
    bound_array_ = nullptr;
    bound_entry_ = nullptr;
    entry_callbacks_ = {};
    on_change_ = {};
    on_entry_change_ = {};
    configure_entry_ = {};
    single_entry_mode_ = false;
    readonly_snapshot_ = groups;
    if (!readonly_snapshot_.is_array()) {
        readonly_snapshot_ = nlohmann::json::array();
    }
    for (auto& item : readonly_snapshot_) {
        if (!item.is_object()) continue;
        devmode::spawn::ensure_spawn_group_entry_defaults(item, default_display_name_for(item), default_resolution_);
    }
    single_entry_shadow_.clear();
    rebuild_rows();
}

void SpawnGroupConfig::load_impl(nlohmann::json* array,
                                 nlohmann::json* entry,
                                 std::function<void()> on_change,
                                 std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change,
                                 ConfigureEntryCallback configure_entry) {
    bound_array_ = array;
    bound_entry_ = entry;
    single_entry_mode_ = (bound_entry_ != nullptr);
    if (bound_entry_) {
        devmode::spawn::ensure_spawn_group_entry_defaults(*bound_entry_, default_display_name_for(*bound_entry_), default_resolution_);
    }
    if (bound_array_) {
        devmode::spawn::ensure_spawn_groups_array(*bound_array_);
        for (auto& item : *bound_array_) {
            if (!item.is_object()) continue;
            devmode::spawn::ensure_spawn_group_entry_defaults(item, default_display_name_for(item), default_resolution_);
        }
    }
    if (bound_entry_) {
        single_entry_shadow_ = nlohmann::json::array();
        single_entry_shadow_.push_back(*bound_entry_);
        devmode::spawn::ensure_spawn_group_entry_defaults(single_entry_shadow_.at(0), default_display_name_for(single_entry_shadow_.at(0)), default_resolution_);
    } else {
        single_entry_shadow_.clear();
        if (bound_array_) {
            entry_callbacks_ = {};
        }
    }
    readonly_snapshot_.clear();
    on_change_ = std::move(on_change);
    on_entry_change_ = std::move(on_entry_change);
    configure_entry_ = std::move(configure_entry);
    rebuild_rows();
}

void SpawnGroupConfig::append_rows(Rows& rows) {
    const bool was_suppressed = suppress_layout_change_callback_;

    if (layout_dirty_) {
        suppress_layout_change_callback_ = true;
        rebuild_layout();
    }
    suppress_layout_change_callback_ = was_suppressed;

    auto layout_rows = build_layout_rows();
    rows.insert(rows.end(), layout_rows.begin(), layout_rows.end());
    set_rows(layout_rows);
}

void SpawnGroupConfig::set_callbacks(Callbacks cb) { callbacks_ = std::move(cb); }

void SpawnGroupConfig::set_on_layout_changed(std::function<void()> cb) { on_layout_change_ = std::move(cb); }

void SpawnGroupConfig::refresh_row_configuration() {
    for (auto& entry : entries_) {
        if (!entry) continue;
        apply_configuration(*entry);
        entry->refresh_configuration();
    }
    mark_layout_dirty();
}

void SpawnGroupConfig::set_embedded_mode(bool embedded) {
    embedded_mode_ = embedded;
    set_floatable(!embedded ? default_floatable_mode_ : false);
    set_scroll_enabled(!embedded);
    if (embedded) {
        setLocked(false);
    }
}

std::string_view SpawnGroupConfig::lock_settings_namespace() const {
    if (embedded_mode_) {
        return {};
    }
    return "spawn_groups";
}

std::string_view SpawnGroupConfig::lock_settings_id() const {
    if (embedded_mode_) {
        return {};
    }
    return "config";
}

void SpawnGroupConfig::expand_group(const std::string& id) {
    if (id.empty()) return;
    expanded_.insert(id);
}

void SpawnGroupConfig::collapse_group(const std::string& id) {
    if (id.empty()) return;
    expanded_.erase(id);
}

bool SpawnGroupConfig::is_expanded(const std::string& id) const {
    if (id.empty()) return false;
    return expanded_.find(id) != expanded_.end();
}

std::vector<std::string> SpawnGroupConfig::expanded_groups() const {
    std::vector<std::string> ids(expanded_.begin(), expanded_.end());
    std::sort(ids.begin(), ids.end());
    return ids;
}

void SpawnGroupConfig::restore_expanded_groups(const std::vector<std::string>& ids) {
    expanded_.clear();
    for (const auto& id : ids) {
        if (!id.empty()) expanded_.insert(id);
    }
    mark_layout_dirty();
}

nlohmann::json SpawnGroupConfig::to_json() const {
    if (bound_array_) return *bound_array_;
    return readonly_snapshot_;
}

void SpawnGroupConfig::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
    if (drag_state_.active) {
        update_drag_visuals(input);
    }
    for (auto& entry : entries_) {
        if (!entry) continue;
        entry->update_embedded_search(input, screen_w, screen_h);
    }
    process_pending_notifications();
}

bool SpawnGroupConfig::handle_event(const SDL_Event& e) {
    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);

    if (drag_state_.active) {
        if (pointer_event) {
            SDL_Point pointer{0, 0};
            if (e.type == SDL_MOUSEMOTION) {
                pointer = SDL_Point{e.motion.x, e.motion.y};
            } else {
                pointer = SDL_Point{e.button.x, e.button.y};
            }
            drag_state_.pointer_y = pointer.y;
            bool inside_panel = SDL_PointInRect(&pointer, &rect_);
            if (e.type == SDL_MOUSEMOTION) {
                if (!inside_panel) {
                    cancel_drag();
                } else {
                    drag_state_.pointer_inside = SDL_PointInRect(&pointer, &body_viewport_);
                }
                process_pending_notifications();
                return true;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                drag_state_.pointer_inside = SDL_PointInRect(&pointer, &body_viewport_);
                if (!inside_panel || !drag_state_.pointer_inside) {
                    cancel_drag();
                } else {
                    finalize_drag(true);
                }
                process_pending_notifications();
                return true;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                process_pending_notifications();
                return true;
            }
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            cancel_drag();
            process_pending_notifications();
            return true;
        }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_LEAVE) {
            cancel_drag();
            process_pending_notifications();
            return true;
        }
        process_pending_notifications();
        return true;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point pointer{e.button.x, e.button.y};
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i]) continue;
            if (entries_[i]->can_begin_drag_at(pointer)) {
                begin_drag(i, pointer.y);
                process_pending_notifications();
                return true;
            }
        }
    }

    bool handled = DockableCollapsible::handle_event(e);
    process_pending_notifications();
    return handled;
}

void SpawnGroupConfig::render(SDL_Renderer* r) const {
    if (!r) return;
    DockableCollapsible::render(r);
}

void SpawnGroupConfig::render_content(SDL_Renderer* r) const {
    if (!r) return;
    DockableCollapsible::render_content(r);
    if (!drag_state_.active) return;
    SDL_Rect source = drag_state_.source_rect;
    source.x = body_viewport_.x;
    source.w = body_viewport_.w;
    if (source.w > 0 && source.h > 0) {
        const SDL_Color& bg = DMStyles::PanelBG();
        SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(r, &source);
    }
    SDL_Rect placeholder = drag_state_.placeholder_rect;
    placeholder.x = body_viewport_.x;
    placeholder.w = body_viewport_.w;
    if (placeholder.w > 0 && placeholder.h > 0) {
        const SDL_Color& highlight = DMStyles::HighlightColor();
        SDL_SetRenderDrawColor(r, highlight.r, highlight.g, highlight.b, highlight.a);
        SDL_RenderFillRect(r, &placeholder);
    }
}

void SpawnGroupConfig::open(nlohmann::json& groups, std::function<void(const nlohmann::json&)> on_save) {
    pending_save_callback_ = std::move(on_save);
    load(groups, {}, {}, {});
    DockableCollapsible::open();
}

void SpawnGroupConfig::request_open_spawn_group(const std::string& id, int, int) {
    if (id.empty()) return;
    pending_focus_id_ = id;
    expand_group(id);
    mark_layout_dirty();
}

void SpawnGroupConfig::set_anchor(int x, int y) {
    anchor_.x = x;
    anchor_.y = y;
}

void SpawnGroupConfig::close_embedded_search() {
    for (auto& entry : entries_) {
        if (!entry) continue;
        if (auto* graph = entry->candidate_editor_widget()) {
            graph->hide_search();
        }
    }
}

SpawnGroupConfig::Entry* SpawnGroupConfig::find_entry_by_id(const std::string& id) {
    if (id.empty()) return nullptr;
    for (auto& entry : entries_) {
        if (entry && entry->spawn_id() == id) {
            return entry.get();
        }
    }
    return nullptr;
}

bool SpawnGroupConfig::should_render_entry_body(const Entry&) const {
    return !drag_state_.active;
}

void SpawnGroupConfig::begin_drag(size_t index, int pointer_y) {
    if (index >= entries_.size()) return;
    if (!entries_[index]) return;
    drag_state_ = DragState{};
    drag_state_.active = true;
    drag_state_.source_index = index;
    drag_state_.hover_index = index;
    drag_state_.pointer_y = pointer_y;
    drag_state_.pointer_inside = false;
    drag_state_.original_order.clear();
    drag_state_.original_order.reserve(entries_.size());
    for (const auto& entry : entries_) {
        drag_state_.original_order.push_back(entry ? entry->spawn_id() : std::string{});
    }
    drag_state_.expansion_snapshot = expanded_groups();
    drag_state_.entry_heights.assign(entries_.size(), 0);
    drag_state_.placeholder_rect = SDL_Rect{0, 0, 0, 0};
    drag_state_.source_rect = SDL_Rect{0, 0, 0, 0};
    expanded_.clear();
    mark_layout_dirty();
}

void SpawnGroupConfig::cancel_drag() {
    if (!drag_state_.active) return;
    auto order = drag_state_.original_order;
    auto expansions = drag_state_.expansion_snapshot;
    drag_state_ = DragState{};
    expanded_.clear();
    for (const auto& id : expansions) {
        if (!id.empty()) {
            expanded_.insert(id);
        }
    }
    if (!order.empty()) {
        restore_order_from_snapshot(order);
    }
    rebuild_rows();
}

void SpawnGroupConfig::finalize_drag(bool commit) {
    if (!drag_state_.active) return;
    auto expansions = drag_state_.expansion_snapshot;
    auto order = drag_state_.original_order;
    size_t source = drag_state_.source_index;
    size_t slot = drag_state_.hover_index;
    drag_state_ = DragState{};
    if (!commit) {
        expanded_.clear();
        for (const auto& id : expansions) {
            if (!id.empty()) expanded_.insert(id);
        }
        if (!order.empty()) {
            restore_order_from_snapshot(order);
        }
        rebuild_rows();
        return;
    }

    if (entries_.empty() || source >= entries_.size() || !entries_[source]) {
        expanded_.clear();
        for (const auto& id : expansions) {
            if (!id.empty()) expanded_.insert(id);
        }
        rebuild_rows();
        return;
    }

    std::string moved_id = entries_[source]->spawn_id();
    size_t dest_slot = std::min(slot, entries_.size());
    if (dest_slot > entries_.size()) dest_slot = entries_.size();
    size_t dest = dest_slot;
    if (dest > source) dest = dest > 0 ? dest - 1 : 0;
    if (dest >= entries_.size()) dest = entries_.size() - 1;
    bool changed = dest != source;
    if (changed) {
        reorder_json(source, dest);
    }

    expanded_.clear();
    for (const auto& id : expansions) {
        if (!id.empty()) expanded_.insert(id);
    }
    if (!moved_id.empty()) {
        expanded_.insert(moved_id);
    }

    rebuild_rows();

    if (!changed) {
        return;
    }

    if (dest >= entries_.size()) dest = entries_.empty() ? 0 : entries_.size() - 1;
    Entry* moved_entry = entries_.empty() ? nullptr : entries_[dest].get();
    nlohmann::json entry_snapshot = moved_entry ? moved_entry->entry_view() : nlohmann::json::object();
    enqueue_notification([this, entry = std::move(entry_snapshot), moved_id, dest]() mutable {
        ChangeSummary summary{};
        if (on_change_) on_change_();
        if (on_entry_change_) on_entry_change_(entry, summary);
        fire_entry_callbacks(entry, summary);
        if (callbacks_.on_reorder) callbacks_.on_reorder(moved_id, dest);
    });
}

void SpawnGroupConfig::update_drag_visuals(const Input& input) {
    if (!drag_state_.active) return;
    SDL_Point pointer{input.getX(), input.getY()};
    drag_state_.pointer_y = pointer.y;
    drag_state_.pointer_inside = SDL_PointInRect(&pointer, &body_viewport_);

    const int fallback_height = DMCheckbox::height();
    if (entries_.empty()) {
        drag_state_.entry_heights.clear();
        drag_state_.hover_index = 0;
        drag_state_.placeholder_rect = slot_rect_for_index(0, fallback_height);
        drag_state_.source_rect = SDL_Rect{0, 0, 0, 0};
        return;
    }

    if (drag_state_.entry_heights.size() != entries_.size()) {
        drag_state_.entry_heights.assign(entries_.size(), fallback_height);
    }

    size_t candidate = entries_.size();
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];
        if (!entry) continue;
        SDL_Rect header = entry->header_rect();
        int height = header.h > 0 ? header.h : fallback_height;
        drag_state_.entry_heights[i] = height;
        if (drag_state_.source_index == i) {
            drag_state_.source_rect = SDL_Rect{body_viewport_.x, header.y, body_viewport_.w, height};
        }
        int threshold = header.y + height / 2;
        if (pointer.y < threshold) {
            candidate = i;
            break;
        }
    }

    SDL_Rect placeholder = slot_rect_for_index(candidate, fallback_height);
    if (candidate != drag_state_.hover_index ||
        placeholder.y != drag_state_.placeholder_rect.y ||
        placeholder.h != drag_state_.placeholder_rect.h) {
        drag_state_.hover_index = candidate;
        drag_state_.placeholder_rect = placeholder;
    }

    if (drag_state_.source_index < entries_.size() && entries_[drag_state_.source_index]) {
        SDL_Rect header = entries_[drag_state_.source_index]->header_rect();
        int height = drag_state_.entry_heights[drag_state_.source_index];
        if (height <= 0) height = fallback_height;
        drag_state_.source_rect = SDL_Rect{body_viewport_.x, header.y, body_viewport_.w, height};
    } else {
        drag_state_.source_rect = SDL_Rect{0, 0, 0, 0};
    }
}

SDL_Rect SpawnGroupConfig::slot_rect_for_index(size_t index, int fallback_height) const {
    SDL_Rect rect{body_viewport_.x, body_viewport_.y, body_viewport_.w, fallback_height};
    if (entries_.empty()) {
        rect.y = body_viewport_.y;
        return rect;
    }

    if (index >= entries_.size()) {
        const auto& last_entry = entries_.back();
        if (!last_entry) return rect;
        SDL_Rect header = last_entry->header_rect();
        int height = fallback_height;
        if (!drag_state_.entry_heights.empty()) {
            height = drag_state_.entry_heights.back();
            if (height <= 0) height = fallback_height;
        }
        rect.y = header.y + header.h + row_gap_;
        rect.h = height;
        int bottom = body_viewport_.y + body_viewport_.h;
        if (rect.y + rect.h > bottom) {
            rect.h = std::max(0, bottom - rect.y);
        }
        return rect;
    }

    const auto& entry = entries_[index];
    if (!entry) return rect;
    SDL_Rect header = entry->header_rect();
    int height = fallback_height;
    if (index < drag_state_.entry_heights.size() && drag_state_.entry_heights[index] > 0) {
        height = drag_state_.entry_heights[index];
    } else if (header.h > 0) {
        height = header.h;
    }
    rect.y = header.y;
    rect.h = height;
    return rect;
}

void SpawnGroupConfig::reorder_json(size_t from, size_t to) {
    auto apply = [from, to](nlohmann::json& arr) {
        if (!arr.is_array()) return;
        if (arr.empty()) return;
        if (from >= arr.size()) return;
        size_t target = std::min(to, arr.size());
        nlohmann::json moved = std::move(arr[from]);
        arr.erase(arr.begin() + static_cast<std::ptrdiff_t>(from));
        if (target > arr.size()) target = arr.size();
        arr.insert(arr.begin() + static_cast<std::ptrdiff_t>(target), std::move(moved));
};

    if (bound_array_) apply(*bound_array_);
    if (bound_entry_) apply(single_entry_shadow_);
    if (!bound_array_ && !bound_entry_ && readonly_snapshot_.is_array()) apply(readonly_snapshot_);
}

void SpawnGroupConfig::restore_order_from_snapshot(const std::vector<std::string>& order) {
    if (order.empty()) return;
    nlohmann::json* arr = nullptr;
    if (bound_array_) {
        arr = bound_array_;
    } else if (bound_entry_) {
        arr = &single_entry_shadow_;
    } else if (readonly_snapshot_.is_array()) {
        arr = &readonly_snapshot_;
    }
    if (!arr || !arr->is_array()) return;
    if (arr->size() != order.size()) return;

    auto get_id = [](const nlohmann::json& entry) -> std::string {
        if (entry.is_object()) {
            auto it = entry.find("spawn_id");
            if (it != entry.end() && it->is_string()) {
                return it->get<std::string>();
            }
        }
        return std::string{};
};

    for (size_t i = 0; i < order.size(); ++i) {
        std::string desired = order[i];
        std::string current = get_id((*arr)[i]);
        if (current == desired) continue;
        for (size_t j = i + 1; j < arr->size(); ++j) {
            if (get_id((*arr)[j]) == desired) {
                reorder_json(j, i);
                break;
            }
        }
    }
}

void SpawnGroupConfig::nudge_priority(Entry& entry, int delta) {
    if (delta == 0) return;
    if (entries_.size() <= 1) {
        entry.set_priority_position(0, entries_.size());
        return;
    }

    size_t source_index = 0;
    bool found = false;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].get() == &entry) {
            source_index = i;
            found = true;
            break;
        }
    }
    if (!found) return;

    int target_index = static_cast<int>(source_index) + delta;
    if (target_index < 0 || target_index >= static_cast<int>(entries_.size())) {
        entry.set_priority_position(source_index, entries_.size());
        return;
    }

    std::vector<std::string> expansions = expanded_groups();
    reorder_json(source_index, static_cast<size_t>(target_index));
    expanded_.clear();
    for (const auto& id : expansions) {
        if (!id.empty()) expanded_.insert(id);
    }

    rebuild_rows();

    if (target_index < 0 || entries_.empty()) return;
    size_t resolved_target = static_cast<size_t>(std::min(target_index, static_cast<int>(entries_.size() - 1)));
    Entry* moved_entry = entries_[resolved_target].get();
    std::string moved_id = moved_entry ? moved_entry->spawn_id() : std::string{};
    if (moved_id.empty()) {
        moved_id = entry.spawn_id();
    }
    nlohmann::json entry_snapshot = moved_entry ? moved_entry->entry_view() : nlohmann::json::object();
    enqueue_notification([this, entry_data = std::move(entry_snapshot), moved_id, dest = resolved_target]() mutable {
        ChangeSummary summary{};
        if (on_change_) on_change_();
        if (on_entry_change_) on_entry_change_(entry_data, summary);
        fire_entry_callbacks(entry_data, summary);
        if (callbacks_.on_reorder) callbacks_.on_reorder(moved_id, dest);
    });
}

void SpawnGroupConfig::rebuild_rows() {
    if (bound_entry_) {
        if (!single_entry_shadow_.is_array()) {
            single_entry_shadow_ = nlohmann::json::array();
        }
        if (single_entry_shadow_.empty()) {
            single_entry_shadow_.push_back(*bound_entry_);
        } else {
            single_entry_shadow_.at(0) = *bound_entry_;
        }
    }
    const nlohmann::json* source = current_source();
    if (!source || !source->is_array()) {
        entries_.clear();
        mark_layout_dirty();
        return;
    }

    std::vector<std::unique_ptr<Entry>> rebuilt;
    rebuilt.reserve(source->size());
    auto previous = std::move(entries_);

    auto take_existing = [&previous](const std::string& id) -> std::unique_ptr<Entry> {
        if (id.empty()) return nullptr;
        for (auto it = previous.begin(); it != previous.end(); ++it) {
            if (*it && (*it)->spawn_id() == id) {
                auto result = std::move(*it);
                previous.erase(it);
                return result;
            }
        }
        return nullptr;
};

    for (size_t i = 0; i < source->size(); ++i) {
        const auto& json_entry = (*source)[i];
        std::string id = json_entry.is_object() ? json_entry.value("spawn_id", std::string{}) : std::string{};
        std::unique_ptr<Entry> group_entry = take_existing(id);
        if (!group_entry) {
            group_entry = std::make_unique<Entry>(*this);
        }
        if (bound_array_) {
            group_entry->bind(&(*bound_array_)[i], i);
        } else if (bound_entry_ && i == 0) {
            group_entry->bind(bound_entry_);
        } else {
            group_entry->bind(nullptr);
            group_entry->set_shadow_entry(json_entry);
        }
        apply_configuration(*group_entry);
        group_entry->sync_from_json();
        group_entry->set_expanded(is_expanded(group_entry->spawn_id()));
        rebuilt.emplace_back(std::move(group_entry));
    }

    entries_ = std::move(rebuilt);
    mark_layout_dirty();
}

void SpawnGroupConfig::apply_configuration(Entry& entry) {
    if (!configure_entry_) return;
    EntryController controller(&entry);
    configure_entry_(controller, entry.entry_view());
}

void SpawnGroupConfig::rebuild_layout() {
    if (!layout_dirty_) return;
    layout_dirty_ = false;
    auto layout_rows = build_layout_rows();
    set_rows(layout_rows);
    if (!suppress_layout_change_callback_ && on_layout_change_) {
        on_layout_change_();
    }
}

void SpawnGroupConfig::mark_layout_dirty() {
    layout_dirty_ = true;
    rebuild_layout();
}

DockableCollapsible::Rows SpawnGroupConfig::build_layout_rows() {
    DockableCollapsible::Rows result;
    bool have_rows = false;
    for (size_t index = 0; index < entries_.size(); ++index) {
        auto& entry = entries_[index];
        have_rows = true;
        entry->set_expanded(is_expanded(entry->spawn_id()));
        entry->set_priority_position(index, entries_.size());
        entry->append_layout_rows(result);
    }

    if (!have_rows) {
        if (!empty_state_label_) {
            empty_state_label_ = std::make_unique<SpawnGroupLabelWidget>("No spawn groups configured.", DMStyles::Label().color, true);
        }
        result.push_back({empty_state_label_.get()});
    }

    if (callbacks_.on_add && !single_entry_mode_) {
        if (!add_button_) {
            add_button_ = std::make_unique<DMButton>("Add Spawn Group", &DMStyles::CreateButton(), 0, DMButton::height());
            add_button_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() {
                if (callbacks_.on_add) callbacks_.on_add();
            });
        }
        result.push_back({add_button_widget_.get()});
    }

    return result;
}

const nlohmann::json* SpawnGroupConfig::current_source() const {
    if (bound_array_) return bound_array_;
    if (bound_entry_) return &single_entry_shadow_;
    if (!readonly_snapshot_.is_null()) return &readonly_snapshot_;
    return nullptr;
}

void SpawnGroupConfig::enqueue_notification(std::function<void()> cb) {
    if (!cb) return;
    pending_notifications_.push_back(std::move(cb));
}

void SpawnGroupConfig::process_pending_notifications() {
    if (processing_notifications_) return;
    processing_notifications_ = true;
    while (!pending_notifications_.empty()) {
        auto cb = std::move(pending_notifications_.front());
        pending_notifications_.pop_front();
        if (cb) {
            cb();
            if (current_entry_) {
                current_entry_ = nullptr;
            }
        }
    }
    processing_notifications_ = false;
}

void SpawnGroupConfig::fire_entry_callbacks(const nlohmann::json& entry, const ChangeSummary& summary) {
    if (summary.method_changed && entry_callbacks_.on_method_changed) {
        std::string method = summary.method;
        if (entry.is_object()) {
            method = entry.value("position", method);
        }
        entry_callbacks_.on_method_changed(method);
    }
    if (summary.quantity_changed && entry_callbacks_.on_quantity_changed) {
        int min_value = 0;
        int max_value = 0;
        if (entry.is_object()) {
            if (entry.contains("quantity") && entry["quantity"].is_number()) {
                int quantity = entry["quantity"].get<int>();
                min_value = quantity;
                max_value = quantity;
            } else {
                min_value = safe_int(entry, "min_number", 0);
                max_value = safe_int(entry, "max_number", min_value);
            }
        }
        entry_callbacks_.on_quantity_changed(min_value, max_value);
    }
    if (summary.candidates_changed && entry_callbacks_.on_candidates_changed) {
        entry_callbacks_.on_candidates_changed(entry);
    }
    if (summary.method_changed && callbacks_.on_regenerate && !entry_callbacks_.on_method_changed) {
        std::string id;
        if (entry.is_object() && entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
            id = entry["spawn_id"].get<std::string>();
        }
        if (id.empty() && current_entry_) {
            id = current_entry_->spawn_id();
        }
        if (!id.empty()) {
            callbacks_.on_regenerate(id);
        }
    }
}

void SpawnGroupConfig::EntryController::set_ownership_label(const std::string& label, SDL_Color color) {
    if (!entry_) return;
    entry_->set_ownership_label(label, color);
}

void SpawnGroupConfig::EntryController::clear_ownership_label() {
    if (!entry_) return;
    entry_->clear_ownership_label();
}

void SpawnGroupConfig::EntryController::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    if (!entry_) return;
    entry_->set_area_names_provider(std::move(provider));
}

void SpawnGroupConfig::EntryController::lock_method_to(const std::string& method) {
    if (!entry_) return;
    entry_->lock_method_to(method);
}

void SpawnGroupConfig::EntryController::clear_method_lock() {
    if (!entry_) return;
    entry_->clear_method_lock();
}

void SpawnGroupConfig::EntryController::set_quantity_hidden(bool hidden) {
    if (!entry_) return;
    entry_->set_quantity_hidden(hidden);
}
