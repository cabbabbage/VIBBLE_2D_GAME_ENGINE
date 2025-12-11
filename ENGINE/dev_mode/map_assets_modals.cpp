#include "map_assets_modals.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL_ttf.h>

#include "DockableCollapsible.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "dm_styles.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "spawn_group_config/widgets/CandidateEditorPieGraphWidget.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

using nlohmann::json;

namespace {

bool is_integral(double value) {
    if (!std::isfinite(value)) return false;
    const double rounded = std::round(value);
    return std::fabs(value - rounded) < 1e-9;
}

class LabelWidget : public Widget {
public:
    LabelWidget() = default;
    explicit LabelWidget(std::string text, SDL_Color color = DMStyles::Label().color, bool subtle = false)
        : text_(std::move(text)), color_(color), subtle_(subtle) {}

    void set_text(const std::string& text) { text_ = text; }
    void set_color(SDL_Color color) { color_ = color; }
    void set_subtle(bool subtle) { subtle_ = subtle; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return DMCheckbox::height(); }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        DMLabelStyle style = DMStyles::Label();
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
};

class CallbackTextBoxWidget : public Widget {
public:
    CallbackTextBoxWidget(std::unique_ptr<DMTextBox> box,
                          std::function<void(const std::string&)> on_change,
                          bool full_row)
        : box_(std::move(box)), on_change_(std::move(on_change)), full_row_(full_row) {
        if (box_) {
            box_->set_on_height_changed([this]() { this->request_layout(); });
        }
    }

    ~CallbackTextBoxWidget() override {
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
        if (!box_) return false;
        std::string before = box_->value();
        bool used = box_->handle_event(e);
        if (used) {
            std::string after = box_->value();
            if (after != before && on_change_) {
                on_change_(after);
            }
        }
        return used;
    }

    void render(SDL_Renderer* renderer) const override {
        if (box_) box_->render(renderer);
    }

    bool wants_full_row() const override { return full_row_; }

    void set_value(const std::string& value) {
        if (box_) box_->set_value(value);
    }

private:
    std::unique_ptr<DMTextBox> box_{};
    std::function<void(const std::string&)> on_change_{};
    bool full_row_ = false;
    SDL_Rect rect_cache_{0, 0, 0, 0};
};

class CandidateListPanelImpl : public DockableCollapsible {
public:
    using SaveCallback = std::function<void()>;
    using RegenCallback = std::function<void(const json&)>;

    CandidateListPanelImpl() : DockableCollapsible("Spawn Group Candidates", true) {
        set_scroll_enabled(true);
        set_floating_content_width(480);
        set_cell_width(420);
        set_row_gap(8);
        set_col_gap(12);
        set_padding(12);
    }

    void set_screen_dimensions(int width, int height) {
        screen_w_ = std::max(width, 0);
        screen_h_ = std::max(height, 0);
        const int kMinVisibleHeight = 320;
        const int kHeightMargin = 200;
        int visible_height = kMinVisibleHeight;
        if (screen_h_ > 0) {
            visible_height = std::max(kMinVisibleHeight, screen_h_ - kHeightMargin);
        }
        set_visible_height(visible_height);
        set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
        if (pie_widget_) {
            pie_widget_->set_screen_dimensions(screen_w_, screen_h_);
        }
    }

    void bind(json* entry,
              std::string default_display_name,
              std::string ownership_label,
              std::optional<SDL_Color> ownership_color,
              SaveCallback on_save,
              RegenCallback on_regen) {
        entry_ = entry;
        default_display_name_ = std::move(default_display_name);
        ownership_label_ = std::move(ownership_label);
        ownership_color_ = ownership_color;
        save_callback_ = std::move(on_save);
        regen_callback_ = std::move(on_regen);

        if (!ownership_label_.empty()) {
            if (!ownership_label_widget_) ownership_label_widget_ = std::make_unique<LabelWidget>();
            ownership_label_widget_->set_text(ownership_label_);
            if (ownership_color_) {
                ownership_label_widget_->set_color(*ownership_color_);
                ownership_label_widget_->set_subtle(false);
            } else {
                ownership_label_widget_->set_subtle(true);
            }
        }

        if (!display_name_widget_) display_name_widget_ = std::make_unique<LabelWidget>();
        if (!candidates_header_) candidates_header_ = std::make_unique<LabelWidget>("Candidates");
        if (!instructions_label_) {
            instructions_label_ = std::make_unique<LabelWidget>( "Scroll on a slice to adjust weight. Double-click to remove.", DMStyles::Label().color, true);
        }
        if (!pie_widget_) {
            pie_widget_ = std::make_unique<CandidateEditorPieGraphWidget>();
        }
        pie_widget_->set_screen_dimensions(screen_w_, screen_h_);
        pie_widget_->set_on_request_layout([this]() { this->layout(); });
        pie_widget_->set_on_adjust([this](int index, int delta) { adjust_candidate_weight(index, delta); });
        pie_widget_->set_on_delete([this](int index) { remove_candidate(index); });
        if (regen_callback_) {
            pie_widget_->set_on_regenerate([this]() { this->handle_regen(); });
        } else {
            pie_widget_->set_on_regenerate({});
        }
        pie_widget_->set_on_add_candidate([this](const std::string& value) { this->add_candidate_from_search(value); });

        if (!ownership_label_.empty()) {
            set_title(ownership_label_ + " Candidates");
        } else {
            set_title("Spawn Group Candidates");
        }

        rebuild_rows(true);
    }

    void notify_save(bool force_rebuild) {
        if (!entry_) return;
        bool sanitized = sanitize_entry();
        if (save_callback_) save_callback_();
        if (force_rebuild || sanitized) {
            rebuild_rows(false);
        } else if (pie_widget_) {
            pie_widget_->set_candidates_from_json(*entry_);
        }
    }

    bool handle_event(const SDL_Event& e) override {
        bool used = DockableCollapsible::handle_event(e);
        if (!entry_ || !expanded_) {
            return used;
        }

        if (grid_resolution_slider_ && grid_resolution_slider_->handle_event(e)) {
            used = true;
            if (entry_) {
                (*entry_)["grid_resolution"] = grid_resolution_slider_->value();
                notify_save(false);
            }
        }

        return used;
    }

    void update(const Input& input, int screen_w, int screen_h) override {
        screen_w_ = std::max(screen_w, 0);
        screen_h_ = std::max(screen_h, 0);
        if (pie_widget_) {
            pie_widget_->set_screen_dimensions(screen_w_, screen_h_);
        }
        DockableCollapsible::update(input, screen_w, screen_h);
        if (pie_widget_) {
            pie_widget_->update_search(input);
        }
    }

protected:
    std::string_view lock_settings_namespace() const override { return "map_assets"; }
    std::string_view lock_settings_id() const override { return "candidates"; }

private:
    static double read_candidate_weight(const json& candidate) {
        if (candidate.is_object()) {
            const auto weight_it = candidate.find("chance");
            if (weight_it != candidate.end()) {
                if (weight_it->is_number_float()) return weight_it->get<double>();
                if (weight_it->is_number_integer()) return static_cast<double>(weight_it->get<int>());
            }
            const auto alt_it = candidate.find("weight");
            if (alt_it != candidate.end()) {
                if (alt_it->is_number_float()) return alt_it->get<double>();
                if (alt_it->is_number_integer()) return static_cast<double>(alt_it->get<int>());
            }
        } else if (candidate.is_number_float()) {
            return candidate.get<double>();
        } else if (candidate.is_number_integer()) {
            return static_cast<double>(candidate.get<int>());
        }
        return 0.0;
    }

    bool sanitize_entry() {
        if (!entry_) return false;
        bool changed = devmode::spawn::ensure_spawn_group_entry_defaults(*entry_, default_display_name_);
        changed = devmode::spawn::sanitize_spawn_group_candidates(*entry_) || changed;
        return changed;
    }

    void rebuild_rows(bool ensure_sanitized) {
        if (!entry_) {
            set_rows({});
            return;
        }

        if (ensure_sanitized) sanitize_entry();

        if (pie_widget_) {
            pie_widget_->set_candidates_from_json(*entry_);
        }

        DockableCollapsible::Rows rows;

        if (ownership_label_widget_) {
            rows.push_back({ownership_label_widget_.get()});
        }

        const std::string display_name = entry_->value("display_name", default_display_name_);
        if (display_name_widget_) {
            display_name_widget_->set_text("Spawn group: " + display_name);
            display_name_widget_->set_subtle(true);
            rows.push_back({display_name_widget_.get()});
        }

        if (candidates_header_) {
            candidates_header_->set_subtle(false);
            rows.push_back({candidates_header_.get()});
        }

        if (instructions_label_) {
            instructions_label_->set_subtle(true);
            rows.push_back({instructions_label_.get()});
        }

        if (!grid_resolution_slider_) {
            constexpr int kMinResolution = 5;
            constexpr int kMaxResolution = 10;
            int current_resolution = kMinResolution;
            if (entry_ && entry_->contains("grid_resolution")) {
                current_resolution = std::max(kMinResolution, entry_->value("grid_resolution", kMinResolution));
            }
            grid_resolution_slider_ = std::make_unique<DMSlider>("Grid Resolution (2^r px)", kMinResolution, kMaxResolution, current_resolution);

            if (entry_) {
                (*entry_)["grid_resolution"] = current_resolution;
            }
        }
        if (grid_resolution_slider_) {
            auto grid_slider_widget = std::make_unique<SliderWidget>(grid_resolution_slider_.get());
            rows.push_back({grid_slider_widget.get()});
            widgets_.push_back(std::move(grid_slider_widget));
        }

        if (pie_widget_) {
            rows.push_back({pie_widget_.get()});
        }

        set_rows(rows);
    }

    void adjust_candidate_weight(int index, int delta) {
        if (!entry_ || delta == 0) return;
        devmode::spawn::ensure_spawn_group_entry_defaults(*entry_, default_display_name_);
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array() || index < 0 || index >= static_cast<int>(candidates.size())) return;
        auto& candidate = candidates[index];
        if (!candidate.is_object()) {
            candidate = json::object();
        }
        double current = read_candidate_weight(candidate);
        double next = std::max(0.0, current + static_cast<double>(delta));
        if (is_integral(next)) {
            candidate["chance"] = static_cast<int>(std::llround(next));
        } else {
            candidate["chance"] = next;
        }
        notify_save(true);
    }

    void remove_candidate(int index) {
        if (!entry_ || index < 0) return;
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array() || index >= static_cast<int>(candidates.size())) return;
        auto it = candidates.begin() + static_cast<json::difference_type>(index);
        candidates.erase(it);
        notify_save(true);
    }

    void add_candidate_from_search(const std::string& label) {
        if (!entry_) return;
        if (label.empty()) return;
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array()) candidates = json::array();

        double max_weight = 0.0;
        for (const auto& candidate : candidates) {
            max_weight = std::max(max_weight, std::max(0.0, read_candidate_weight(candidate)));
        }

        double new_weight = max_weight > 0.0 ? max_weight * 0.05 : 5.0;
        if (new_weight <= 0.0) {
            new_weight = 5.0;
        }

        json candidate = json::object();
        candidate["name"] = label;
        if (is_integral(new_weight)) {
            candidate["chance"] = static_cast<int>(std::llround(new_weight));
        } else {
            candidate["chance"] = new_weight;
        }

        candidates.push_back(std::move(candidate));
        notify_save(true);
    }

    void handle_regen() {
        if (!entry_) return;
        const bool sanitized = sanitize_entry();
        if (sanitized && pie_widget_) {
            pie_widget_->set_candidates_from_json(*entry_);
        }
        if (save_callback_) {
            save_callback_();
        }
        if (regen_callback_) {
            regen_callback_(*entry_);
        }
    }

    json* entry_ = nullptr;
    std::string default_display_name_{};
    std::string ownership_label_{};
    std::optional<SDL_Color> ownership_color_{};
    SaveCallback save_callback_{};
    RegenCallback regen_callback_{};

    int screen_w_ = 1920;
    int screen_h_ = 1080;

    std::unique_ptr<LabelWidget> ownership_label_widget_{};
    std::unique_ptr<LabelWidget> display_name_widget_{};
    std::unique_ptr<LabelWidget> candidates_header_{};
    std::unique_ptr<LabelWidget> instructions_label_{};
    std::unique_ptr<CandidateEditorPieGraphWidget> pie_widget_{};
    std::unique_ptr<DMSlider> grid_resolution_slider_{};
    std::vector<std::unique_ptr<Widget>> widgets_{};
};

}

class CandidateListPanel : public CandidateListPanelImpl {
public:
    using CandidateListPanelImpl::CandidateListPanelImpl;
};

SingleSpawnGroupModal::SingleSpawnGroupModal() = default;
SingleSpawnGroupModal::~SingleSpawnGroupModal() = default;

void SingleSpawnGroupModal::set_on_close(std::function<void()> cb) {
    on_close_ = std::move(cb);
    if (panel_) {
        panel_->set_on_close([this]() {
            if (on_close_) on_close_();
        });
    }
}

void SingleSpawnGroupModal::ensure_single_group(json& section,
                                                const std::string& default_display_name) {
    if (!section.is_object()) {
        section = json::object();
    }
    auto& groups = devmode::spawn::ensure_spawn_groups_array(section);
    if (groups.empty()) {
        json entry = json::object();
        devmode::spawn::ensure_spawn_group_entry_defaults(entry, default_display_name);
        groups.push_back(std::move(entry));
    } else {
        devmode::spawn::ensure_spawn_group_entry_defaults(groups[0], default_display_name);
        if (groups.size() > 1) {
            json first = groups[0];
            groups = json::array();
            groups.push_back(std::move(first));
        }
    }
}

void SingleSpawnGroupModal::open(json& map_info,
                                 const std::string& section_key,
                                 const std::string& default_display_name,
                                 const std::string& ownership_label,
                                 SDL_Color ownership_color,
                                 SaveCallback on_save,
                                 RegenCallback on_regen) {
    map_info_ = &map_info;
    on_save_ = std::move(on_save);
    on_regen_ = std::move(on_regen);
    section_ = &(*map_info_)[section_key];
    ensure_single_group(*section_, default_display_name);

    auto& groups = (*section_)["spawn_groups"];
    entry_ = &groups.front();

    if (!panel_) panel_ = std::make_unique<CandidateListPanel>();
    panel_->set_screen_dimensions(screen_w_, screen_h_);
    panel_->bind(entry_,
                 default_display_name,
                 ownership_label,
                 ownership_label.empty() ? std::optional<SDL_Color>{} : std::optional<SDL_Color>{ownership_color},
                 [this]() {
                     if (on_save_) on_save_();
                 },
                 [this](const json& updated_entry) {
                     if (on_regen_) on_regen_(updated_entry);
                 });

    if (panel_) {
        panel_->set_on_close([this]() {
            if (on_close_) on_close_();
        });
    }

    panel_->open();
    panel_->force_pointer_ready();
    position_initialized_ = false;
    ensure_visible_position();
}

void SingleSpawnGroupModal::close() {
    if (panel_) panel_->close();
}

bool SingleSpawnGroupModal::visible() const {
    return panel_ && panel_->is_visible();
}

void SingleSpawnGroupModal::update(const Input& input) {
    if (panel_) panel_->update(input, screen_w_, screen_h_);
}

bool SingleSpawnGroupModal::handle_event(const SDL_Event& e) {
    if (!panel_) return false;
    return panel_->handle_event(e);
}

void SingleSpawnGroupModal::render(SDL_Renderer* r) const {
    if (panel_) panel_->render(r);
}

bool SingleSpawnGroupModal::is_point_inside(int x, int y) const {
    if (!panel_) return false;
    return panel_->is_point_inside(x, y);
}

void SingleSpawnGroupModal::set_screen_dimensions(int width, int height) {
    screen_w_ = std::max(width, 0);
    screen_h_ = std::max(height, 0);
    if (panel_) panel_->set_screen_dimensions(screen_w_, screen_h_);
    position_initialized_ = false;
    ensure_visible_position();
}

void SingleSpawnGroupModal::set_floating_stack_key(std::string key) {
    stack_key_ = std::move(key);
}

void SingleSpawnGroupModal::set_on_open_area(
    std::function<void(const std::string&, const std::string&)> cb) {
    on_open_area_ = std::move(cb);
}

void SingleSpawnGroupModal::ensure_visible_position() {
    if (!panel_) return;
    if (position_initialized_) return;

    panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});

    SDL_Rect rect = panel_->rect();
    constexpr int kPreferredWidth = 360;
    constexpr int kPreferredHeight = 420;

    FloatingPanelLayoutManager::PanelInfo info;
    info.panel = panel_.get();
    info.force_layout = true;
    info.preferred_width = rect.w > 0 ? std::max(rect.w, kPreferredWidth) : kPreferredWidth;
    int resolved_height = rect.h > 0 ? rect.h : panel_->height();
    if (resolved_height <= 0) {
        resolved_height = kPreferredHeight;
    }
    info.preferred_height = std::max(resolved_height, kPreferredHeight);

    std::vector<FloatingPanelLayoutManager::PanelInfo> panels;
    panels.push_back(info);
    FloatingPanelLayoutManager::instance().layoutAll(panels);

    position_initialized_ = true;
}
