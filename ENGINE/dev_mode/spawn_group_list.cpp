#include "spawn_group_list.hpp"

#include <algorithm>
#include <utility>
#include <unordered_set>

#include "dm_styles.hpp"
#include "widgets.hpp"
#include "utils/input.hpp"
#include "search_assets.hpp"

using nlohmann::json;

namespace {
static std::vector<std::string> kSpawnMethods{
    "Exact", "Random", "Percent", "Center", "Perimeter"
};

static bool method_uses_range(const std::string& m) {
    return !(m == "Exact" || m == "Center" || m == "Percent");
}
}

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
};

struct SpawnGroupList::EntryRow {
    // Data
    json* array = nullptr;         // bound array (editable mode)
    json* entry = nullptr;         // bound entry (editable mode)
    json  ro_entry;                // read-only snapshot (readonly mode)
    bool  read_only = false;
    std::string id;
    int index = -1;

    // Header controls
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

    // Body controls
    std::unique_ptr<DMTextBox> name_box;
    std::unique_ptr<TextBoxWidget> name_w;
    std::unique_ptr<DMDropdown> method_dd;
    std::unique_ptr<DropdownWidget> method_w;
    std::unique_ptr<DMRangeSlider> qty_sl;
    std::unique_ptr<RangeSliderWidget> qty_w;
    std::unique_ptr<DMButton> add_cand_btn;
    std::unique_ptr<ButtonWidget> add_cand_w;
    std::vector<std::unique_ptr<CandidateRow>> candidates;

    // Host integration
    std::function<std::vector<std::string>()> area_names_provider;
    std::string method_lock;
    bool quantity_hidden = false;

    // Ownership/parent label
    std::string owner_label;
    SDL_Color owner_color{255,255,255,255};
    std::unique_ptr<DMTextBox> owner_lbl_box;
    std::unique_ptr<TextBoxWidget> owner_lbl_w;

    // Area link UI
    std::unique_ptr<DMTextBox> link_lbl_box;
    std::unique_ptr<TextBoxWidget> link_lbl_w;
    std::unique_ptr<DMDropdown> link_dd;
    std::unique_ptr<DropdownWidget> link_dd_w;
    std::unique_ptr<DMButton> link_clear_btn;
    std::unique_ptr<ButtonWidget> link_clear_w;
};

// RowController methods
void SpawnGroupList::RowController::set_ownership_label(const std::string& label, SDL_Color color) {
    if (!row_) return;
    row_->owner_label = label;
    row_->owner_color = color;
    if (row_->owner_lbl_box) {
        std::string owner = label.empty() ? std::string("") : std::string("Parent: ") + label;
        row_->owner_lbl_box->set_value(owner);
    }
}
void SpawnGroupList::RowController::clear_ownership_label() {
    if (!row_) return;
    row_->owner_label.clear();
    if (row_->owner_lbl_box) row_->owner_lbl_box->set_value("");
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
    set_cell_width(260);
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

        // Body if expanded
        if (r->expanded) {
            if (!r->read_only) {
                // Build/editable controls
                if (!r->owner_lbl_box) {
                    std::string owner = r->owner_label.empty() ? std::string("") : std::string("Parent: ") + r->owner_label;
                    r->owner_lbl_box = std::make_unique<DMTextBox>("", owner);
                    r->owner_lbl_w = std::make_unique<TextBoxWidget>(r->owner_lbl_box.get(), true);
                }
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
                // Area link
                if (!r->link_lbl_box) {
                    std::string link = r->entry->value("link", std::string{});
                    std::string label = std::string("Link: ") + (link.empty() ? std::string("(None)") : link);
                    r->link_lbl_box = std::make_unique<DMTextBox>("", label);
                    r->link_lbl_w = std::make_unique<TextBoxWidget>(r->link_lbl_box.get(), true);
                } else {
                    std::string link = r->entry->value("link", std::string{});
                    std::string label = std::string("Link: ") + (link.empty() ? std::string("(None)") : link);
                    r->link_lbl_box->set_value(label);
                }
                if (!r->link_dd) {
                    std::vector<std::string> opts{ "(None)" };
                    if (r->area_names_provider) {
                        auto names = r->area_names_provider();
                        opts.insert(opts.end(), names.begin(), names.end());
                    }
                    int idx = 0;
                    const std::string current = r->entry->value("link", std::string{});
                    for (size_t i = 1; i < opts.size(); ++i) if (opts[i] == current) { idx = static_cast<int>(i); break; }
                    r->link_dd = std::make_unique<DMDropdown>("Pick Area", opts, idx);
                    r->link_dd_w = std::make_unique<DropdownWidget>(r->link_dd.get());
                }
                if (!r->link_clear_btn) {
                    r->link_clear_btn = std::make_unique<DMButton>("Clear Link", &DMStyles::WarnButton(), 100, DMButton::height());
                    r->link_clear_w = std::make_unique<ButtonWidget>(r->link_clear_btn.get(), [this, rr=r.get()](){
                        if (!rr->entry) return;
                        (*rr->entry)["link"] = "";
                        if (on_change_) on_change_();
                        this->rebuild_layout();
                    });
                }
                if (!r->add_cand_btn) {
                    r->add_cand_btn = std::make_unique<DMButton>("Add Candidate", &DMStyles::CreateButton(), 140, DMButton::height());
                    r->add_cand_w   = std::make_unique<ButtonWidget>(r->add_cand_btn.get(), [this, rr=r.get()](){ open_asset_search(*rr, {}); });
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
                        cr->name_w   = std::make_unique<TextBoxWidget>(cr->name_box.get());
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
                        r->candidates.push_back(std::move(cr));
                    }
                }

                if (r->owner_lbl_w) out.push_back({ r->owner_lbl_w.get() });
                out.push_back({ r->name_w.get() });
                if (r->method_w) out.push_back({ r->method_w.get() });
                const std::string method = r->entry->value("position", std::string{"Exact"});
                if (!r->quantity_hidden && method_uses_range(method)) {
                    out.push_back({ r->qty_w.get() });
                }
                if (r->link_lbl_w) out.push_back({ r->link_lbl_w.get() });
                if (r->link_dd_w && r->link_clear_w) out.push_back({ r->link_dd_w.get(), r->link_clear_w.get() });
                out.push_back({ r->add_cand_w.get() });
                for (auto& cr : r->candidates) {
                    out.push_back({ cr->name_w.get(), cr->chance_w.get(), cr->up_w.get(), cr->down_w.get(), cr->del_w.get() });
                }
            } else {
                // Readonly body: just show name and method
                if (!r->name_box) {
                    r->name_box = std::make_unique<DMTextBox>("Name", entry_display_name(r->ro_entry));
                    r->name_w = std::make_unique<TextBoxWidget>(r->name_box.get(), true);
                }
                out.push_back({ r->name_w.get() });
            }
        }
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
    DockableCollapsible::update(input, screen_w, screen_h);
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
        if (r->link_dd) {
            int idx = std::max(0, r->link_dd->selected());
            // Reconstruct options to map index to value
            std::vector<std::string> opts{ "(None)" };
            if (r->area_names_provider) {
                auto names = r->area_names_provider();
                opts.insert(opts.end(), names.begin(), names.end());
            }
            std::string new_link = (idx <= 0 || idx >= (int)opts.size()) ? std::string{} : opts[(size_t)idx];
            if (r->entry->value("link", std::string{}) != new_link) {
                (*r->entry)["link"] = new_link;
                changed = true;
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
    bool used = DockableCollapsible::handle_event(e);
    // Drop-down overlay rendering is handled in widgets
    return used;
}

void SpawnGroupList::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
    DMDropdown::render_active_options(r);
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
void SpawnGroupList::ensure_asset_search() {}
void SpawnGroupList::open_asset_search(EntryRow&, std::function<void(const std::string&)>) {}
void SpawnGroupList::close_asset_search() {}

