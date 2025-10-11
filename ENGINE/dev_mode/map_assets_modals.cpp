#include "map_assets_modals.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <SDL_ttf.h>

#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "spawn_group_config/spawn_group_utils.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

using nlohmann::json;

namespace {

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

double parse_double_or(const std::string& text, double fallback) {
    if (text.empty()) return fallback;
    const char* cstr = text.c_str();
    char* end = nullptr;
    double value = std::strtod(cstr, &end);
    if (end == cstr) return fallback;
    while (end && *end) {
        if (!std::isspace(static_cast<unsigned char>(*end))) return fallback;
        ++end;
    }
    if (!std::isfinite(value)) return fallback;
    return value;
}

bool is_integral(double value) {
    if (!std::isfinite(value)) return false;
    const double rounded = std::round(value);
    return std::fabs(value - rounded) < 1e-9;
}

std::string chance_to_string(const json& candidate) {
    if (!candidate.is_object()) return "0";
    const auto it = candidate.find("chance");
    if (it == candidate.end()) return "0";
    if (it->is_number_integer()) {
        return std::to_string(it->get<int>());
    }
    if (it->is_number_float()) {
        double value = it->get<double>();
        if (is_integral(value)) {
            return std::to_string(static_cast<int>(std::llround(value)));
        }
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    return "0";
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
                                              static_cast<Uint8>(style.color.g / 2),
                                              static_cast<Uint8>(style.color.b / 2), style.color.a}
                                  : style.color;
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
        : box_(std::move(box)), on_change_(std::move(on_change)), full_row_(full_row) {}

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

    CandidateListPanelImpl() : DockableCollapsible("Spawn Group Candidates", true) {
        set_scroll_enabled(true);
        set_cell_width(360);
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
    }

    void bind(json* entry,
              std::string default_display_name,
              std::string ownership_label,
              std::optional<SDL_Color> ownership_color,
              SaveCallback on_save) {
        entry_ = entry;
        default_display_name_ = std::move(default_display_name);
        ownership_label_ = std::move(ownership_label);
        ownership_color_ = ownership_color;
        save_callback_ = std::move(on_save);

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
        if (!empty_candidates_label_) {
            empty_candidates_label_ = std::make_unique<LabelWidget>("No candidates configured", DMStyles::Label().color, true);
        }
        if (!add_button_) {
            add_button_ = std::make_unique<DMButton>("Add Candidate", &DMStyles::ListButton(), 0, DMButton::height());
            add_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() { add_candidate(); });
        }

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
        }
    }

private:
    struct CandidateRow {
        size_t index = 0;
        std::unique_ptr<LabelWidget> label;
        std::unique_ptr<CallbackTextBoxWidget> name_widget;
        std::unique_ptr<CallbackTextBoxWidget> chance_widget;
        std::unique_ptr<DMButton> remove_button;
        std::unique_ptr<ButtonWidget> remove_widget;
    };

    bool sanitize_entry() {
        if (!entry_) return false;
        bool changed = devmode::spawn::ensure_spawn_group_entry_defaults(*entry_, default_display_name_);
        changed = devmode::spawn::sanitize_spawn_group_candidates(*entry_) || changed;
        return changed;
    }

    void rebuild_rows(bool ensure_sanitized) {
        if (!entry_) {
            set_rows({});
            candidate_rows_.clear();
            return;
        }

        if (ensure_sanitized) sanitize_entry();

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

        candidate_rows_.clear();
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array() || candidates.empty()) {
            if (empty_candidates_label_) rows.push_back({empty_candidates_label_.get()});
        } else {
            candidate_rows_.reserve(candidates.size());
            for (size_t i = 0; i < candidates.size(); ++i) {
                CandidateRow row;
                row.index = i;
                row.label = std::make_unique<LabelWidget>(
                    "Candidate " + std::to_string(i + 1), DMStyles::Label().color, true);

                std::string name = candidates[i].value("name", std::string{"null"});
                auto name_box = std::make_unique<DMTextBox>("Asset Name", name);
                row.name_widget = std::make_unique<CallbackTextBoxWidget>(
                    std::move(name_box),
                    [this, idx = row.index](const std::string& value) { on_name_changed(idx, value); },
                    true);

                std::string chance = chance_to_string(candidates[i]);
                auto chance_box = std::make_unique<DMTextBox>("Chance", chance);
                row.chance_widget = std::make_unique<CallbackTextBoxWidget>(
                    std::move(chance_box),
                    [this, idx = row.index](const std::string& value) { on_chance_changed(idx, value); },
                    false);

                row.remove_button = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(), 0, DMButton::height());
                row.remove_widget = std::make_unique<ButtonWidget>(row.remove_button.get(), [this, idx = row.index]() {
                    remove_candidate(idx);
                });

                candidate_rows_.push_back(std::move(row));
            }

            for (auto& row : candidate_rows_) {
                rows.push_back({row.label.get()});
                rows.push_back({row.name_widget.get()});
                DockableCollapsible::Row chance_row;
                chance_row.push_back(row.chance_widget.get());
                chance_row.push_back(row.remove_widget.get());
                rows.push_back(std::move(chance_row));
            }
        }

        if (add_widget_) {
            rows.push_back({add_widget_.get()});
        }

        set_rows(rows);
    }

    void on_name_changed(size_t index, const std::string& value) {
        if (!entry_) return;
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array() || index >= candidates.size()) return;
        candidates[index]["name"] = trim(value);
        notify_save(false);
    }

    void on_chance_changed(size_t index, const std::string& value) {
        if (!entry_) return;
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array() || index >= candidates.size()) return;
        double fallback = 0.0;
        const auto it = candidates[index].find("chance");
        if (it != candidates[index].end()) {
            if (it->is_number_float()) fallback = it->get<double>();
            else if (it->is_number_integer()) fallback = static_cast<double>(it->get<int>());
        }
        double parsed = parse_double_or(value, fallback);
        if (parsed < 0.0) parsed = 0.0;
        if (is_integral(parsed)) {
            candidates[index]["chance"] = static_cast<int>(std::llround(parsed));
        } else {
            candidates[index]["chance"] = parsed;
        }
        notify_save(false);
    }

    void remove_candidate(size_t index) {
        if (!entry_) return;
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array() || index >= candidates.size()) return;
        auto it = candidates.begin() + static_cast<json::difference_type>(index);
        candidates.erase(it);
        notify_save(true);
    }

    void add_candidate() {
        if (!entry_) return;
        auto& candidates = (*entry_)["candidates"];
        if (!candidates.is_array()) candidates = json::array();
        candidates.push_back({{"name", "null"}, {"chance", 1}});
        notify_save(true);
    }

    json* entry_ = nullptr;
    std::string default_display_name_{};
    std::string ownership_label_{};
    std::optional<SDL_Color> ownership_color_{};
    SaveCallback save_callback_{};

    int screen_w_ = 1920;
    int screen_h_ = 1080;

    std::unique_ptr<LabelWidget> ownership_label_widget_{};
    std::unique_ptr<LabelWidget> display_name_widget_{};
    std::unique_ptr<LabelWidget> candidates_header_{};
    std::unique_ptr<LabelWidget> empty_candidates_label_{};
    std::unique_ptr<DMButton> add_button_{};
    std::unique_ptr<ButtonWidget> add_widget_{};

    std::vector<CandidateRow> candidate_rows_{};
};

}  // namespace

class CandidateListPanel : public CandidateListPanelImpl {
public:
    using CandidateListPanelImpl::CandidateListPanelImpl;
};

SingleSpawnGroupModal::SingleSpawnGroupModal() = default;
SingleSpawnGroupModal::~SingleSpawnGroupModal() = default;

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
                                 SaveCallback on_save) {
    map_info_ = &map_info;
    on_save_ = std::move(on_save);
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
                 });

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
    SDL_Rect rect = panel_->rect();
    constexpr int kPreferredWidth = 360;
    if (rect.w <= 0) rect.w = kPreferredWidth;
    rect.w = std::max(rect.w, kPreferredWidth);
    if (rect.h <= 0) rect.h = 420;
    const int margin = 16;
    const bool have_w = screen_w_ > 0;
    const bool have_h = screen_h_ > 0;
    int max_x = have_w ? std::max(margin, screen_w_ - rect.w - margin) : 0;
    int max_y = have_h ? std::max(margin, screen_h_ - rect.h - margin) : 0;
    SDL_Point pos = panel_->position();
    bool reposition = !position_initialized_;
    if (have_w && (pos.x < margin || pos.x > max_x)) reposition = true;
    if (have_h && (pos.y < margin || pos.y > max_y)) reposition = true;
    if (!reposition) return;
    int x = pos.x;
    int y = pos.y;
    if (have_w) {
        int centered = screen_w_ / 2 - rect.w / 2;
        x = std::clamp(centered, margin, max_x);
    }
    if (have_h) {
        int centered = screen_h_ / 2 - rect.h / 2;
        y = std::clamp(centered, margin, max_y);
    }
    if (have_w || have_h) {
        panel_->set_position(x, y);
        position_initialized_ = true;
    }
}

