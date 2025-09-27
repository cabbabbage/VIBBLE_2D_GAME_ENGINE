#include "spawn_group_config_ui.hpp"

#include "DockableCollapsible.hpp"
#include "FloatingDockableManager.hpp"
#include "dm_styles.hpp"
#include "search_assets.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <utility>
#include <SDL_ttf.h>

class LabelWidget : public Widget {
public:
    explicit LabelWidget(std::string text)
        : text_(std::move(text)),
          color_(DMStyles::Label().color) {}

    void set_text(std::string text) { text_ = std::move(text); }

    void set_color(SDL_Color color) {
        color_ = color;
        has_override_color_ = true;
    }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int /*w*/) const override {
        const DMLabelStyle& st = DMStyles::Label();
        int line_height = st.font_size + 4;
        int lines = 1;
        for (char c : text_) {
            if (c == '\n') ++lines;
        }
        return std::max(line_height, lines * line_height);
    }

    bool handle_event(const SDL_Event& /*e*/) override { return false; }

    void render(SDL_Renderer* r) const override {
        const DMLabelStyle& st = DMStyles::Label();
        SDL_Color color = has_override_color_ ? color_ : st.color;
        TTF_Font* font = st.open_font();
        if (!font) return;
        int base_height = st.font_size + 2;
        int dummy_w = 0;
        int dummy_h = 0;
        if (TTF_SizeUTF8(font, "Ag", &dummy_w, &dummy_h) == 0) {
            base_height = dummy_h + 2;
        }
        size_t start = 0;
        int line = 0;
        while (start <= text_.size()) {
            size_t end = text_.find('\n', start);
            std::string segment = (end == std::string::npos)
                                      ? text_.substr(start)
                                      : text_.substr(start, end - start);
            if (!segment.empty()) {
                SDL_Surface* surf = TTF_RenderUTF8_Blended(font, segment.c_str(), color);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                    if (tex) {
                        SDL_Rect dst{ rect_.x, rect_.y + line * base_height, surf->w, surf->h };
                        SDL_RenderCopy(r, tex, nullptr, &dst);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_FreeSurface(surf);
                }
            }
            ++line;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        TTF_CloseFont(font);
    }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 255, 255, 255};
    bool has_override_color_ = false;
};

SpawnGroupConfigUI::~SpawnGroupConfigUI() = default;

namespace {
constexpr int kDefaultScreenW = 1920;
constexpr int kDefaultScreenH = 1080;
constexpr int kPerimeterRadiusMin = 0;
constexpr int kPerimeterRadiusMax = 10000;
constexpr int kPerimeterDefaultRadius = 50;
constexpr int kSpawnConfigMaxHeight = 560;

int clamp_slider_value(int value, int min_value, int max_value) {
    return std::clamp(value, min_value, max_value);
}

std::optional<int> parse_int_like(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_number_float()) {
        return static_cast<int>(std::lround(value.get<double>()));
    }
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
        }
    }
    return std::nullopt;
}

std::pair<int, int> read_range(const nlohmann::json& src,
                               const std::string& min_key,
                               const std::string& max_key,
                               int default_min,
                               int default_max) {
    int vmin = default_min;
    int vmax = default_max;
    if (src.contains(min_key)) {
        if (auto parsed = parse_int_like(src[min_key])) {
            vmin = *parsed;
        }
    }
    if (src.contains(max_key)) {
        if (auto parsed = parse_int_like(src[max_key])) {
            vmax = *parsed;
        }
    }
    if (!src.contains(min_key) && src.contains(max_key)) {
        vmin = vmax;
    }
    if (!src.contains(max_key) && src.contains(min_key)) {
        vmax = vmin;
    }
    return {vmin, vmax};
}

int read_single_value(const nlohmann::json& src,
                      const std::string& key,
                      int fallback) {
    if (!src.contains(key)) {
        return fallback;
    }
    if (auto parsed = parse_int_like(src[key])) {
        return *parsed;
    }
    return fallback;
}

std::string resolve_display_name(const nlohmann::json& entry) {
    if (entry.contains("display_name") && entry["display_name"].is_string()) {
        return entry["display_name"].get<std::string>();
    }
    if (entry.contains("name") && entry["name"].is_string()) {
        return entry["name"].get<std::string>();
    }
    if (entry.contains("asset") && entry["asset"].is_string()) {
        return entry["asset"].get<std::string>();
    }
    return std::string{};
}

bool candidate_represents_tag(const nlohmann::json& entry, std::string& name_out) {
    if (entry.contains("name") && entry["name"].is_string()) {
        const std::string value = entry["name"].get<std::string>();
        if (!value.empty() && value.front() == '#') {
            name_out = value;
            return true;
        }
    }
    if (entry.contains("tag")) {
        const auto& tag = entry["tag"];
        if (tag.is_boolean()) {
            if (tag.get<bool>()) {
                if (name_out.empty()) {
                    name_out = resolve_display_name(entry);
                }
                return true;
            }
        } else if (tag.is_string()) {
            name_out = tag.get<std::string>();
            return true;
        }
    }
    if (entry.contains("tag_name") && entry["tag_name"].is_string()) {
        name_out = entry["tag_name"].get<std::string>();
        return true;
    }
    return false;
}

int read_candidate_weight(const nlohmann::json& entry, int fallback = 100) {
    if (entry.contains("chance") && entry["chance"].is_number_integer()) {
        return entry["chance"].get<int>();
    }
    if (entry.contains("percent") && entry["percent"].is_number_integer()) {
        return entry["percent"].get<int>();
    }
    if (entry.contains("weight") && entry["weight"].is_number_integer()) {
        return entry["weight"].get<int>();
    }
    if (entry.contains("probability") && entry["probability"].is_number_integer()) {
        return entry["probability"].get<int>();
    }
    return fallback;
}

bool is_placeholder_candidate(const nlohmann::json& candidate) {
    if (candidate.is_null()) {
        return true;
    }
    if (candidate.is_object()) {
        std::string name = candidate.value("name", candidate.value("asset", std::string{}));
        return name == "null";
    }
    if (candidate.is_string()) {
        return candidate.get<std::string>() == "null";
    }
    return false;
}

std::string format_percent_summary(const nlohmann::json& entry,
                                   const char* primary_min,
                                   const char* primary_max,
                                   const char* legacy_min,
                                   const char* legacy_max) {
    auto has_value = [&](const char* key) {
        return key && entry.contains(key) && entry[key].is_number_integer();
    };
    bool has_min = has_value(primary_min) || has_value(legacy_min);
    bool has_max = has_value(primary_max) || has_value(legacy_max);
    if (!has_min && !has_max) {
        return "Not set";
    }
    auto read_value = [&](const char* primary, const char* legacy) {
        if (primary && entry.contains(primary) && entry[primary].is_number_integer()) {
            return entry[primary].get<int>();
        }
        if (legacy && entry.contains(legacy) && entry[legacy].is_number_integer()) {
            return entry[legacy].get<int>();
        }
        return 0;
    };
    int min_val = has_value(primary_min) || has_value(legacy_min)
                      ? read_value(primary_min, legacy_min)
                      : read_value(primary_max, legacy_max);
    int max_val = has_value(primary_max) || has_value(legacy_max)
                      ? read_value(primary_max, legacy_max)
                      : min_val;
    if (min_val > max_val) std::swap(min_val, max_val);
    std::string range = (min_val == max_val)
                            ? std::to_string(min_val) + "%"
                            : std::to_string(min_val) + "% - " + std::to_string(max_val) + "%";
    return range;
}

std::string format_exact_offset_summary(const nlohmann::json& entry) {
    bool has_dx = entry.contains("dx") && entry["dx"].is_number_integer();
    bool has_dy = entry.contains("dy") && entry["dy"].is_number_integer();
    int dx = has_dx ? entry["dx"].get<int>()
                    : (entry.contains("exact_dx") && entry["exact_dx"].is_number_integer() ? entry["exact_dx"].get<int>() : 0);
    int dy = has_dy ? entry["dy"].get<int>()
                    : (entry.contains("exact_dy") && entry["exact_dy"].is_number_integer() ? entry["exact_dy"].get<int>() : 0);
    if (!has_dx && !has_dy) {
        return "Not set";
    }
    return "ΔX: " + std::to_string(dx) + "\nΔY: " + std::to_string(dy);
}

std::string format_exact_room_summary(const nlohmann::json& entry) {
    bool has_w = entry.contains("origional_width") && entry["origional_width"].is_number_integer();
    bool has_h = entry.contains("origional_height") && entry["origional_height"].is_number_integer();
    if (!has_w && entry.contains("exact_origin_width") && entry["exact_origin_width"].is_number_integer()) {
        has_w = true;
    }
    if (!has_h && entry.contains("exact_origin_height") && entry["exact_origin_height"].is_number_integer()) {
        has_h = true;
    }
    if (!has_w && !has_h) {
        return "Not recorded\nDrag to capture current room size.";
    }
    int w = entry.contains("origional_width") && entry["origional_width"].is_number_integer()
                ? entry["origional_width"].get<int>()
                : (entry.contains("exact_origin_width") && entry["exact_origin_width"].is_number_integer()
                       ? entry["exact_origin_width"].get<int>()
                       : 0);
    int h = entry.contains("origional_height") && entry["origional_height"].is_number_integer()
                ? entry["origional_height"].get<int>()
                : (entry.contains("exact_origin_height") && entry["exact_origin_height"].is_number_integer()
                       ? entry["exact_origin_height"].get<int>()
                       : 0);
    return "Width: " + std::to_string(w) + "\nHeight: " + std::to_string(h) + "\nCaptured when adjusting exact spawn.";
}
}

SpawnGroupConfigUI::SpawnGroupConfigUI() {
    spawn_methods_ = {"Random", "Center", "Perimeter", "Exact", "Percent", "Entrance", "Exit"};
    panel_ = std::make_unique<DockableCollapsible>("Spawn Group", true, 0, 0);
    panel_->set_expanded(true);
    panel_->set_visible(false);
    panel_->set_available_height_override(kSpawnConfigMaxHeight);
    panel_->set_work_area(SDL_Rect{0, 0, 0, 0});

    ownership_text_ = "Room: Unknown";
    ownership_color_ = DMStyles::Label().color;
    has_ownership_color_ = false;

    b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 96, DMButton::height());
    b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() { close(); });

    add_button_ = std::make_unique<DMButton>("Add Candidate...", &DMStyles::CreateButton(), 180, DMButton::height());
    add_button_w_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() {
        ensure_search();
        if (!search_) return;
        if (panel_) {
            const SDL_Rect& r = panel_->rect();
            search_->set_position(r.x + r.w + 16, r.y);
        }
        search_->open([this](const std::string& value) {
            add_candidate(value, 100);
        });
    });

    ensure_search();
    pending_summary_.method = spawn_methods_.empty() ? std::string{} : spawn_methods_.front();
}

bool SpawnGroupConfigUI::method_forces_single_quantity(const std::string& method) const {
    return method == "Exact" || method == "Percent";
}

void SpawnGroupConfigUI::ensure_search() {
    if (!search_) {
        search_ = std::make_unique<SearchAssets>();
    }
}

void SpawnGroupConfigUI::set_position(int x, int y) {
    if (panel_) panel_->set_position(x, y);
    if (search_ && search_->visible()) {
        const SDL_Rect& r = panel_ ? panel_->rect() : SDL_Rect{x, y, 0, 0};
        search_->set_position(r.x + r.w + 16, r.y);
    }
}

SDL_Point SpawnGroupConfigUI::position() const {
    if (panel_) {
        return panel_->position();
    }
    return SDL_Point{0, 0};
}

void SpawnGroupConfigUI::handle_method_change() {
    if (method_locked_) return; // ignore UI changes when locked
    if (!dd_method_) return;
    std::string previous_method;
    if (!spawn_methods_.empty()) {
        int prev_index = std::clamp(method_, 0, static_cast<int>(spawn_methods_.size()) - 1);
        previous_method = spawn_methods_[prev_index];
    }
    int selected = dd_method_->selected();
    if (selected < 0 || selected >= static_cast<int>(spawn_methods_.size())) {
        selected = 0;
    }
    if (selected != method_) {
        method_ = selected;
        std::string new_method;
        if (!spawn_methods_.empty()) {
            int clamped_index = std::clamp(method_, 0, static_cast<int>(spawn_methods_.size()) - 1);
            new_method = spawn_methods_[clamped_index];
        }
        if (new_method == "Perimeter" && previous_method != "Perimeter" && perimeter_radius_ <= 0) {
            perimeter_radius_ = kPerimeterDefaultRadius;
            entry_["radius"] = perimeter_radius_;
            if (entry_.contains("perimeter_radius")) {
                entry_.erase("perimeter_radius");
            }
        }
        rebuild_widgets();
        rebuild_rows();
    }
}

void SpawnGroupConfigUI::load(const nlohmann::json& data) {
    entry_ = data.is_object() ? data : nlohmann::json::object();
    spawn_id_ = entry_.value("spawn_id", std::string{});

    std::string method = entry_.value("position", spawn_methods_.front());
    if (method == "Exact Position") {
        method = "Exact";
    }
    if (method_locked_ && !forced_method_.empty()) {
        method = forced_method_;
    }
    auto it = std::find(spawn_methods_.begin(), spawn_methods_.end(), method);
    if (it == spawn_methods_.end()) {
        spawn_methods_.push_back(method);
        method_ = static_cast<int>(spawn_methods_.size() - 1);
    } else {
        method_ = static_cast<int>(std::distance(spawn_methods_.begin(), it));
    }

    min_number_ = read_single_value(entry_, "min_number", 1);
    max_number_ = read_single_value(entry_, "max_number", std::max(1, min_number_));
    overlap_ = entry_.value("check_overlap", false);
    spacing_ = entry_.value("enforce_spacing", entry_.value("check_min_spacing", false));
    perimeter_radius_ = read_single_value(entry_, "radius", entry_.value("perimeter_radius", 0));
    if (perimeter_radius_ < 0) perimeter_radius_ = 0;

    if (!entry_.contains("candidates") || !entry_["candidates"].is_array()) {
        entry_["candidates"] = nlohmann::json::array();
    }

    auto& candidates = entry_["candidates"];
    if (candidates.is_array()) {
        bool has_null = false;
        nlohmann::json::size_type null_index = 0;
        for (nlohmann::json::size_type idx = 0; idx < candidates.size(); ++idx) {
            if (is_placeholder_candidate(candidates[idx])) {
                has_null = true;
                null_index = idx;
                break;
            }
        }
        if (!has_null) {
            if (candidates.empty()) {
                nlohmann::json null_entry;
                null_entry["name"] = "null";
                null_entry["chance"] = 0;
                candidates.insert(candidates.begin(), std::move(null_entry));
            }
        } else if (null_index != 0 && null_index < candidates.size()) {
            auto null_entry = candidates[null_index];
            candidates.erase(candidates.begin() + static_cast<nlohmann::json::difference_type>(null_index));
            candidates.insert(candidates.begin(), std::move(null_entry));
        }
    }

    if (panel_) {
        std::string title = spawn_id_;
        if (title.empty()) {
            if (entry_.contains("display_name") && entry_["display_name"].is_string()) {
                title = entry_["display_name"].get<std::string>();
            } else if (entry_.contains("name") && entry_["name"].is_string()) {
                title = entry_["name"].get<std::string>();
            }
        }
        if (title.empty()) {
            const auto& arr = entry_["candidates"];
            if (arr.is_array()) {
                for (const auto& cand : arr) {
                    std::string name;
                    if (cand.is_object()) {
                        name = cand.value("display_name", cand.value("label", std::string{}));
                        if (name.empty()) name = cand.value("name", cand.value("asset", std::string{}));
                    } else if (cand.is_string()) {
                        name = cand.get<std::string>();
                    }
                    if (name == "null") continue;
                    if (!name.empty()) {
                        title = name;
                        break;
                    }
                }
            }
        }
        if (title.empty()) title = "Asset";
        panel_->set_title(title);
    }

    baseline_method_ = spawn_methods_.empty() ? std::string{} : spawn_methods_[method_];
    baseline_min_ = min_number_;
    baseline_max_ = max_number_;
    pending_summary_ = {};
    pending_summary_.method = baseline_method_;

    rebuild_widgets();
    rebuild_rows();
}

void SpawnGroupConfigUI::open_panel() {
    if (!panel_) return;
    FloatingDockableManager::instance().open_floating(
        "Asset Config", panel_.get(), [this]() { this->close(); }, floating_stack_key_);
    const bool was_visible = panel_->is_visible();
    panel_->set_visible(true);
    if (!was_visible) {
        panel_->set_expanded(true);
    }
    Input dummy;
    panel_->update(dummy, kDefaultScreenW, kDefaultScreenH);
}

void SpawnGroupConfigUI::close() {
    if (panel_) panel_->set_visible(false);
    if (search_) search_->close();
}

bool SpawnGroupConfigUI::visible() const {
    return (panel_ && panel_->is_visible()) || (search_ && search_->visible());
}

void SpawnGroupConfigUI::rebuild_widgets() {
    if (!ownership_text_.empty()) {
        ownership_label_ = std::make_unique<LabelWidget>(ownership_text_);
        if (has_ownership_color_) {
            ownership_label_->set_color(ownership_color_);
        }
    } else {
        ownership_label_.reset();
    }

    dd_method_.reset();
    dd_method_w_.reset();
    locked_method_label_.reset();
    if (method_locked_) {
        // Show read-only method info
        std::string forced = (!forced_method_.empty() ? forced_method_ : (spawn_methods_.empty() ? std::string{} : spawn_methods_[std::clamp(method_, 0, static_cast<int>(spawn_methods_.size() - 1))]));
        locked_method_label_ = std::make_unique<LabelWidget>(std::string("Method: ") + forced + " (locked)");
    } else {
        dd_method_ = std::make_unique<DMDropdown>("Method", spawn_methods_, method_);
        dd_method_w_ = std::make_unique<DropdownWidget>(dd_method_.get());
    }

    cb_overlap_ = std::make_unique<DMCheckbox>("Check Overlap", overlap_);
    cb_overlap_w_ = std::make_unique<CheckboxWidget>(cb_overlap_.get());
    cb_spacing_ = std::make_unique<DMCheckbox>("Check Min Spacing", spacing_);
    cb_spacing_w_ = std::make_unique<CheckboxWidget>(cb_spacing_.get());

    s_minmax_.reset();
    s_minmax_w_.reset();
    s_minmax_label_.reset();
    perimeter_radius_slider_.reset();
    perimeter_radius_widget_.reset();
    percent_x_label_.reset();
    percent_y_label_.reset();
    exact_offset_label_.reset();
    exact_room_label_.reset();

    std::string method = spawn_methods_.empty() ? std::string{} : spawn_methods_[std::clamp(method_, 0, static_cast<int>(spawn_methods_.size() - 1))];
    if (method == "Perimeter") {
        if (min_number_ < 2) min_number_ = 2;
        if (max_number_ < 2) max_number_ = 2;
        if (max_number_ < min_number_) max_number_ = min_number_;
    }
    if (!quantity_hidden_ && !method_forces_single_quantity(method)) {
        int min_val = std::min(min_number_, max_number_);
        int max_val = std::max(min_number_, max_number_);
        if (method == "Perimeter") {
            min_val = std::max(min_val, 2);
            max_val = std::max(max_val, 2);
        }
        s_minmax_label_ = std::make_unique<LabelWidget>("Quantity (Min/Max)");
        s_minmax_ = std::make_unique<DMRangeSlider>(-100, 500, min_val, max_val);
        s_minmax_w_ = std::make_unique<RangeSliderWidget>(s_minmax_.get());
    }

    if (method == "Perimeter") {
        perimeter_radius_ = clamp_slider_value(perimeter_radius_, kPerimeterRadiusMin, kPerimeterRadiusMax);
        perimeter_radius_slider_ = std::make_unique<DMSlider>(
            "Perimeter Radius", kPerimeterRadiusMin, kPerimeterRadiusMax, perimeter_radius_);
        perimeter_radius_widget_ = std::make_unique<SliderWidget>(perimeter_radius_slider_.get());
    } else if (method == "Percent") {
        percent_x_label_ = std::make_unique<LabelWidget>(
            "Percent X: " + format_percent_summary(entry_, "p_x_min", "p_x_max", "percent_x_min", "percent_x_max"));
        percent_y_label_ = std::make_unique<LabelWidget>(
            "Percent Y: " + format_percent_summary(entry_, "p_y_min", "p_y_max", "percent_y_min", "percent_y_max"));
    } else if (method == "Exact") {
        exact_offset_label_ = std::make_unique<LabelWidget>(
            std::string("Exact Offset:\n") + format_exact_offset_summary(entry_));
        exact_room_label_ = std::make_unique<LabelWidget>(
            std::string("Saved Room Size:\n") + format_exact_room_summary(entry_));
    }

    candidates_.clear();
    auto& arr = entry_["candidates"];
    if (arr.is_array()) {
        for (nlohmann::json::size_type idx = 0; idx < arr.size(); ++idx) {
            const auto& cand_json = arr[idx];
            CandidateRow row;
            row.index = static_cast<size_t>(idx);
            bool has_explicit_weight = false;

            if (cand_json.is_null()) {
                row.placeholder = true;
                row.name = "null";
                row.chance = 0;
            } else if (cand_json.is_object()) {
                if ((cand_json.contains("chance") && cand_json["chance"].is_number_integer()) ||
                    (cand_json.contains("percent") && cand_json["percent"].is_number_integer()) ||
                    (cand_json.contains("weight") && cand_json["weight"].is_number_integer()) ||
                    (cand_json.contains("probability") && cand_json["probability"].is_number_integer())) {
                    has_explicit_weight = true;
                }
                std::string name = resolve_display_name(cand_json);
                bool is_tag = candidate_represents_tag(cand_json, name);
                if (name.empty()) name = "null";
                row.name = is_tag ? (name.front() == '#' ? name : "#" + name) : name;
                row.chance = read_candidate_weight(cand_json);
            } else if (cand_json.is_string()) {
                row.name = cand_json.get<std::string>();
                row.chance = 100;
            } else {
                row.name = "null";
                row.chance = 0;
            }

            if (row.name == "null") {
                row.placeholder = true;
                if (!has_explicit_weight) {
                    row.chance = 0;
                }
            }

            row.name_box = std::make_unique<DMTextBox>("Candidate", row.name);
            row.name_w = std::make_unique<TextBoxWidget>(row.name_box.get());
            row.chance_label = std::make_unique<LabelWidget>("Chance (0 / 0)");
            row.chance_slider = std::make_unique<DMSlider>("", 0, 100, clamp_slider_value(row.chance, 0, 100));
            row.chance_w = std::make_unique<SliderWidget>(row.chance_slider.get());

            if (!row.placeholder) {
                row.del_button = std::make_unique<DMButton>("X", &DMStyles::DeleteButton(), 40, DMButton::height());
                size_t remove_index = row.index;
                row.del_w = std::make_unique<ButtonWidget>(row.del_button.get(), [this, remove_index]() {
                    remove_candidate(remove_index);
                });
            }

            candidates_.push_back(std::move(row));
        }
    }
}

void SpawnGroupConfigUI::rebuild_rows() {
    if (!panel_) return;
    DockableCollapsible::Rows rows;

    if (ownership_label_) {
        rows.push_back({ ownership_label_.get() });
    }

    DockableCollapsible::Row header_row;
    if (dd_method_w_) header_row.push_back(dd_method_w_.get());
    else if (locked_method_label_) header_row.push_back(locked_method_label_.get());
    if (b_done_w_) header_row.push_back(b_done_w_.get());
    if (!header_row.empty()) rows.push_back(header_row);

    if (s_minmax_label_ || s_minmax_w_) {
        DockableCollapsible::Row quantity_row;
        if (s_minmax_label_) quantity_row.push_back(s_minmax_label_.get());
        if (s_minmax_w_) quantity_row.push_back(s_minmax_w_.get());
        if (!quantity_row.empty()) rows.push_back(quantity_row);
    }

    DockableCollapsible::Row toggles;
    if (cb_overlap_w_) toggles.push_back(cb_overlap_w_.get());
    if (cb_spacing_w_) toggles.push_back(cb_spacing_w_.get());
    if (!toggles.empty()) rows.push_back(toggles);

    if (perimeter_radius_widget_) {
        rows.push_back({ perimeter_radius_widget_.get() });
    }

    if (percent_x_label_ || percent_y_label_) {
        DockableCollapsible::Row percent_row;
        if (percent_x_label_) percent_row.push_back(percent_x_label_.get());
        if (percent_y_label_) percent_row.push_back(percent_y_label_.get());
        if (!percent_row.empty()) rows.push_back(percent_row);
    }

    if (exact_offset_label_ || exact_room_label_) {
        DockableCollapsible::Row exact_row;
        if (exact_offset_label_) exact_row.push_back(exact_offset_label_.get());
        if (exact_room_label_) exact_row.push_back(exact_room_label_.get());
        if (!exact_row.empty()) rows.push_back(exact_row);
    }

    if (add_button_w_) rows.push_back({ add_button_w_.get() });

    for (auto& row : candidates_) {
        DockableCollapsible::Row name_row;
        if (row.name_w) name_row.push_back(row.name_w.get());
        if (row.del_w) name_row.push_back(row.del_w.get());
        if (!name_row.empty()) rows.push_back(name_row);

        DockableCollapsible::Row chance_row;
        if (row.chance_label) chance_row.push_back(row.chance_label.get());
        if (row.chance_w) chance_row.push_back(row.chance_w.get());
        if (!chance_row.empty()) rows.push_back(chance_row);
    }

    panel_->set_cell_width(200);
    panel_->set_rows(rows);
    refresh_chance_labels(total_chance());
}

void SpawnGroupConfigUI::add_candidate(const std::string& raw_name, int chance) {
    if (!entry_.contains("candidates") || !entry_["candidates"].is_array()) {
        entry_["candidates"] = nlohmann::json::array();
    }
    auto& arr = entry_["candidates"];
    std::string name = raw_name;
    if (name.empty()) name = "null";
    const bool placeholder = (name == "null");

    if (!placeholder && arr.is_array()) {
        bool has_real_candidate = false;
        for (const auto& existing : arr) {
            if (!is_placeholder_candidate(existing)) {
                has_real_candidate = true;
                break;
            }
        }
        if (!has_real_candidate) {
            nlohmann::json filtered = nlohmann::json::array();
            for (const auto& existing : arr) {
                if (!is_placeholder_candidate(existing)) {
                    filtered.push_back(existing);
                }
            }
            arr = std::move(filtered);
        }
    }

    nlohmann::json candidate;
    candidate["name"] = name;
    candidate["chance"] = placeholder ? 0 : clamp_slider_value(chance, 0, 100);

    arr.push_back(std::move(candidate));
    rebuild_widgets();
    rebuild_rows();
    sync_json();
}

void SpawnGroupConfigUI::lock_method_to(const std::string& method) {
    method_locked_ = true;
    forced_method_ = method;
    // Adjust entry and internal state immediately
    if (!forced_method_.empty()) {
        entry_["position"] = forced_method_;
        auto it = std::find(spawn_methods_.begin(), spawn_methods_.end(), forced_method_);
        if (it == spawn_methods_.end()) {
            spawn_methods_.push_back(forced_method_);
            method_ = static_cast<int>(spawn_methods_.size() - 1);
        } else {
            method_ = static_cast<int>(std::distance(spawn_methods_.begin(), it));
        }
    }
    rebuild_widgets();
    rebuild_rows();
}

void SpawnGroupConfigUI::set_quantity_hidden(bool hidden) {
    quantity_hidden_ = hidden;
    rebuild_widgets();
    rebuild_rows();
}

void SpawnGroupConfigUI::set_on_close(std::function<void()> cb) {
    close_callbacks_.clear();
    next_close_callback_id_ = 1;
    if (cb) {
        close_callbacks_.push_back({next_close_callback_id_++, std::move(cb)});
    }
    bind_on_close_callbacks();
}

size_t SpawnGroupConfigUI::add_on_close_callback(std::function<void()> cb) {
    if (!cb) {
        return 0;
    }
    size_t id = next_close_callback_id_++;
    close_callbacks_.push_back({id, std::move(cb)});
    bind_on_close_callbacks();
    return id;
}

void SpawnGroupConfigUI::remove_on_close_callback(size_t handle) {
    if (handle == 0) {
        return;
    }
    auto it = std::remove_if(close_callbacks_.begin(), close_callbacks_.end(),
                             [handle](const CloseCallbackEntry& entry) { return entry.id == handle; });
    if (it != close_callbacks_.end()) {
        close_callbacks_.erase(it, close_callbacks_.end());
        bind_on_close_callbacks();
    }
}

void SpawnGroupConfigUI::clear_on_close_callbacks() {
    close_callbacks_.clear();
    next_close_callback_id_ = 1;
    bind_on_close_callbacks();
}

void SpawnGroupConfigUI::set_floating_stack_key(std::string key) {
    floating_stack_key_ = std::move(key);
}

void SpawnGroupConfigUI::remove_candidate(size_t index) {
    if (!entry_.contains("candidates") || !entry_["candidates"].is_array()) return;
    auto& arr = entry_["candidates"];
    if (index >= arr.size()) return;
    const auto& cand = arr[index];
    bool is_placeholder = is_placeholder_candidate(cand);
    if (is_placeholder) return;
    arr.erase(arr.begin() + static_cast<nlohmann::json::difference_type>(index));
    rebuild_widgets();
    rebuild_rows();
    sync_json();
}

void SpawnGroupConfigUI::sync_json() {
    if (cb_overlap_) {
        overlap_ = cb_overlap_->value();
        entry_["check_overlap"] = overlap_;
    }
    if (cb_spacing_) {
        spacing_ = cb_spacing_->value();
        entry_["enforce_spacing"] = spacing_;
        if (entry_.contains("check_min_spacing")) entry_.erase("check_min_spacing");
    }

    std::string method = spawn_methods_.empty() ? std::string{} : spawn_methods_[std::clamp(method_, 0, static_cast<int>(spawn_methods_.size() - 1))];
    entry_["position"] = method;
    pending_summary_.method = method;
    if (!pending_summary_.method_changed && method != baseline_method_) {
        pending_summary_.method_changed = true;
    }

    if (perimeter_radius_slider_) {
        perimeter_radius_ = clamp_slider_value(perimeter_radius_slider_->value(),
                                               kPerimeterRadiusMin,
                                               kPerimeterRadiusMax);
        perimeter_radius_slider_->set_value(perimeter_radius_);
        entry_["radius"] = perimeter_radius_;
        if (entry_.contains("perimeter_radius")) {
            entry_.erase("perimeter_radius");
        }
    } else if (method == "Perimeter") {
        entry_["radius"] = std::max(perimeter_radius_, 0);
        if (entry_.contains("perimeter_radius")) {
            entry_.erase("perimeter_radius");
        }
    }

    if (s_minmax_) {
        min_number_ = s_minmax_->min_value();
        max_number_ = s_minmax_->max_value();
    }
    if (method == "Perimeter") {
        if (min_number_ < 2) {
            min_number_ = 2;
            if (s_minmax_) s_minmax_->set_min_value(min_number_);
        }
        if (max_number_ < 2) {
            max_number_ = 2;
        }
        if (max_number_ < min_number_) {
            max_number_ = min_number_;
        }
        if (s_minmax_) {
            s_minmax_->set_min_value(min_number_);
            s_minmax_->set_max_value(max_number_);
        }
    }
    entry_["min_number"] = min_number_;
    entry_["max_number"] = max_number_;
    if (!pending_summary_.quantity_changed && (min_number_ != baseline_min_ || max_number_ != baseline_max_)) {
        pending_summary_.quantity_changed = true;
    }

    if (!entry_.contains("candidates") || !entry_["candidates"].is_array()) {
        entry_["candidates"] = nlohmann::json::array();
    }

    auto& arr = entry_["candidates"];
    size_t real_candidate_count = 0;
    CandidateRow* sole_candidate = nullptr;
    for (auto& row : candidates_) {
        if (!row.placeholder) {
            ++real_candidate_count;
            if (real_candidate_count == 1) {
                sole_candidate = &row;
            }
        }
    }
    if (real_candidate_count == 1 && sole_candidate && sole_candidate->chance_slider) {
        sole_candidate->chance_slider->set_value(100);
    }

    for (auto& row : candidates_) {
        if (row.index >= arr.size()) continue;
        auto& cand = arr[row.index];
        if (!cand.is_object()) cand = nlohmann::json::object();

        std::string name_value = row.name_box ? row.name_box->value() : row.name;
        if (row.placeholder) {
            name_value = "null";
        }
        if (name_value.empty()) name_value = "null";
        cand["name"] = name_value;
        if (cand.contains("tag")) cand.erase("tag");
        if (cand.contains("tag_name")) cand.erase("tag_name");
        if (row.chance_slider) {
            cand["chance"] = row.chance_slider->value();
        }
    }

    int total = total_chance();
    entry_["chance_denominator"] = total;
    refresh_chance_labels(total);
}

void SpawnGroupConfigUI::update(const Input& input) {
    if (panel_ && panel_->is_visible()) {
        panel_->update(input, kDefaultScreenW, kDefaultScreenH);
        handle_method_change();
        sync_json();
    }
    if (search_ && search_->visible()) {
        search_->update(input);
        if (panel_) {
            const SDL_Rect& r = panel_->rect();
            search_->set_position(r.x + r.w + 16, r.y);
        }
    }
}

bool SpawnGroupConfigUI::handle_event(const SDL_Event& e) {
    bool used = false;
    if (search_ && search_->visible()) {
        used |= search_->handle_event(e);
    }
    if (panel_ && panel_->is_visible()) {
        if (panel_->handle_event(e)) {
            used = true;
            handle_method_change();
            sync_json();
        }
    }
    return used;
}

void SpawnGroupConfigUI::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
    if (search_ && search_->visible()) search_->render(r);
}

nlohmann::json SpawnGroupConfigUI::to_json() const {
    return entry_;
}

bool SpawnGroupConfigUI::is_point_inside(int x, int y) const {
    if (panel_ && panel_->is_visible() && panel_->is_point_inside(x, y)) {
        return true;
    }
    if (search_ && search_->visible() && search_->is_point_inside(x, y)) {
        return true;
    }
    return false;
}

SpawnGroupConfigUI::ChangeSummary SpawnGroupConfigUI::consume_change_summary() {
    ChangeSummary result = pending_summary_;
    pending_summary_ = {};
    baseline_method_ = spawn_methods_.empty() ? std::string{} : spawn_methods_[std::clamp(method_, 0, static_cast<int>(spawn_methods_.size() - 1))];
    baseline_min_ = min_number_;
    baseline_max_ = max_number_;
    pending_summary_.method = baseline_method_;
    return result;
}

void SpawnGroupConfigUI::set_ownership_label(const std::string& label, SDL_Color color) {
    ownership_text_ = label;
    ownership_color_ = color;
    has_ownership_color_ = true;
    if (ownership_label_) {
        ownership_label_->set_text(ownership_text_);
        ownership_label_->set_color(ownership_color_);
    }
    if (panel_) {
        rebuild_rows();
    }
}

int SpawnGroupConfigUI::total_chance() const {
    int total = 0;
    for (const auto& row : candidates_) {
        if (row.chance_slider) {
            total += row.chance_slider->value();
        }
    }
    return total;
}

void SpawnGroupConfigUI::refresh_chance_labels(int total_chance) {
    if (total_chance < 0) total_chance = 0;
    for (auto& row : candidates_) {
        if (!row.chance_label) continue;
        int numerator = row.chance_slider ? row.chance_slider->value() : 0;
        std::string prefix = row.placeholder ? "Null chance" : "Chance";
        prefix += " (" + std::to_string(numerator) + " / " + std::to_string(total_chance) + ")";
        row.chance_label->set_text(std::move(prefix));
    }
}

void SpawnGroupConfigUI::bind_on_close_callbacks() {
    if (!panel_) {
        return;
    }
    if (close_callbacks_.empty()) {
        panel_->set_on_close({});
        return;
    }
    panel_->set_on_close([this]() {
        auto callbacks = close_callbacks_;
        for (const auto& entry : callbacks) {
            if (entry.cb) {
                entry.cb();
            }
        }
    });
}
