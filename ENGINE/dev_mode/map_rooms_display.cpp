#include "map_rooms_display.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "SlidingWindowContainer.hpp"
#include "dm_styles.hpp"
#include "font_cache.hpp"
#include "map_layers_common.hpp"
#include "widgets.hpp"
#include "dev_mode_color_utils.hpp"
#include "dev_mode_sdl_event_utils.hpp"

namespace {
constexpr int kRoomDeleteButtonMaxSize = 22;

std::string ellipsize(const std::string& text, int max_width, const DMLabelStyle& style) {
    if (max_width <= 0) {
        return std::string{};
    }
    SDL_Point full = MeasureLabelText(style, text);
    if (full.x <= max_width) {
        return text;
    }
    const std::string ellipsis = "...";
    SDL_Point ellipsis_size = MeasureLabelText(style, ellipsis);
    if (ellipsis_size.x > max_width) {
        return std::string{};
    }
    std::string result = text;
    while (!result.empty()) {
        result.pop_back();
        SDL_Point trial = MeasureLabelText(style, result + ellipsis);
        if (trial.x <= max_width) {
            return result + ellipsis;
        }
    }
    return text;
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

}

MapRoomsDisplay::MapRoomsDisplay() {
    create_room_button_ = std::make_unique<DMButton>("Create Room", &DMStyles::CreateButton(), 180, DMButton::height());
}

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

void MapRoomsDisplay::set_on_rooms_changed(std::function<void()> cb) {
    on_rooms_changed_ = std::move(cb);
}

void MapRoomsDisplay::set_on_create_room(std::function<void()> cb) {
    on_create_room_ = std::move(cb);
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
    const int top_spacing = DMSpacing::section_gap();
    const int padding = DMSpacing::small_gap();
    int y = ctx.content_top + top_spacing;

    if (create_room_button_) {
        int button_width = create_room_button_->preferred_width();
        if (button_width <= 0) {
            button_width = 180;
        }
        button_width = std::min(ctx.content_width, button_width);
        button_width = std::max(button_width, 0);
        SDL_Rect button_rect{ctx.content_x, y - ctx.scroll_value, button_width, DMButton::height()};
        create_room_button_->set_rect(button_rect);
        y += button_rect.h + gap;
    }

    for (auto& row : rooms_) {
        row.rect = SDL_Rect{ctx.content_x, y - ctx.scroll_value, ctx.content_width, row_height};
        int delete_size = std::max(4, row.rect.h - 2 * padding);
        delete_size = std::min(delete_size, kRoomDeleteButtonMaxSize);
        int actual_delete = std::max(16, delete_size);
        row.delete_rect = SDL_Rect{row.rect.x + row.rect.w - actual_delete - padding,
                                   row.rect.y + (row.rect.h - actual_delete) / 2, actual_delete, actual_delete};
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

    if (create_room_button_) {
        create_room_button_->render(renderer);
    }

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
    const DMButtonStyle& delete_style = DMStyles::DeleteButton();

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
        const int base_swatch = std::max(12, row.rect.h - padding * 2);
        const int max_swatch = std::max(12, row.rect.w / 6);
        const int swatch_size = std::max(12, std::min(base_swatch, max_swatch));
        const int swatch_height = std::max(8, std::min(swatch_size, std::max(0, row.rect.h - padding)));
        SDL_Rect swatch{row.rect.x + padding,
                        row.rect.y + std::max(0, (row.rect.h - swatch_height) / 2), swatch_size, swatch_height};
        int text_x = row.rect.x + padding;
        if (swatch.w > 0 && swatch.h > 0 && swatch.x + swatch.w <= row.rect.x + row.rect.w) {
            SDL_Color swatch_color = row.display_color;
            swatch_color.a = 255;
            if (row.key == hovered_room_) {
                swatch_color = lighten(swatch_color, 0.18f);
                swatch_color.a = 255;
            }
            SDL_SetRenderDrawColor(renderer, swatch_color.r, swatch_color.g, swatch_color.b, swatch_color.a);
            SDL_RenderFillRect(renderer, &swatch);
            SDL_Color swatch_outline = DMStyles::Border();
            SDL_SetRenderDrawColor(renderer, swatch_outline.r, swatch_outline.g, swatch_outline.b, swatch_outline.a);
            SDL_RenderDrawRect(renderer, &swatch);
            text_x = swatch.x + swatch.w + padding;
        }

        SDL_Point text_size = MeasureLabelText(label_style, row.name);
        int text_y = row.rect.y + (row.rect.h - text_size.y) / 2;
        const int available = row.delete_rect.x - text_x - padding;
        std::string label = row.name;
        if (available > 0) {
            label = ellipsize(row.name, available, label_style);
        } else {
            label.clear();
        }
        if (!label.empty()) {
            DrawLabelText(renderer, label, text_x, text_y, label_style);
        }

        SDL_Color delete_fill = delete_style.bg;
        if (row.key == hovered_delete_room_) {
            delete_fill = delete_style.hover_bg;
        }
        SDL_SetRenderDrawColor(renderer, delete_fill.r, delete_fill.g, delete_fill.b, delete_fill.a);
        SDL_RenderFillRect(renderer, &row.delete_rect);
        SDL_SetRenderDrawColor(renderer, delete_style.border.r, delete_style.border.g, delete_style.border.b, delete_style.border.a);
        SDL_RenderDrawRect(renderer, &row.delete_rect);

        SDL_Color glyph = delete_style.text;
        SDL_SetRenderDrawColor(renderer, glyph.r, glyph.g, glyph.b, glyph.a);
        const int inset = std::max(2, row.delete_rect.w / 4);
        SDL_RenderDrawLine(renderer, row.delete_rect.x + inset, row.delete_rect.y + inset, row.delete_rect.x + row.delete_rect.w - inset, row.delete_rect.y + row.delete_rect.h - inset);
        SDL_RenderDrawLine(renderer, row.delete_rect.x + inset, row.delete_rect.y + row.delete_rect.h - inset, row.delete_rect.x + row.delete_rect.w - inset, row.delete_rect.y + inset);
    }
}

bool MapRoomsDisplay::handle_event(const SDL_Event& e) {
    if (create_room_button_) {
        const bool button_used = create_room_button_->handle_event(e);
        if (button_used && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            create_room_entry();
        }
        if (button_used) {
            return true;
        }
    }

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
            bool delete_hovered = false;
            for (const auto& row : rooms_) {
                if (SDL_PointInRect(&p, &row.rect) == SDL_TRUE) {
                    hovered = true;
                    set_hovered_room(row.key);
                    if (SDL_PointInRect(&p, &row.delete_rect) == SDL_TRUE) {
                        hovered_delete_room_ = row.key;
                        delete_hovered = true;
                        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                            delete_room_entry(row.key);
                            return true;
                        }
                        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                            return true;
                        }
                        break;
                    }
                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        if (on_select_room_) {
                            on_select_room_(row.key);
                        }
                        consumed = true;
                    }
                    break;
                }
            }
            if (!delete_hovered) {
                hovered_delete_room_.clear();
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
        row.display_color = SDL_Color{180, 188, 202, 255};
        if (auto display = utils::display_color::read(it.value())) {
            row.display_color = *display;
            row.display_color.a = 255;
        }
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
        hovered_delete_room_.clear();
        return;
    }
    hovered_room_.clear();
    hovered_delete_room_.clear();
}

void MapRoomsDisplay::create_room_entry() {
    if (on_create_room_) {
        on_create_room_();
        return;
    }
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }
    std::string key = map_layers::create_room_entry(*map_info_);
    if (key.empty()) {
        return;
    }
    rebuild_rows();
    if (container_) {
        container_->request_layout();
    }
    if (on_rooms_changed_) {
        on_rooms_changed_();
    }
}

void MapRoomsDisplay::delete_room_entry(const std::string& key) {
    if (!map_info_ || !map_info_->is_object()) {
        return;
    }

    auto rooms_it = map_info_->find("rooms_data");
    if (rooms_it == map_info_->end() || !rooms_it->is_object()) {
        return;
    }

    auto layer_cleanup = [&](nlohmann::json& layer) {
        auto rooms_it_layer = layer.find("rooms");
        if (rooms_it_layer != layer.end() && rooms_it_layer->is_array()) {
            auto& rooms_arr = *rooms_it_layer;
            rooms_arr.erase(std::remove_if(rooms_arr.begin(), rooms_arr.end(), [&](nlohmann::json& candidate) {
                              bool remove = false;
                              if (candidate.is_object()) {
                                  if (candidate.value("name", std::string()) == key) {
                                      remove = true;
                                  }
                                  auto children_it = candidate.find("required_children");
                                  if (children_it != candidate.end() && children_it->is_array()) {
                                      auto& children = *children_it;
                                      children.erase(std::remove_if(children.begin(), children.end(), [&](const nlohmann::json& child) {
                                                          return child.is_string() && child.get<std::string>() == key;
                                                      }),
                                                     children.end());
                                  }
                              } else if (candidate.is_string() && candidate.get<std::string>() == key) {
                                  remove = true;
                              }
                              return remove;
                          }),
                          rooms_arr.end());
        }
};

    auto layers_it = map_info_->find("map_layers");
    if (layers_it != map_info_->end() && layers_it->is_array()) {
        for (auto& layer : *layers_it) {
            if (layer.is_object()) {
                layer_cleanup(layer);
            }
        }
    }

    rooms_it->erase(key);

    hovered_room_.clear();
    hovered_delete_room_.clear();
    rebuild_rows();
    if (container_) {
        container_->request_layout();
    }
    if (on_rooms_changed_) {
        on_rooms_changed_();
    }
}

