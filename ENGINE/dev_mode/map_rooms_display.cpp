#include "map_rooms_display.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "SlidingWindowContainer.hpp"
#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "widgets.hpp"

namespace {

SDL_Point event_point_from_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        return SDL_Point{e.motion.x, e.motion.y};
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        return SDL_Point{e.button.x, e.button.y};
    }
    int mx = 0;
    int my = 0;
    SDL_GetMouseState(&mx, &my);
    return SDL_Point{mx, my};
}

std::string room_display_name(const std::string& key, const nlohmann::json& payload) {
    if (payload.is_object()) {
        const std::string name = payload.value("name", std::string{});
        if (!name.empty()) {
            return name;
        }
    }
    return key;
}

}  // namespace

MapRoomsDisplay::MapRoomsDisplay() = default;

MapRoomsDisplay::~MapRoomsDisplay() {
    detach_container();
}

void MapRoomsDisplay::attach_container(SlidingWindowContainer* container) {
    if (container == container_) {
        return;
    }
    if (container_) {
        clear_container_callbacks(*container_);
    }
    container_ = container;
    if (container_) {
        configure_container(*container_);
        container_->set_header_text(header_text_);
        container_->set_scrollbar_visible(true);
        container_->set_header_visible(true);
        container_->set_close_button_enabled(false);
        container_->set_blocks_editor_interactions(true);
        container_->request_layout();
    }
}

void MapRoomsDisplay::detach_container() {
    if (!container_) {
        return;
    }
    clear_container_callbacks(*container_);
    container_ = nullptr;
}

void MapRoomsDisplay::set_map_info(nlohmann::json* map_info) {
    if (map_info_ == map_info) {
        return;
    }
    map_info_ = map_info;
    rebuild_rows();
}

void MapRoomsDisplay::set_on_select_room(SelectRoomCallback cb) {
    on_select_room_ = std::move(cb);
}

void MapRoomsDisplay::set_header_text(const std::string& text) {
    header_text_ = text;
    if (container_) {
        container_->set_header_text(header_text_);
    }
}

void MapRoomsDisplay::refresh() {
    rebuild_rows();
}

void MapRoomsDisplay::configure_container(SlidingWindowContainer& container) {
    container.set_layout_function([this](const SlidingWindowContainer::LayoutContext& ctx) {
        return this->layout_content(ctx);
    });
    container.set_render_function([this](SDL_Renderer* renderer) { this->render(renderer); });
    container.set_event_function([this](const SDL_Event& e) { return this->handle_event(e); });
    container.set_update_function([this](const Input& input, int screen_w, int screen_h) {
        this->update(input, screen_w, screen_h);
    });
}

void MapRoomsDisplay::clear_container_callbacks(SlidingWindowContainer& container) {
    container.set_layout_function({});
    container.set_render_function({});
    container.set_event_function({});
    container.set_update_function({});
    container.set_blocks_editor_interactions(false);
}

int MapRoomsDisplay::layout_content(const SlidingWindowContainer::LayoutContext& ctx) {
    const int row_height = DMButton::height();
    const int gap = DMSpacing::item_gap();
    int y = ctx.content_top;

    for (auto& row : rooms_) {
        row.rect = SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, row_height};
        y += row_height + gap;
    }

    return y;
}

void MapRoomsDisplay::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    const SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);

    const DMLabelStyle& label_style = DMStyles::Label();

    if (rooms_.empty()) {
        const std::string message = "No rooms defined";
        SDL_Point size = MeasureLabelText(label_style, message);
        int text_x = 0;
        int text_y = 0;
        if (container_) {
            SDL_Rect panel = container_->panel_rect();
            text_x = panel.x + DMSpacing::panel_padding();
            text_y = panel.y + DMSpacing::panel_padding();
            if (size.y < panel.h) {
                text_y = panel.y + (panel.h - size.y) / 2;
            }
        }
        DrawLabelText(renderer, message, text_x, text_y, label_style);
        return;
    }

    const SDL_Color border = DMStyles::Border();
    const SDL_Color hover_fill = DMStyles::ButtonHoverFill();
    const SDL_Color normal_fill = DMStyles::ButtonBaseFill();

    for (const auto& row : rooms_) {
        SDL_Color fill = normal_fill;
        if (row.key == hovered_room_) {
            fill = hover_fill;
        }
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &row.rect);
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &row.rect);

        const int padding = DMSpacing::small_gap();
        SDL_Point text_size = MeasureLabelText(label_style, row.name);
        int text_x = row.rect.x + padding;
        int text_y = row.rect.y + (row.rect.h - text_size.y) / 2;
        DrawLabelText(renderer, row.name, text_x, text_y, label_style);
    }
}

bool MapRoomsDisplay::handle_event(const SDL_Event& e) {
    if (rooms_.empty()) {
        return false;
    }

    switch (e.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            SDL_Point p = event_point_from_event(e);
            bool consumed = false;
            bool hovered = false;
            for (const auto& row : rooms_) {
                if (SDL_PointInRect(&p, &row.rect) == SDL_TRUE) {
                    hovered = true;
                    set_hovered_room(row.key);
                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        if (on_select_room_) {
                            on_select_room_(row.key);
                        }
                        consumed = true;
                    }
                    break;
                }
            }
            if (!hovered && e.type == SDL_MOUSEMOTION) {
                clear_hover();
            }
            return consumed;
        }
        default:
            break;
    }

    return false;
}

void MapRoomsDisplay::update(const Input&, int, int) {
    // No-op for now; placeholder for potential future behavior.
}

void MapRoomsDisplay::rebuild_rows() {
    rooms_.clear();
    clear_hover();

    if (!map_info_ || !map_info_->is_object()) {
        if (container_) {
            container_->request_layout();
        }
        return;
    }

    auto rooms_it = map_info_->find("rooms_data");
    if (rooms_it == map_info_->end() || !rooms_it->is_object()) {
        if (container_) {
            container_->request_layout();
        }
        return;
    }

    for (auto it = rooms_it->begin(); it != rooms_it->end(); ++it) {
        if (!it.value().is_object()) {
            continue;
        }
        RoomRow row;
        row.key = it.key();
        row.name = room_display_name(row.key, it.value());
        rooms_.push_back(std::move(row));
    }

    std::sort(
        rooms_.begin(),
        rooms_.end(),
        [](const RoomRow& a, const RoomRow& b) {
            if (a.name == b.name) {
                return a.key < b.key;
            }
            return a.name < b.name;
        });

    if (container_) {
        container_->request_layout();
    }
}

void MapRoomsDisplay::set_hovered_room(const std::string& key) {
    if (hovered_room_ == key) {
        return;
    }
    hovered_room_ = key;
}

void MapRoomsDisplay::clear_hover() {
    if (hovered_room_.empty()) {
        return;
    }
    hovered_room_.clear();
}

