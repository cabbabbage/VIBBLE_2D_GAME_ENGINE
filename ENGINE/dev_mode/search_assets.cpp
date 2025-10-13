#include "search_assets.hpp"
#include "DockableCollapsible.hpp"
#include "FloatingDockableManager.hpp"
#include "widgets.hpp"
#include "dm_styles.hpp"
#include "tag_utils.hpp"
#include "utils/input.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <cctype>
#include <algorithm>

SearchAssets::SearchAssets() {
    panel_ = std::make_unique<DockableCollapsible>("Search Assets", true, 64, 64);
    panel_->set_expanded(true);
    panel_->set_visible(false);
    panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    panel_->set_close_button_enabled(true);
    panel_->set_scroll_enabled(true);
    panel_->reset_scroll();
    query_ = std::make_unique<DMTextBox>("Search", "");
    query_widget_ = std::make_unique<TextBoxWidget>(query_.get());
    panel_->set_rows({ { query_widget_.get() } });
    panel_->set_cell_width(260);
    last_known_position_ = panel_->position();
    pending_position_ = last_known_position_;
    has_pending_position_ = true;
    tag_data_version_ = tag_utils::tag_version();
}

void SearchAssets::apply_position(int x, int y) {
    if (!panel_) {
        panel_ = std::make_unique<DockableCollapsible>("Search Assets", true, x, y);
        panel_->set_expanded(true);
        panel_->set_visible(false);
        panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
        panel_->set_close_button_enabled(true);
        panel_->set_scroll_enabled(true);
        panel_->reset_scroll();
        panel_->set_cell_width(260);
        if (!query_) {
            query_ = std::make_unique<DMTextBox>("Search", "");
            query_widget_ = std::make_unique<TextBoxWidget>(query_.get());
            panel_->set_rows({ { query_widget_.get() } });
        }
    }
    if (embedded_) {
        panel_->set_rect(SDL_Rect{x, y, panel_->rect().w, panel_->rect().h});
        return;
    }
    panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    panel_->set_position(x, y);
}

void SearchAssets::set_position(int x, int y) {
    if (embedded_) {
        embedded_rect_.x = x;
        embedded_rect_.y = y;
        if (panel_) {
            SDL_Rect rect = panel_->rect();
            rect.x = x;
            rect.y = y;
            panel_->set_rect(rect);
        }
        return;
    }
    pending_position_ = SDL_Point{x, y};
    has_pending_position_ = true;
    has_custom_position_ = false;
    apply_position(x, y);
    ensure_visible_position();
    if (panel_) {
        last_known_position_ = panel_->position();
    }
}

void SearchAssets::set_anchor_position(int x, int y) {
    if (embedded_) {
        set_position(x, y);
        return;
    }
    pending_position_ = SDL_Point{x, y};
    has_pending_position_ = true;
    if (has_custom_position_) {
        return;
    }
    apply_position(x, y);
    ensure_visible_position();
    if (panel_) {
        last_known_position_ = panel_->position();
    }
}

void SearchAssets::set_screen_dimensions(int width, int height) {
    if (width > 0) {
        screen_w_ = width;
    }
    if (height > 0) {
        screen_h_ = height;
    }
    if (embedded_) {
        if (panel_) {
            panel_->set_work_area(SDL_Rect{0, 0, embedded_rect_.w > 0 ? embedded_rect_.w : screen_w_,
                                           embedded_rect_.h > 0 ? embedded_rect_.h : screen_h_});
            if (embedded_rect_.w > 0 || embedded_rect_.h > 0) {
                SDL_Rect rect = embedded_rect_;
                if (rect.w <= 0) rect.w = panel_->rect().w;
                if (rect.h <= 0) rect.h = panel_->rect().h;
                panel_->set_rect(rect);
            }
        }
        return;
    }
    if (panel_) {
        panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
        SDL_Point pos = panel_->position();
        panel_->set_position(pos.x, pos.y);
        ensure_visible_position();
        last_known_position_ = panel_->position();
        if (!has_custom_position_) {
            pending_position_ = last_known_position_;
            has_pending_position_ = true;
        }
    }
}

void SearchAssets::set_floating_stack_key(std::string key) {
    floating_stack_key_ = std::move(key);
}

void SearchAssets::set_embedded_mode(bool embedded) {
    embedded_ = embedded;
    if (!panel_) {
        return;
    }
    panel_->set_floatable(!embedded_);
    panel_->set_show_header(!embedded_);
    panel_->set_close_button_enabled(!embedded_);
    if (embedded_) {
        panel_->set_scroll_enabled(true);
        panel_->set_work_area(SDL_Rect{0, 0, embedded_rect_.w, embedded_rect_.h});
    } else {
        panel_->set_work_area(SDL_Rect{0, 0, screen_w_, screen_h_});
    }
}

void SearchAssets::set_embedded_rect(const SDL_Rect& rect) {
    embedded_rect_ = rect;
    if (!panel_) {
        return;
    }
    if (!embedded_) {
        apply_position(rect.x, rect.y);
        return;
    }
    SDL_Rect applied = rect;
    if (applied.w <= 0) {
        applied.w = panel_->rect().w > 0 ? panel_->rect().w : 260;
    }
    if (applied.h <= 0) {
        applied.h = panel_->rect().h > 0 ? panel_->rect().h : 0;
    }
    panel_->set_cell_width(std::max(120, applied.w - 20));
    if (applied.h > 0) {
        panel_->set_visible_height(applied.h);
        panel_->set_available_height_override(applied.h);
    }
    panel_->set_work_area(SDL_Rect{0, 0, applied.w, applied.h});
    panel_->set_rect(applied);
    Input dummy;
    panel_->update(dummy, applied.w, applied.h);
}

SDL_Rect SearchAssets::rect() const {
    if (!panel_) {
        return SDL_Rect{0, 0, 0, 0};
    }
    return panel_->rect();
}

std::string SearchAssets::to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void SearchAssets::open(Callback cb) {
    cb_ = std::move(cb);
    if (all_.empty()) load_assets();
    if (embedded_) {
        if (panel_) {
            panel_->set_visible(true);
            panel_->set_expanded(true);
            panel_->reset_scroll();
            panel_->force_pointer_ready();
            SDL_Rect applied = embedded_rect_;
            if (applied.w <= 0) applied.w = panel_->rect().w;
            if (applied.h <= 0) applied.h = panel_->rect().h;
            panel_->set_rect(applied);
            Input dummy;
            panel_->update(dummy, applied.w, applied.h);
        }
        last_query_.clear();
        filter_assets();
        return;
    }
    SDL_Point target = last_known_position_;
    if (has_custom_position_) {
        target = last_known_position_;
    } else if (has_pending_position_) {
        target = pending_position_;
    }
    apply_position(target.x, target.y);
    ensure_visible_position();
    if (!floating_stack_key_.empty()) {
        FloatingDockableManager::instance().open_floating(
            "Search Assets",
            panel_.get(),
            [this]() { this->close(); },
            floating_stack_key_);
    }
    panel_->set_visible(true);
    panel_->set_expanded(true);
    panel_->reset_scroll();
    Input dummy;
    panel_->update(dummy, screen_w_, screen_h_);
    ensure_visible_position();
    last_known_position_ = panel_->position();
    if (!has_custom_position_) {
        pending_position_ = last_known_position_;
        has_pending_position_ = true;
    }
    last_query_.clear();
    filter_assets();
}

void SearchAssets::close() {
    if (panel_) {
        if (embedded_) {
            panel_->set_visible(false);
        } else {
            last_known_position_ = panel_->position();
            if (!has_custom_position_) {
                pending_position_ = last_known_position_;
                has_pending_position_ = true;
            }
            panel_->set_visible(false);
        }
    }
    cb_ = nullptr;
}

bool SearchAssets::visible() const {
    return panel_ && panel_->is_visible();
}

void SearchAssets::load_assets() {
    namespace fs = std::filesystem;
    all_.clear();
    fs::path src("SRC");
    if (!fs::exists(src) || !fs::is_directory(src)) return;
    for (auto& p : fs::directory_iterator(src)) {
        if (!p.is_directory()) continue;
        fs::path info = p.path() / "info.json";
        if (!fs::exists(info)) continue;
        try {
            std::ifstream f(info);
            nlohmann::json j; f >> j;
            Asset a;
            a.name = j.value("asset_name", p.path().filename().string());
            if (j.contains("tags") && j["tags"].is_array()) {
                for (auto& t : j["tags"]) if (t.is_string()) a.tags.push_back(t.get<std::string>());
            }
            all_.push_back(std::move(a));
        } catch (...) {}
    }
}

void SearchAssets::filter_assets() {
    if (!panel_ || !panel_->is_visible()) return;
    auto current_version = tag_utils::tag_version();
    if (current_version != tag_data_version_) {
        load_assets();
        tag_data_version_ = current_version;
    }
    std::string q = to_lower(query_ ? query_->value() : "");
    results_.clear();
    std::set<std::string> tagset;
    for (const auto& a : all_) {
        std::string ln = to_lower(a.name);
        if (q.empty() || ln.find(q) != std::string::npos) {
            results_.push_back({a.name,false});
        }
        for (const auto& t : a.tags) {
            std::string lt = to_lower(t);
            if (lt.find(q) != std::string::npos) tagset.insert(t);
        }
    }
    for (const auto& t : tagset) {
        results_.push_back({t,true});
    }
    buttons_.clear();
    button_widgets_.clear();
    DockableCollapsible::Rows rows;
    rows.push_back({ query_widget_.get() });
    for (const auto& r : results_) {
        auto b = std::make_unique<DMButton>(r.second ? ("#"+r.first) : r.first, &DMStyles::ListButton(), 200, DMButton::height());
        auto bw = std::make_unique<ButtonWidget>(b.get(), [this, r]() {
            std::string v = r.first;
            if (r.second) v = "#" + v;
            if (cb_) cb_(v);
            close();
        });
        buttons_.push_back(std::move(b));
        button_widgets_.push_back(std::move(bw));
        rows.push_back({ button_widgets_.back().get() });
    }
    panel_->set_rows(rows);
    Input dummy;
    panel_->update(dummy, screen_w_, screen_h_);
}

bool SearchAssets::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    SDL_Point before = panel_->position();
    bool used = panel_->handle_event(e);
    SDL_Point after = panel_->position();
    if (!embedded_) {
        if (after.x != before.x || after.y != before.y) {
            has_custom_position_ = true;
            last_known_position_ = after;
            ensure_visible_position();
        }
    }
    std::string q = query_ ? query_->value() : "";
    if (q != last_query_) { last_query_ = q; filter_assets(); }
    return used;
}

void SearchAssets::update(const Input& input) {
    if (panel_ && panel_->is_visible()) {
        if (embedded_) {
            int w = embedded_rect_.w > 0 ? embedded_rect_.w : screen_w_;
            int h = embedded_rect_.h > 0 ? embedded_rect_.h : screen_h_;
            panel_->update(input, w, h);
        } else {
            panel_->update(input, screen_w_, screen_h_);
            last_known_position_ = panel_->position();
            if (!has_custom_position_) {
                pending_position_ = last_known_position_;
                has_pending_position_ = true;
            }
        }
        if (tag_utils::tag_version() != tag_data_version_) {
            filter_assets();
        }
    }
}

void SearchAssets::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
}

bool SearchAssets::is_point_inside(int x, int y) const {
    if (!panel_ || !panel_->is_visible()) return false;
    return panel_->is_point_inside(x, y);
}

void SearchAssets::ensure_visible_position() {
    if (embedded_) {
        return;
    }
    if (!panel_) {
        return;
    }
    if (screen_w_ <= 0 && screen_h_ <= 0) {
        return;
    }
    SDL_Rect rect = panel_->rect();
    if ((rect.w <= 0 || rect.h <= 0) && (screen_w_ > 0 || screen_h_ > 0)) {
        Input dummy;
        panel_->update(dummy, screen_w_, screen_h_);
        rect = panel_->rect();
    }
    const int margin = 12;
    bool adjusted = false;
    int x = rect.x;
    int y = rect.y;
    if (screen_w_ > 0) {
        int max_x = std::max(margin, screen_w_ - rect.w - margin);
        if (rect.w >= screen_w_ - margin * 2) {
            max_x = margin;
        }
        int clamped = std::clamp(x, margin, max_x);
        if (clamped != x) {
            x = clamped;
            adjusted = true;
        }
    }
    if (screen_h_ > 0) {
        int max_y = std::max(margin, screen_h_ - rect.h - margin);
        if (rect.h >= screen_h_ - margin * 2) {
            max_y = margin;
        }
        int clamped = std::clamp(y, margin, max_y);
        if (clamped != y) {
            y = clamped;
            adjusted = true;
        }
    }
    if (adjusted) {
        panel_->set_position(x, y);
        last_known_position_ = panel_->position();
        if (!has_custom_position_) {
            pending_position_ = last_known_position_;
            has_pending_position_ = true;
        }
    }
}
