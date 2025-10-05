#include "spawn_group_config_ui.hpp"

#include "DockableCollapsible.hpp"
#include "FloatingDockableManager.hpp"
#include "dm_styles.hpp"
#include "widgets.hpp"
#include "search_assets.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include <SDL_ttf.h>

namespace {
constexpr int kDefaultScreenW = 1920;
constexpr int kDefaultScreenH = 1080;
constexpr int kQuantityMin = 1;
constexpr int kQuantityMax = 1000;
constexpr int kChanceMin = 0;
constexpr int kChanceMax = 1000;
constexpr int kMaxPerimeterRadius = 10000;

template <typename T>
T json_value_or(const nlohmann::json& obj, const char* key, T default_value) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return default_value;
    }
    try {
        return it->get<T>();
    } catch (const nlohmann::json::exception&) {
        return default_value;
    }
}

int clamp_quantity(int value) {
    return std::clamp(value, kQuantityMin, kQuantityMax);
}

int clamp_chance(int value) {
    return std::clamp(value, kChanceMin, kChanceMax);
}

std::string fallback_spawn_id(const nlohmann::json& entry) {
    if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
        return entry["spawn_id"].get<std::string>();
    }
    if (entry.contains("display_name") && entry["display_name"].is_string()) {
        return entry["display_name"].get<std::string>();
    }
    if (entry.contains("name") && entry["name"].is_string()) {
        return entry["name"].get<std::string>();
    }
    return "Spawn Group";
}
} // namespace

class StaticLabel : public Widget {
public:
    StaticLabel() = default;
    explicit StaticLabel(std::string text) : text_(std::move(text)) {}

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    void set_color(SDL_Color color) {
        color_ = color;
        has_color_override_ = true;
    }

    void clear_color_override() {
        has_color_override_ = false;
        color_ = DMStyles::Label().color;
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        if (w <= 0) {
            return DMStyles::Label().font_size + DMSpacing::small_gap();
        }
        const DMLabelStyle& st = DMStyles::Label();
        TTF_Font* font = st.open_font();
        if (!font) {
            return st.font_size + DMSpacing::small_gap();
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), st.color, std::max(10, w));
        int height = surface ? surface->h : (st.font_size + DMSpacing::small_gap());
        if (surface) {
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
        return height;
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* r) const override {
        if (text_.empty()) {
            return;
        }
        const DMLabelStyle& st = DMStyles::Label();
        SDL_Color draw_color = has_color_override_ ? color_ : st.color;
        TTF_Font* font = st.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), draw_color, std::max(10, rect_.w));
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surface);
            if (tex) {
                SDL_Rect dst{ rect_.x, rect_.y, surface->w, surface->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 255, 255, 255};
    bool has_color_override_ = false;
};

SpawnGroupsConfigPanel::SpawnGroupsConfigPanel(int start_x, int start_y)
    : DockableCollapsible("Spawn Group", true, start_x, start_y) {
    spawn_methods_ = {"Random", "Center", "Perimeter", "Exact", "Percent", "Entrance", "Exit"};
    panel_title_ = "Spawn Group";
    default_position_ = SDL_Point{start_x, start_y};

    set_visible(false);
    set_expanded(true);
    set_scroll_enabled(true);
    set_cell_width(280);

    build_static_controls();
    ensure_candidate_rows();
    refresh_method_control();
    refresh_quantity_control();
    refresh_perimeter_control();
    refresh_area_controls();
    rebuild_rows();
}

SpawnGroupsConfigPanel::~SpawnGroupsConfigPanel() = default;

void SpawnGroupsConfigPanel::build_static_controls() {
    if (!header_label_) {
        header_label_ = std::make_unique<StaticLabel>("ID: Spawn Group");
    }
    if (!ownership_label_) {
        ownership_label_ = std::make_unique<StaticLabel>();
    }
    if (!locked_method_label_) {
        locked_method_label_ = std::make_unique<StaticLabel>();
    }
    if (!quantity_label_) {
        quantity_label_ = std::make_unique<StaticLabel>("Quantity");
    }
    if (!candidate_summary_label_) {
        candidate_summary_label_ = std::make_unique<StaticLabel>("Total chance: 0");
    }
    if (!area_hint_label_) {
        area_hint_label_ = std::make_unique<StaticLabel>("Linked area: (none)");
    }
    if (!persistence_warning_label_) {
        persistence_warning_label_ = std::make_unique<StaticLabel>();
        persistence_warning_label_->set_color(SDL_Color{255, 120, 120, 255});
    }

    if (!overlap_checkbox_) {
        overlap_checkbox_ = std::make_unique<DMCheckbox>("Check overlap", false);
        overlap_widget_ = std::make_unique<CheckboxWidget>(overlap_checkbox_.get());
    }
    if (!spacing_checkbox_) {
        spacing_checkbox_ = std::make_unique<DMCheckbox>("Enforce spacing", false);
        spacing_widget_ = std::make_unique<CheckboxWidget>(spacing_checkbox_.get());
    }

    if (!add_candidate_button_) {
        add_candidate_button_ = std::make_unique<DMButton>("Add Candidate", &DMStyles::CreateButton(), 140, DMButton::height());
        add_candidate_widget_ = std::make_unique<ButtonWidget>(add_candidate_button_.get(), [this]() {
            add_candidate_row("", 0);
            request_rebuild_rows();
        });
    }

    if (!browse_assets_button_) {
        browse_assets_button_ = std::make_unique<DMButton>("Browse Assets", &DMStyles::HeaderButton(), 150, DMButton::height());
        browse_assets_widget_ = std::make_unique<ButtonWidget>(browse_assets_button_.get(), [this]() {
            open_asset_search();
        });
    }

    if (!area_clear_button_) {
        area_clear_button_ = std::make_unique<DMButton>("Clear", &DMStyles::DeleteButton(), 80, DMButton::height());
        area_clear_widget_ = std::make_unique<ButtonWidget>(area_clear_button_.get(), [this]() {
            reset_area_selection();
        });
    }
}

void SpawnGroupsConfigPanel::ensure_candidate_rows() {
    if (!candidates_.empty()) {
        return;
    }
    add_candidate_row("", 0);
}

void SpawnGroupsConfigPanel::add_candidate_row(const std::string& name, int chance) {
    auto row = std::make_unique<CandidateRow>();
    row->last_name = name;
    row->last_chance = clamp_chance(chance);

    row->name_box = std::make_unique<DMTextBox>("Asset", row->last_name);
    row->name_widget = std::make_unique<TextBoxWidget>(row->name_box.get(), true);

    row->chance_slider = std::make_unique<DMSlider>("Chance", kChanceMin, kChanceMax, row->last_chance);
    row->chance_widget = std::make_unique<SliderWidget>(row->chance_slider.get());

    row->remove_button = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(), 90, DMButton::height());
    CandidateRow* target = row.get();
    row->remove_widget = std::make_unique<ButtonWidget>(row->remove_button.get(), [this, target]() {
        remove_candidate_row(target);
    });

    candidates_.push_back(std::move(row));
}

void SpawnGroupsConfigPanel::remove_candidate_row(CandidateRow* target) {
    if (!target) {
        return;
    }
    candidates_.erase(std::remove_if(candidates_.begin(), candidates_.end(),
                                     [target](const std::unique_ptr<CandidateRow>& row) {
                                         return row.get() == target;
                                     }),
                      candidates_.end());
    ensure_candidate_rows();
    mark_dirty();
    request_rebuild_rows();
}

void SpawnGroupsConfigPanel::refresh_method_control() {
    if (spawn_methods_.empty()) {
        return;
    }
    if (method_index_ < 0 || method_index_ >= static_cast<int>(spawn_methods_.size())) {
        method_index_ = 0;
    }
    method_dropdown_ = std::make_unique<DMDropdown>("Method", spawn_methods_, method_index_);
    method_widget_ = std::make_unique<DropdownWidget>(method_dropdown_.get());

    if (locked_method_label_) {
        const std::string method = spawn_methods_[method_index_];
        locked_method_label_->set_text("Method: " + method + " (locked)");
    }
}

void SpawnGroupsConfigPanel::refresh_quantity_control() {
    quantity_slider_ = std::make_unique<DMRangeSlider>(kQuantityMin, kQuantityMax, quantity_min_, quantity_max_);
    quantity_widget_ = std::make_unique<RangeSliderWidget>(quantity_slider_.get());
}

void SpawnGroupsConfigPanel::refresh_perimeter_control() {
    perimeter_slider_ = std::make_unique<DMSlider>("Perimeter Radius", 0, kMaxPerimeterRadius, std::max(0, perimeter_radius_));
    perimeter_widget_ = std::make_unique<SliderWidget>(perimeter_slider_.get());
}

void SpawnGroupsConfigPanel::refresh_area_controls() {
    std::string current_link = json_value_or(entry_, "link", std::string{});
    if (area_hint_label_) {
        area_hint_label_->set_text(current_link.empty() ? "Linked area: (none)" : "Linked area: " + current_link);
    }

    area_dropdown_options_.clear();
    area_dropdown_.reset();
    area_dropdown_widget_.reset();

    if (!area_names_provider_) {
        return;
    }

    area_names_ = area_names_provider_();
    area_dropdown_options_.push_back("(none)");
    area_dropdown_options_.insert(area_dropdown_options_.end(), area_names_.begin(), area_names_.end());

    int selected = 0;
    if (!current_link.empty()) {
        for (size_t i = 0; i < area_names_.size(); ++i) {
            if (area_names_[i] == current_link) {
                selected = static_cast<int>(i) + 1;
                break;
            }
        }
    }

    area_dropdown_ = std::make_unique<DMDropdown>("Linked Area", area_dropdown_options_, selected);
    area_dropdown_widget_ = std::make_unique<DropdownWidget>(area_dropdown_.get());
}

void SpawnGroupsConfigPanel::update_candidate_summary() {
    if (!candidate_summary_label_) {
        return;
    }
    int total = 0;
    for (const auto& row : candidates_) {
        total += clamp_chance(row->last_chance);
    }
    candidate_summary_label_->set_text("Total chance: " + std::to_string(total));
}

void SpawnGroupsConfigPanel::rebuild_rows() {
    rows_dirty_ = false;
    if (!panel_title_.empty()) {
        set_title(panel_title_);
    }

    DockableCollapsible::Rows rows;

    if (header_label_) {
        rows.push_back({ header_label_.get() });
    }

    if (persistence_warning_label_) {
        persistence_warning_label_->set_text(persistence_warning_text_);
        if (!persistence_warning_text_.empty()) {
            rows.push_back({ persistence_warning_label_.get() });
        }
    }

    if (!ownership_text_.empty() && ownership_label_) {
        ownership_label_->set_text(ownership_text_);
        if (has_ownership_color_) {
            ownership_label_->set_color(ownership_color_);
        } else {
            ownership_label_->clear_color_override();
        }
        rows.push_back({ ownership_label_.get() });
    }

    const bool show_perimeter = !spawn_methods_.empty() && method_index_ >= 0 && method_index_ < static_cast<int>(spawn_methods_.size()) && spawn_methods_[method_index_] == "Perimeter";

    if (method_locked_) {
        if (locked_method_label_) {
            rows.push_back({ locked_method_label_.get() });
        }
    } else if (method_widget_) {
        rows.push_back({ method_widget_.get() });
    }

    DockableCollapsible::Row toggles;
    if (overlap_widget_) {
        toggles.push_back(overlap_widget_.get());
    }
    if (spacing_widget_) {
        toggles.push_back(spacing_widget_.get());
    }
    if (!toggles.empty()) {
        rows.push_back(std::move(toggles));
    }

    if (!quantity_hidden_) {
        if (quantity_label_) {
            rows.push_back({ quantity_label_.get() });
        }
        if (quantity_widget_) {
            rows.push_back({ quantity_widget_.get() });
        }
    }

    if (show_perimeter && perimeter_widget_) {
        rows.push_back({ perimeter_widget_.get() });
    }

    if (area_names_provider_) {
        if (area_hint_label_) {
            rows.push_back({ area_hint_label_.get() });
        }
        DockableCollapsible::Row area_row;
        if (area_dropdown_widget_) {
            area_row.push_back(area_dropdown_widget_.get());
        }
        if (area_clear_widget_) {
            area_row.push_back(area_clear_widget_.get());
        }
        if (!area_row.empty()) {
            rows.push_back(std::move(area_row));
        }
    }

    update_candidate_summary();
    if (candidate_summary_label_) {
        rows.push_back({ candidate_summary_label_.get() });
    }

    for (auto& candidate : candidates_) {
        if (candidate->name_widget) {
            rows.push_back({ candidate->name_widget.get() });
        }
        DockableCollapsible::Row row;
        if (candidate->chance_widget) {
            row.push_back(candidate->chance_widget.get());
        }
        if (candidate->remove_widget) {
            row.push_back(candidate->remove_widget.get());
        }
        if (!row.empty()) {
            rows.push_back(std::move(row));
        }
    }

    DockableCollapsible::Row actions;
    if (add_candidate_widget_) {
        actions.push_back(add_candidate_widget_.get());
    }
    if (browse_assets_widget_) {
        actions.push_back(browse_assets_widget_.get());
    }
    if (!actions.empty()) {
        rows.push_back(std::move(actions));
    }

    set_rows(rows);
    update_asset_search_anchor();
}

void SpawnGroupsConfigPanel::request_rebuild_rows() {
    DockableCollapsible::set_rows(DockableCollapsible::Rows{});
    rows_dirty_ = true;
}

void SpawnGroupsConfigPanel::sync_candidates() {
    nlohmann::json array = nlohmann::json::array();
    int total = 0;
    bool updated = false;

    for (auto& candidate : candidates_) {
        if (!candidate->name_box || !candidate->chance_slider) {
            continue;
        }
        std::string name = candidate->name_box->value();
        int chance = clamp_chance(candidate->chance_slider->value());
        if (name != candidate->last_name || chance != candidate->last_chance) {
            updated = true;
        }
        candidate->last_name = std::move(name);
        candidate->last_chance = chance;
        total += chance;
        array.push_back({ {"name", candidate->last_name}, {"chance", candidate->last_chance} });
    }

    if (!entry_.contains("candidates") || entry_["candidates"] != array) {
        entry_["candidates"] = array;
        mark_dirty();
    }

    if (json_value_or(entry_, "chance_denominator", std::numeric_limits<int>::min()) != total) {
        entry_["chance_denominator"] = total;
        mark_dirty();
    }

    if (updated) {
        mark_dirty();
    }

    if (candidate_summary_label_) {
        candidate_summary_label_->set_text("Total chance: " + std::to_string(total));
    }
}

void SpawnGroupsConfigPanel::sync_from_widgets() {
    if (!method_locked_ && method_dropdown_) {
        int selected = std::clamp(method_dropdown_->selected(), 0, static_cast<int>(spawn_methods_.size()) - 1);
        if (selected != method_index_) {
            method_index_ = selected;
            const std::string method = spawn_methods_[method_index_];
            entry_["position"] = method;
            mark_dirty();
            if (method != baseline_method_) {
                pending_summary_.method_changed = true;
                pending_summary_.method = method;
                baseline_method_ = method;
            }
            refresh_perimeter_control();
            rebuild_rows();
        }
    } else if (method_locked_) {
        const std::string method = forced_method_.empty() ? spawn_methods_.front() : forced_method_;
        entry_["position"] = method;
        pending_summary_.method = method;
    }

    if (!quantity_hidden_ && quantity_slider_) {
        int min_val = clamp_quantity(quantity_slider_->min_value());
        int max_val = clamp_quantity(quantity_slider_->max_value());
        if (min_val != quantity_min_ || max_val != quantity_max_) {
            quantity_min_ = min_val;
            quantity_max_ = max_val;
            entry_["min_number"] = quantity_min_;
            entry_["max_number"] = quantity_max_;
            mark_dirty();
            if (quantity_min_ != baseline_min_ || quantity_max_ != baseline_max_) {
                pending_summary_.quantity_changed = true;
                baseline_min_ = quantity_min_;
                baseline_max_ = quantity_max_;
            }
        }
    }

    if (overlap_checkbox_) {
        bool value = overlap_checkbox_->value();
        if (value != overlap_enabled_) {
            overlap_enabled_ = value;
            entry_["check_overlap"] = overlap_enabled_;
            mark_dirty();
        }
    }

    if (spacing_checkbox_) {
        bool value = spacing_checkbox_->value();
        if (value != spacing_enabled_) {
            spacing_enabled_ = value;
            entry_["enforce_spacing"] = spacing_enabled_;
            mark_dirty();
        }
    }

    if (perimeter_slider_) {
        int radius = std::max(0, perimeter_slider_->value());
        if (radius != perimeter_radius_) {
            perimeter_radius_ = radius;
            entry_["radius"] = perimeter_radius_;
            mark_dirty();
        }
    }

    if (area_dropdown_ && !area_dropdown_options_.empty()) {
        int selected = std::clamp(area_dropdown_->selected(), 0, static_cast<int>(area_dropdown_options_.size()) - 1);
        std::string current = json_value_or(entry_, "link", std::string{});
        std::string desired;
        if (selected > 0 && selected < static_cast<int>(area_dropdown_options_.size())) {
            desired = area_dropdown_options_[selected];
        }
        if (desired != current) {
            if (desired.empty()) {
                entry_.erase("link");
            } else {
                entry_["link"] = desired;
            }
            if (area_hint_label_) {
                area_hint_label_->set_text(desired.empty() ? "Linked area: (none)" : "Linked area: " + desired);
            }
            mark_dirty();
        }
    }

    sync_candidates();
}

void SpawnGroupsConfigPanel::open(const nlohmann::json& data, std::function<void(const nlohmann::json&)> on_save) {
    load(data);
    on_save_callback_ = std::move(on_save);
    dirty_ = false;
    open_panel();
}

void SpawnGroupsConfigPanel::set_screen_dimensions(int width, int height) {
    if (width > 0) {
        screen_w_ = width;
    }
    if (height > 0) {
        screen_h_ = height;
    }
    if (screen_w_ <= 0) {
        screen_w_ = kDefaultScreenW;
    }
    if (screen_h_ <= 0) {
        screen_h_ = kDefaultScreenH;
    }
    set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    if (asset_search_) {
        asset_search_->set_screen_dimensions(screen_w_, screen_h_);
        update_asset_search_anchor();
    }
    clamp_to_screen();
}

void SpawnGroupsConfigPanel::open_panel() {
    if (!has_custom_position_) {
        DockableCollapsible::set_position(default_position_.x, default_position_.y);
    }

    auto close_cb = [this]() { this->close(); };
    const std::string title = panel_title_.empty() ? std::string("Spawn Group") : panel_title_;
    if (floating_stack_key_.empty()) {
        FloatingDockableManager::instance().open_floating(title, this, close_cb);
    } else {
        FloatingDockableManager::instance().open_floating(title, this, close_cb, floating_stack_key_);
    }

    set_visible(true);
    set_expanded(true);
    clamp_to_screen();
    force_pointer_ready();
    Input dummy;
    update(dummy, screen_w_, screen_h_);
}

void SpawnGroupsConfigPanel::notify_close_listeners() {
    if (on_close_callback_) {
        on_close_callback_();
    }
    for (const auto& entry : close_callbacks_) {
        if (entry.cb) {
            entry.cb();
        }
    }
}

void SpawnGroupsConfigPanel::close() {
    if (!is_visible()) {
        return;
    }
    dispatch_save();
    dirty_ = false;
    set_visible(false);
    if (asset_search_) {
        asset_search_->close();
    }
    notify_close_listeners();
}

bool SpawnGroupsConfigPanel::visible() const { return is_visible(); }

bool SpawnGroupsConfigPanel::is_open() const { return is_visible(); }

void SpawnGroupsConfigPanel::set_position(int x, int y) {
    has_custom_position_ = true;
    DockableCollapsible::set_position(x, y);
    clamp_to_screen();
    update_asset_search_anchor();
}

SDL_Point SpawnGroupsConfigPanel::position() const { return DockableCollapsible::position(); }

void SpawnGroupsConfigPanel::clamp_to_screen() {
    SDL_Point pos = DockableCollapsible::position();
    SDL_Rect bounds = rect();
    if (screen_w_ <= 0 || screen_h_ <= 0) {
        return;
    }
    int clamped_x = std::clamp(pos.x, 0, std::max(0, screen_w_ - bounds.w));
    int clamped_y = std::clamp(pos.y, 0, std::max(0, screen_h_ - bounds.h));
    DockableCollapsible::set_position(clamped_x, clamped_y);
}

void SpawnGroupsConfigPanel::update(const Input& input, int screen_w, int screen_h) {
    if (screen_w > 0) {
        screen_w_ = screen_w;
    }
    if (screen_h > 0) {
        screen_h_ = screen_h;
    }

    if (rows_dirty_) {
        rebuild_rows();
    }

    DockableCollapsible::update(input, screen_w_, screen_h_);
    SDL_Point pos = DockableCollapsible::position();
    if (pos.x != default_position_.x || pos.y != default_position_.y) {
        has_custom_position_ = true;
    }

    sync_from_widgets();
    if (dirty_) {
        dispatch_save();
        dirty_ = false;
    }

    if (asset_search_) {
        asset_search_->update(input);
    }
    update_asset_search_anchor();
}

bool SpawnGroupsConfigPanel::handle_event(const SDL_Event& e) {
    if (asset_search_ && asset_search_->visible()) {
        if (asset_search_->handle_event(e)) {
            return true;
        }
        switch (e.type) {
        case SDL_MOUSEMOTION: {
            if (asset_search_->is_point_inside(e.motion.x, e.motion.y)) {
                return true;
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (asset_search_->is_point_inside(e.button.x, e.button.y)) {
                return true;
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            if (asset_search_->is_point_inside(mx, my)) {
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    if (DockableCollapsible::handle_event(e)) {
        return true;
    }
    return false;
}

void SpawnGroupsConfigPanel::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
    DMDropdown::render_active_options(r);
    if (asset_search_) {
        asset_search_->render(r);
    }
}

nlohmann::json SpawnGroupsConfigPanel::to_json() const { return entry_; }

bool SpawnGroupsConfigPanel::is_point_inside(int x, int y) const {
    if (DockableCollapsible::is_point_inside(x, y)) {
        return true;
    }
    if (asset_search_ && asset_search_->visible() && asset_search_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

SDL_Rect SpawnGroupsConfigPanel::rect() const { return DockableCollapsible::rect(); }

SpawnGroupsConfigPanel::ChangeSummary SpawnGroupsConfigPanel::consume_change_summary() {
    ChangeSummary out = pending_summary_;
    if (out.method.empty()) {
        out.method = baseline_method_;
    }
    pending_summary_ = {};
    pending_summary_.method = baseline_method_;
    return out;
}

void SpawnGroupsConfigPanel::set_ownership_label(const std::string& label, SDL_Color color) {
    ownership_text_ = label;
    ownership_color_ = color;
    has_ownership_color_ = true;
    if (ownership_label_) {
        ownership_label_->set_text(label);
        ownership_label_->set_color(color);
    }
    rebuild_rows();
}

void SpawnGroupsConfigPanel::lock_method_to(const std::string& method) {
    forced_method_ = method;
    method_locked_ = true;
    auto it = std::find(spawn_methods_.begin(), spawn_methods_.end(), forced_method_);
    method_index_ = (it != spawn_methods_.end()) ? static_cast<int>(std::distance(spawn_methods_.begin(), it)) : 0;
    baseline_method_ = spawn_methods_[method_index_];
    entry_["position"] = baseline_method_;
    pending_summary_.method = baseline_method_;
    refresh_method_control();
    rebuild_rows();
}

void SpawnGroupsConfigPanel::set_quantity_hidden(bool hidden) {
    quantity_hidden_ = hidden;
    rebuild_rows();
}

void SpawnGroupsConfigPanel::set_on_close(std::function<void()> cb) { on_close_callback_ = std::move(cb); }

size_t SpawnGroupsConfigPanel::add_on_close_callback(std::function<void()> cb) {
    CloseCallbackEntry entry;
    entry.id = next_close_callback_id_++;
    entry.cb = std::move(cb);
    close_callbacks_.push_back(std::move(entry));
    return close_callbacks_.back().id;
}

void SpawnGroupsConfigPanel::remove_on_close_callback(size_t handle) {
    close_callbacks_.erase(std::remove_if(close_callbacks_.begin(), close_callbacks_.end(),
                                          [handle](const CloseCallbackEntry& entry) { return entry.id == handle; }),
                           close_callbacks_.end());
}

void SpawnGroupsConfigPanel::clear_on_close_callbacks() { close_callbacks_.clear(); }

void SpawnGroupsConfigPanel::set_floating_stack_key(std::string key) {
    floating_stack_key_ = std::move(key);
    if (asset_search_) {
        asset_search_->set_floating_stack_key(floating_stack_key_);
    }
}

void SpawnGroupsConfigPanel::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    area_names_provider_ = std::move(provider);
    refresh_area_controls();
    rebuild_rows();
}

void SpawnGroupsConfigPanel::set_persistence_warning(const std::string& message) {
    persistence_warning_text_ = message;
    if (persistence_warning_label_) {
        persistence_warning_label_->set_text(persistence_warning_text_);
    }
    request_rebuild_rows();
}

void SpawnGroupsConfigPanel::load(const nlohmann::json& asset) {
    entry_ = asset;
    dirty_ = false;
    spawn_id_ = fallback_spawn_id(asset);
    panel_title_ = spawn_id_.empty() ? std::string("Spawn Group") : std::string("Spawn Group: ") + spawn_id_;
    if (header_label_) {
        header_label_->set_text("ID: " + spawn_id_);
    }

    if (ownership_label_) {
        ownership_label_->set_text(ownership_text_);
        if (has_ownership_color_) {
            ownership_label_->set_color(ownership_color_);
        } else {
            ownership_label_->clear_color_override();
        }
    }

    const std::string method = json_value_or(asset, "position", spawn_methods_.empty() ? std::string{} : spawn_methods_.front());
    auto it = std::find(spawn_methods_.begin(), spawn_methods_.end(), method);
    method_index_ = (it != spawn_methods_.end()) ? static_cast<int>(std::distance(spawn_methods_.begin(), it)) : 0;
    baseline_method_ = spawn_methods_[method_index_];
    pending_summary_ = {};
    pending_summary_.method = baseline_method_;

    quantity_min_ = clamp_quantity(json_value_or(asset, "min_number", kQuantityMin));
    quantity_max_ = clamp_quantity(json_value_or(asset, "max_number", std::max(quantity_min_, kQuantityMin)));
    if (quantity_min_ > quantity_max_) {
        std::swap(quantity_min_, quantity_max_);
    }
    baseline_min_ = quantity_min_;
    baseline_max_ = quantity_max_;

    overlap_enabled_ = json_value_or(asset, "check_overlap", false);
    spacing_enabled_ = json_value_or(asset, "enforce_spacing", false);
    perimeter_radius_ = std::max(0, json_value_or(asset, "radius", json_value_or(asset, "perimeter_radius", 0)));

    if (overlap_checkbox_) {
        overlap_checkbox_->set_value(overlap_enabled_);
    }
    if (spacing_checkbox_) {
        spacing_checkbox_->set_value(spacing_enabled_);
    }

    refresh_method_control();
    refresh_quantity_control();
    refresh_perimeter_control();

    candidates_.clear();
    if (asset.contains("candidates") && asset["candidates"].is_array()) {
        for (const auto& candidate : asset["candidates"]) {
            std::string name = json_value_or(candidate, "name", std::string{});
            int chance = json_value_or(candidate, "chance", 0);
            add_candidate_row(name, chance);
        }
    }
    ensure_candidate_rows();

    refresh_area_controls();
    rebuild_rows();
    sync_candidates();
}

void SpawnGroupsConfigPanel::dispatch_save() {
    if (on_save_callback_) {
        on_save_callback_(entry_);
    }
}

void SpawnGroupsConfigPanel::mark_dirty() { dirty_ = true; }

void SpawnGroupsConfigPanel::open_asset_search() {
    if (!asset_search_) {
        asset_search_ = std::make_unique<SearchAssets>();
        if (!floating_stack_key_.empty()) {
            asset_search_->set_floating_stack_key(floating_stack_key_);
        }
    }
    asset_search_->set_screen_dimensions(screen_w_, screen_h_);
    update_asset_search_anchor();
    asset_search_->open([this](const std::string& selection) {
        if (selection.empty() || (selection.front() == '#')) {
            return;
        }
        add_candidate_row(selection, 0);
        request_rebuild_rows();
    });
}

void SpawnGroupsConfigPanel::update_asset_search_anchor() {
    if (!asset_search_) {
        return;
    }
    SDL_Rect bounds = rect();
    int anchor_x = bounds.x + bounds.w + DMSpacing::item_gap();
    int anchor_y = bounds.y + DMSpacing::panel_padding();
    asset_search_->set_anchor_position(anchor_x, anchor_y);
}

void SpawnGroupsConfigPanel::reset_area_selection() {
    if (entry_.contains("link")) {
        entry_.erase("link");
        mark_dirty();
    }
    if (area_dropdown_) {
        area_dropdown_ = nullptr;
        area_dropdown_widget_ = nullptr;
    }
    refresh_area_controls();
    request_rebuild_rows();
}

