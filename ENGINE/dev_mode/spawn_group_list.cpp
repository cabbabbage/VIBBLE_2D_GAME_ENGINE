#include "spawn_group_list.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SDL_ttf.h>

#include "dm_styles.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"
#include "search_assets.hpp"
#include "FloatingDockableManager.hpp"

using nlohmann::json;

namespace {

struct CandidateAssetRecord {
    std::string name;
    std::vector<std::string> tags;
};

struct CandidateAssetCache {
    bool loaded = false;
    std::vector<CandidateAssetRecord> assets;
    std::vector<std::string> tags;
};

static CandidateAssetCache& candidate_asset_cache() {
    static CandidateAssetCache cache;
    if (cache.loaded) {
        return cache;
    }
    namespace fs = std::filesystem;
    cache.assets.clear();
    cache.tags.clear();
    std::set<std::string> tagset;
    fs::path src("SRC");
    if (fs::exists(src) && fs::is_directory(src)) {
        for (auto& entry : fs::directory_iterator(src)) {
            if (!entry.is_directory()) continue;
            fs::path info_path = entry.path() / "info.json";
            if (!fs::exists(info_path)) continue;
            try {
                std::ifstream f(info_path);
                if (!f) continue;
                nlohmann::json j; f >> j;
                CandidateAssetRecord record;
                record.name = j.value("asset_name", entry.path().filename().string());
                if (j.contains("tags") && j["tags"].is_array()) {
                    for (const auto& t : j["tags"]) {
                        if (!t.is_string()) continue;
                        std::string tag = t.get<std::string>();
                        record.tags.push_back(tag);
                        tagset.insert(tag);
                    }
                }
                cache.assets.push_back(std::move(record));
            } catch (...) {}
        }
    }
    std::sort(cache.assets.begin(), cache.assets.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    cache.tags.assign(tagset.begin(), tagset.end());
    cache.loaded = true;
    return cache;
}

static std::string lower_copy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

class SpacerWidget : public Widget {
public:
    explicit SpacerWidget(int height) : height_(std::max(0, height)) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        rect_.h = height_;
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override { return height_; }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer*) const override {}

    bool wants_full_row() const override { return true; }

private:
    int height_ = 0;
    SDL_Rect rect_{0,0,0,0};
};

class SectionLabelWidget : public Widget {
public:
    SectionLabelWidget(std::string text,
                       bool full_row = true,
                       SDL_Color color = DMStyles::Label().color,
                       int font_size = DMStyles::Label().font_size)
        : text_(std::move(text)),
          color_(color),
          font_size_(std::max(8, font_size)),
          full_row_(full_row) {}

    void set_text(std::string text) { text_ = std::move(text); }
    void set_color(SDL_Color color) { color_ = color; }
    void set_font_size(int size) { font_size_ = std::max(8, size); }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override {
        return font_size_ + DMSpacing::small_gap();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        TTF_Font* font = TTF_OpenFont(DMStyles::Label().font_path.c_str(), font_size_);
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), color_);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = rect_;
            dst.w = surf->w;
            dst.h = surf->h;
            if (tex) {
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return full_row_; }

private:
    std::string text_;
    SDL_Color color_;
    int font_size_ = 12;
    bool full_row_ = true;
    SDL_Rect rect_{0,0,0,0};
};

class RowRectMarkerWidget : public Widget {
public:
    RowRectMarkerWidget(SDL_Rect* target, bool begin)
        : target_rect_(target), begin_(begin) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (!target_rect_) return;
        if (begin_) {
            *target_rect_ = SDL_Rect{r.x, r.y, r.w, 0};
        } else {
            if (target_rect_->w <= 0) {
                target_rect_->w = r.w;
            }
            int bottom = r.y + r.h;
            if (bottom < target_rect_->y) {
                bottom = target_rect_->y;
            }
            target_rect_->h = bottom - target_rect_->y;
        }
    }

    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return 0; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer*) const override {}
    bool wants_full_row() const override { return true; }

private:
    SDL_Rect* target_rect_ = nullptr;
    bool begin_ = false;
    SDL_Rect rect_{0,0,0,0};
};

}  // namespace

struct SpawnGroupList::EntryRow {
    json* array = nullptr;
    json* entry = nullptr;
    json  ro_entry;
    bool  read_only = false;
    std::string id;
    int index = -1;

    bool expanded = false;
    std::unique_ptr<DMButton> toggle_btn;
    std::unique_ptr<ButtonWidget> toggle_w;
    std::unique_ptr<DMButton> up_btn;
    std::unique_ptr<ButtonWidget> up_w;
    std::unique_ptr<DMButton> down_btn;
    std::unique_ptr<ButtonWidget> down_w;
    std::unique_ptr<DMButton> del_btn;
    std::unique_ptr<ButtonWidget> del_w;
    std::unique_ptr<DMButton> dup_btn;
    std::unique_ptr<ButtonWidget> dup_w;

    std::unique_ptr<DMTextBox> name_box;
    std::unique_ptr<TextBoxWidget> name_w;
    std::unique_ptr<DMDropdown> method_dd;
    std::unique_ptr<DropdownWidget> method_w;
    std::unique_ptr<DMRangeSlider> qty_sl;
    std::unique_ptr<RangeSliderWidget> qty_w;
    std::unique_ptr<CandidateList> candidate_list;

    std::function<std::vector<std::string>()> area_names_provider;
    std::string method_lock;
    bool quantity_hidden = false;

    std::string owner_label;
    SDL_Color owner_color{255,255,255,255};

    std::unique_ptr<DMButton> link_btn;
    std::unique_ptr<ButtonWidget> link_btn_w;

    std::unique_ptr<Widget> outline_begin_marker;
    std::unique_ptr<Widget> outline_end_marker;
    SDL_Rect outline_rect{0,0,0,0};

    std::unique_ptr<Widget> body_begin_marker;
    std::unique_ptr<Widget> body_end_marker;
    SDL_Rect body_rect{0,0,0,0};

    SDL_Rect candidates_rect{0,0,0,0};

    std::unique_ptr<SectionLabelWidget> owner_label_widget;
    std::unique_ptr<SpacerWidget> body_top_gap;
    std::unique_ptr<SectionLabelWidget> general_label;
};

class SpawnGroupList::CandidateList {
public:
    CandidateList(SpawnGroupList& owner, EntryRow& row, bool defer_commit = false)
        : owner_(owner), row_(row), defer_commit_until_unfocus_(defer_commit) {}
    ~CandidateList();

    void rebuild();
    void append_rows(DockableCollapsible::Rows& out);
    bool sync_to_json();
    void add_candidate(const std::string& asset_name);

private:
    struct CandidateInfo {
        std::string name;
        double weight = 0.0;
    };

    class CandidatePieWidget : public Widget {
    public:
        explicit CandidatePieWidget(CandidateList& list) : list_(list) {}

        void set_rect(const SDL_Rect& r) override { rect_ = r; }
        const SDL_Rect& rect() const override { return rect_; }
        int height_for_width(int w) const override;
        bool handle_event(const SDL_Event& e) override;
        void render(SDL_Renderer* r) const override;
        bool wants_full_row() const override { return true; }

    private:
        struct Layout {
            SDL_FPoint center{0.f, 0.f};
            float radius = 0.f;
            SDL_Rect legend{0, 0, 0, 0};
        };

        Layout compute_layout() const;
        int index_for_point(int x, int y) const;

        SDL_Rect rect_{0, 0, 0, 0};
        CandidateList& list_;
    };

    class SearchTextBoxWidget : public Widget {
    public:
        SearchTextBoxWidget(CandidateList& list, DMTextBox* box)
            : list_(list), box_(box) {}

        void set_box(DMTextBox* box) { box_ = box; }

        void set_rect(const SDL_Rect& r) override {
            if (box_) box_->set_rect(r);
            rect_ = box_ ? box_->rect() : SDL_Rect{r.x, r.y, r.w, r.h};
        }

        const SDL_Rect& rect() const override { return box_ ? box_->rect() : rect_; }

        int height_for_width(int w) const override {
            return box_ ? box_->preferred_height(w) : DMTextBox::height();
        }

        bool handle_event(const SDL_Event& e) override {
            if (!box_) return false;
            std::string before = box_->value();
            bool used = box_->handle_event(e);
            if (before != box_->value()) {
                list_.on_search_query_changed();
            }
            return used;
        }

        void render(SDL_Renderer* r) const override {
            if (box_) box_->render(r);
        }

        bool wants_full_row() const override { return true; }

    private:
        CandidateList& list_;
        DMTextBox* box_ = nullptr;
        SDL_Rect rect_{0, 0, 0, 0};
    };

    struct SearchResult {
        std::string label;
        bool is_tag = false;
        bool is_null = false;
    };

    void ensure_common_widgets();
    void ensure_search_widgets();
    void clear_hover();
    void set_hover(int idx, bool inside);
    bool handle_scroll(int steps);
    bool remove_hovered();
    void ensure_positive_total();
    void renormalize();
    double step_amount() const;
    SDL_Color color_for_index(size_t index, bool highlight) const;
    const std::vector<CandidateInfo>& candidates() const { return candidates_; }
    double total_weight() const { return total_weight_; }
    bool has_hover() const { return hover_active_ && hover_index_ >= 0 && hover_index_ < static_cast<int>(candidates_.size()); }
    int hovered_index() const { return has_hover() ? hover_index_ : -1; }
    bool adjust_weight_internal(int idx, int steps);
    void set_scroll_focus(bool focus);
    bool scroll_focused() const { return scroll_focus_; }
    void open_search();
    void close_search();
    bool refresh_search_results(bool force = false);
    void on_search_query_changed();
    void handle_search_selection(int index);

    SpawnGroupList& owner_;
    EntryRow& row_;
    std::vector<CandidateInfo> candidates_;
    double total_weight_ = 100.0;
    int hover_index_ = -1;
    bool hover_active_ = false;
    bool dirty_ = false;
    bool scroll_focus_ = false;
    bool pending_commit_ = false;
    bool defer_commit_until_unfocus_ = false;

    std::unique_ptr<SpacerWidget> top_gap_;
    std::unique_ptr<RowRectMarkerWidget> begin_marker_;
    std::unique_ptr<RowRectMarkerWidget> end_marker_;
    std::unique_ptr<SectionLabelWidget> header_label_;
    std::unique_ptr<SpacerWidget> bottom_gap_;
    std::unique_ptr<DMButton> add_btn_;
    std::unique_ptr<ButtonWidget> add_w_;
    std::unique_ptr<CandidatePieWidget> pie_widget_;
    std::unique_ptr<SpacerWidget> search_gap_;
    std::unique_ptr<DMTextBox> search_box_;
    std::unique_ptr<SearchTextBoxWidget> search_box_widget_;
    std::unique_ptr<SectionLabelWidget> no_results_label_;
    std::vector<SearchResult> search_results_;
    std::vector<std::unique_ptr<DMButton>> search_buttons_;
    std::vector<std::unique_ptr<ButtonWidget>> search_button_widgets_;
    std::string last_search_query_;
    bool search_open_ = false;
};

namespace {
static std::vector<std::string> kSpawnMethods{
    "Exact", "Random", "Percent", "Center", "Perimeter"
};

static bool method_uses_range(const std::string& m) {
    return !(m == "Exact" || m == "Center" || m == "Percent");
}
}  // namespace

SpawnGroupList::CandidateList::~CandidateList() {
    set_scroll_focus(false);
    DMWidgetsSetSliderScrollCapture(this, false);
    if (search_box_ && search_box_->is_editing()) {
        SDL_StopTextInput();
    }
}

void SpawnGroupList::CandidateList::rebuild() {
    candidates_.clear();
    hover_index_ = -1;
    hover_active_ = false;
    dirty_ = false;
    pending_commit_ = false;
    set_scroll_focus(false);
    total_weight_ = 0.0;
    if (!row_.entry) return;
    auto& entry = *row_.entry;
    if (!entry.contains("candidates") || !entry["candidates"].is_array()) return;
    const auto& arr = entry["candidates"];
    candidates_.reserve(arr.size());
    for (const auto& candidate : arr) {
        CandidateInfo info;
        info.name = candidate.value("name", std::string{"null"});
        info.weight = static_cast<double>(candidate.value("chance", 0));
        if (info.weight < 0.0) info.weight = 0.0;
        candidates_.push_back(std::move(info));
    }
    total_weight_ = std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
        [](double acc, const CandidateInfo& info) {
            return acc + std::max(0.0, info.weight);
        });
    if (!candidates_.empty() && total_weight_ <= 0.0) {
        total_weight_ = static_cast<double>(candidates_.size()) * 100.0;
        double share = total_weight_ / candidates_.size();
        for (auto& info : candidates_) info.weight = share;
        dirty_ = true;
        pending_commit_ = false;
    }
    ensure_common_widgets();
}

void SpawnGroupList::CandidateList::ensure_common_widgets() {
    if (!top_gap_)
        top_gap_ = std::make_unique<SpacerWidget>(std::max(6, DMSpacing::section_gap() / 2));
    if (!begin_marker_)
        begin_marker_ = std::make_unique<RowRectMarkerWidget>(&row_.candidates_rect, true);
    if (!end_marker_)
        end_marker_ = std::make_unique<RowRectMarkerWidget>(&row_.candidates_rect, false);
    if (!header_label_)
        header_label_ = std::make_unique<SectionLabelWidget>("Candidates", false);
    if (!bottom_gap_)
        bottom_gap_ = std::make_unique<SpacerWidget>(DMSpacing::item_gap());
    if (!add_btn_ && row_.entry) {
        add_btn_ = std::make_unique<DMButton>("Add Candidate", &DMStyles::CreateButton(), 160, DMButton::height());
        add_w_ = std::make_unique<ButtonWidget>(add_btn_.get(), [this]() {
            if (search_open_) {
                close_search();
            } else {
                open_search();
            }
        });
    }
    if (!pie_widget_)
        pie_widget_ = std::make_unique<CandidatePieWidget>(*this);
    if (header_label_)
        header_label_->set_text("Candidates");
}

void SpawnGroupList::CandidateList::ensure_search_widgets() {
    if (!search_gap_)
        search_gap_ = std::make_unique<SpacerWidget>(DMSpacing::small_gap());
    if (!search_box_)
        search_box_ = std::make_unique<DMTextBox>("Search Assets", "");
    if (!search_box_widget_)
        search_box_widget_ = std::make_unique<SearchTextBoxWidget>(*this, search_box_.get());
    else
        search_box_widget_->set_box(search_box_.get());
    if (!no_results_label_)
        no_results_label_ = std::make_unique<SectionLabelWidget>("No matching assets", true);
}

void SpawnGroupList::CandidateList::append_rows(DockableCollapsible::Rows& out) {
    ensure_common_widgets();
    if (top_gap_) out.push_back({ top_gap_.get() });
    if (begin_marker_) out.push_back({ begin_marker_.get() });

    DockableCollapsible::Row header_row;
    if (header_label_) header_row.push_back(header_label_.get());
    if (add_w_) header_row.push_back(add_w_.get());
    if (!header_row.empty()) out.push_back(header_row);
    if (search_open_) {
        ensure_search_widgets();
        if (search_gap_) out.push_back({ search_gap_.get() });
        if (search_box_widget_) out.push_back({ search_box_widget_.get() });
        if (search_button_widgets_.empty() && no_results_label_) {
            out.push_back({ no_results_label_.get() });
        } else {
            for (auto& w : search_button_widgets_) {
                if (w) out.push_back({ w.get() });
            }
        }
    }
    if (pie_widget_) out.push_back({ pie_widget_.get() });
    if (end_marker_) out.push_back({ end_marker_.get() });
    if (bottom_gap_) out.push_back({ bottom_gap_.get() });
}

void SpawnGroupList::CandidateList::open_search() {
    ensure_common_widgets();
    ensure_search_widgets();
    if (search_box_) {
        search_box_->set_value("");
    }
    last_search_query_.clear();
    search_open_ = true;
    refresh_search_results(true);
    owner_.rebuild_layout();
}

void SpawnGroupList::CandidateList::close_search() {
    if (!search_open_) return;
    if (search_box_ && search_box_->is_editing()) {
        SDL_StopTextInput();
    }
    search_open_ = false;
    last_search_query_.clear();
    search_results_.clear();
    search_buttons_.clear();
    search_button_widgets_.clear();
    if (search_box_) {
        search_box_->set_value("");
    }
    owner_.rebuild_layout();
}

bool SpawnGroupList::CandidateList::refresh_search_results(bool force) {
    if (!search_open_) return false;
    ensure_search_widgets();

    std::vector<SearchResult> previous_results = search_results_;
    std::string previous_query = last_search_query_;
    std::string query = search_box_ ? search_box_->value() : std::string{};
    if (!force && query == previous_query) {
        return false;
    }

    last_search_query_ = query;
    search_results_.clear();
    search_buttons_.clear();
    search_button_widgets_.clear();

    const auto& cache = candidate_asset_cache();
    std::string query_lc = lower_copy(query);
    bool query_is_tag = !query.empty() && query.front() == '#';
    std::string tag_query = query_is_tag ? query.substr(1) : query;
    std::string tag_query_lc = lower_copy(tag_query);

    auto contains_ci = [](const std::string& text, const std::string& needle_lc) {
        if (needle_lc.empty()) return true;
        std::string lowered = lower_copy(text);
        return lowered.find(needle_lc) != std::string::npos;
    };

    search_results_.push_back(SearchResult{"null", false, true});

    for (const auto& asset : cache.assets) {
        bool match = false;
        if (query_is_tag) {
            if (tag_query_lc.empty()) {
                match = true;
            } else {
                for (const auto& tag : asset.tags) {
                    if (contains_ci(tag, tag_query_lc)) { match = true; break; }
                }
            }
        } else {
            match = contains_ci(asset.name, query_lc);
            if (!match && !query_lc.empty()) {
                for (const auto& tag : asset.tags) {
                    if (contains_ci(tag, query_lc)) { match = true; break; }
                }
            }
        }
        if (match) {
            search_results_.push_back(SearchResult{asset.name, false, false});
        }
    }

    for (const auto& tag : cache.tags) {
        bool match = query_is_tag ? contains_ci(tag, tag_query_lc) : contains_ci(tag, query_lc);
        if (!match) continue;
        search_results_.push_back(SearchResult{"#" + tag, true, false});
    }

    search_buttons_.reserve(search_results_.size());
    search_button_widgets_.reserve(search_results_.size());
    for (size_t i = 0; i < search_results_.size(); ++i) {
        const auto& result = search_results_[i];
        auto btn = std::make_unique<DMButton>(result.label, &DMStyles::ListButton(), 220, DMButton::height());
        auto widget = std::make_unique<ButtonWidget>(btn.get(), [this, idx = static_cast<int>(i)]() {
            this->handle_search_selection(idx);
        });
        search_buttons_.push_back(std::move(btn));
        search_button_widgets_.push_back(std::move(widget));
    }

    if (no_results_label_) {
        no_results_label_->set_text("No matching assets or tags");
    }

    bool changed = query != previous_query;
    if (!changed) {
        if (search_results_.size() != previous_results.size()) {
            changed = true;
        } else {
            for (size_t i = 0; i < search_results_.size(); ++i) {
                const auto& a = search_results_[i];
                const auto& b = previous_results[i];
                if (a.label != b.label || a.is_tag != b.is_tag || a.is_null != b.is_null) {
                    changed = true;
                    break;
                }
            }
        }
    }

    return changed;
}

void SpawnGroupList::CandidateList::on_search_query_changed() {
    if (!search_open_) return;
    if (refresh_search_results(false)) {
        owner_.rebuild_layout();
    }
}

void SpawnGroupList::CandidateList::handle_search_selection(int index) {
    if (index < 0 || index >= static_cast<int>(search_results_.size())) return;
    const auto& result = search_results_[index];
    if (result.is_tag) {
        if (search_box_) {
            search_box_->set_value(result.label);
            on_search_query_changed();
        }
        return;
    }
    std::string asset_name = result.is_null ? std::string{"null"} : result.label;
    close_search();
    add_candidate(asset_name);
}

bool SpawnGroupList::CandidateList::sync_to_json() {
    if (!dirty_ || !row_.entry) return false;
    auto& entry = *row_.entry;
    if (!entry.contains("candidates") || !entry["candidates"].is_array()) {
        entry["candidates"] = json::array();
    }
    auto& arr = entry["candidates"];
    arr = json::array();

    if (candidates_.empty()) {
        dirty_ = false;
        pending_commit_ = false;
        return true;
    }

    double sum = std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
        [](double acc, const CandidateInfo& info) {
            return acc + std::max(0.0, info.weight);
        });
    if (sum <= 0.0) {
        sum = static_cast<double>(candidates_.size());
        for (auto& c : candidates_) c.weight = 1.0;
        total_weight_ = sum;
    }

    int target_total = static_cast<int>(std::round(total_weight_));
    if (target_total <= 0) {
        target_total = static_cast<int>(std::round(sum));
    }
    if (target_total < 0) target_total = 0;

    std::vector<int> rounded;
    rounded.reserve(candidates_.size());
    int remaining = target_total;
    for (size_t i = 0; i < candidates_.size(); ++i) {
        int value = static_cast<int>(std::round(std::max(0.0, candidates_[i].weight)));
        if (i + 1 == candidates_.size()) {
            value = std::max(0, remaining);
        } else {
            if (remaining < 0) remaining = 0;
            value = std::max(0, std::min(value, remaining));
            remaining -= value;
        }
        rounded.push_back(value);
        json cand;
        cand["name"] = candidates_[i].name;
        cand["chance"] = value;
        arr.push_back(std::move(cand));
    }
    if (!rounded.empty() && remaining > 0) {
        rounded.back() += remaining;
        arr.back()["chance"] = rounded.back();
    }
    int total_written = std::accumulate(rounded.begin(), rounded.end(), 0);
    total_weight_ = static_cast<double>(total_written);
    for (size_t i = 0; i < rounded.size(); ++i) {
        candidates_[i].weight = static_cast<double>(rounded[i]);
    }
    dirty_ = false;
    pending_commit_ = false;
    return true;
}

void SpawnGroupList::CandidateList::set_scroll_focus(bool focus) {
    if (scroll_focus_ == focus) {
        return;
    }
    scroll_focus_ = focus;
    DMWidgetsSetSliderScrollCapture(this, scroll_focus_);
    if (!scroll_focus_ && defer_commit_until_unfocus_) {
        if (pending_commit_) {
            dirty_ = true;
            pending_commit_ = false;
        }
    }
}

void SpawnGroupList::CandidateList::clear_hover() {
    hover_index_ = -1;
    hover_active_ = false;
    set_scroll_focus(false);
}

void SpawnGroupList::CandidateList::set_hover(int idx, bool inside) {
    if (!inside) {
        clear_hover();
        return;
    }
    hover_active_ = true;
    if (idx >= 0 && idx < static_cast<int>(candidates_.size())) {
        hover_index_ = idx;
    } else {
        hover_index_ = -1;
    }
}

bool SpawnGroupList::CandidateList::handle_scroll(int steps) {
    if (!scroll_focus_ || !has_hover() || steps == 0) return false;
    return adjust_weight_internal(hover_index_, steps);
}

bool SpawnGroupList::CandidateList::remove_hovered() {
    if (!has_hover()) return false;
    int idx = hover_index_;
    if (idx < 0 || idx >= static_cast<int>(candidates_.size())) return false;
    double removed = std::max(0.0, candidates_[idx].weight);
    candidates_.erase(candidates_.begin() + idx);
    if (candidates_.empty()) {
        total_weight_ = 100.0;
        clear_hover();
    } else {
        double sum = std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
            [](double acc, const CandidateInfo& info) { return acc + std::max(0.0, info.weight); });
        if (sum <= 0.0) {
            double share = (total_weight_ > 0.0 ? total_weight_ : 100.0) / candidates_.size();
            for (auto& c : candidates_) c.weight = share;
        } else {
            if (total_weight_ <= 0.0) total_weight_ = sum + removed;
            double factor = (sum > 0.0) ? (total_weight_ / sum) : 1.0;
            for (auto& c : candidates_) c.weight *= factor;
        }
        if (hover_index_ >= static_cast<int>(candidates_.size())) {
            hover_index_ = static_cast<int>(candidates_.size()) - 1;
        }
        renormalize();
    }
    dirty_ = true;
    pending_commit_ = false;
    return true;
}

void SpawnGroupList::CandidateList::ensure_positive_total() {
    if (total_weight_ > 0.0) return;
    if (candidates_.empty()) {
        total_weight_ = 100.0;
        return;
    }
    total_weight_ = std::accumulate(candidates_.begin(), candidates_.end(), 0.0,
        [](double acc, const CandidateInfo& info) { return acc + std::max(0.0, info.weight); });
    if (total_weight_ <= 0.0) {
        total_weight_ = static_cast<double>(candidates_.size()) * 100.0;
        double share = total_weight_ / candidates_.size();
        for (auto& c : candidates_) c.weight = share;
    }
}

void SpawnGroupList::CandidateList::renormalize() {
    if (candidates_.empty()) {
        total_weight_ = 0.0;
        return;
    }
    double sum = 0.0;
    for (auto& c : candidates_) {
        if (c.weight < 0.0) c.weight = 0.0;
        sum += c.weight;
    }
    if (sum <= 0.0) {
        double share = (total_weight_ > 0.0 ? total_weight_ : 1.0) / candidates_.size();
        for (auto& c : candidates_) c.weight = share;
        sum = share * candidates_.size();
    }
    if (total_weight_ <= 0.0) total_weight_ = sum;
    if (sum > 0.0) {
        double factor = total_weight_ / sum;
        for (auto& c : candidates_) c.weight *= factor;
    }
}

double SpawnGroupList::CandidateList::step_amount() const {
    double base = total_weight_;
    if (base <= 0.0) {
        base = static_cast<double>(std::max<size_t>(1, candidates_.size())) * 100.0;
    }
    double step = base * 0.01;
    if (step < 1.0) step = 1.0;
    return step;
}

bool SpawnGroupList::CandidateList::adjust_weight_internal(int idx, int steps) {
    if (idx < 0 || idx >= static_cast<int>(candidates_.size()) || steps == 0) return false;
    if (candidates_.size() <= 1) return false;
    ensure_positive_total();
    double total = total_weight_;
    if (total <= 0.0) return false;
    double current = std::max(0.0, candidates_[idx].weight);
    double others = std::max(0.0, total - current);
    double step = step_amount();
    if (steps > 0) {
        if (others <= 0.0) return false;
        double delta = step * steps;
        delta = std::min(delta, others);
        if (delta <= 0.0) return false;
        double factor = (others - delta) / std::max(1e-6, others);
        for (size_t i = 0; i < candidates_.size(); ++i) {
            if (static_cast<int>(i) == idx) continue;
            candidates_[i].weight *= factor;
        }
        candidates_[idx].weight = current + delta;
    } else {
        double delta = step * (-steps);
        delta = std::min(delta, current);
        if (delta <= 0.0) return false;
        double prev_others = std::max(0.0, others);
        candidates_[idx].weight = current - delta;
        double new_others = total - candidates_[idx].weight;
        if (candidates_.size() > 1) {
            if (prev_others > 0.0) {
                double factor = new_others / prev_others;
                for (size_t i = 0; i < candidates_.size(); ++i) {
                    if (static_cast<int>(i) == idx) continue;
                    candidates_[i].weight *= factor;
                }
            } else {
                double share = (candidates_.size() > 1) ? (new_others / (candidates_.size() - 1)) : 0.0;
                for (size_t i = 0; i < candidates_.size(); ++i) {
                    if (static_cast<int>(i) == idx) continue;
                    candidates_[i].weight = share;
                }
            }
        }
    }
    renormalize();
    if (defer_commit_until_unfocus_) {
        pending_commit_ = true;
    } else {
        dirty_ = true;
    }
    return true;
}

SDL_Color SpawnGroupList::CandidateList::color_for_index(size_t index, bool highlight) const {
    static const SDL_Color palette[] = {
        {230, 126, 34, 230}, {46, 204, 113, 230}, {52, 152, 219, 230}, {155, 89, 182, 230},
        {241, 196, 15, 230}, {231, 76, 60, 230}, {26, 188, 156, 230}, {149, 165, 166, 230}
    };
    const size_t palette_size = sizeof(palette) / sizeof(palette[0]);
    SDL_Color color = palette_size ? palette[index % palette_size] : SDL_Color{200, 200, 200, 230};
    if (highlight) {
        auto bump = [](Uint8 channel) -> Uint8 {
            int v = static_cast<int>(channel) + 40;
            if (v > 255) v = 255;
            return static_cast<Uint8>(v);
        };
        color.r = bump(color.r);
        color.g = bump(color.g);
        color.b = bump(color.b);
        color.a = 255;
    }
    return color;
}

void SpawnGroupList::CandidateList::add_candidate(const std::string& asset_name) {
    if (asset_name.empty()) return;
    ensure_positive_total();
    if (candidates_.empty()) {
        double base_total = std::max(100.0, total_weight_);
        candidates_.push_back(CandidateInfo{asset_name, base_total});
        total_weight_ = base_total;
    } else {
        double base_total = total_weight_;
        if (base_total <= 0.0) base_total = std::max(100.0, static_cast<double>(candidates_.size()) * 100.0);
        double target_total = std::max(base_total, 100.0);
        double new_share = std::max(1.0, target_total * 0.05);
        if (new_share >= target_total) {
            target_total = std::max(new_share * 1.05, new_share + 1.0);
        }
        double scale_factor = (base_total > 0.0) ? ((target_total - new_share) / base_total) : 0.0;
        if (scale_factor < 0.0) scale_factor = 0.0;
        for (auto& c : candidates_) c.weight *= scale_factor;
        candidates_.push_back(CandidateInfo{asset_name, new_share});
        total_weight_ = target_total;
    }
    hover_index_ = static_cast<int>(candidates_.size()) - 1;
    hover_active_ = true;
    renormalize();
    dirty_ = true;
    pending_commit_ = false;
}

int SpawnGroupList::CandidateList::CandidatePieWidget::height_for_width(int w) const {
    return std::max(220, w / 2);
}

SpawnGroupList::CandidateList::CandidatePieWidget::Layout
SpawnGroupList::CandidateList::CandidatePieWidget::compute_layout() const {
    Layout layout{};
    const int padding = DMSpacing::item_gap();
    int inner_w = std::max(0, rect_.w - padding * 2);
    int inner_h = std::max(0, rect_.h - padding * 2);
    int legend_width = std::min(200, inner_w / 3);
    int chart_w = inner_w - legend_width - padding;
    if (chart_w < 140) {
        legend_width = 0;
        chart_w = inner_w;
    }
    int chart_size = std::max(0, std::min(inner_h, chart_w));
    if (chart_size <= 0) chart_size = std::max(0, std::min(inner_w, inner_h));
    layout.radius = chart_size * 0.5f;
    float center_x = rect_.x + padding + layout.radius;
    float center_y = rect_.y + padding + inner_h * 0.5f;
    if (legend_width == 0) {
        center_x = rect_.x + rect_.w * 0.5f;
    }
    layout.center = SDL_FPoint{center_x, center_y};
    layout.legend = SDL_Rect{rect_.x + padding + chart_w + padding, rect_.y + padding, legend_width, inner_h};
    if (legend_width == 0) {
        layout.legend = SDL_Rect{rect_.x + padding, rect_.y + padding, inner_w, inner_h};
    }
    return layout;
}

int SpawnGroupList::CandidateList::CandidatePieWidget::index_for_point(int x, int y) const {
    const auto& candidates = list_.candidates();
    if (candidates.empty()) return -1;
    Layout layout = compute_layout();
    if (layout.radius <= 0.f) return -1;
    float dx = static_cast<float>(x) - layout.center.x;
    float dy = static_cast<float>(y) - layout.center.y;
    float dist_sq = dx * dx + dy * dy;
    if (dist_sq > layout.radius * layout.radius) return -1;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;
    constexpr double kStartAngle = -1.5707963267948966;
    double total = list_.total_weight();
    if (total <= 0.0) {
        total = std::accumulate(candidates.begin(), candidates.end(), 0.0,
            [](double acc, const auto& info) { return acc + std::max(0.0, info.weight); });
        if (total <= 0.0) return -1;
    }
    double angle = std::atan2(dy, dx);
    double relative = angle - kStartAngle;
    while (relative < 0.0) relative += kTwoPi;
    while (relative >= kTwoPi) relative -= kTwoPi;
    double accum = 0.0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        double weight = std::max(0.0, candidates[i].weight);
        double portion = (total > 0.0) ? (weight / total) : 0.0;
        double sweep = portion * kTwoPi;
        if (i + 1 == candidates.size()) {
            sweep = kTwoPi - accum;
        }
        accum += sweep;
        if (relative <= accum + 1e-6) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool SpawnGroupList::CandidateList::CandidatePieWidget::handle_event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_MOUSEMOTION: {
        bool inside = e.motion.x >= rect_.x && e.motion.x < rect_.x + rect_.w &&
                      e.motion.y >= rect_.y && e.motion.y < rect_.y + rect_.h;
        int idx = inside ? index_for_point(e.motion.x, e.motion.y) : -1;
        list_.set_hover(idx, inside);
        return inside;
    }
    case SDL_MOUSEBUTTONDOWN: {
        bool inside = e.button.x >= rect_.x && e.button.x < rect_.x + rect_.w &&
                      e.button.y >= rect_.y && e.button.y < rect_.y + rect_.h;
        int idx = inside ? index_for_point(e.button.x, e.button.y) : -1;
        list_.set_hover(idx, inside);
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (inside && idx >= 0) {
                list_.set_scroll_focus(true);
                if (e.button.clicks >= 2) {
                    return list_.remove_hovered();
                }
                return true;
            }
            list_.set_scroll_focus(false);
        }
        return inside;
    }
    case SDL_MOUSEWHEEL: {
        int mouse_x = 0;
        int mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        bool inside = mouse_x >= rect_.x && mouse_x < rect_.x + rect_.w &&
                      mouse_y >= rect_.y && mouse_y < rect_.y + rect_.h;
        if (inside) {
            int idx = index_for_point(mouse_x, mouse_y);
            list_.set_hover(idx, true);
        }
        bool has_hover = list_.has_hover();
        bool focused = list_.scroll_focused();
        int steps = e.wheel.y;
#if SDL_VERSION_ATLEAST(2,0,18)
        if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) steps = -steps;
#endif
        if (steps != 0 && has_hover && focused) {
            if (list_.handle_scroll(steps)) {
                return true;
            }
        }
        if (focused || inside || has_hover) {
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void SpawnGroupList::CandidateList::CandidatePieWidget::render(SDL_Renderer* r) const {
    if (!r) return;
    const auto& candidates = list_.candidates();
    Layout layout = compute_layout();
    float radius = layout.radius;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color border = DMStyles::Border();
    border.a = 220;
    auto draw_text = [&](TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center) {
        SDL_Rect dst{0, 0, 0, 0};
        if (!font) return dst;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
        if (!surf) return dst;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        dst = SDL_Rect{x, y, surf->w, surf->h};
        if (center) {
            dst.x -= dst.w / 2;
            dst.y -= dst.h / 2;
        }
        if (tex) {
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
        return dst;
    };

    int font_size = std::max(11, DMStyles::Label().font_size - 1);
    TTF_Font* font = TTF_OpenFont(DMStyles::Label().font_path.c_str(), font_size);

    if (candidates.empty() || radius <= 0.f) {
        const int segments = 64;
        std::vector<SDL_Point> outline;
        outline.reserve(segments + 1);
        constexpr double kTwoPi = 6.28318530717958647692;
        constexpr double kStartAngle = -1.5707963267948966;
        for (int i = 0; i <= segments; ++i) {
            double t = kStartAngle + kTwoPi * (static_cast<double>(i) / segments);
            outline.push_back(SDL_Point{static_cast<int>(layout.center.x + radius * std::cos(t)),
                                        static_cast<int>(layout.center.y + radius * std::sin(t))});
        }
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
        if (!outline.empty()) SDL_RenderDrawLines(r, outline.data(), static_cast<int>(outline.size()));
        draw_text(font, "No candidates configured", static_cast<int>(layout.center.x), static_cast<int>(layout.center.y), DMStyles::Label().color, true);
        if (font) TTF_CloseFont(font);
        return;
    }

    double total = list_.total_weight();
    if (total <= 0.0) {
        total = std::accumulate(candidates.begin(), candidates.end(), 0.0,
            [](double acc, const auto& info) { return acc + std::max(0.0, info.weight); });
        if (total <= 0.0) total = 1.0;
    }
    constexpr double kPi = 3.14159265358979323846;

    constexpr double kTwoPi = 6.28318530717958647692;
    constexpr double kStartAngle = -1.5707963267948966;
    double angle = kStartAngle;
    double used = 0.0;
    int hovered = list_.hovered_index();

    for (size_t i = 0; i < candidates.size(); ++i) {
        double weight = std::max(0.0, candidates[i].weight);
        double portion = (total > 0.0) ? (weight / total) : 0.0;
        double sweep = portion * kTwoPi;
        if (i + 1 == candidates.size()) {
            sweep = kTwoPi - used;
        }
        if (sweep <= 0.0) {
            used += sweep;
            angle += sweep;
            continue;
        }
        bool highlight = (static_cast<int>(i) == hovered);
        SDL_Color color = list_.color_for_index(i, highlight);
        float slice_radius = radius + (highlight ? 6.0f : 0.0f);
        int segments = std::max(6, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 32.0))));
#if SDL_VERSION_ATLEAST(2,0,18)
        std::vector<SDL_Vertex> verts;
        verts.reserve(segments + 2);
        SDL_Vertex center_vert{};
        center_vert.position = SDL_FPoint{layout.center.x, layout.center.y};
        center_vert.color = color;
        verts.push_back(center_vert);
        for (int s = 0; s <= segments; ++s) {
            double t = angle + sweep * (static_cast<double>(s) / segments);
            SDL_Vertex v{};
            v.position = SDL_FPoint{layout.center.x + slice_radius * static_cast<float>(std::cos(t)),
                                    layout.center.y + slice_radius * static_cast<float>(std::sin(t))};
            v.color = color;
            verts.push_back(v);
        }
        std::vector<int> idxs;
        idxs.reserve(segments * 3);
        for (int s = 1; s <= segments; ++s) {
            idxs.push_back(0);
            idxs.push_back(s);
            idxs.push_back(s + 1);
        }
        SDL_RenderGeometry(r, nullptr, verts.data(), static_cast<int>(verts.size()), idxs.data(), static_cast<int>(idxs.size()));
#else
        SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
        for (int s = 0; s <= segments; ++s) {
            double t = angle + sweep * (static_cast<double>(s) / segments);
            SDL_RenderDrawLine(r,
                static_cast<int>(layout.center.x),
                static_cast<int>(layout.center.y),
                static_cast<int>(layout.center.x + slice_radius * std::cos(t)),
                static_cast<int>(layout.center.y + slice_radius * std::sin(t)));
        }
#endif
        used += sweep;
        angle += sweep;
    }

    const int outline_segments = 96;
    std::vector<SDL_Point> outline;
    outline.reserve(outline_segments + 1);
    float outline_radius = radius + 6.0f;
    for (int i = 0; i <= outline_segments; ++i) {
        double t = kStartAngle + kTwoPi * (static_cast<double>(i) / outline_segments);
        outline.push_back(SDL_Point{static_cast<int>(layout.center.x + outline_radius * std::cos(t)),
                                    static_cast<int>(layout.center.y + outline_radius * std::sin(t))});
    }
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    if (!outline.empty()) SDL_RenderDrawLines(r, outline.data(), static_cast<int>(outline.size()));

    if (layout.legend.w > 60 && font) {
        SDL_Color text_color = DMStyles::Label().color;
        int font_height = TTF_FontHeight(font);
        int row_height = std::max(font_height + 6, 20);
        int y = layout.legend.y;
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (y + row_height > layout.legend.y + layout.legend.h) break;
            SDL_Color swatch = list_.color_for_index(i, static_cast<int>(i) == hovered);
            SDL_Rect box{layout.legend.x, y, 16, 16};
            SDL_SetRenderDrawColor(r, swatch.r, swatch.g, swatch.b, 255);
            SDL_RenderFillRect(r, &box);
            SDL_Color outline_col = DMStyles::Border();
            outline_col.a = 255;
            SDL_SetRenderDrawColor(r, outline_col.r, outline_col.g, outline_col.b, outline_col.a);
            SDL_RenderDrawRect(r, &box);
            double percent = (total > 0.0) ? (std::max(0.0, candidates[i].weight) / total) * 100.0 : 0.0;
            std::ostringstream label;
            label << candidates[i].name << " - " << std::fixed << std::setprecision(1) << percent << "% ("
                  << static_cast<int>(std::round(std::max(0.0, candidates[i].weight))) << ")";
            draw_text(font, label.str(), box.x + box.w + 8, y + (row_height - font_height) / 2, text_color, false);
            y += row_height;
        }
    } else if (font) {
        std::ostringstream summary;
        summary << "Total weight: " << static_cast<int>(std::round(total));
        draw_text(font, summary.str(), rect_.x + DMSpacing::item_gap(), rect_.y + DMSpacing::item_gap(), DMStyles::Label().color, false);
    }

    if (font) TTF_CloseFont(font);
}

class AreaLinkPanel {
public:
    AreaLinkPanel();

    void set_screen_dimensions(int w, int h);
    void set_anchor(SDL_Point anchor);
    void set_parent_rect(const SDL_Rect& rect);

    void open(const std::vector<std::string>& areas,
              std::function<void(const std::string&)> on_select);
    void close();
    bool visible() const;

    void update(const Input& input);
    bool handle_event(const SDL_Event& e);
    void render(SDL_Renderer* r) const;

private:
    void ensure_panel();
    void rebuild_rows();
    void apply_default_position();

    std::unique_ptr<DockableCollapsible> panel_;
    std::vector<std::unique_ptr<DMButton>> buttons_;
    std::vector<std::unique_ptr<ButtonWidget>> button_widgets_;
    std::vector<std::string> areas_;
    std::function<void(const std::string&)> on_select_;
    SDL_Point anchor_{0,0};
    SDL_Rect parent_rect_{0,0,0,0};
    int screen_w_ = 1920;
    int screen_h_ = 1080;
};

AreaLinkPanel::AreaLinkPanel() = default;

void AreaLinkPanel::set_screen_dimensions(int w, int h) {
    screen_w_ = std::max(0, w);
    screen_h_ = std::max(0, h);
    if (panel_) {
        Input dummy;
        panel_->update(dummy, screen_w_, screen_h_);
    }
}

void AreaLinkPanel::set_anchor(SDL_Point anchor) {
    anchor_ = anchor;
    apply_default_position();
}

void AreaLinkPanel::set_parent_rect(const SDL_Rect& rect) {
    parent_rect_ = rect;
    apply_default_position();
}

void AreaLinkPanel::ensure_panel() {
    if (panel_) return;
    panel_ = std::make_unique<DockableCollapsible>("Areas", true, 0, 0);
    panel_->set_show_header(true);
    panel_->set_close_button_enabled(true);
    panel_->set_scroll_enabled(true);
    panel_->set_visible_height(320);
    panel_->set_cell_width(220);
    panel_->set_on_close([this]() {
        on_select_ = nullptr;
    });
}

void AreaLinkPanel::open(const std::vector<std::string>& areas,
                         std::function<void(const std::string&)> on_select) {
    ensure_panel();
    if (!panel_) return;
    areas_ = areas;
    on_select_ = std::move(on_select);
    rebuild_rows();
    panel_->set_visible(true);
    panel_->set_expanded(true);
    panel_->reset_scroll();
    FloatingDockableManager::instance().open_floating(
        "Area Picker", panel_.get(), [this]() { this->close(); }, "spawn-group-area");
    panel_->force_pointer_ready();
    apply_default_position();
    Input dummy;
    panel_->update(dummy, screen_w_, screen_h_);
    apply_default_position();
}

void AreaLinkPanel::close() {
    if (!panel_) return;
    panel_->set_visible(false);
    on_select_ = nullptr;
}

bool AreaLinkPanel::visible() const {
    return panel_ && panel_->is_visible();
}

void AreaLinkPanel::update(const Input& input) {
    if (!panel_ || !panel_->is_visible()) return;
    panel_->update(input, screen_w_, screen_h_);
}

bool AreaLinkPanel::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    return panel_->handle_event(e);
}

void AreaLinkPanel::render(SDL_Renderer* r) const {
    if (!panel_ || !panel_->is_visible()) return;
    panel_->render(r);
}

void AreaLinkPanel::rebuild_rows() {
    if (!panel_) return;
    buttons_.clear();
    button_widgets_.clear();
    DockableCollapsible::Rows rows;
    for (const auto& area : areas_) {
        auto btn = std::make_unique<DMButton>(area, &DMStyles::ListButton(), 200, DMButton::height());
        auto widget = std::make_unique<ButtonWidget>(btn.get(), [this, area]() {
            if (on_select_) on_select_(area);
        });
        buttons_.push_back(std::move(btn));
        button_widgets_.push_back(std::move(widget));
        rows.push_back({ button_widgets_.back().get() });
    }
    if (rows.empty()) {
        auto none_btn = std::make_unique<DMButton>("No Areas", &DMStyles::ListButton(), 200, DMButton::height());
        none_btn->set_style(&DMStyles::ListButton());
        none_btn->set_text("No Areas");
        auto none_widget = std::make_unique<ButtonWidget>(none_btn.get());
        buttons_.push_back(std::move(none_btn));
        button_widgets_.push_back(std::move(none_widget));
        rows.push_back({ button_widgets_.back().get() });
    }
    panel_->set_rows(rows);
    Input dummy;
    panel_->update(dummy, screen_w_, screen_h_);
}

void AreaLinkPanel::apply_default_position() {
    if (!panel_) return;
    int width = panel_->rect().w;
    if (width <= 0) width = 220;
    int spacing = DMSpacing::item_gap();
    int x = parent_rect_.x - width - spacing;
    if (x <= 0) {
        x = std::max(spacing, anchor_.x - width - spacing);
    }
    if (x < DMSpacing::panel_padding()) {
        x = DMSpacing::panel_padding();
    }
    int y = parent_rect_.y;
    panel_->set_position(x, y);
}

// RowController methods
void SpawnGroupList::RowController::set_ownership_label(const std::string& label, SDL_Color color) {
    if (!row_) return;
    row_->owner_label = label;
    row_->owner_color = color;
}
void SpawnGroupList::RowController::clear_ownership_label() {
    if (!row_) return;
    row_->owner_label.clear();
}
void SpawnGroupList::RowController::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    if (!row_) return;
        row_->area_names_provider = std::move(provider);
}
void SpawnGroupList::RowController::set_stack_key(std::string) {}
void SpawnGroupList::RowController::lock_method_to(const std::string& method) {
    if (!row_) return;
    row_->method_lock = method;
}
void SpawnGroupList::RowController::clear_method_lock() {
    if (!row_) return;
    row_->method_lock.clear();
}
void SpawnGroupList::RowController::set_quantity_hidden(bool hidden) {
    if (!row_) return;
    row_->quantity_hidden = hidden;
}

SpawnGroupList::SpawnGroupList(bool floatable)
    : DockableCollapsible("Spawn Groups", floatable) {
    set_scroll_enabled(true);
    set_cell_width(320);
    set_row_gap(DMSpacing::item_gap());
    set_col_gap(DMSpacing::item_gap());
}

SpawnGroupList::~SpawnGroupList() = default;

void SpawnGroupList::set_screen_dimensions(int width, int height) {
    screen_w_ = std::max(0, width);
    screen_h_ = std::max(0, height);
}

static std::string entry_display_name(const json& e) {
    if (!e.is_object()) return std::string{};
    if (e.contains("display_name") && e["display_name"].is_string()) return e["display_name"].get<std::string>();
    if (e.contains("name") && e["name"].is_string()) return e["name"].get<std::string>();
    if (e.contains("spawn_id") && e["spawn_id"].is_string()) return e["spawn_id"].get<std::string>();
    return std::string{"Spawn"};
}

void SpawnGroupList::load(json& groups,
                          std::function<void()> on_change,
                          std::function<void(const json&, const ChangeSummary&)> on_entry_change,
                          ConfigureEntryCallback configure_entry) {
    bound_array_ = &groups;
    on_change_ = std::move(on_change);
    on_entry_change_ = std::move(on_entry_change);
    configure_entry_ = std::move(configure_entry);
    rows_.clear();
    if (!groups.is_array()) return;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (!groups[i].is_object()) continue;
        auto row = std::make_unique<EntryRow>();
        row->array = &groups;
        row->entry = &groups[i];
        row->index = static_cast<int>(i);
        row->read_only = false;
        row->id = groups[i].value("spawn_id", std::string{});
        if (configure_entry_) {
            RowController ctl(row.get());
            try { configure_entry_(ctl, *row->entry); } catch (...) {}
        }
        rows_.push_back(std::move(row));
    }
    request_layout();
}

void SpawnGroupList::load(const json& groups) {
    readonly_snapshot_ = groups;
    bound_array_ = nullptr;
    on_change_ = nullptr;
    rows_.clear();
    if (!readonly_snapshot_.is_array()) return;
    for (size_t i = 0; i < readonly_snapshot_.size(); ++i) {
        if (!readonly_snapshot_[i].is_object()) continue;
        auto row = std::make_unique<EntryRow>();
        row->ro_entry = readonly_snapshot_[i];
        row->index = static_cast<int>(i);
        row->read_only = true;
        row->id = row->ro_entry.value("spawn_id", std::string{});
        rows_.push_back(std::move(row));
    }
    request_layout();
}

void SpawnGroupList::append_rows(Rows& rows) {
    Rows out;

    // Top-level Add Spawn Group button
    if (!add_group_btn_) {
        add_group_btn_ = std::make_unique<DMButton>("Add Spawn Group", &DMStyles::CreateButton(), 160, DMButton::height());
        add_group_btn_w_ = std::make_unique<ButtonWidget>(add_group_btn_.get(), [this]() {
            if (callbacks_.on_add) callbacks_.on_add();
        });
    }
    out.push_back({ add_group_btn_w_.get() });
    for (auto& r : rows_) {
        // Header
        r->outline_rect = SDL_Rect{0,0,0,0};
        r->body_rect = SDL_Rect{0,0,0,0};
        r->candidates_rect = SDL_Rect{0,0,0,0};
        if (!r->outline_begin_marker)
            r->outline_begin_marker = std::make_unique<RowRectMarkerWidget>(&r->outline_rect, true);
        if (!r->outline_end_marker)
            r->outline_end_marker = std::make_unique<RowRectMarkerWidget>(&r->outline_rect, false);
        out.push_back({ r->outline_begin_marker.get() });
        if (!r->toggle_btn) {
            const std::string label = entry_display_name(r->read_only ? r->ro_entry : *r->entry);
            r->toggle_btn = std::make_unique<DMButton>(label, &DMStyles::ListButton(), 180, DMButton::height());
            r->toggle_w = std::make_unique<ButtonWidget>(r->toggle_btn.get(), [this, rr=r.get()](){
                rr->expanded = !rr->expanded;
                this->rebuild_layout();
            });
            r->dup_btn = std::make_unique<DMButton>("+", &DMStyles::ListButton(), 28, DMButton::height());
            r->dup_w   = std::make_unique<ButtonWidget>(r->dup_btn.get(), [this, rr=r.get()](){ if (callbacks_.on_duplicate) callbacks_.on_duplicate(rr->id); });
            r->up_btn = std::make_unique<DMButton>("↑", &DMStyles::ListButton(), 28, DMButton::height());
            r->up_w   = std::make_unique<ButtonWidget>(r->up_btn.get(), [this, rr=r.get()](){ if (callbacks_.on_move_up) callbacks_.on_move_up(rr->id); });
            r->down_btn = std::make_unique<DMButton>("↓", &DMStyles::ListButton(), 28, DMButton::height());
            r->down_w   = std::make_unique<ButtonWidget>(r->down_btn.get(), [this, rr=r.get()](){ if (callbacks_.on_move_down) callbacks_.on_move_down(rr->id); });
            r->del_btn = std::make_unique<DMButton>("X", &DMStyles::DeleteButton(), 28, DMButton::height());
            r->del_w   = std::make_unique<ButtonWidget>(r->del_btn.get(), [this, rr=r.get()](){ if (callbacks_.on_delete) callbacks_.on_delete(rr->id); });
        } else {
            r->toggle_btn->set_text(entry_display_name(r->read_only ? r->ro_entry : *r->entry));
        }
        out.push_back({ r->toggle_w.get(), r->dup_w.get(), r->up_w.get(), r->down_w.get(), r->del_w.get() });

        if (!r->owner_label.empty()) {
            if (!r->owner_label_widget) {
                int font_sz = std::max(10, DMStyles::Label().font_size - 2);
                r->owner_label_widget = std::make_unique<SectionLabelWidget>(r->owner_label, true, r->owner_color, font_sz);
            } else {
                r->owner_label_widget->set_text(r->owner_label);
                r->owner_label_widget->set_color(r->owner_color);
                r->owner_label_widget->set_font_size(std::max(10, DMStyles::Label().font_size - 2));
            }
            out.push_back({ r->owner_label_widget.get() });
        } else {
            r->owner_label_widget.reset();
        }

        // Body if expanded
        if (r->expanded) {
            if (!r->read_only) {
                if (!r->body_begin_marker)
                    r->body_begin_marker = std::make_unique<RowRectMarkerWidget>(&r->body_rect, true);
                if (!r->body_end_marker)
                    r->body_end_marker = std::make_unique<RowRectMarkerWidget>(&r->body_rect, false);
                if (r->body_begin_marker)
                    out.push_back({ r->body_begin_marker.get() });

                if (!r->body_top_gap)
                    r->body_top_gap = std::make_unique<SpacerWidget>(DMSpacing::item_gap());
                out.push_back({ r->body_top_gap.get() });

                // Build/editable controls
                if (!r->name_box) {
                    r->name_box = std::make_unique<DMTextBox>("Name", entry_display_name(*r->entry));
                    r->name_w = std::make_unique<TextBoxWidget>(r->name_box.get(), true);
                }
                if (r->method_lock.empty() && !r->method_dd) {
                    int idx = 0;
                    const std::string method = r->entry->value("position", std::string{"Exact"});
                    for (size_t i = 0; i < kSpawnMethods.size(); ++i) if (kSpawnMethods[i] == method) { idx = static_cast<int>(i); break; }
                    r->method_dd = std::make_unique<DMDropdown>("Spawn Method", kSpawnMethods, idx);
                    r->method_w  = std::make_unique<DropdownWidget>(r->method_dd.get());
                }
                if (!r->qty_sl) {
                    int mn = r->entry->value("min_number", r->entry->value("max_number", 1));
                    int mx = r->entry->value("max_number", mn);
                    r->qty_sl = std::make_unique<DMRangeSlider>(0, 100, mn, mx);
                    r->qty_sl->set_defer_commit_until_unfocus(true);
                    r->qty_w  = std::make_unique<RangeSliderWidget>(r->qty_sl.get());
                }
                // Area link button
                std::string link_value = r->entry->value("link", std::string{});
                const std::string link_label = link_value.empty() ? std::string("Link Area") : link_value;
                if (!r->link_btn) {
                    r->link_btn = std::make_unique<DMButton>(link_label, &DMStyles::ListButton(), 180, DMButton::height());
                    r->link_btn_w = std::make_unique<ButtonWidget>(r->link_btn.get(), [this, rr=r.get()](){
                        if (!rr->entry) return;
                        std::string current = rr->entry->value("link", std::string{});
                        if (!current.empty()) {
                            (*rr->entry)["link"] = std::string{};
                            if (on_change_) on_change_();
                            this->close_area_panel();
                            this->rebuild_layout();
                        } else {
                            this->open_area_panel(*rr);
                        }
                    });
                } else {
                    r->link_btn->set_text(link_label);
                }

                if (!r->general_label)
                    r->general_label = std::make_unique<SectionLabelWidget>("General Settings", true);
                else
                    r->general_label->set_text("General Settings");
                out.push_back({ r->general_label.get() });
                out.push_back({ r->name_w.get() });
                DockableCollapsible::Row method_row;
                if (r->method_w) method_row.push_back(r->method_w.get());
                if (r->link_btn_w) method_row.push_back(r->link_btn_w.get());
                if (!method_row.empty()) out.push_back(method_row);
                const std::string method = r->entry->value("position", std::string{"Exact"});
                if (!r->quantity_hidden && method_uses_range(method)) {
                    out.push_back({ r->qty_w.get() });
                }

                if (!r->candidate_list)
                    r->candidate_list = std::make_unique<CandidateList>(*this, *r, true);
                r->candidate_list->rebuild();
                r->candidate_list->append_rows(out);
                if (r->body_end_marker)
                    out.push_back({ r->body_end_marker.get() });
            } else {
                // Readonly body: just show name and method
                if (!r->name_box) {
                    r->name_box = std::make_unique<DMTextBox>("Name", entry_display_name(r->ro_entry));
                    r->name_w = std::make_unique<TextBoxWidget>(r->name_box.get(), true);
                }
                out.push_back({ r->name_w.get() });
            }
        } else {
            r->body_rect = SDL_Rect{0,0,0,0};
            EntryRow* area_row = lookup_row(area_panel_row_ref_);
            if ((area_row && area_row == r.get()) || (!area_row && area_panel_row_ref_.valid())) {
                close_area_panel();
            }
            EntryRow* search_row = lookup_row(asset_search_row_ref_);
            if ((search_row && search_row == r.get()) || (!search_row && asset_search_row_ref_.valid())) {
                close_asset_search();
            }
        }
        out.push_back({ r->outline_end_marker.get() });
    }
    // Make content available for embedding and floating modes
    layout_dirty_ = false;
    set_rows(out);
    for (const auto& rr : out) rows.push_back(rr);
}

void SpawnGroupList::set_callbacks(Callbacks cb) { callbacks_ = std::move(cb); }

void SpawnGroupList::set_on_layout_changed(std::function<void()> cb) {
    on_layout_change_ = std::move(cb);
}

void SpawnGroupList::expand_group(const std::string& id) {
    if (auto* r = find_row(id)) { r->expanded = true; rebuild_layout(); }
}
void SpawnGroupList::collapse_group(const std::string& id) {
    if (auto* r = find_row(id)) { r->expanded = false; rebuild_layout(); }
}
bool SpawnGroupList::is_expanded(const std::string& id) const {
    return find_row(id) ? find_row(id)->expanded : false;
}

std::vector<std::string> SpawnGroupList::expanded_groups() const {
    std::vector<std::string> out;
    for (const auto& r : rows_) if (r->expanded && !r->id.empty()) out.push_back(r->id);
    return out;
}
void SpawnGroupList::restore_expanded_groups(const std::vector<std::string>& ids) {
    std::unordered_set<std::string> wanted(ids.begin(), ids.end());
    bool changed = false;
    for (auto& row : rows_) {
        const bool should_expand = !row->id.empty() && wanted.find(row->id) != wanted.end();
        if (row->expanded != should_expand) {
            row->expanded = should_expand;
            changed = true;
        }
    }
    if (changed) {
        suppress_layout_callback_ = true;
        rebuild_layout();
        suppress_layout_callback_ = false;
    }
}

nlohmann::json SpawnGroupList::to_json() const {
    if (bound_array_) return *bound_array_;
    return readonly_snapshot_;
}

void SpawnGroupList::update(const Input& input, int screen_w, int screen_h) {
    set_screen_dimensions(screen_w, screen_h);
    if (asset_search_) {
        asset_search_->set_screen_dimensions(screen_w_, screen_h_);
        asset_search_->set_anchor_position(anchor_.x, anchor_.y);
    }
    if (area_panel_) {
        area_panel_->set_screen_dimensions(screen_w_, screen_h_);
        area_panel_->set_anchor(anchor_);
        SDL_Rect parent = rect();
        if (auto* row = lookup_row(area_panel_row_ref_)) {
            const SDL_Rect& body = row->body_rect;
            if (body.w > 0 && body.h > 0) parent = body;
        } else if (area_panel_row_ref_.valid()) {
            close_area_panel();
        }
        area_panel_->set_parent_rect(parent);
    }
    DockableCollapsible::update(input, screen_w, screen_h);
    if (pending_asset_search_open_) {
        if (is_visible()) {
            if (EntryRow* target = lookup_row(pending_asset_search_row_ref_)) {
                open_asset_search(*target, std::move(pending_asset_search_callback_));
            }
        }
        pending_asset_search_callback_ = {};
        clear_row_ref(pending_asset_search_row_ref_);
        pending_asset_search_open_ = false;
    }
    if (!is_visible()) {
        close_area_panel();
        close_asset_search();
    }
    if (area_panel_) {
        area_panel_->update(input);
        if (!area_panel_->visible()) clear_row_ref(area_panel_row_ref_);
    }
    if (asset_search_) {
        asset_search_->update(input);
        if (!asset_search_->visible()) clear_row_ref(asset_search_row_ref_);
    }
    // Sync editable widgets back into JSON
    if (!bound_array_) return;
    for (auto& r : rows_) {
        if (r->read_only || !r->entry) continue;
        bool changed = false;
        // Enforce method lock
        if (!r->method_lock.empty()) {
            if (r->entry->value("position", std::string{}) != r->method_lock) {
                (*r->entry)["position"] = r->method_lock;
                changed = true;
                if (on_entry_change_) {
                    ChangeSummary cs{}; cs.method_changed = true; cs.method = r->method_lock;
                    on_entry_change_(*r->entry, cs);
                }
            }
        }
        if (r->name_box && r->name_box->value() != r->entry->value("display_name", std::string{})) {
            (*r->entry)["display_name"] = r->name_box->value();
            changed = true;
        }
        if (r->method_dd) {
            int idx = std::clamp(r->method_dd->selected(), 0, (int)kSpawnMethods.size()-1);
            const std::string method = kSpawnMethods[idx];
            if (r->entry->value("position", std::string{}) != method) {
                (*r->entry)["position"] = method;
                changed = true;
                if (on_entry_change_) {
                    ChangeSummary cs{}; cs.method_changed = true; cs.method = method;
                    on_entry_change_(*r->entry, cs);
                }
            }
        }
        if (r->qty_sl) {
            int mn = std::max(0, r->qty_sl->min_value());
            int mx = std::max(mn, r->qty_sl->max_value());
            if (r->entry->value("min_number", mn) != mn) { (*r->entry)["min_number"] = mn; changed = true; }
            if (r->entry->value("max_number", mx) != mx) { (*r->entry)["max_number"] = mx; changed = true; }
            if (changed && on_entry_change_) {
                ChangeSummary cs{}; cs.quantity_changed = true; cs.method = r->entry->value("position", std::string{});
                on_entry_change_(*r->entry, cs);
            }
        }
        if (r->candidate_list && r->candidate_list->sync_to_json()) {
            changed = true;
        }
        if (changed && on_change_) on_change_();
    }
}

bool SpawnGroupList::handle_event(const SDL_Event& e) {
    bool used = false;
    if (area_panel_ && area_panel_->handle_event(e)) used = true;
    if (asset_search_ && asset_search_->handle_event(e)) used = true;
    if (DockableCollapsible::handle_event(e)) used = true;
    // Drop-down overlay rendering is handled in widgets
    return used;
}

void SpawnGroupList::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
    if (area_panel_) area_panel_->render(r);
    if (asset_search_) asset_search_->render(r);
    DMDropdown::render_active_options(r);
}

void SpawnGroupList::render_content(SDL_Renderer* r) const {
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color accent = DMStyles::AccentButton().border;
    SDL_Color inner = DMStyles::AccentButton().bg;
    inner.a = 40;
    accent.a = 200;
    SDL_Color separator = DMStyles::Border();
    separator.a = 255;
    for (const auto& row : rows_) {
        if (!row) continue;
        SDL_Rect rect = row->outline_rect;
        if (row->expanded && row->body_rect.w > 0 && row->body_rect.h > 0) {
            if (rect.w <= 0 || rect.h <= 0) {
                rect = row->body_rect;
            } else {
                int left = std::min(rect.x, row->body_rect.x);
                int top = std::min(rect.y, row->body_rect.y);
                int right = std::max(rect.x + rect.w, row->body_rect.x + row->body_rect.w);
                int bottom = std::max(rect.y + rect.h, row->body_rect.y + row->body_rect.h);
                rect = SDL_Rect{left, top, right - left, bottom - top};
            }
        }
        if (rect.w <= 0 || rect.h <= 0) continue;
        SDL_Rect outline = rect;
        outline.x -= 4;
        outline.y -= 4;
        outline.w += 8;
        outline.h += 8;
        if (outline.w <= 0 || outline.h <= 0) continue;

        SDL_Color panel_fill = DMStyles::PanelBG();
        panel_fill.a = 35;
        SDL_SetRenderDrawColor(r, panel_fill.r, panel_fill.g, panel_fill.b, panel_fill.a);
        SDL_RenderFillRect(r, &outline);

        SDL_SetRenderDrawColor(r, inner.r, inner.g, inner.b, inner.a);
        SDL_RenderDrawRect(r, &outline);
        SDL_Rect inner_outline = outline;
        inner_outline.x += 1;
        inner_outline.y += 1;
        inner_outline.w -= 2;
        inner_outline.h -= 2;
        SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, accent.a);
        SDL_RenderDrawRect(r, &inner_outline);

        if (row->expanded && row->candidates_rect.w > 0 && row->candidates_rect.h > 0) {
            SDL_Rect cand = row->candidates_rect;
            cand.x -= 6;
            cand.y -= 6;
            cand.w += 12;
            cand.h += 12;
            if (cand.w > 0 && cand.h > 0) {
                SDL_Color cand_fill = DMStyles::PanelBG();
                cand_fill.a = 36;
                SDL_SetRenderDrawColor(r, cand_fill.r, cand_fill.g, cand_fill.b, cand_fill.a);
                SDL_RenderFillRect(r, &cand);

                SDL_Color outer{255, 255, 255, 235};
                SDL_SetRenderDrawColor(r, outer.r, outer.g, outer.b, outer.a);
                SDL_RenderDrawRect(r, &cand);

                SDL_Rect cand_inner = cand;
                cand_inner.x += 1;
                cand_inner.y += 1;
                cand_inner.w = std::max(0, cand_inner.w - 2);
                cand_inner.h = std::max(0, cand_inner.h - 2);
                SDL_Color inner_white{255, 255, 255, 160};
                if (cand_inner.w > 0 && cand_inner.h > 0) {
                    SDL_SetRenderDrawColor(r, inner_white.r, inner_white.g, inner_white.b, inner_white.a);
                    SDL_RenderDrawRect(r, &cand_inner);
                }
            }
        }

        int sep_y = outline.y + outline.h + 2;
        int sep_x1 = outline.x;
        int sep_x2 = outline.x + outline.w;
        SDL_SetRenderDrawColor(r, separator.r, separator.g, separator.b, separator.a);
        SDL_RenderDrawLine(r, sep_x1, sep_y, sep_x2, sep_y);
    }
}

void SpawnGroupList::open(json& groups, std::function<void(const json&)> on_save) {
    // Floating open: bind to array, show as a floating panel with rows inside
    set_floatable(true);
    set_show_header(true);
    set_close_button_enabled(true);
    set_expanded(true);
    auto save_cb = [this, on_save]() {
        if (on_save) on_save(this->to_json());
    };
    load(groups, save_cb);
    Rows dummy; append_rows(dummy);
    DockableCollapsible::open();
}

void SpawnGroupList::request_open_spawn_group(const std::string& id, int x, int y) {
    set_position(x, y);
    expand_group(id);
}

void SpawnGroupList::set_anchor(int x, int y) { anchor_.x = x; anchor_.y = y; }

SpawnGroupList::EntryRow* SpawnGroupList::find_row(const std::string& id) {
    for (auto& r : rows_) if (r->id == id) return r.get();
    return nullptr;
}
const SpawnGroupList::EntryRow* SpawnGroupList::find_row(const std::string& id) const {
    for (const auto& r : rows_) if (r->id == id) return r.get();
    return nullptr;
}

void SpawnGroupList::bind_row_ref(RowRef& ref, EntryRow& row) {
    ref.id = row.id;
    ref.index = row.index;
}

SpawnGroupList::EntryRow* SpawnGroupList::lookup_row(RowRef& ref) {
    if (!ref.id.empty()) {
        if (auto* row = find_row(ref.id)) {
            ref.index = row->index;
            return row;
        }
        return nullptr;
    }
    if (ref.index >= 0 && ref.index < static_cast<int>(rows_.size())) {
        return rows_[ref.index].get();
    }
    return nullptr;
}

const SpawnGroupList::EntryRow* SpawnGroupList::lookup_row(const RowRef& ref) const {
    if (!ref.id.empty()) {
        return find_row(ref.id);
    }
    if (ref.index >= 0 && ref.index < static_cast<int>(rows_.size())) {
        return rows_[ref.index].get();
    }
    return nullptr;
}

void SpawnGroupList::clear_row_ref(RowRef& ref) {
    ref.id.clear();
    ref.index = -1;
}

void SpawnGroupList::rebuild_layout() {
    request_layout();
    if (suppress_layout_callback_) {
        Rows dummy;
        append_rows(dummy);
        return;
    }
    if (on_layout_change_) {
        on_layout_change_();
    } else {
        Rows dummy;
        append_rows(dummy);
    }
}
void SpawnGroupList::request_layout() { layout_dirty_ = true; }
void SpawnGroupList::notify_data_changed(EntryRow&, bool, bool) { if (on_change_) on_change_(); }
void SpawnGroupList::ensure_asset_search() {
    if (!asset_search_) {
        asset_search_ = std::make_unique<SearchAssets>();
        asset_search_->set_floating_stack_key("spawn-group-assets");
    }
    if (asset_search_) {
        asset_search_->set_screen_dimensions(screen_w_, screen_h_);
        asset_search_->set_anchor_position(anchor_.x, anchor_.y);
    }
}

void SpawnGroupList::request_asset_search_open(EntryRow& row, std::function<void(const std::string&)> callback) {
    bind_row_ref(pending_asset_search_row_ref_, row);
    pending_asset_search_callback_ = std::move(callback);
    pending_asset_search_open_ = true;
}

void SpawnGroupList::open_asset_search(EntryRow& row, std::function<void(const std::string&)> callback) {
    close_area_panel();
    ensure_asset_search();
    if (!asset_search_) return;
    bind_row_ref(asset_search_row_ref_, row);
    SDL_Rect parent = rect();
    if (row.candidates_rect.w > 0 && row.candidates_rect.h > 0) {
        parent = row.candidates_rect;
    } else if (row.body_rect.w > 0 && row.body_rect.h > 0) {
        parent = row.body_rect;
    }
    const int search_width = 280;
    const int spacing = DMSpacing::item_gap();
    const int margin = DMSpacing::panel_padding();
    int x = parent.x + parent.w + spacing;
    if (screen_w_ > 0) {
        int max_x = std::max(margin, screen_w_ - search_width - margin);
        if (x > max_x) {
            x = std::max(margin, parent.x - search_width - spacing);
        }
        x = std::clamp(x, margin, max_x);
    }
    int y = parent.y;
    if (screen_h_ > 0) {
        int max_y = std::max(margin, screen_h_ - parent.h - margin);
        y = std::clamp(y, margin, max_y);
    }
    asset_search_->set_position(x, y);
    asset_search_->open([this, cb=std::move(callback)](const std::string& selection) {
        if (selection.empty()) return;
        if (!selection.empty() && selection.front() == '#') return;
        EntryRow* rr = lookup_row(asset_search_row_ref_);
        if (!rr || !rr->entry) {
            close_asset_search();
            return;
        }
        json& entry = *rr->entry;
        if (!entry.contains("candidates") || !entry["candidates"].is_array()) {
            entry["candidates"] = json::array();
        }
        if (!rr->candidate_list)
            rr->candidate_list = std::make_unique<CandidateList>(*this, *rr, true);
        rr->candidate_list->rebuild();
        rr->candidate_list->add_candidate(selection);
        bool changed = rr->candidate_list->sync_to_json();
        if (on_change_ && changed) on_change_();
        if (cb) cb(selection);
        rebuild_layout();
    });
}

void SpawnGroupList::close_asset_search() {
    clear_row_ref(asset_search_row_ref_);
    if (asset_search_) asset_search_->close();
}

void SpawnGroupList::ensure_area_panel() {
    if (!area_panel_) {
        area_panel_ = std::make_unique<AreaLinkPanel>();
    }
    if (area_panel_) {
        area_panel_->set_screen_dimensions(screen_w_, screen_h_);
        area_panel_->set_anchor(anchor_);
        SDL_Rect parent = rect();
        if (auto* row = lookup_row(area_panel_row_ref_)) {
            const SDL_Rect& body = row->body_rect;
            if (body.w > 0 && body.h > 0) parent = body;
        } else if (area_panel_row_ref_.valid()) {
            close_area_panel();
        }
        area_panel_->set_parent_rect(parent);
    }
}

void SpawnGroupList::open_area_panel(EntryRow& row) {
    if (EntryRow* current = lookup_row(area_panel_row_ref_)) {
        if (current != &row) {
            close_area_panel();
        }
    } else if (area_panel_row_ref_.valid()) {
        close_area_panel();
    }
    close_asset_search();
    ensure_area_panel();
    if (!area_panel_) return;
    std::vector<std::string> names;
    if (row.area_names_provider) {
        names = row.area_names_provider();
    }
    bind_row_ref(area_panel_row_ref_, row);
    SDL_Rect parent = rect();
    if (row.body_rect.w > 0 && row.body_rect.h > 0) parent = row.body_rect;
    area_panel_->set_parent_rect(parent);
    area_panel_->open(names, [this](const std::string& selected) {
        EntryRow* rr = lookup_row(area_panel_row_ref_);
        if (!rr || !rr->entry) {
            close_area_panel();
            return;
        }
        (*rr->entry)["link"] = selected;
        if (on_change_) on_change_();
        rebuild_layout();
        if (area_panel_) area_panel_->close();
        clear_row_ref(area_panel_row_ref_);
    });
}

void SpawnGroupList::close_area_panel() {
    clear_row_ref(area_panel_row_ref_);
    if (area_panel_) area_panel_->close();
}

