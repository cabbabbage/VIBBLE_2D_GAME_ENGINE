#include "SpawnGroupConfig.hpp"

#include <algorithm>
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
#include "widgets.hpp"
#include "widgets/CandidateEditorPieGraphWidget.hpp"

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

namespace {
constexpr const char* kDefaultMethod = "Random";
constexpr int kDefaultMinNumber = 1;
constexpr int kDefaultMaxNumber = 1;
constexpr int kExactDefaultQuantity = 1;

std::function<std::vector<std::string>()> empty_provider() {
    return []() { return std::vector<std::string>{}; };
}

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

class CallbackTextBoxWidget : public Widget {
public:
    CallbackTextBoxWidget(std::unique_ptr<DMTextBox> box,
                          std::function<void(const std::string&)> on_change,
                          bool full_row,
                          bool editable)
        : box_(std::move(box)), on_change_(std::move(on_change)), full_row_(full_row), editable_(editable) {}

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

    DMTextBox* box() { return box_.get(); }

    void set_editable(bool editable) { editable_ = editable; }

private:
    std::unique_ptr<DMTextBox> box_{};
    std::function<void(const std::string&)> on_change_{};
    bool full_row_ = false;
    bool editable_ = true;
    SDL_Rect rect_cache_{0, 0, 0, 0};
};

class CallbackCheckboxWidget : public Widget {
public:
    CallbackCheckboxWidget(std::unique_ptr<DMCheckbox> checkbox,
                           std::function<void(bool)> on_change,
                           bool editable)
        : checkbox_(std::move(checkbox)), on_change_(std::move(on_change)), editable_(editable) {}

    void set_rect(const SDL_Rect& r) override {
        if (checkbox_) checkbox_->set_rect(r);
        rect_cache_ = r;
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
    std::vector<std::string> options{"Random", "Perimeter", "Exact"};
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

struct SpawnGroupConfig::RowEntry {
    struct CandidateWidgets {
        std::unique_ptr<DMTextBox> name_box;
        std::unique_ptr<CallbackTextBoxWidget> name_widget;
        std::unique_ptr<DMTextBox> chance_box;
        std::unique_ptr<CallbackTextBoxWidget> chance_widget;
        std::unique_ptr<DMButton> remove_button;
        std::unique_ptr<ButtonWidget> remove_widget;
};

    explicit RowEntry(SpawnGroupConfig& owner)
        : owner_(&owner),
          area_provider_(empty_provider()),
          candidate_graph_(std::make_unique<CandidateEditorPieGraphWidget>()) {
        editable_ = owner_->bound_array_ != nullptr;
        method_options_ = build_method_options(kDefaultMethod);

        toggle_button_ = std::make_unique<DMButton>("▶", &DMStyles::ListButton(), 28, DMButton::height());
        toggle_widget_ = std::make_unique<ButtonWidget>(toggle_button_.get(), [this]() {
            expanded_state_ = !expanded_state_;
            if (expanded_state_) owner_->expand_group(spawn_id());
            else owner_->collapse_group(spawn_id());
            update_toggle_label();
            owner_->mark_layout_dirty();
        });

        spawn_id_label_ = std::make_unique<LabelWidget>();
        ownership_label_ = std::make_unique<LabelWidget>();
        ownership_label_->set_subtle(true);

        regenerate_button_ = std::make_unique<DMButton>("Regenerate", &DMStyles::ListButton(), 0, DMButton::height());
        regenerate_widget_ = std::make_unique<ButtonWidget>(regenerate_button_.get(), [this]() {
            if (!owner_ || !owner_->callbacks_.on_regenerate) return;
            owner_->callbacks_.on_regenerate(spawn_id());
        });

        duplicate_button_ = std::make_unique<DMButton>("Duplicate", &DMStyles::ListButton(), 0, DMButton::height());
        duplicate_widget_ = std::make_unique<ButtonWidget>(duplicate_button_.get(), [this]() {
            if (!owner_ || !owner_->callbacks_.on_duplicate) return;
            owner_->callbacks_.on_duplicate(spawn_id());
        });

        delete_button_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 0, DMButton::height());
        delete_widget_ = std::make_unique<ButtonWidget>(delete_button_.get(), [this]() {
            if (!owner_ || !owner_->callbacks_.on_delete) return;
            owner_->callbacks_.on_delete(spawn_id());
        });

        move_up_button_ = std::make_unique<DMButton>("Up", &DMStyles::ListButton(), 0, DMButton::height());
        move_up_widget_ = std::make_unique<ButtonWidget>(move_up_button_.get(), [this]() {
            if (!owner_ || !owner_->callbacks_.on_move_up) return;
            owner_->callbacks_.on_move_up(spawn_id());
        });

        move_down_button_ = std::make_unique<DMButton>("Down", &DMStyles::ListButton(), 0, DMButton::height());
        move_down_widget_ = std::make_unique<ButtonWidget>(move_down_button_.get(), [this]() {
            if (!owner_ || !owner_->callbacks_.on_move_down) return;
            owner_->callbacks_.on_move_down(spawn_id());
        });

        auto name_box = std::make_unique<DMTextBox>("Display Name", "");
        name_widget_ = std::make_unique<CallbackTextBoxWidget>(std::move(name_box),
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

        area_widget_ = std::make_unique<CallbackDropdownWidget>(
            "Area", std::vector<std::string>{"None"}, [this](int index) { on_area_changed(index); }, editable_);

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

        auto min_box = std::make_unique<DMTextBox>("Min Number", "");
        min_widget_ = std::make_unique<CallbackTextBoxWidget>(std::move(min_box),
            [this](const std::string& text) { on_min_changed(text); }, false, editable_);

        auto max_box = std::make_unique<DMTextBox>("Max Number", "");
        max_widget_ = std::make_unique<CallbackTextBoxWidget>(std::move(max_box),
            [this](const std::string& text) { on_max_changed(text); }, false, editable_);

        auto exact_box = std::make_unique<DMTextBox>("Quantity", "");
        exact_widget_ = std::make_unique<CallbackTextBoxWidget>(std::move(exact_box),
            [this](const std::string& text) { on_exact_changed(text); }, false, editable_);

        candidate_header_ = std::make_unique<LabelWidget>("Candidates");
        candidate_header_->set_subtle(true);

        add_candidate_button_ = std::make_unique<DMButton>("Add Candidate", &DMStyles::CreateButton(), 0, DMButton::height());
        add_candidate_widget_ = std::make_unique<ButtonWidget>(add_candidate_button_.get(), [this]() {
            if (!editable_) return;
            if (auto* entry = mutable_entry()) {
                devmode::spawn::sanitize_spawn_group_candidates(*entry);
                nlohmann::json candidate = nlohmann::json::object();
                candidate["name"] = "null";
                candidate["chance"] = 0;
                entry->at("candidates").push_back(candidate);
                update_candidate_graph();
                rebuild_candidate_widgets();
                notify_change(false, false, true);
                owner_->mark_layout_dirty();
            }
        });

        empty_candidates_label_ = std::make_unique<LabelWidget>("No candidates", DMStyles::Label().color, true);

        rebuild_candidate_widgets();
        sync_from_json();
        update_toggle_label();
    }

    void bind(nlohmann::json* entry) {
        entry_ = entry;
        editable_ = owner_ && owner_->bound_array_ != nullptr;
        if (!entry_) {
            shadow_entry_ = nlohmann::json::object();
        }
        update_candidate_graph();
    }

    void set_shadow_entry(const nlohmann::json& entry) {
        shadow_entry_ = entry;
        update_candidate_graph();
    }

    nlohmann::json* mutable_entry() { return entry_; }

    const nlohmann::json* mutable_entry() const { return entry_; }

    const nlohmann::json& entry_view() const {
        if (entry_) {
            return *entry_;
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
        ownership_label_ = label;
        ownership_color_ = color;
    }

    void clear_ownership_label() {
        ownership_label_.clear();
        ownership_color_.reset();
    }

    void set_area_names_provider(std::function<std::vector<std::string>()> provider) {
        area_provider_ = provider ? std::move(provider) : empty_provider();
    }

    const std::function<std::vector<std::string>()>& area_names_provider() const {
        return area_provider_;
    }

    void set_stack_key(std::string key) { stack_key_ = std::move(key); }

    const std::optional<std::string>& stack_key() const { return stack_key_; }

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
        spawn_id_label_->set_text(id.empty() ? std::string("<no id>") : id);

        std::string display = safe_string(entry, "display_name", {});
        name_widget_->set_value(display);

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
        current_method_ = method;
        use_exact_quantity_ = (method == "Exact" || method == "Exact Position");

        int min_number = safe_int(entry, "min_number", kDefaultMinNumber);
        int max_number = safe_int(entry, "max_number", std::max(min_number, kDefaultMaxNumber));
        if (max_number < min_number) max_number = min_number;
        int quantity = safe_int(entry, "quantity", use_exact_quantity_ ? min_number : kExactDefaultQuantity);

        min_widget_->set_value(std::to_string(min_number));
        max_widget_->set_value(std::to_string(max_number));
        exact_widget_->set_value(std::to_string(quantity));

        bool enforce_spacing = entry.is_object() ? entry.value("enforce_spacing", false) : false;
        enforce_widget_->set_value(enforce_spacing);

        update_area_dropdown_from_provider();
        update_candidate_graph();
        rebuild_candidate_widgets();
        update_ownership_label();
    }

    void refresh_configuration() {
        update_area_dropdown_from_provider();
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
        min_widget_->set_editable(editable_);
        max_widget_->set_editable(editable_);
        exact_widget_->set_editable(editable_);
    }

    void set_expanded(bool expanded) {
        expanded_state_ = expanded;
        update_toggle_label();
    }

    bool expanded() const { return expanded_state_; }

    void append_layout_rows(DockableCollapsible::Rows& rows) {
        DockableCollapsible::Row header_row;
        header_row.push_back(toggle_widget_.get());
        header_row.push_back(spawn_id_label_.get());
        header_row.push_back(ownership_label_.get());
        header_row.push_back(regenerate_widget_.get());
        header_row.push_back(duplicate_widget_.get());
        header_row.push_back(delete_widget_.get());
        header_row.push_back(move_up_widget_.get());
        header_row.push_back(move_down_widget_.get());
        rows.push_back(header_row);

        rows.push_back({name_widget_.get()});

        DockableCollapsible::Row method_row;
        method_row.push_back(method_widget_.get());
        if (show_area_dropdown_) {
            method_row.push_back(area_widget_.get());
        }
        method_row.push_back(enforce_widget_.get());
        rows.push_back(method_row);

        if (!quantity_hidden()) {
            if (use_exact_quantity_) {
                rows.push_back({exact_widget_.get()});
            } else {
                DockableCollapsible::Row qty_row;
                qty_row.push_back(min_widget_.get());
                qty_row.push_back(max_widget_.get());
                rows.push_back(qty_row);
            }
        }

        if (expanded_state_) {
            rows.push_back({candidate_header_.get()});
            if (candidate_rows_.empty()) {
                rows.push_back({empty_candidates_label_.get()});
            } else {
                for (auto& candidate : candidate_rows_) {
                    DockableCollapsible::Row candidate_row;
                    candidate_row.push_back(candidate.name_widget.get());
                    candidate_row.push_back(candidate.chance_widget.get());
                    candidate_row.push_back(candidate.remove_widget.get());
                    rows.push_back(std::move(candidate_row));
                }
            }
            rows.push_back({add_candidate_widget_.get()});
            if (auto* graph = candidate_editor_widget()) {
                rows.push_back({graph});
            }
        }
    }

private:
    void notify_change(bool method_changed, bool quantity_changed, bool candidates_changed) {
        if (!owner_) return;
        SpawnGroupConfig::ChangeSummary summary;
        summary.method_changed = method_changed;
        summary.quantity_changed = quantity_changed;
        summary.candidates_changed = candidates_changed;
        summary.method = current_method_;

        nlohmann::json entry_copy = entry_view();

        owner_->enqueue_notification([owner = owner_, entry = std::move(entry_copy), summary]() mutable {
            if (!owner) return;
            if (owner->on_change_) owner->on_change_();
            if (owner->on_entry_change_) owner->on_entry_change_(entry, summary);
            owner->fire_entry_callbacks(entry, summary);
        });
    }

    void update_toggle_label() {
        if (!toggle_button_) return;
        toggle_button_->set_text(expanded_state_ ? "▼" : "▶");
    }

    void update_ownership_label() {
        if (!ownership_label_) return;
        const std::string& label = ownership_label_;
        if (label.empty()) {
            ownership_label_->set_text("");
            ownership_label_->set_subtle(true);
        } else {
            ownership_label_->set_text(label);
            if (auto color = ownership_color_) {
                ownership_label_->set_color(*color);
            }
            ownership_label_->set_subtle(false);
        }
    }

    void update_area_dropdown_from_provider() {
        if (!area_widget_) return;
        auto provider = area_names_provider();
        std::vector<std::string> options = provider ? provider() : std::vector<std::string>{};
        std::string current = safe_string(entry_view(), "area", std::string{});
        if (!current.empty() && std::find(options.begin(), options.end(), current) == options.end()) {
            options.push_back(current);
        }
        if (options.empty()) {
            show_area_dropdown_ = false;
            area_options_.clear();
            return;
        }
        show_area_dropdown_ = true;
        area_options_ = options;
        int index = 0;
        for (size_t i = 0; i < area_options_.size(); ++i) {
            if (area_options_[i] == current) {
                index = static_cast<int>(i);
                break;
            }
        }
        area_widget_->set_options(area_options_, index);
    }

    void update_candidate_graph() {
        if (auto* graph = candidate_editor_widget()) {
            graph->set_candidates_from_json(entry_view());
        }
    }

    void rebuild_candidate_widgets() {
        candidate_rows_.clear();
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
            widgets.name_widget = std::make_unique<CallbackTextBoxWidget>(std::move(widgets.name_box),
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
            widgets.chance_widget = std::make_unique<CallbackTextBoxWidget>(std::move(widgets.chance_box),
                [this, i](const std::string& text) {
                    if (!editable_) return;
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
            widgets.remove_widget = std::make_unique<ButtonWidget>(widgets.remove_button.get(), [this, i]() {
                if (!editable_) return;
                if (auto* entry = mutable_entry()) {
                    devmode::spawn::sanitize_spawn_group_candidates(*entry);
                    auto& arr = entry->at("candidates");
                    if (arr.is_array() && arr.size() > 1 && i < arr.size()) {
                        arr.erase(arr.begin() + static_cast<nlohmann::json::difference_type>(i));
                        update_candidate_graph();
                        rebuild_candidate_widgets();
                        notify_change(false, false, true);
                        owner_->mark_layout_dirty();
                    }
                }
            });

            candidate_rows_.push_back(std::move(widgets));
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
            } else {
                int min_number = safe_int(*entry, "min_number", kDefaultMinNumber);
                int max_number = safe_int(*entry, "max_number", std::max(min_number, kDefaultMaxNumber));
                if (max_number < min_number) max_number = min_number;
                (*entry)["min_number"] = min_number;
                (*entry)["max_number"] = max_number;
            }
            current_method_ = method;
            use_exact_quantity_ = (method == "Exact" || method == "Exact Position");
            notify_change(method != previous, true, false);
            owner_->mark_layout_dirty();
            sync_from_json();
        }
    }

    void on_area_changed(int index) {
        if (!editable_) return;
        if (index < 0 || index >= static_cast<int>(area_options_.size())) return;
        if (auto* entry = mutable_entry()) {
            (*entry)["area"] = area_options_[index];
            notify_change(false, false, false);
        }
    }

    void on_min_changed(const std::string& text) {
        if (!editable_) return;
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

    SpawnGroupConfig* owner_ = nullptr;
    nlohmann::json* entry_ = nullptr;
    nlohmann::json shadow_entry_ = nlohmann::json::object();
    std::string ownership_label_{};
    std::optional<SDL_Color> ownership_color_{};
    std::function<std::vector<std::string>()> area_provider_{};
    std::optional<std::string> stack_key_{};
    std::optional<std::string> method_lock_{};
    bool quantity_hidden_ = false;
    std::unique_ptr<CandidateEditorPieGraphWidget> candidate_graph_{};
    bool editable_ = false;
    bool expanded_state_ = false;
    bool use_exact_quantity_ = false;
    std::string current_method_ = kDefaultMethod;

    std::unique_ptr<DMButton> toggle_button_{};
    std::unique_ptr<ButtonWidget> toggle_widget_{};
    std::unique_ptr<LabelWidget> spawn_id_label_{};
    std::unique_ptr<LabelWidget> ownership_label_{};

    std::unique_ptr<DMButton> regenerate_button_{};
    std::unique_ptr<ButtonWidget> regenerate_widget_{};
    std::unique_ptr<DMButton> duplicate_button_{};
    std::unique_ptr<ButtonWidget> duplicate_widget_{};
    std::unique_ptr<DMButton> delete_button_{};
    std::unique_ptr<ButtonWidget> delete_widget_{};
    std::unique_ptr<DMButton> move_up_button_{};
    std::unique_ptr<ButtonWidget> move_up_widget_{};
    std::unique_ptr<DMButton> move_down_button_{};
    std::unique_ptr<ButtonWidget> move_down_widget_{};

    std::unique_ptr<CallbackTextBoxWidget> name_widget_{};

    std::vector<std::string> method_options_{};
    std::unique_ptr<CallbackDropdownWidget> method_widget_{};

    bool show_area_dropdown_ = false;
    std::vector<std::string> area_options_{};
    std::unique_ptr<CallbackDropdownWidget> area_widget_{};

    std::unique_ptr<CallbackCheckboxWidget> enforce_widget_{};

    std::unique_ptr<CallbackTextBoxWidget> min_widget_{};
    std::unique_ptr<CallbackTextBoxWidget> max_widget_{};
    std::unique_ptr<CallbackTextBoxWidget> exact_widget_{};

    std::vector<CandidateWidgets> candidate_rows_{};
    std::unique_ptr<LabelWidget> candidate_header_{};
    std::unique_ptr<DMButton> add_candidate_button_{};
    std::unique_ptr<ButtonWidget> add_candidate_widget_{};
    std::unique_ptr<LabelWidget> empty_candidates_label_{};
};

SpawnGroupConfig::SpawnGroupConfig(bool floatable)
    : DockableCollapsible("Spawn Groups", floatable),
      default_floatable_mode_(floatable) {
    set_cell_width(260);
}

SpawnGroupConfig::~SpawnGroupConfig() = default;

void SpawnGroupConfig::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
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
    readonly_snapshot_ = groups;
    if (!readonly_snapshot_.is_array()) {
        readonly_snapshot_ = nlohmann::json::array();
    }
    for (auto& item : readonly_snapshot_) {
        if (!item.is_object()) continue;
        devmode::spawn::ensure_spawn_group_entry_defaults(item, default_display_name_for(item));
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
    if (bound_entry_) {
        devmode::spawn::ensure_spawn_group_entry_defaults(*bound_entry_, default_display_name_for(*bound_entry_));
    }
    if (bound_array_) {
        devmode::spawn::ensure_spawn_groups_array(*bound_array_);
        for (auto& item : *bound_array_) {
            if (!item.is_object()) continue;
            devmode::spawn::ensure_spawn_group_entry_defaults(item, default_display_name_for(item));
        }
    }
    if (bound_entry_) {
        single_entry_shadow_ = nlohmann::json::array();
        single_entry_shadow_.push_back(*bound_entry_);
        devmode::spawn::ensure_spawn_group_entry_defaults(single_entry_shadow_.at(0), default_display_name_for(single_entry_shadow_.at(0)));
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
    for (auto& row : rows_) {
        row->refresh_configuration();
    }
    mark_layout_dirty();
}

void SpawnGroupConfig::set_embedded_mode(bool embedded) {
    embedded_mode_ = embedded;
    set_floatable(!embedded ? default_floatable_mode_ : false);
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
    process_pending_notifications();
}

bool SpawnGroupConfig::handle_event(const SDL_Event& e) {
    bool handled = DockableCollapsible::handle_event(e);
    process_pending_notifications();
    return handled;
}

void SpawnGroupConfig::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
}

void SpawnGroupConfig::render_content(SDL_Renderer* r) const {
    DockableCollapsible::render_content(r);
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

void SpawnGroupConfig::close_asset_search() { pending_focus_id_.reset(); }

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
        rows_.clear();
        mark_layout_dirty();
        return;
    }

    std::vector<std::unique_ptr<RowEntry>> rebuilt;
    rebuilt.reserve(source->size());
    auto previous = std::move(rows_);

    auto take_existing = [&previous](const std::string& id) -> std::unique_ptr<RowEntry> {
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
        const auto& entry = (*source)[i];
        std::string id = entry.is_object() ? entry.value("spawn_id", std::string{}) : std::string{};
        std::unique_ptr<RowEntry> row_entry = take_existing(id);
        if (!row_entry) {
            row_entry = std::make_unique<RowEntry>(*this);
        }
        if (bound_array_) {
            row_entry->bind(&(*bound_array_)[i]);
        } else if (bound_entry_ && i == 0) {
            row_entry->bind(bound_entry_);
        } else {
            row_entry->bind(nullptr);
            row_entry->set_shadow_entry(entry);
        }
        apply_configuration(*row_entry);
        row_entry->sync_from_json();
        row_entry->set_expanded(is_expanded(row_entry->spawn_id()));
        rebuilt.emplace_back(std::move(row_entry));
    }

    rows_ = std::move(rebuilt);
    mark_layout_dirty();
}

void SpawnGroupConfig::apply_configuration(RowEntry& row) {
    if (!configure_entry_) return;
    RowController controller(&row);
    configure_entry_(controller, row.entry_view());
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
    for (auto& row : rows_) {
        have_rows = true;
        row->set_expanded(is_expanded(row->spawn_id()));
        row->append_layout_rows(result);
    }

    if (!have_rows) {
        if (!empty_state_label_) {
            empty_state_label_ = std::make_unique<LabelWidget>("No spawn groups configured.", DMStyles::Label().color, true);
        }
        result.push_back({empty_state_label_.get()});
    }

    if (callbacks_.on_add) {
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
        if (cb) cb();
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
}

void SpawnGroupConfig::RowController::set_ownership_label(const std::string& label, SDL_Color color) {
    if (!row_) return;
    row_->set_ownership_label(label, color);
}

void SpawnGroupConfig::RowController::clear_ownership_label() {
    if (!row_) return;
    row_->clear_ownership_label();
}

void SpawnGroupConfig::RowController::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    if (!row_) return;
    row_->set_area_names_provider(std::move(provider));
}

void SpawnGroupConfig::RowController::set_stack_key(std::string key) {
    if (!row_) return;
    row_->set_stack_key(std::move(key));
}

void SpawnGroupConfig::RowController::lock_method_to(const std::string& method) {
    if (!row_) return;
    row_->lock_method_to(method);
}

void SpawnGroupConfig::RowController::clear_method_lock() {
    if (!row_) return;
    row_->clear_method_lock();
}

void SpawnGroupConfig::RowController::set_quantity_hidden(bool hidden) {
    if (!row_) return;
    row_->set_quantity_hidden(hidden);
}
