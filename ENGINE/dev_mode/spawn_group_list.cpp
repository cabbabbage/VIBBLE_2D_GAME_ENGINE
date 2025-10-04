#include "spawn_group_list.hpp"

#include <algorithm>
#include <sstream>

#include "dm_styles.hpp"
#include "widgets.hpp"

namespace {
class SimpleLabel : public Widget {
public:
    explicit SimpleLabel(std::string text) : text_(std::move(text)) {}
    void set_text(std::string t) { text_ = std::move(t); }
    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return DMStyles::Label().font_size + DMSpacing::small_gap() * 2; }
    bool handle_event(const SDL_Event&) override { return false; }
    void render(SDL_Renderer* r) const override {
        const DMLabelStyle& st = DMStyles::Label();
        TTF_Font* font = st.open_font();
        if (!font) return;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text_.c_str(), st.color);
        if (!surf) { TTF_CloseFont(font); return; }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ rect_.x, rect_.y, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
        TTF_CloseFont(font);
    }
    bool wants_full_row() const override { return true; }
private:
    SDL_Rect rect_{0,0,0,0};
    std::string text_;
};

std::string build_summary_for(const nlohmann::json& entry, int display_index) {
    // display_index is 1-based for humans
    const std::string display = entry.value("display_name", entry.value("name", entry.value("spawn_id", std::string{"Spawn"})));
    std::string method = entry.value("position", std::string{"Unknown"});
    int min_q = entry.value("min_number", entry.value("max_number", 0));
    int max_q = entry.value("max_number", min_q);
    std::ostringstream ss;
    ss << display_index << ". " << display << " - " << method << " (" << min_q << "-" << max_q << ")";
    return ss.str();
}
}

SpawnGroupList::SpawnGroupList() = default;
SpawnGroupList::~SpawnGroupList() = default;

void SpawnGroupList::load(const nlohmann::json& groups) {
    rows_.clear();
    snapshot_.clear();
    if (!groups.is_array()) return;
    snapshot_ = groups;
    // Render in the given order; index defines priority (0-based), display uses 1-based
    int display_index = 1;
    for (const auto& entry : groups) {
        if (!entry.is_object()) { ++display_index; continue; }
        std::string spawn_id = entry.value("spawn_id", std::string{});
        if (spawn_id.empty()) { ++display_index; continue; }

        auto row = std::make_unique<RowWidgets>();
        row->id = spawn_id;
        row->label = std::make_unique<SimpleLabel>(build_summary_for(entry, display_index));

        // Icon buttons
        row->btn_edit = std::make_unique<DMButton>("✎", &DMStyles::HeaderButton(), 36, DMButton::height());
        row->w_edit = std::make_unique<ButtonWidget>(row->btn_edit.get(), [this, spawn_id]() {
            if (cbs_.on_edit) cbs_.on_edit(spawn_id);
        });

        row->btn_up = std::make_unique<DMButton>("▲", &DMStyles::ListButton(), 32, DMButton::height());
        row->w_up = std::make_unique<ButtonWidget>(row->btn_up.get(), [this, spawn_id]() {
            if (cbs_.on_move_up) cbs_.on_move_up(spawn_id);
        });

        row->btn_down = std::make_unique<DMButton>("▼", &DMStyles::ListButton(), 32, DMButton::height());
        row->w_down = std::make_unique<ButtonWidget>(row->btn_down.get(), [this, spawn_id]() {
            if (cbs_.on_move_down) cbs_.on_move_down(spawn_id);
        });

        row->btn_dup = std::make_unique<DMButton>("Duplicate", &DMStyles::HeaderButton(), 96, DMButton::height());
        row->w_dup = std::make_unique<ButtonWidget>(row->btn_dup.get(), [this, spawn_id]() {
            if (cbs_.on_duplicate) cbs_.on_duplicate(spawn_id);
        });

        row->btn_del = std::make_unique<DMButton>("🗑", &DMStyles::DeleteButton(), 36, DMButton::height());
        row->w_del = std::make_unique<ButtonWidget>(row->btn_del.get(), [this, spawn_id]() {
            if (cbs_.on_delete) cbs_.on_delete(spawn_id);
        });

        rows_.push_back(std::move(row));
        ++display_index;
    }
}

void SpawnGroupList::set_callbacks(Callbacks cb) {
    cbs_ = std::move(cb);
}

void SpawnGroupList::append_rows(Rows& rows) {
    for (auto& e : rows_) {
        DockableCollapsible::Row r;
        if (e->label) r.push_back(e->label.get());
        if (e->w_edit) r.push_back(e->w_edit.get());
        if (e->w_up)   r.push_back(e->w_up.get());
        if (e->w_down) r.push_back(e->w_down.get());
        if (e->w_dup)  r.push_back(e->w_dup.get());
        if (e->w_del)  r.push_back(e->w_del.get());
        if (!r.empty()) rows.push_back(std::move(r));
    }
}

