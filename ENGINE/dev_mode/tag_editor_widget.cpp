#include "tag_editor_widget.hpp"

#include "dm_styles.hpp"
#include "tag_library.hpp"
#include "tag_utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace {
constexpr int kChipWidth = 132;
constexpr int kRecommendChipWidth = 148;
constexpr size_t kRecommendationPreviewCount = 5;
constexpr size_t kMaxRecommendations = std::numeric_limits<size_t>::max();

struct TagDatasetEntry {
    std::vector<std::string> tags;
    std::vector<std::string> anti_tags;
};

struct TagStats {
    int tag_count = 0;
    int anti_count = 0;
    int co_with_tags = 0;
    int co_with_anti = 0;
    int cross_hits = 0;
};

std::string to_lower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

void append_strings(const nlohmann::json& node, std::set<std::string>& dest) {
    if (node.is_array()) {
        for (const auto& entry : node) {
            if (!entry.is_string()) continue;
            auto norm = tag_utils::normalize(entry.get<std::string>());
            if (!norm.empty()) dest.insert(std::move(norm));
        }
    } else if (node.is_string()) {
        auto norm = tag_utils::normalize(node.get<std::string>());
        if (!norm.empty()) dest.insert(std::move(norm));
    }
}

void extract_tag_section(const nlohmann::json& node, std::set<std::string>& tags, std::set<std::string>& anti) {
    if (node.is_array() || node.is_string()) {
        append_strings(node, tags);
        return;
    }
    if (!node.is_object()) return;
    if (node.contains("include")) append_strings(node["include"], tags);
    if (node.contains("tags")) append_strings(node["tags"], tags);
    if (node.contains("exclude")) append_strings(node["exclude"], anti);
    if (node.contains("anti_tags")) append_strings(node["anti_tags"], anti);
}

void extract_anti_section(const nlohmann::json& node, std::set<std::string>& anti) {
    if (node.is_array() || node.is_string()) {
        append_strings(node, anti);
        return;
    }
    if (!node.is_object()) return;
    if (node.contains("include")) append_strings(node["include"], anti);
    if (node.contains("exclude")) append_strings(node["exclude"], anti);
    if (node.contains("tags")) append_strings(node["tags"], anti);
    if (node.contains("anti_tags")) append_strings(node["anti_tags"], anti);
}

void collect_tags_recursive(const nlohmann::json& node, std::set<std::string>& tags, std::set<std::string>& anti) {
    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            if (key == "tags") {
                extract_tag_section(value, tags, anti);
            } else if (key == "anti_tags") {
                extract_anti_section(value, anti);
            } else {
                collect_tags_recursive(value, tags, anti);
            }
        }
    } else if (node.is_array()) {
        for (const auto& value : node) {
            collect_tags_recursive(value, tags, anti);
        }
    }
}

const std::vector<TagDatasetEntry>& tag_dataset() {
    static std::vector<TagDatasetEntry> dataset;
    static bool loaded = false;
    if (loaded) return dataset;
    loaded = true;

    auto add_file = [&](const std::filesystem::path& path) {
        std::ifstream in(path);
        if (!in) return;
        nlohmann::json data;
        try {
            in >> data;
        } catch (...) {
            return;
        }
        std::set<std::string> tags;
        std::set<std::string> anti;
        collect_tags_recursive(data, tags, anti);
        if (tags.empty() && anti.empty()) return;
        TagDatasetEntry entry;
        entry.tags.assign(tags.begin(), tags.end());
        entry.anti_tags.assign(anti.begin(), anti.end());
        dataset.push_back(std::move(entry));
    };

    std::error_code ec;
    const std::filesystem::directory_options opts = std::filesystem::directory_options::skip_permission_denied;
    std::array<std::filesystem::path, 2> roots{ std::filesystem::path("SRC"), std::filesystem::path("MAPS") };
    for (const auto& root : roots) {
        if (!std::filesystem::exists(root, ec)) {
            ec.clear();
            continue;
        }
        std::filesystem::recursive_directory_iterator it(root, opts, ec);
        std::filesystem::recursive_directory_iterator end;
        while (it != end) {
            if (ec) {
                ec.clear();
                it.increment(ec);
                continue;
            }
            if (it->is_regular_file(ec) && it->path().extension() == ".json") {
                add_file(it->path());
            }
            if (ec) ec.clear();
            it.increment(ec);
        }
        ec.clear();
    }
    return dataset;
}

bool contains_any(const std::set<std::string>& haystack, const std::vector<std::string>& needles) {
    for (const auto& value : needles) {
        if (haystack.count(value)) return true;
    }
    return false;
}

bool contains_any(const std::set<std::string>& haystack, const std::set<std::string>& needles) {
    for (const auto& value : needles) {
        if (haystack.count(value)) return true;
    }
    return false;
}

std::unique_ptr<DMButton> make_button(const std::string& text, const DMButtonStyle& style, int width) {
    return std::make_unique<DMButton>(text, &style, width, DMButton::height());
}

}

TagEditorWidget::TagEditorWidget() = default;
TagEditorWidget::~TagEditorWidget() = default;

void TagEditorWidget::set_tags(const std::vector<std::string>& tags,
                               const std::vector<std::string>& anti_tags) {
    tags_.clear();
    anti_tags_.clear();
    clear_search();
    for (const auto& t : tags) {
        auto norm = normalize(t);
        if (!norm.empty()) tags_.insert(std::move(norm));
    }
    for (const auto& t : anti_tags) {
        auto norm = normalize(t);
        if (norm.empty()) continue;
        if (tags_.count(norm)) continue;
        anti_tags_.insert(std::move(norm));
    }
    refresh_recommendations();
    rebuild_buttons();
    reset_toggle_state();
    mark_dirty();
}

std::vector<std::string> TagEditorWidget::tags() const {
    return std::vector<std::string>(tags_.begin(), tags_.end());
}

std::vector<std::string> TagEditorWidget::anti_tags() const {
    return std::vector<std::string>(anti_tags_.begin(), anti_tags_.end());
}

void TagEditorWidget::set_on_changed(std::function<void(const std::vector<std::string>&,
                                                        const std::vector<std::string>&)> cb) {
    on_changed_ = std::move(cb);
}

void TagEditorWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    mark_dirty();
}

int TagEditorWidget::height_for_width(int w) const {
    auto self = const_cast<TagEditorWidget*>(this);
    int width = std::max(40, w);
    return self->layout(width, 0, 0, false);
}

bool TagEditorWidget::handle_event(const SDL_Event& e) {
    layout_if_needed();
    bool used = false;
    for (const auto& chip : tag_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { remove_tag(value); }, used);
    }
    for (const auto& chip : anti_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { remove_anti_tag(value); }, used);
    }
    for (const auto& chip : rec_tag_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { add_tag(value); }, used);
    }
    for (const auto& chip : rec_anti_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { add_anti_tag(value); }, used);
    }
    if (show_more_tags_btn_ && show_more_tags_btn_->rect().w > 0) {
        if (show_more_tags_btn_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                show_all_tag_recs_ = !show_all_tag_recs_;
                update_toggle_labels();
                mark_dirty();
            }
        }
    }
    if (show_more_anti_btn_ && show_more_anti_btn_->rect().w > 0) {
        if (show_more_anti_btn_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                show_all_anti_recs_ = !show_all_anti_recs_;
                update_toggle_labels();
                mark_dirty();
            }
        }
    }
    if (tag_search_box_ && tag_search_box_->rect().w > 0) {
        bool search_used = tag_search_box_->handle_event(e);
        if (search_used) {
            used = true;
            search_input_ = tag_search_box_->value();
            std::string lowered = to_lower(search_input_);
            if (lowered != search_query_) {
                search_query_ = std::move(lowered);
                update_search_filter();
                mark_dirty();
            }
        } else if (tag_search_box_->is_editing() && e.type == SDL_KEYDOWN &&
                   (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) {
            used = true;
            add_search_text_as_tag();
        } else if (event_targets_rect(e, tag_search_box_->rect())) {
            used = true;
        }
    }
    if (add_tag_btn_ && add_tag_btn_->rect().w > 0) {
        if (add_tag_btn_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                add_search_text_as_tag();
            }
        }
    }
    return used;
}

void TagEditorWidget::render(SDL_Renderer* r) const {
    if (!r) return;
    const_cast<TagEditorWidget*>(this)->layout_if_needed();

    draw_label(r, "Tags", tags_label_rect_);
    draw_label(r, "Anti Tags", anti_label_rect_);
    if (rec_tags_label_rect_.w > 0) draw_label(r, "Tag Recommendations", rec_tags_label_rect_);
    if (rec_anti_label_rect_.w > 0) draw_label(r, "Anti Tag Recommendations", rec_anti_label_rect_);

    for (const auto& chip : tag_chips_) {
        if (chip.button) chip.button->render(r);
    }
    for (const auto& chip : anti_chips_) {
        if (chip.button) chip.button->render(r);
    }
    for (const auto& chip : rec_tag_chips_) {
        if (chip.button) chip.button->render(r);
    }
    for (const auto& chip : rec_anti_chips_) {
        if (chip.button) chip.button->render(r);
    }
    if (tag_search_box_ && tag_search_box_->rect().w > 0) {
        tag_search_box_->render(r);
    }
    if (add_tag_btn_ && add_tag_btn_->rect().w > 0) {
        add_tag_btn_->render(r);
    }
    if (show_more_tags_btn_ && show_more_tags_btn_->rect().w > 0) {
        show_more_tags_btn_->render(r);
    }
    if (show_more_anti_btn_ && show_more_anti_btn_->rect().w > 0) {
        show_more_anti_btn_->render(r);
    }
}

void TagEditorWidget::rebuild_buttons() {
    tag_chips_.clear();
    anti_chips_.clear();
    rec_tag_chips_.clear();
    rec_anti_chips_.clear();

    const auto& tag_style = DMStyles::CreateButton();
    const auto& anti_style = DMStyles::DeleteButton();
    const auto& rec_style = DMStyles::ListButton();

    for (const auto& value : tags_) {
        Chip chip;
        chip.value = value;
        chip.button = make_button(value, tag_style, kChipWidth);
        tag_chips_.push_back(std::move(chip));
    }

    for (const auto& value : anti_tags_) {
        Chip chip;
        chip.value = value;
        chip.button = make_button(value, anti_style, kChipWidth);
        anti_chips_.push_back(std::move(chip));
    }

    for (const auto& value : recommended_tags_) {
        Chip add_chip;
        add_chip.value = value;
        add_chip.button = make_button("+ " + value, rec_style, kRecommendChipWidth);
        rec_tag_chips_.push_back(std::move(add_chip));
    }

    for (const auto& value : recommended_anti_) {
        Chip anti_chip;
        anti_chip.value = value;
        anti_chip.button = make_button("- " + value, rec_style, kRecommendChipWidth);
        rec_anti_chips_.push_back(std::move(anti_chip));
    }

    update_search_filter();
}

void TagEditorWidget::refresh_recommendations() {
    const auto& dataset = tag_dataset();
    std::unordered_map<std::string, TagStats> stats;

    for (const auto& entry : dataset) {
        bool shares_tag = contains_any(tags_, entry.tags);
        bool shares_anti = contains_any(anti_tags_, entry.anti_tags);
        bool shares_cross = false;
        if (!shares_cross) {
            shares_cross = contains_any(anti_tags_, entry.tags);
        }
        if (!shares_cross) {
            shares_cross = contains_any(tags_, entry.anti_tags);
        }

        for (const auto& value : entry.tags) {
            auto& st = stats[value];
            st.tag_count++;
            if (shares_tag) st.co_with_tags++;
            if (shares_anti) st.co_with_anti++;
            if (shares_cross) st.cross_hits++;
        }
        for (const auto& value : entry.anti_tags) {
            auto& st = stats[value];
            st.anti_count++;
            if (shares_tag) st.co_with_tags++;
            if (shares_anti) st.co_with_anti++;
            if (shares_cross) st.cross_hits++;
        }
    }

    for (const auto& value : tags_) {
        auto& st = stats[value];
        st.tag_count++;
        st.co_with_tags += 2;
    }
    for (const auto& value : anti_tags_) {
        auto& st = stats[value];
        st.anti_count++;
        st.co_with_anti += 2;
    }

    std::set<std::string> candidates(TagLibrary::instance().tags().begin(), TagLibrary::instance().tags().end());
    for (const auto& [value, _] : stats) {
        candidates.insert(value);
    }

    struct CandidateScore {
        std::string value;
        double tag_score = 0.0;
        double anti_score = 0.0;
        double tie_break = 0.0;
    };

    std::vector<CandidateScore> scores;
    scores.reserve(candidates.size());
    for (const auto& value : candidates) {
        const auto it = stats.find(value);
        TagStats st;
        if (it != stats.end()) st = it->second;

        CandidateScore cs;
        cs.value = value;
        cs.tag_score = st.tag_count + 0.5 * st.anti_count + 2.0 * st.co_with_tags + 1.2 * st.co_with_anti + 0.5 * st.cross_hits;
        cs.anti_score = st.anti_count + 0.5 * st.tag_count + 2.0 * st.co_with_anti + 1.2 * st.co_with_tags + 0.5 * st.cross_hits;
        cs.tie_break = st.tag_count + st.anti_count + st.co_with_tags + st.co_with_anti + st.cross_hits;
        scores.push_back(std::move(cs));
    }

    auto make_list = [&](auto selector) {
        std::vector<std::string> output;
        std::vector<std::string> zero_scores;
        auto sorted = scores;
        std::sort(sorted.begin(), sorted.end(), [&](const CandidateScore& a, const CandidateScore& b) {
            double sa = selector(a);
            double sb = selector(b);
            if (sa == sb) {
                if (a.tie_break == b.tie_break) return a.value < b.value;
                return a.tie_break > b.tie_break;
            }
            return sa > sb;
        });
        for (const auto& cand : sorted) {
            if (tags_.count(cand.value) || anti_tags_.count(cand.value)) continue;
            double score = selector(cand);
            if (score > 0.0) {
                output.push_back(cand.value);
            } else {
                zero_scores.push_back(cand.value);
            }
            if (output.size() >= kMaxRecommendations) break;
        }
        if (output.size() < kMaxRecommendations) {
            std::sort(zero_scores.begin(), zero_scores.end());
            for (const auto& value : zero_scores) {
                if (tags_.count(value) || anti_tags_.count(value)) continue;
                output.push_back(value);
                if (output.size() >= kMaxRecommendations) break;
            }
        }
        return output;
    };

    recommended_tags_ = make_list([](const CandidateScore& c) { return c.tag_score; });
    recommended_anti_ = make_list([](const CandidateScore& c) { return c.anti_score; });
}

void TagEditorWidget::mark_dirty() {
    layout_dirty_ = true;
}

void TagEditorWidget::layout_if_needed() const {
    if (!layout_dirty_) return;
    auto self = const_cast<TagEditorWidget*>(this);
    int width = std::max(40, rect_.w);
    self->layout(width, rect_.x, rect_.y, true);
    self->layout_dirty_ = false;
}

int TagEditorWidget::layout(int width, int origin_x, int origin_y, bool apply) {
    const int pad = DMSpacing::small_gap();
    const int label_gap = DMSpacing::label_gap();
    const int section_gap = DMSpacing::item_gap();
    int y = origin_y + pad;
    int label_h = label_height();

    if (apply) {
        tags_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
    }
    y += label_h + label_gap;
    y = layout_grid(tag_chips_, width, origin_x, y, apply);
    y += section_gap;

    if (apply) {
        anti_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
    }
    y += label_h + label_gap;
    y = layout_grid(anti_chips_, width, origin_x, y, apply);
    y += section_gap;

    bool has_tag_recs = !rec_tag_chips_.empty();
    bool has_anti_recs = !rec_anti_chips_.empty();

    if (has_tag_recs) {
        if (apply) {
            rec_tags_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
        }
        y += label_h + label_gap;
        size_t matches = filtered_tag_order_.empty() && search_query_.empty()
                              ? rec_tag_chips_.size()
                              : filtered_tag_order_.size();
        size_t visible_tags = show_all_tag_recs_ ? matches : std::min(kRecommendationPreviewCount, matches);
        y = layout_grid(rec_tag_chips_, width, origin_x, y, apply, visible_tags,
                        filtered_tag_order_.empty() && search_query_.empty() ? nullptr : &filtered_tag_order_);

        bool show_tag_toggle = show_all_tag_recs_ || rec_tag_chips_.size() > kRecommendationPreviewCount;
        int toggle_gap = DMSpacing::small_gap();
        if (show_tag_toggle) {
            if (!show_more_tags_btn_) {
                show_more_tags_btn_ = make_button("Show More", DMStyles::ListButton(), kRecommendChipWidth);
            }
            int control_y = y + toggle_gap;
            if (show_all_tag_recs_) {
                if (!tag_search_box_) {
                    tag_search_box_ = std::make_unique<DMTextBox>("Search Tags", search_input_);
                }
                if (!add_tag_btn_) {
                    add_tag_btn_ = std::make_unique<DMButton>("+", &DMStyles::CreateButton(), 36, DMTextBox::height());
                }
                tag_search_box_->set_value(search_input_);
                int button_gap = DMSpacing::small_gap();
                int desired_button = std::min(48, std::max(28, width / 5 + 20));
                int button_width = std::min(width, desired_button);
                int min_search = 60;
                int search_width = width - button_width - button_gap;
                if (search_width < min_search) {
                    int deficit = min_search - search_width;
                    button_width = std::max(24, button_width - deficit);
                    if (button_width > width) button_width = width;
                    search_width = width - button_width - button_gap;
                }
                if (search_width < 0) {
                    search_width = 0;
                    button_width = width;
                }
                if (apply) {
                    if (search_width > 0) {
                        tag_search_box_->set_rect(SDL_Rect{ origin_x, control_y, search_width, DMTextBox::height() });
                    } else {
                        tag_search_box_->set_rect(SDL_Rect{0,0,0,0});
                    }
                    int add_x = origin_x + (search_width > 0 ? search_width + button_gap : 0);
                    int final_button_width = std::min(width, std::max(24, button_width));
                    add_tag_btn_->set_rect(SDL_Rect{ add_x, control_y, final_button_width, DMTextBox::height() });
                }
                control_y += DMTextBox::height();
                control_y += DMSpacing::small_gap();
            } else if (apply) {
                if (tag_search_box_) tag_search_box_->set_rect(SDL_Rect{0,0,0,0});
                if (add_tag_btn_) add_tag_btn_->set_rect(SDL_Rect{0,0,0,0});
            }
            if (apply) {
                update_toggle_labels();
                int button_w = std::max(80, std::min(width, kRecommendChipWidth));
                show_more_tags_btn_->set_rect(SDL_Rect{ origin_x, control_y, button_w, DMButton::height() });
            }
            y = control_y + DMButton::height();
        } else {
            if (apply) {
                if (show_more_tags_btn_) show_more_tags_btn_->set_rect(SDL_Rect{0,0,0,0});
                if (tag_search_box_) tag_search_box_->set_rect(SDL_Rect{0,0,0,0});
                if (add_tag_btn_) add_tag_btn_->set_rect(SDL_Rect{0,0,0,0});
            }
        }
        y += section_gap;
    } else if (apply) {
        rec_tags_label_rect_ = SDL_Rect{0,0,0,0};
        if (show_more_tags_btn_) show_more_tags_btn_->set_rect(SDL_Rect{0,0,0,0});
        if (tag_search_box_) tag_search_box_->set_rect(SDL_Rect{0,0,0,0});
        if (add_tag_btn_) add_tag_btn_->set_rect(SDL_Rect{0,0,0,0});
    }

    if (has_anti_recs) {
        if (apply) {
            rec_anti_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
        }
        y += label_h + label_gap;
        size_t visible_anti = show_all_anti_recs_ ? rec_anti_chips_.size()
                                                  : std::min(kRecommendationPreviewCount, rec_anti_chips_.size());
        y = layout_grid(rec_anti_chips_, width, origin_x, y, apply, visible_anti);

        bool show_anti_toggle = show_all_anti_recs_ || rec_anti_chips_.size() > kRecommendationPreviewCount;
        if (show_anti_toggle) {
            if (!show_more_anti_btn_) {
                show_more_anti_btn_ = make_button("Show More", DMStyles::ListButton(), kRecommendChipWidth);
            }
            int toggle_gap = DMSpacing::small_gap();
            if (apply) {
                update_toggle_labels();
                int button_w = std::max(80, std::min(width, kRecommendChipWidth));
                show_more_anti_btn_->set_rect(SDL_Rect{ origin_x, y + toggle_gap, button_w, DMButton::height() });
            }
            y += toggle_gap + DMButton::height();
        } else if (apply && show_more_anti_btn_) {
            show_more_anti_btn_->set_rect(SDL_Rect{0,0,0,0});
        }
        y += section_gap;
    } else if (apply) {
        rec_anti_label_rect_ = SDL_Rect{0,0,0,0};
        if (show_more_anti_btn_) show_more_anti_btn_->set_rect(SDL_Rect{0,0,0,0});
    }

    y += pad;
    return y - origin_y;
}

int TagEditorWidget::layout_grid(std::vector<Chip>& chips, int width, int origin_x, int start_y, bool apply,
                                size_t visible_count, const std::vector<size_t>* display_order) {
    size_t available = display_order ? display_order->size() : chips.size();
    size_t count = std::min(visible_count, available);
    if (apply) {
        for (auto& chip : chips) {
            if (chip.button) chip.button->set_rect(SDL_Rect{0,0,0,0});
        }
    }
    if (count == 0) {
        return start_y;
    }
    const int gap = DMSpacing::small_gap();
    int chip_width = &chips == &rec_tag_chips_ || &chips == &rec_anti_chips_ ? kRecommendChipWidth : kChipWidth;
    chip_width = std::min(chip_width, width);
    chip_width = std::max(chip_width, 80);
    int columns = std::max(1, (width + gap) / (chip_width + gap));
    int chip_height = DMButton::height();

    for (size_t i = 0; i < count; ++i) {
        size_t idx = display_order ? (*display_order)[i] : i;
        if (idx >= chips.size()) continue;
        int row = static_cast<int>(i / columns);
        int col = static_cast<int>(i % columns);
        int x = origin_x + col * (chip_width + gap);
        int y = start_y + row * (chip_height + gap);
        if (apply && chips[idx].button) {
            chips[idx].button->set_rect(SDL_Rect{ x, y, chip_width, chip_height });
        }
    }

    int rows = static_cast<int>((count + columns - 1) / columns);
    if (rows <= 0) return start_y;
    int total_height = rows * chip_height + (rows - 1) * gap;
    return start_y + total_height;
}

int TagEditorWidget::label_height() {
    static int cached = 0;
    if (cached > 0) return cached;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        cached = style.font_size;
        return cached;
    }
    int w = 0;
    int h = 0;
    TTF_SizeUTF8(font, "Tags", &w, &h);
    TTF_CloseFont(font);
    cached = h;
    return cached;
}

void TagEditorWidget::draw_label(SDL_Renderer* r, const std::string& text, const SDL_Rect& rect) const {
    if (rect.w <= 0 && rect.h <= 0) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ rect.x, rect.y, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
    TTF_CloseFont(font);
}

void TagEditorWidget::handle_chip_click(const Chip& chip, const SDL_Event& e,
                                        const std::function<void(const std::string&)>& on_click,
                                        bool& used) {
    if (!chip.button) return;
    if (chip.button->handle_event(e)) {
        used = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            on_click(chip.value);
        }
    }
}

void TagEditorWidget::add_tag(const std::string& value) {
    auto norm = normalize(value);
    if (norm.empty()) return;
    bool changed = false;
    if (anti_tags_.erase(norm) > 0) changed = true;
    if (tags_.insert(norm).second) changed = true;
    if (changed) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

void TagEditorWidget::add_anti_tag(const std::string& value) {
    auto norm = normalize(value);
    if (norm.empty()) return;
    bool changed = false;
    if (tags_.erase(norm) > 0) changed = true;
    if (anti_tags_.insert(norm).second) changed = true;
    if (changed) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

void TagEditorWidget::remove_tag(const std::string& value) {
    auto norm = normalize(value);
    if (tags_.erase(norm) > 0) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

void TagEditorWidget::remove_anti_tag(const std::string& value) {
    auto norm = normalize(value);
    if (anti_tags_.erase(norm) > 0) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

std::string TagEditorWidget::normalize(const std::string& value) {
    return tag_utils::normalize(value);
}

void TagEditorWidget::notify_changed() {
    if (!on_changed_) return;
    on_changed_(tags(), anti_tags());
}

void TagEditorWidget::reset_toggle_state() {
    show_all_tag_recs_ = false;
    show_all_anti_recs_ = false;
    update_toggle_labels();
    if (show_more_tags_btn_) show_more_tags_btn_->set_rect(SDL_Rect{0,0,0,0});
    if (show_more_anti_btn_) show_more_anti_btn_->set_rect(SDL_Rect{0,0,0,0});
    if (tag_search_box_) tag_search_box_->set_rect(SDL_Rect{0,0,0,0});
    if (add_tag_btn_) add_tag_btn_->set_rect(SDL_Rect{0,0,0,0});
}

void TagEditorWidget::update_toggle_labels() {
    if (show_more_tags_btn_) {
        show_more_tags_btn_->set_text(show_all_tag_recs_ ? "Show Less" : "Show More");
    }
    if (show_more_anti_btn_) {
        show_more_anti_btn_->set_text(show_all_anti_recs_ ? "Show Less" : "Show More");
    }
}

void TagEditorWidget::update_search_filter() {
    filtered_tag_order_.clear();
    if (rec_tag_chips_.empty()) return;
    if (search_query_.empty()) {
        filtered_tag_order_.reserve(rec_tag_chips_.size());
        for (size_t i = 0; i < rec_tag_chips_.size(); ++i) {
            filtered_tag_order_.push_back(i);
        }
        return;
    }
    for (size_t i = 0; i < rec_tag_chips_.size(); ++i) {
        std::string lowered = to_lower(rec_tag_chips_[i].value);
        if (lowered.find(search_query_) != std::string::npos) {
            filtered_tag_order_.push_back(i);
        }
    }
}

void TagEditorWidget::clear_search() {
    search_input_.clear();
    search_query_.clear();
    if (tag_search_box_) tag_search_box_->set_value("");
    update_search_filter();
}

void TagEditorWidget::add_search_text_as_tag() {
    auto normalized = normalize(search_input_);
    if (normalized.empty()) return;
    if (tags_.count(normalized)) return;
    add_tag(normalized);
    clear_search();
}

bool TagEditorWidget::event_targets_rect(const SDL_Event& e, const SDL_Rect& rect) {
    if (rect.w <= 0 || rect.h <= 0) return false;
    SDL_Point p{0,0};
    bool relevant = false;
    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            p = SDL_Point{ e.button.x, e.button.y };
            relevant = true;
            break;
        case SDL_MOUSEMOTION:
            p = SDL_Point{ e.motion.x, e.motion.y };
            relevant = true;
            break;
        default:
            break;
    }
    if (!relevant) return false;
    return SDL_PointInRect(&p, &rect) != 0;
}
