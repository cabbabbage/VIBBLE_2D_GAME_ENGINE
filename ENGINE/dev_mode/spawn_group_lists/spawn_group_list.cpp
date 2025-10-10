#include "spawn_group_list.hpp"

#include <algorithm>
#include <utility>

#include "spawn_group_row.hpp"

namespace {
class ScopedFlag {
public:
    ScopedFlag(bool& flag, bool value) : flag_(flag), previous_(flag) { flag_ = value; }
    ~ScopedFlag() { flag_ = previous_; }

private:
    bool& flag_;
    bool previous_;
};
}  // namespace

SpawnGroupList::SpawnGroupList(bool floatable)
    : DockableCollapsible("Spawn Groups", floatable),
      default_floatable_mode_(floatable) {}

SpawnGroupList::~SpawnGroupList() = default;

void SpawnGroupList::set_screen_dimensions(int width, int height) {
    screen_w_ = width;
    screen_h_ = height;
}

void SpawnGroupList::load(nlohmann::json& groups,
                          std::function<void()> on_change,
                          std::function<void(const nlohmann::json&, const ChangeSummary&)> on_entry_change,
                          ConfigureEntryCallback configure_entry) {
    bound_array_ = &groups;
    readonly_snapshot_.clear();
    on_change_ = std::move(on_change);
    on_entry_change_ = std::move(on_entry_change);
    configure_entry_ = std::move(configure_entry);
    rebuild_rows();
}

void SpawnGroupList::load(const nlohmann::json& groups) {
    bound_array_ = nullptr;
    readonly_snapshot_ = groups;
    rebuild_rows();
}

void SpawnGroupList::append_rows(Rows& rows) {
    ScopedFlag guard(suppress_layout_change_callback_, true);
    rebuild_rows();
    for (size_t i = 0; i < rows_.size(); ++i) {
        rows.emplace_back();
    }
    rebuild_layout();
}

void SpawnGroupList::set_callbacks(Callbacks cb) {
    callbacks_ = std::move(cb);
}

void SpawnGroupList::set_on_layout_changed(std::function<void()> cb) {
    on_layout_change_ = std::move(cb);
}

void SpawnGroupList::refresh_row_configuration() {
    for (auto& row : rows_) {
        apply_configuration(*row);
    }
}

void SpawnGroupList::set_embedded_mode(bool embedded) {
    embedded_mode_ = embedded;
    set_floatable(!embedded ? default_floatable_mode_ : false);
}

void SpawnGroupList::expand_group(const std::string& id) {
    if (id.empty()) return;
    expanded_.insert(id);
}

void SpawnGroupList::collapse_group(const std::string& id) {
    if (id.empty()) return;
    expanded_.erase(id);
}

bool SpawnGroupList::is_expanded(const std::string& id) const {
    if (id.empty()) return false;
    return expanded_.find(id) != expanded_.end();
}

std::vector<std::string> SpawnGroupList::expanded_groups() const {
    std::vector<std::string> ids(expanded_.begin(), expanded_.end());
    std::sort(ids.begin(), ids.end());
    return ids;
}

void SpawnGroupList::restore_expanded_groups(const std::vector<std::string>& ids) {
    expanded_.clear();
    for (const auto& id : ids) {
        if (!id.empty()) {
            expanded_.insert(id);
        }
    }
}

nlohmann::json SpawnGroupList::to_json() const {
    if (bound_array_) {
        return *bound_array_;
    }
    return readonly_snapshot_;
}

void SpawnGroupList::update(const Input& input, int screen_w, int screen_h) {
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool SpawnGroupList::handle_event(const SDL_Event& e) {
    return DockableCollapsible::handle_event(e);
}

void SpawnGroupList::render(SDL_Renderer* r) const {
    DockableCollapsible::render(r);
}

void SpawnGroupList::render_content(SDL_Renderer* r) const {
    DockableCollapsible::render_content(r);
}

void SpawnGroupList::open(nlohmann::json& groups, std::function<void(const nlohmann::json&)> on_save) {
    pending_save_callback_ = std::move(on_save);
    load(groups, {}, {}, {});
    DockableCollapsible::open();
}

void SpawnGroupList::request_open_spawn_group(const std::string& id, int, int) {
    pending_focus_id_ = id;
}

void SpawnGroupList::set_anchor(int x, int y) {
    anchor_.x = x;
    anchor_.y = y;
}

void SpawnGroupList::close_asset_search() {
    pending_focus_id_.reset();
}

void SpawnGroupList::rebuild_rows() {
    const nlohmann::json* source = current_source();
    std::vector<std::unique_ptr<SpawnGroupRow>> rebuilt;
    if (!source || !source->is_array()) {
        rows_.clear();
        layout_dirty_ = true;
        return;
    }

    rebuilt.reserve(source->size());
    auto previous = std::move(rows_);
    auto take_existing = [&previous](const std::string& id) -> std::unique_ptr<SpawnGroupRow> {
        if (id.empty()) {
            return nullptr;
        }
        for (auto it = previous.begin(); it != previous.end(); ++it) {
            if ((*it) && (*it)->spawn_id() == id) {
                auto result = std::move(*it);
                previous.erase(it);
                return result;
            }
        }
        return nullptr;
    };

    for (size_t i = 0; i < source->size(); ++i) {
        const auto& entry = (*source)[i];
        std::string id;
        if (entry.contains("spawn_id") && entry["spawn_id"].is_string()) {
            id = entry["spawn_id"].get<std::string>();
        }

        auto row = take_existing(id);
        const bool is_new_row = !row;
        if (!row) {
            row = std::make_unique<SpawnGroupRow>();
        }

        if (bound_array_) {
            row->bind(&(*bound_array_)[i]);
        } else {
            row->bind(nullptr);
            row->set_shadow_entry(entry);
        }

        if (is_new_row) {
            apply_configuration(*row);
        }

        rebuilt.emplace_back(std::move(row));
    }

    rows_ = std::move(rebuilt);
    layout_dirty_ = true;
}

void SpawnGroupList::apply_configuration(SpawnGroupRow& row) {
    if (!configure_entry_) {
        return;
    }
    RowController controller(&row);
    configure_entry_(controller, row.entry_view());
}

void SpawnGroupList::rebuild_layout() {
    if (!layout_dirty_) {
        return;
    }
    layout_dirty_ = false;
    Rows layout_rows;
    layout_rows.reserve(rows_.size());
    for (size_t i = 0; i < rows_.size(); ++i) {
        layout_rows.emplace_back();
    }
    set_rows(layout_rows);
    if (!suppress_layout_change_callback_ && on_layout_change_) {
        on_layout_change_();
    }
}

const nlohmann::json* SpawnGroupList::current_source() const {
    if (bound_array_) {
        return bound_array_;
    }
    return &readonly_snapshot_;
}

void SpawnGroupList::RowController::set_ownership_label(const std::string& label, SDL_Color color) {
    if (!row_) return;
    row_->set_ownership_label(label, color);
}

void SpawnGroupList::RowController::clear_ownership_label() {
    if (!row_) return;
    row_->clear_ownership_label();
}

void SpawnGroupList::RowController::set_area_names_provider(std::function<std::vector<std::string>()> provider) {
    if (!row_) return;
    row_->set_area_names_provider(std::move(provider));
}

void SpawnGroupList::RowController::set_stack_key(std::string key) {
    if (!row_) return;
    row_->set_stack_key(std::move(key));
}

void SpawnGroupList::RowController::lock_method_to(const std::string& method) {
    if (!row_) return;
    row_->lock_method_to(method);
}

void SpawnGroupList::RowController::clear_method_lock() {
    if (!row_) return;
    row_->clear_method_lock();
}

void SpawnGroupList::RowController::set_quantity_hidden(bool hidden) {
    if (!row_) return;
    row_->set_quantity_hidden(hidden);
}

