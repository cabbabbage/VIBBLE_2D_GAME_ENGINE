#include "room_selector_popup.hpp"

#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "widgets.hpp"

#include <algorithm>

RoomSelectorPopup::RoomSelectorPopup() {
    create_room_button_ = std::make_unique<DMButton>("Create New Room", &DMStyles::CreateButton(), 220, DMButton::height());
    confirm_button_ = std::make_unique<DMButton>("Create", &DMStyles::CreateButton(), 120, DMButton::height());
    cancel_button_ = std::make_unique<DMButton>("Cancel", &DMStyles::HeaderButton(), 120, DMButton::height());
    geometry_dirty_ = true;
}

RoomSelectorPopup::~RoomSelectorPopup() {
    close();
}

void RoomSelectorPopup::set_anchor_rect(const SDL_Rect& rect) {
    if (anchor_rect_.x == rect.x && anchor_rect_.y == rect.y &&
        anchor_rect_.w == rect.w && anchor_rect_.h == rect.h) {
        return;
    }
    anchor_rect_ = rect;
    if (visible_) {
        position_from_anchor();
    }
    geometry_dirty_ = true;
}

void RoomSelectorPopup::set_screen_bounds(const SDL_Rect& bounds) {
    if (screen_bounds_.x == bounds.x && screen_bounds_.y == bounds.y &&
        screen_bounds_.w == bounds.w && screen_bounds_.h == bounds.h) {
        return;
    }
    screen_bounds_ = bounds;
    if (visible_) {
        position_from_anchor();
    }
}

void RoomSelectorPopup::set_create_callbacks(SuggestRoomFn suggest_cb, CreateRoomFn create_cb) {
    suggest_room_fn_ = std::move(suggest_cb);
    create_room_fn_ = std::move(create_cb);
}

void RoomSelectorPopup::open(const std::vector<std::string>& rooms, RoomCallback cb) {
    callback_ = std::move(cb);
    creating_room_ = false;
    name_input_.reset();
    scroll_offset_ = 0;
    geometry_dirty_ = true;
    set_rooms(rooms);
    position_from_anchor();
    visible_ = true;
    ensure_geometry();
}

void RoomSelectorPopup::set_rooms(const std::vector<std::string>& rooms) {
    rooms_ = rooms;
    rebuild_room_buttons();
    geometry_dirty_ = true;
}

void RoomSelectorPopup::close() {
    if (creating_room_) {
        SDL_StopTextInput();
    }
    visible_ = false;
    callback_ = nullptr;
    creating_room_ = false;
    name_input_.reset();
    scroll_offset_ = 0;
    geometry_dirty_ = true;
}

void RoomSelectorPopup::update(const Input&) {
    if (!visible_) return;
    ensure_geometry();
}

bool RoomSelectorPopup::handle_event(const SDL_Event& e) {
    if (!visible_) return false;
    ensure_geometry();

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
                     e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y };
        if (!SDL_PointInRect(&p, &rect_)) {
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                close();
            }
            return false;
        }
    }

    bool used = false;
    if (e.type == SDL_MOUSEWHEEL) {
        SDL_Point mouse_pos{0, 0};
        SDL_GetMouseState(&mouse_pos.x, &mouse_pos.y);
        if (SDL_PointInRect(&mouse_pos, &content_clip_)) {
            const int step = DMButton::height() + DMSpacing::small_gap();
            scroll_by(-e.wheel.y * step);
            used = true;
        }
    }

    layout_widgets();

    if (create_room_button_ && create_room_button_->handle_event(e)) {
        used = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            begin_create_room();
        }
    }

    if (creating_room_) {
        if (name_input_ && name_input_->handle_event(e)) {
            used = true;
            geometry_dirty_ = true;
        }
        if (confirm_button_ && confirm_button_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                finalize_create_room();
                return true;
            }
        }
        if (cancel_button_ && cancel_button_->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                cancel_create_room();
                return true;
            }
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                finalize_create_room();
                return true;
            }
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                cancel_create_room();
                return true;
            }
        }
    }

    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& btn = buttons_[i];
        if (!btn) continue;
        if (btn->handle_event(e)) {
            used = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (callback_) callback_(rooms_[i]);
                close();
                return true;
            }
        }
    }
    return used;
}

void RoomSelectorPopup::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;
    ensure_geometry();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &rect_);
    const SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect_);

    SDL_Rect prev_clip{};
    SDL_RenderGetClipRect(renderer, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(renderer);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(renderer, &content_clip_);

    layout_widgets();
    for (const auto& btn : buttons_) {
        if (btn) btn->render(renderer);
    }
    if (create_room_button_) create_room_button_->render(renderer);
    if (creating_room_) {
        if (name_input_) name_input_->render(renderer);
        if (confirm_button_) confirm_button_->render(renderer);
        if (cancel_button_) cancel_button_->render(renderer);
    }

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
}

bool RoomSelectorPopup::is_point_inside(int x, int y) const {
    if (!visible_) return false;
    SDL_Point p{ x, y };
    return SDL_PointInRect(&p, &rect_);
}

void RoomSelectorPopup::rebuild_room_buttons() {
    buttons_.clear();
    const int margin = DMSpacing::item_gap();
    int button_width = rect_.w - margin * 2;
    if (button_width <= 0) {
        button_width = 220;
    } else {
        button_width = std::max(button_width, 220);
    }
    for (const auto& room : rooms_) {
        auto btn = std::make_unique<DMButton>(room, &DMStyles::ListButton(), button_width, DMButton::height());
        buttons_.push_back(std::move(btn));
    }
}

void RoomSelectorPopup::ensure_geometry() const {
    if (!geometry_dirty_) return;
    const int margin = DMSpacing::item_gap();
    rect_.w = std::max(rect_.w, 220 + margin * 2);
    const int content_width = std::max(0, rect_.w - margin * 2);
    const int button_height = DMButton::height();
    const int spacing = DMSpacing::small_gap();

    int total = margin;
    if (!rooms_.empty()) {
        total += static_cast<int>(rooms_.size()) * (button_height + spacing);
        total -= spacing;
    }
    total += DMSpacing::item_gap();
    total += button_height;
    if (creating_room_) {
        total += spacing;
        int input_height = DMTextBox::height();
        if (name_input_) {
            input_height = name_input_->preferred_height(content_width);
        }
        total += input_height;
        total += spacing;
        total += button_height;
    }
    total += margin;

    content_height_ = total;
    const int min_height = button_height * 3 + margin * 2;
    const int max_height = 520;
    rect_.h = std::min(std::max(content_height_, min_height), max_height);

    content_clip_ = SDL_Rect{ rect_.x + margin, rect_.y + margin,
                               std::max(0, rect_.w - margin * 2), std::max(0, rect_.h - margin * 2) };
    max_scroll_ = std::max(0, content_height_ - rect_.h);
    if (scroll_offset_ > max_scroll_) scroll_offset_ = max_scroll_;
    if (scroll_offset_ < 0) scroll_offset_ = 0;
    geometry_dirty_ = false;
    position_from_anchor();
}

void RoomSelectorPopup::layout_widgets() const {
    ensure_geometry();
    const int margin = DMSpacing::item_gap();
    const int spacing = DMSpacing::small_gap();
    const int button_height = DMButton::height();
    const int content_width = std::max(0, rect_.w - margin * 2);

    int y = rect_.y + margin - scroll_offset_;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto& btn = buttons_[i];
        if (!btn) continue;
        btn->set_rect(SDL_Rect{ rect_.x + margin, y, content_width, button_height });
        y += button_height;
        if (i + 1 < buttons_.size()) {
            y += spacing;
        }
    }

    y += DMSpacing::item_gap();
    if (create_room_button_) {
        create_room_button_->set_rect(SDL_Rect{ rect_.x + margin, y, content_width, DMButton::height() });
    }
    y += DMButton::height();

    if (creating_room_) {
        y += spacing;
        if (name_input_) {
            const int input_h = name_input_->preferred_height(content_width);
            name_input_->set_rect(SDL_Rect{ rect_.x + margin, y, content_width, input_h });
            const SDL_Rect input_rect = name_input_->rect();
            y = input_rect.y + input_rect.h;
        }
        y += spacing;
        int left_w = std::max(1, (content_width - spacing) / 2);
        int right_w = std::max(1, content_width - left_w - spacing);
        if (left_w + right_w + spacing > content_width) {
            right_w = std::max(1, content_width - left_w - spacing);
        }
        int button_y = y;
        if (confirm_button_) {
            confirm_button_->set_rect(SDL_Rect{ rect_.x + margin, button_y, left_w, DMButton::height() });
        }
        if (cancel_button_) {
            cancel_button_->set_rect(SDL_Rect{ rect_.x + margin + left_w + spacing, button_y, right_w, DMButton::height() });
        }
        y += DMButton::height();
    }
}

void RoomSelectorPopup::begin_create_room() {
    std::string suggestion = suggest_room_fn_ ? suggest_room_fn_() : std::string("room");
    if (suggestion.empty()) suggestion = "room";
    name_input_ = std::make_unique<DMTextBox>("Room Name", suggestion);
    creating_room_ = true;
    geometry_dirty_ = true;
    ensure_geometry();
    scroll_offset_ = max_scroll_;
    SDL_StartTextInput();
}

void RoomSelectorPopup::cancel_create_room() {
    if (creating_room_) {
        SDL_StopTextInput();
    }
    creating_room_ = false;
    name_input_.reset();
    geometry_dirty_ = true;
}

void RoomSelectorPopup::finalize_create_room() {
    if (!creating_room_) return;
    std::string desired = name_input_ ? name_input_->value() : std::string();
    std::string created;
    if (create_room_fn_) {
        created = create_room_fn_(desired);
    }
    if (created.empty()) {
        return;
    }
    SDL_StopTextInput();
    creating_room_ = false;
    name_input_.reset();
    geometry_dirty_ = true;
    if (callback_) {
        callback_(created);
    }
    close();
}

void RoomSelectorPopup::scroll_by(int delta) {
    if (delta == 0) return;
    ensure_geometry();
    int new_offset = scroll_offset_ + delta;
    if (new_offset < 0) new_offset = 0;
    if (new_offset > max_scroll_) new_offset = max_scroll_;
    scroll_offset_ = new_offset;
}

void RoomSelectorPopup::position_from_anchor() const {
    if (screen_bounds_.w > 0 && screen_bounds_.h > 0) {
        const int centered_x = screen_bounds_.x + (screen_bounds_.w - rect_.w) / 2;
        const int centered_y = screen_bounds_.y + (screen_bounds_.h - rect_.h) / 2;
        const int max_x = screen_bounds_.x + std::max(0, screen_bounds_.w - rect_.w);
        const int max_y = screen_bounds_.y + std::max(0, screen_bounds_.h - rect_.h);
        rect_.x = std::clamp(centered_x, screen_bounds_.x, max_x);
        rect_.y = std::clamp(centered_y, screen_bounds_.y, max_y);
        return;
    }

    rect_.x = anchor_rect_.x + anchor_rect_.w + DMSpacing::item_gap();
    rect_.y = anchor_rect_.y;
}

