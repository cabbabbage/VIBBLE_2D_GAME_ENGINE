#include "spawn_group_config_ui.hpp"

#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "widgets.hpp"
#include "search_assets.hpp"

#include <algorithm>
#include <utility>

#include <SDL_ttf.h>

constexpr int kDefaultScreenW = 1920;
constexpr int kDefaultScreenH = 1080;
constexpr int kQuantityMin = 1;
constexpr int kQuantityMax = 1000;
constexpr int kChanceMin = 0;
constexpr int kChanceMax = 1000;

class LabelWidget : public Widget {
public:
    LabelWidget() = default;
    explicit LabelWidget(std::string text) : text_(std::move(text)) {}

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    void set_color(SDL_Color color) {
        color_ = color;
        has_color_override_ = true;
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        const DMLabelStyle& st = DMStyles::Label();
        TTF_Font* font = st.open_font();
        if (!font) {
            return st.font_size + 4;
        }
        int line_height = st.font_size + 2;
        int total = 0;
        int current = 0;
        for (char c : text_) {
            if (c == '\n') {
                total += line_height;
                current = 0;
                continue;
            }
            current += st.font_size;
            if (current >= w && w > 0) {
                total += line_height;
                current = 0;
            }
        }
        total += line_height;
        TTF_CloseFont(font);
        return std::max(line_height, total);
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* r) const override {
        const DMLabelStyle& st = DMStyles::Label();
        SDL_Color color = has_color_override_ ? color_ : st.color;
        TTF_Font* font = st.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color, std::max(10, rect_.w));
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (tex) {
                SDL_Rect dst{ rect_.x, rect_.y, surf->w, surf->h };
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

    void clear_color_override() {
        has_color_override_ = false;
        color_ = DMStyles::Label().color;
    }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 255, 255, 255};
    bool has_color_override_ = false;
};

namespace {
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
}

SpawnGroupsConfigPanel::SpawnGroupsConfigPanel(int start_x, int start_y)
    : DockableCollapsible("Spawn Group", true, start_x, start_y) {
    spawn_methods_ = {"Random", "Center", "Perimeter", "Exact", "Percent", "Entrance", "Exit"};
    panel_title_ = "Spawn Group";
    set_visible(false);
    set_expanded(true);
    set_scroll_enabled(true);
    set_cell_width(240);
    set_available_height_override(560);
    set_close_button_enabled(true);
    set_work_area(SDL_Rect{0, 0, kDefaultScreenW, kDefaultScreenH});

    header_label_ = std::make_unique<LabelWidget>();
    ownership_label_ = std::make_unique<LabelWidget>();
    locked_method_label_ = std::make_unique<LabelWidget>();
    quantity_label_ = std::make_unique<LabelWidget>("Quantity");
    candidate_summary_label_ = std::make_unique<LabelWidget>("Total chance: 0");

    overlap_checkbox_ = std::make_unique<DMCheckbox>("Check overlap", false);
    overlap_widget_ = std::make_unique<CheckboxWidget>(overlap_checkbox_.get());
    spacing_checkbox_ = std::make_unique<DMCheckbox>("Enforce spacing", false);
    spacing_widget_ = std::make_unique<CheckboxWidget>(spacing_checkbox_.get());

    add_candidate_button_ = std::make_unique<DMButton>("Add Candidate", &DMStyles::CreateButton(), 150, DMButton::height());
    add_candidate_widget_ = std::make_unique<ButtonWidget>(add_candidate_button_.get(), [this]() {
        if (!asset_search_) {
            asset_search_ = std::make_unique<SearchAssets>();
        }
        if (!asset_search_) {
            return;
        }
        asset_search_->set_screen_dimensions(screen_w_, screen_h_);
        SDL_Point pos = position();
        SDL_Rect bounds = rect();
        int anchor_x = pos.x + bounds.w + DMSpacing::item_gap();
        int anchor_y = pos.y + DMSpacing::panel_padding();
        asset_search_->set_anchor_position(anchor_x, anchor_y);
        asset_search_->open([this](const std::string& selection) {
            if (selection.empty()) {
                return;
            }
            if (!selection.empty() && selection.front() == '#') {
                return;
            }
            add_candidate(selection, 0);
            rebuild_layout();
            sync_candidates();
        });
    });

    done_button_ = std::make_unique<DMButton>("Save & Close", &DMStyles::ListButton(), 140, DMButton::height());
    done_widget_ = std::make_unique<ButtonWidget>(done_button_.get(), [this]() {
        dispatch_save();
        close();
    });

    // Link/Unlink area buttons are created lazily in rebuild_layout() when provider is present

    rebuild_method_widget();
    rebuild_quantity_widget();
    rebuild_perimeter_widget();
    rebuild_candidate_summary();
    rebuild_layout();
}

SpawnGroupsConfigPanel::~SpawnGroupsConfigPanel() = default;

void SpawnGroupsConfigPanel::rebuild_method_widget() {
    method_dropdown_ = std::make_unique<DMDropdown>("Method", spawn_methods_, method_index_);
    method_widget_ = std::make_unique<DropdownWidget>(method_dropdown_.get());
}

void SpawnGroupsConfigPanel::rebuild_quantity_widget() {
    quantity_slider_ = std::make_unique<DMRangeSlider>(kQuantityMin, kQuantityMax, quantity_min_, quantity_max_);
    quantity_widget_ = std::make_unique<RangeSliderWidget>(quantity_slider_.get());
}

void SpawnGroupsConfigPanel::rebuild_perimeter_widget() {
    perimeter_slider_ = std::make_unique<DMSlider>("Perimeter Radius", 0, 10000, std::max(0, perimeter_radius_));
    perimeter_widget_ = std::make_unique<SliderWidget>(perimeter_slider_.get());
}

void SpawnGroupsConfigPanel::rebuild_candidate_summary() {
    int total = 0;
    for (const auto& c : candidates_) {
        total += clamp_chance(c->last_chance);
    }
    candidate_summary_label_->set_text("Total chance: " + std::to_string(total));
}

void SpawnGroupsConfigPanel::ensure_candidate_controls() {
    for (auto& row : candidates_) {
        if (!row->name_box) {
            row->name_box = std::make_unique<DMTextBox>("Asset", row->last_name);
            row->name_widget = std::make_unique<TextBoxWidget>(row->name_box.get(), true);
        }
        if (!row->chance_slider) {
            row->chance_slider = std::make_unique<DMSlider>("Chance", kChanceMin, kChanceMax, clamp_chance(row->last_chance));
            row->chance_widget = std::make_unique<SliderWidget>(row->chance_slider.get());
        }
        if (!row->remove_button) {
            row->remove_button = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(), 90, DMButton::height());
            CandidateRow* target = row.get();
            row->remove_widget = std::make_unique<ButtonWidget>(row->remove_button.get(), [this, target]() {
                remove_candidate(target);
            });
        }
    }
}

void SpawnGroupsConfigPanel::rebuild_layout() {
    ensure_candidate_controls();
    rebuild_candidate_summary();

    DockableCollapsible::Rows rows;

    if (!panel_title_.empty()) {
        set_title(panel_title_);
    }

    if (header_label_) {
        rows.push_back({ header_label_.get() });
    }

    if (!ownership_text_.empty() && ownership_label_) {
        rows.push_back({ ownership_label_.get() });
    }

    const std::string current_method = (method_index_ >= 0 && method_index_ < static_cast<int>(spawn_methods_.size()))
                                           ? spawn_methods_[method_index_]
                                           : std::string{};

    if (method_locked_) {
        if (locked_method_label_) {
            locked_method_label_->set_text("Method: " + current_method + " (locked)");
            rows.push_back({ locked_method_label_.get() });
        }
    } else if (method_widget_) {
        rows.push_back({ method_widget_.get() });
    }

    DockableCollapsible::Row checkbox_row;
    if (overlap_widget_) checkbox_row.push_back(overlap_widget_.get());
    if (spacing_widget_) checkbox_row.push_back(spacing_widget_.get());
    if (!checkbox_row.empty()) {
        rows.push_back(checkbox_row);
    }

    if (!quantity_hidden_) {
        if (quantity_label_) {
            rows.push_back({ quantity_label_.get() });
        }
        if (quantity_widget_) {
            rows.push_back({ quantity_widget_.get() });
        }
    }

    if (current_method == "Perimeter" && perimeter_widget_) {
        rows.push_back({ perimeter_widget_.get() });
    }

    if (candidate_summary_label_) {
        rows.push_back({ candidate_summary_label_.get() });
    }

    // Link/Unlink area row (only if a provider is set)
    if (area_names_provider_) {
        bool has_link = false;
        if (entry_.contains("link") && entry_["link"].is_string()) {
            const std::string v = entry_["link"].get<std::string>();
            has_link = !v.empty();
        }
        if (has_link) {
            if (!unlink_area_button_) {
                unlink_area_button_ = std::make_unique<DMButton>("Unlink", &DMStyles::DeleteButton(), 90, DMButton::height());
                unlink_area_widget_ = std::make_unique<ButtonWidget>(unlink_area_button_.get(), [this]() {
                    if (entry_.contains("link")) entry_.erase("link");
                    rebuild_layout();
                });
            }
            if (unlink_area_widget_) rows.push_back({ unlink_area_widget_.get() });
        } else {
            if (!link_area_button_) {
                link_area_button_ = std::make_unique<DMButton>("Link to area", &DMStyles::CreateButton(), 140, DMButton::height());
                link_area_widget_ = std::make_unique<ButtonWidget>(link_area_button_.get(), [this]() {
                    if (!area_names_provider_) return;
                    std::vector<std::string> names = area_names_provider_();
                    if (names.empty()) return;
                    if (!area_picker_) area_picker_ = std::make_unique<AreaPicker>();
                    area_picker_->set_screen_dimensions(screen_w_, screen_h_);
                    SDL_Point pos = position();
                    SDL_Rect bounds = rect();
                    int anchor_x = pos.x + bounds.w + DMSpacing::item_gap();
                    int anchor_y = pos.y + DMSpacing::panel_padding();
                    area_picker_->set_anchor_position(anchor_x, anchor_y);
                    area_picker_->open(names, [this](const std::string& selected) {
                        if (!selected.empty()) {
                            entry_["link"] = selected;
                            rebuild_layout();
                        }
                    });
                });
            }
            if (link_area_widget_) rows.push_back({ link_area_widget_.get() });
        }
    }

    for (auto& candidate : candidates_) {
        DockableCollapsible::Row name_row;
        if (candidate->name_widget) {
            name_row.push_back(candidate->name_widget.get());
        }
        if (!name_row.empty()) {
            rows.push_back(name_row);
        }
        DockableCollapsible::Row chance_row;
        if (candidate->chance_widget) {
            chance_row.push_back(candidate->chance_widget.get());
        }
        if (candidate->remove_widget) {
            chance_row.push_back(candidate->remove_widget.get());
        }
        if (!chance_row.empty()) {
            rows.push_back(chance_row);
        }
    }

    if (add_candidate_widget_) {
        rows.push_back({ add_candidate_widget_.get() });
    }

    if (done_widget_) {
        rows.push_back({ done_widget_.get() });
    }

    set_rows(rows);
}

void SpawnGroupsConfigPanel::add_candidate(const std::string& name, int chance) {
    auto row = std::make_unique<CandidateRow>();
    row->last_name = name;
    row->last_chance = clamp_chance(chance);
    candidates_.push_back(std::move(row));
    ensure_candidate_controls();
}

void SpawnGroupsConfigPanel::remove_candidate(const CandidateRow* row) {
    candidates_.erase(std::remove_if(candidates_.begin(), candidates_.end(),
                                     [row](const std::unique_ptr<CandidateRow>& ptr) { return ptr.get() == row; }),
                      candidates_.end());
    rebuild_candidate_summary();
    rebuild_layout();
    sync_candidates();
}

void SpawnGroupsConfigPanel::sync_candidates() {
    nlohmann::json array = nlohmann::json::array();
    int total = 0;
    for (auto& candidate : candidates_) {
        std::string name = candidate->name_box ? candidate->name_box->value() : candidate->last_name;
        int chance = candidate->chance_slider ? candidate->chance_slider->value() : candidate->last_chance;
        candidate->last_name = name;
        candidate->last_chance = clamp_chance(chance);
        total += candidate->last_chance;
        array.push_back({ {"name", name}, {"chance", candidate->last_chance} });
    }
    entry_["candidates"] = array;
    entry_["chance_denominator"] = total;
    candidate_summary_label_->set_text("Total chance: " + std::to_string(total));
}

void SpawnGroupsConfigPanel::sync_from_widgets() {
    if (!method_locked_ && method_dropdown_) {
        int selected = method_dropdown_->selected();
        selected = std::clamp(selected, 0, static_cast<int>(spawn_methods_.size()) - 1);
        if (selected != method_index_) {
            method_index_ = selected;
            const std::string method = spawn_methods_[method_index_];
            entry_["position"] = method;
            if (method != baseline_method_) {
                pending_summary_.method_changed = true;
                pending_summary_.method = method;
                baseline_method_ = method;
            }
            rebuild_layout();
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
        }
    }

    if (spacing_checkbox_) {
        bool value = spacing_checkbox_->value();
        if (value != spacing_enabled_) {
            spacing_enabled_ = value;
            entry_["enforce_spacing"] = spacing_enabled_;
        }
    }

    if (perimeter_slider_) {
        int radius = std::max(0, perimeter_slider_->value());
        if (radius != perimeter_radius_) {
            perimeter_radius_ = radius;
            entry_["radius"] = perimeter_radius_;
        }
    }

    sync_candidates();
}

void SpawnGroupsConfigPanel::load(const nlohmann::json& asset) {
    entry_ = asset;
    save_dispatched_ = false;
    spawn_id_ = fallback_spawn_id(asset);
    panel_title_ = "Spawn Group";
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

    const std::string method = asset.value("position", spawn_methods_.empty() ? std::string{} : spawn_methods_.front());
    auto it = std::find(spawn_methods_.begin(), spawn_methods_.end(), method);
    method_index_ = (it != spawn_methods_.end()) ? static_cast<int>(std::distance(spawn_methods_.begin(), it)) : 0;
    baseline_method_ = spawn_methods_[method_index_];
    pending_summary_ = {};
    pending_summary_.method = baseline_method_;

    quantity_min_ = clamp_quantity(asset.value("min_number", kQuantityMin));
    quantity_max_ = clamp_quantity(asset.value("max_number", std::max(quantity_min_, kQuantityMin)));
    baseline_min_ = quantity_min_;
    baseline_max_ = quantity_max_;

    overlap_enabled_ = asset.value("check_overlap", false);
    spacing_enabled_ = asset.value("enforce_spacing", false);
    perimeter_radius_ = asset.value("radius", asset.value("perimeter_radius", 0));
    if (perimeter_radius_ < 0) perimeter_radius_ = 0;

    if (overlap_checkbox_) overlap_checkbox_->set_value(overlap_enabled_);
    if (spacing_checkbox_) spacing_checkbox_->set_value(spacing_enabled_);

    rebuild_method_widget();
    rebuild_quantity_widget();
    rebuild_perimeter_widget();

    candidates_.clear();
    if (asset.contains("candidates") && asset["candidates"].is_array()) {
        for (const auto& candidate : asset["candidates"]) {
            std::string name = candidate.value("name", std::string{});
            int chance = candidate.value("chance", 0);
            add_candidate(name, chance);
        }
    }

    if (candidates_.empty()) {
        add_candidate("", 0);
    }

    rebuild_layout();
    sync_candidates();
}

void SpawnGroupsConfigPanel::open(const nlohmann::json& data, std::function<void(const nlohmann::json&)> on_save) {
    load(data);
    on_save_callback_ = std::move(on_save);
    save_dispatched_ = false;
    open_panel();
}

void SpawnGroupsConfigPanel::set_screen_dimensions(int width, int height) {
    if (width > 0) screen_w_ = width;
    if (height > 0) screen_h_ = height;
    if (screen_w_ <= 0) screen_w_ = kDefaultScreenW;
    if (screen_h_ <= 0) screen_h_ = kDefaultScreenH;
    set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    if (asset_search_) {
        asset_search_->set_screen_dimensions(screen_w_, screen_h_);
    }
    if (area_picker_) {
        area_picker_->set_screen_dimensions(screen_w_, screen_h_);
    }
    clamp_to_screen();
}

void SpawnGroupsConfigPanel::open_panel() {
    set_visible(true);
    set_expanded(true);
    clamp_to_screen();
}

void SpawnGroupsConfigPanel::notify_close_listeners() {
    if (on_close_callback_) {
        on_close_callback_();
    }
    for (const auto& entry : close_callbacks_) {
        if (entry.cb) entry.cb();
    }
}

void SpawnGroupsConfigPanel::close() {
    if (!is_visible()) {
        return;
    }
    dispatch_save();
    set_visible(false);
    if (asset_search_) {
        asset_search_->close();
    }
    notify_close_listeners();
}

bool SpawnGroupsConfigPanel::visible() const { return is_visible(); }

bool SpawnGroupsConfigPanel::is_open() const { return is_visible(); }

void SpawnGroupsConfigPanel::set_position(int x, int y) {
    DockableCollapsible::set_position(x, y);
    clamp_to_screen();
    if (asset_search_) {
        SDL_Point pos = DockableCollapsible::position();
        SDL_Rect bounds = rect();
        int anchor_x = pos.x + bounds.w + DMSpacing::item_gap();
        int anchor_y = pos.y + DMSpacing::panel_padding();
        asset_search_->set_anchor_position(anchor_x, anchor_y);
    }
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
    if (screen_w > 0) screen_w_ = screen_w;
    if (screen_h > 0) screen_h_ = screen_h;
    DockableCollapsible::update(input, screen_w_, screen_h_);
    sync_from_widgets();
    if (asset_search_) {
        asset_search_->update(input);
    }
    if (area_picker_) {
        area_picker_->update(input);
    }
}

bool SpawnGroupsConfigPanel::handle_event(const SDL_Event& e) {
    if (area_picker_ && area_picker_->visible()) {
        if (area_picker_->handle_event(e)) {
            return true;
        }

        switch (e.type) {
        case SDL_MOUSEMOTION: {
            if (area_picker_->is_point_inside(e.motion.x, e.motion.y)) {
                return true;
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (area_picker_->is_point_inside(e.button.x, e.button.y)) {
                return true;
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            if (area_picker_->is_point_inside(mx, my)) {
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
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
    if (area_picker_) {
        area_picker_->render(r);
    }
    if (asset_search_) {
        asset_search_->render(r);
    }
}

nlohmann::json SpawnGroupsConfigPanel::to_json() const { return entry_; }

bool SpawnGroupsConfigPanel::is_point_inside(int x, int y) const {
    if (DockableCollapsible::is_point_inside(x, y)) {
        return true;
    }
    if (area_picker_ && area_picker_->visible() && area_picker_->is_point_inside(x, y)) {
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
    rebuild_layout();
}

void SpawnGroupsConfigPanel::lock_method_to(const std::string& method) {
    forced_method_ = method;
    method_locked_ = true;
    auto it = std::find(spawn_methods_.begin(), spawn_methods_.end(), forced_method_);
    method_index_ = (it != spawn_methods_.end()) ? static_cast<int>(std::distance(spawn_methods_.begin(), it)) : 0;
    baseline_method_ = spawn_methods_[method_index_];
    entry_["position"] = baseline_method_;
    rebuild_layout();
}

void SpawnGroupsConfigPanel::set_quantity_hidden(bool hidden) {
    quantity_hidden_ = hidden;
    rebuild_layout();
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

void SpawnGroupsConfigPanel::set_floating_stack_key(std::string key) { floating_stack_key_ = std::move(key); }

void SpawnGroupsConfigPanel::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    area_names_provider_ = std::move(provider);
    rebuild_layout();
}

void SpawnGroupsConfigPanel::dispatch_save() {
    if (!save_dispatched_ && on_save_callback_) {
        on_save_callback_(entry_);
        save_dispatched_ = true;
    }
}

// -----------------------------
// Simple Area Picker
// -----------------------------
struct SpawnGroupsConfigPanel::AreaPicker {
    using Callback = std::function<void(const std::string&)>;
    void set_screen_dimensions(int w, int h) { screen_w_ = w; screen_h_ = h; }
    void set_anchor_position(int x, int y) {
        anchor_pos_ = {x, y};
        has_anchor_ = true;
        apply_position(x, y);
        ensure_visible_position();
    }
    void open(const std::vector<std::string>& options, Callback cb) {
        options_ = options;
        cb_ = std::move(cb);
        if (!panel_) {
            panel_ = std::make_unique<DockableCollapsible>("Select Area", true, anchor_pos_.x, anchor_pos_.y);
            panel_->set_expanded(true);
            panel_->set_visible(false);
            panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
            panel_->set_close_button_enabled(true);
            panel_->set_scroll_enabled(true);
            panel_->set_cell_width(220);
        }
        rebuild_buttons();
        panel_->set_visible(true);
        panel_->set_expanded(true);
        Input dummy;
        panel_->update(dummy, screen_w_, screen_h_);
        ensure_visible_position();
    }
    void close() {
        if (panel_) panel_->set_visible(false);
        cb_ = nullptr;
    }
    bool visible() const { return panel_ && panel_->is_visible(); }
    void update(const Input& input) {
        if (panel_ && panel_->is_visible()) panel_->update(input, screen_w_, screen_h_);
    }
    bool handle_event(const SDL_Event& e) {
        if (!panel_ || !panel_->is_visible()) return false;
        SDL_Point before = panel_->position();
        bool used = panel_->handle_event(e);
        SDL_Point after = panel_->position();
        if (after.x != before.x || after.y != before.y) ensure_visible_position();
        return used;
    }
    void render(SDL_Renderer* r) const { if (panel_ && panel_->is_visible()) panel_->render(r); }
    bool is_point_inside(int x, int y) const { return panel_ && panel_->is_visible() && panel_->is_point_inside(x, y); }
private:
    void apply_position(int x, int y) {
        if (!panel_) {
            panel_ = std::make_unique<DockableCollapsible>("Select Area", true, x, y);
            panel_->set_expanded(true);
            panel_->set_visible(false);
            panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
            panel_->set_close_button_enabled(true);
            panel_->set_scroll_enabled(true);
            panel_->set_cell_width(220);
        }
        panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
        panel_->set_position(x, y);
    }
    void ensure_visible_position() {
        if (!panel_) return;
        SDL_Rect rect = panel_->rect();
        const int margin = 12;
        int x = rect.x;
        int y = rect.y;
        if (screen_w_ > 0) {
            int max_x = std::max(margin, screen_w_ - rect.w - margin);
            x = std::clamp(x, margin, max_x);
        }
        if (screen_h_ > 0) {
            int max_y = std::max(margin, screen_h_ - rect.h - margin);
            y = std::clamp(y, margin, max_y);
        }
        panel_->set_position(x, y);
    }
    void rebuild_buttons() {
        buttons_.clear();
        button_widgets_.clear();
        DockableCollapsible::Rows rows;
        for (const auto& name : options_) {
            auto b = std::make_unique<DMButton>(name, &DMStyles::ListButton(), 200, DMButton::height());
            auto bw = std::make_unique<ButtonWidget>(b.get(), [this, name]() {
                if (cb_) cb_(name);
                close();
            });
            buttons_.push_back(std::move(b));
            button_widgets_.push_back(std::move(bw));
            rows.push_back({ button_widgets_.back().get() });
        }
        if (panel_) panel_->set_rows(rows);
    }
    std::unique_ptr<DockableCollapsible> panel_;
    std::vector<std::string> options_;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<ButtonWidget>> button_widgets_;
    Callback cb_;
    int screen_w_ = 1920;
    int screen_h_ = 1080;
    SDL_Point anchor_pos_{64,64};
    bool has_anchor_ = false;
};
