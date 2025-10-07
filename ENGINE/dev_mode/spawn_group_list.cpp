#include "spawn_group_list.hpp"

#include <algorithm>
#include <utility>
#include <unordered_set>

#include <SDL_ttf.h>

#include "dm_styles.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"
#include "search_assets.hpp"
#include "FloatingDockableManager.hpp"

using nlohmann::json;

namespace {

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

struct SpawnGroupList::CandidateRow {
    std::unique_ptr<DMTextBox> name_box;
    std::unique_ptr<TextBoxWidget> name_w;
    std::unique_ptr<DMSlider> chance_sl;
    std::unique_ptr<SliderWidget> chance_w;
    std::unique_ptr<DMButton> up_btn;
    std::unique_ptr<ButtonWidget> up_w;
    std::unique_ptr<DMButton> down_btn;
    std::unique_ptr<ButtonWidget> down_w;
    std::unique_ptr<DMButton> del_btn;
    std::unique_ptr<ButtonWidget> del_w;
    std::unique_ptr<SpacerWidget> spacing;
};

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
    std::vector<std::unique_ptr<CandidateRow>> candidates;

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

    struct CandidatesConfig {
        std::unique_ptr<SpacerWidget> top_gap;
        std::unique_ptr<SectionLabelWidget> header_label;
        std::unique_ptr<SectionLabelWidget> empty_label;
        std::unique_ptr<DMButton> add_btn;
        std::unique_ptr<ButtonWidget> add_w;
        std::unique_ptr<RowRectMarkerWidget> begin_marker;
        std::unique_ptr<RowRectMarkerWidget> end_marker;
        std::unique_ptr<SpacerWidget> bottom_gap;
    };

    std::unique_ptr<CandidatesConfig> candidates_config;
};

namespace {
static std::vector<std::string> kSpawnMethods{
    "Exact", "Random", "Percent", "Center", "Perimeter"
};

static bool method_uses_range(const std::string& m) {
    return !(m == "Exact" || m == "Center" || m == "Percent");
}
}  // namespace

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

                // Rebuild candidate rows from json
                r->candidates.clear();
                auto& e = *r->entry;
                if (e.contains("candidates") && e["candidates"].is_array()) {
                    for (size_t ci = 0; ci < e["candidates"].size(); ++ci) {
                        auto cr = std::make_unique<CandidateRow>();
                        const auto& c = e["candidates"][ci];
                        const std::string nm = c.value("name", std::string{"null"});
                        int chance = c.value("chance", 0);
                        cr->name_box = std::make_unique<DMTextBox>("Asset", nm);
                        cr->name_w   = std::make_unique<TextBoxWidget>(cr->name_box.get(), true);
                        cr->chance_sl= std::make_unique<DMSlider>("Weight", 0, 100, chance);
                        cr->chance_w = std::make_unique<SliderWidget>(cr->chance_sl.get());
                        cr->up_btn   = std::make_unique<DMButton>("↑", &DMStyles::ListButton(), 24, DMButton::height());
                        cr->up_w     = std::make_unique<ButtonWidget>(cr->up_btn.get(), [this, rr=r.get(), ci](){
                            if (!rr->entry || !rr->entry->contains("candidates")) return;
                            auto& arr = (*rr->entry)["candidates"];
                            if (!arr.is_array()) return;
                            if (ci <= 0) return;
                            std::swap(arr[ci-1], arr[ci]);
                            if (on_change_) on_change_();
                            this->rebuild_layout();
                        });
                        cr->down_btn = std::make_unique<DMButton>("↓", &DMStyles::ListButton(), 24, DMButton::height());
                        cr->down_w   = std::make_unique<ButtonWidget>(cr->down_btn.get(), [this, rr=r.get(), ci](){
                            if (!rr->entry || !rr->entry->contains("candidates")) return;
                            auto& arr = (*rr->entry)["candidates"];
                            if (!arr.is_array()) return;
                            if (ci+1 >= arr.size()) return;
                            std::swap(arr[ci], arr[ci+1]);
                            if (on_change_) on_change_();
                            this->rebuild_layout();
                        });
                        cr->del_btn  = std::make_unique<DMButton>("X", &DMStyles::DeleteButton(), 24, DMButton::height());
                        cr->del_w    = std::make_unique<ButtonWidget>(cr->del_btn.get(), [this, rr=r.get(), ci](){
                            if (!rr->entry || !rr->entry->contains("candidates")) return;
                            auto& arr = (*rr->entry)["candidates"];
                            if (!arr.is_array()) return;
                            if (ci >= arr.size()) return;
                            arr.erase(arr.begin()+static_cast<int>(ci));
                            if (on_change_) on_change_();
                            this->rebuild_layout();
                        });
                        cr->spacing = std::make_unique<SpacerWidget>(DMSpacing::small_gap());
                        r->candidates.push_back(std::move(cr));
                    }
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

                if (!r->candidates_config)
                    r->candidates_config = std::make_unique<EntryRow::CandidatesConfig>();
                auto& cand_cfg = *r->candidates_config;
                if (!cand_cfg.top_gap)
                    cand_cfg.top_gap = std::make_unique<SpacerWidget>(std::max(6, DMSpacing::section_gap() / 2));
                out.push_back({ cand_cfg.top_gap.get() });
                if (!cand_cfg.begin_marker)
                    cand_cfg.begin_marker = std::make_unique<RowRectMarkerWidget>(&r->candidates_rect, true);
                if (!cand_cfg.end_marker)
                    cand_cfg.end_marker = std::make_unique<RowRectMarkerWidget>(&r->candidates_rect, false);
                out.push_back({ cand_cfg.begin_marker.get() });
                if (!cand_cfg.header_label)
                    cand_cfg.header_label = std::make_unique<SectionLabelWidget>("Candidates", false);
                else
                    cand_cfg.header_label->set_text("Candidates");
                DockableCollapsible::Row header_row;
                header_row.push_back(cand_cfg.header_label.get());
                if (!cand_cfg.add_btn) {
                    cand_cfg.add_btn = std::make_unique<DMButton>("Add Candidate", &DMStyles::CreateButton(), 160, DMButton::height());
                    cand_cfg.add_w   = std::make_unique<ButtonWidget>(cand_cfg.add_btn.get(), [this, rr=r.get()](){ open_asset_search(*rr, {}); });
                }
                header_row.push_back(cand_cfg.add_w.get());
                out.push_back(header_row);

                if (r->candidates.empty()) {
                    if (!cand_cfg.empty_label) {
                        cand_cfg.empty_label = std::make_unique<SectionLabelWidget>("No candidates configured", true, DMStyles::Label().color, std::max(10, DMStyles::Label().font_size - 2));
                    }
                    cand_cfg.empty_label->set_text("No candidates configured");
                    cand_cfg.empty_label->set_font_size(std::max(10, DMStyles::Label().font_size - 2));
                    out.push_back({ cand_cfg.empty_label.get() });
                } else {
                    if (cand_cfg.empty_label)
                        cand_cfg.empty_label->set_text("No candidates configured");
                    for (size_t ci = 0; ci < r->candidates.size(); ++ci) {
                        auto& cr = r->candidates[ci];
                        out.push_back({ cr->name_w.get() });
                        out.push_back({ cr->chance_w.get() });
                        DockableCollapsible::Row controls;
                        controls.push_back(cr->up_w.get());
                        controls.push_back(cr->down_w.get());
                        controls.push_back(cr->del_w.get());
                        out.push_back(controls);
                        if (ci + 1 < r->candidates.size() && cr->spacing) {
                            out.push_back({ cr->spacing.get() });
                        }
                    }
                }
                out.push_back({ cand_cfg.end_marker.get() });
                if (!cand_cfg.bottom_gap)
                    cand_cfg.bottom_gap = std::make_unique<SpacerWidget>(DMSpacing::item_gap());
                out.push_back({ cand_cfg.bottom_gap.get() });
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
        // candidates weight sync
        if (r->entry->contains("candidates") && (*r->entry)["candidates"].is_array()) {
            auto& carr = (*r->entry)["candidates"];
            size_t i = 0;
            for (auto& cr : r->candidates) {
                if (i >= carr.size()) break;
                int v = std::clamp(cr->chance_sl ? cr->chance_sl->value() : 0, 0, 100);
                if (carr[i].value("chance", 0) != v) { carr[i]["chance"] = v; changed = true; }
                ++i;
            }
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
    SDL_Color list_border = DMStyles::ListButton().border;
    list_border.a = 220;
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
                SDL_Color cand_fill = DMStyles::ListButton().bg;
                cand_fill.a = 28;
                SDL_SetRenderDrawColor(r, cand_fill.r, cand_fill.g, cand_fill.b, cand_fill.a);
                SDL_RenderFillRect(r, &cand);
                SDL_SetRenderDrawColor(r, list_border.r, list_border.g, list_border.b, list_border.a / 2);
                SDL_RenderDrawRect(r, &cand);
                SDL_Rect cand_inner = cand;
                cand_inner.x += 1;
                cand_inner.y += 1;
                cand_inner.w -= 2;
                cand_inner.h -= 2;
                SDL_SetRenderDrawColor(r, list_border.r, list_border.g, list_border.b, list_border.a);
                SDL_RenderDrawRect(r, &cand_inner);
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

void SpawnGroupList::open_asset_search(EntryRow& row, std::function<void(const std::string&)> callback) {
    close_area_panel();
    ensure_asset_search();
    if (!asset_search_) return;
    bind_row_ref(asset_search_row_ref_, row);
    SDL_Rect parent = rect();
    int search_width = 280;
    int spacing = DMSpacing::item_gap();
    int x = parent.x + parent.w + spacing;
    if (x + search_width > screen_w_ - DMSpacing::panel_padding()) {
        x = std::max(DMSpacing::panel_padding(), screen_w_ - search_width - DMSpacing::panel_padding());
    }
    int y = parent.y;
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
        json candidate;
        candidate["name"] = selection;
        candidate["chance"] = 100;
        entry["candidates"].push_back(candidate);
        if (on_change_) on_change_();
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

