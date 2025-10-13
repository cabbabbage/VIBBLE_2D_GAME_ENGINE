#include "FrameMovementEditor.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <utility>

#include "../../AnimationDocument.hpp"
#include "../../PanelLayoutConstants.hpp"
#include "../../../../dm_styles.hpp"
#include "../../../../draw_utils.hpp"
#include "FramePropertiesPanel.hpp"
#include "MovementCanvas.hpp"
#include "TotalsPanel.hpp"

namespace animation_editor {

namespace {

constexpr int kSidePanelWidth = 240;
constexpr int kTotalsHeight = 120;
constexpr int kVariantHeaderPadding = kPanelPadding;
constexpr int kVariantTabHeight = 28;
constexpr int kVariantTabSpacing = 6;
constexpr int kVariantTabWidth = 140;
constexpr int kVariantCloseSize = 18;

int clamp_index(int index, int max_value) {
    if (max_value <= 0) return 0;
    return std::clamp(index, 0, max_value - 1);
}

std::vector<MovementFrame> parse_movement_frames(const nlohmann::json& payload) {
    std::vector<MovementFrame> frames;
    if (!payload.is_array()) {
        frames.push_back(MovementFrame{});
        return frames;
    }
    for (const auto& entry : payload) {
        MovementFrame frame;
        if (entry.is_array()) {
            if (!entry.empty() && entry[0].is_number()) {
                frame.dx = entry[0].get<float>();
            }
            if (entry.size() > 1 && entry[1].is_number()) {
                frame.dy = entry[1].get<float>();
            }
            if (entry.size() > 2 && entry[2].is_boolean()) {
                frame.resort_z = entry[2].get<bool>();
            }
        } else if (entry.is_object()) {
            frame.dx = entry.value("dx", 0.0f);
            frame.dy = entry.value("dy", 0.0f);
            frame.resort_z = entry.value("resort_z", false);
        }
        frames.push_back(frame);
    }
    if (frames.empty()) {
        frames.push_back(MovementFrame{});
    }
    frames.front().dx = 0.0f;
    frames.front().dy = 0.0f;
    return frames;
}

nlohmann::json serialize_frames_to_json(const std::vector<MovementFrame>& frames) {
    nlohmann::json movement = nlohmann::json::array();
    for (size_t i = 0; i < frames.size(); ++i) {
        const MovementFrame& frame = frames[i];
        int dx = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dx));
        int dy = static_cast<int>(std::lround(i == 0 ? 0.0f : frame.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (frame.resort_z) {
            entry.push_back(frame.resort_z);
        }
        movement.push_back(entry);
    }
    if (movement.empty()) {
        movement.push_back(nlohmann::json::array({0, 0}));
    }
    movement[0][0] = 0;
    movement[0][1] = 0;
    return movement;
}

bool frames_equal(const std::vector<MovementFrame>& a, const std::vector<MovementFrame>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const MovementFrame& lhs = a[i];
        const MovementFrame& rhs = b[i];
        if (lhs.resort_z != rhs.resort_z) return false;
        if (std::fabs(lhs.dx - rhs.dx) > 0.001f) return false;
        if (std::fabs(lhs.dy - rhs.dy) > 0.001f) return false;
    }
    return true;
}

void render_tab_text(SDL_Renderer* renderer, const std::string& text, const SDL_Rect& rect, SDL_Color color) {
    if (!renderer || text.empty()) {
        return;
    }

    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{rect.x + (rect.w - surface->w) / 2, rect.y + (rect.h - surface->h) / 2, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

std::vector<MovementFrame> default_variant_frames() {
    return parse_movement_frames(nlohmann::json::array());
}

}

FrameMovementEditor::FrameMovementEditor() { ensure_children(); }

void FrameMovementEditor::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    load_frames_from_document();
}

void FrameMovementEditor::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    load_frames_from_document();
}

void FrameMovementEditor::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    update_layout();
}

void FrameMovementEditor::set_close_callback(CloseCallback callback) { close_callback_ = std::move(callback); }

void FrameMovementEditor::update() {
    ensure_children();
    if (canvas_) {
        canvas_->update();
        if (selected_index_ != canvas_->selected_index()) {
            selected_index_ = canvas_->selected_index();
            synchronize_selection();
        }
    }
    if (totals_panel_) totals_panel_->update();
    if (properties_panel_) {
        properties_panel_->update();
        if (properties_panel_->take_dirty_flag()) {
            mark_dirty();
        }
    }

    if (dirty_) {
        apply_changes();
        dirty_ = false;
    }
}

void FrameMovementEditor::render(SDL_Renderer* renderer) const {
    render_variant_header(renderer);
    if (canvas_) canvas_->render(renderer);
    if (totals_panel_) totals_panel_->render(renderer);
    if (properties_panel_) properties_panel_->render(renderer);
}

bool FrameMovementEditor::handle_event(const SDL_Event& e) {
    if (handle_variant_header_event(e)) {
        return true;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (close_callback_) close_callback_();
        return true;
    }

    bool consumed = false;
    if (canvas_ && canvas_->handle_event(e)) {
        const auto& updated_frames = canvas_->frames();
        bool changed = !frames_equal(frames_, updated_frames);
        frames_ = updated_frames;
        selected_index_ = canvas_->selected_index();
        if (totals_panel_) totals_panel_->set_frames(frames_);
        if (properties_panel_) {
            properties_panel_->set_frames(&frames_);
            properties_panel_->refresh_from_selection();
        }
        if (changed) {
            mark_dirty();
        } else {
            synchronize_selection();
        }
        consumed = true;
    }

    if (totals_panel_ && totals_panel_->handle_event(e)) {
        synchronize_selection();
        consumed = true;
    }

    if (properties_panel_ && properties_panel_->handle_event(e)) {
        mark_dirty();
        consumed = true;
    }

    return consumed;
}

void FrameMovementEditor::load_frames_from_document() {
    ensure_children();
    frames_.clear();
    selected_index_ = 0;
    variants_.clear();
    variant_tabs_.clear();
    active_variant_index_ = 0;

    if (!document_ || animation_id_.empty()) {
        MovementVariant variant;
        variant.name = "Primary";
        variant.primary = true;
        variant.frames = default_variant_frames();
        variants_.push_back(std::move(variant));
    } else {
        auto payload_dump = document_->animation_payload(animation_id_);
        nlohmann::json payload = nlohmann::json::object();
        if (payload_dump.has_value()) {
            payload = nlohmann::json::parse(*payload_dump, nullptr, false);
            if (!payload.is_object()) {
                payload = nlohmann::json::object();
            }
        }

        nlohmann::json movement = nlohmann::json::array();
        if (payload.contains("movement")) {
            movement = payload["movement"];
        }

        MovementVariant primary;
        primary.name = "Primary";
        primary.primary = true;
        primary.frames = parse_movement_frames(movement);
        variants_.push_back(std::move(primary));

        if (payload.contains("movement_variants")) {
            const nlohmann::json& variants_json = payload["movement_variants"];
            if (variants_json.is_array()) {
                int generated_index = 1;
                for (const auto& entry : variants_json) {
                    MovementVariant variant;
                    variant.primary = false;
                    nlohmann::json movement_payload = entry;
                    if (entry.is_object()) {
                        variant.name = entry.value("name", "");
                        if (entry.contains("movement")) {
                            movement_payload = entry["movement"];
                        }
                    }
                    if (variant.name.empty()) {
                        variant.name = "Alternative " + std::to_string(generated_index);
                    }
                    ++generated_index;
                    variant.frames = parse_movement_frames(movement_payload);
                    variants_.push_back(std::move(variant));
                }
            }
        }

        if (variants_.empty()) {
            MovementVariant variant;
            variant.name = "Primary";
            variant.primary = true;
            variant.frames = default_variant_frames();
            variants_.push_back(std::move(variant));
        }
    }

    frames_ = variants_[active_variant_index_].frames;
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    variant_tabs_.resize(variants_.size());

    update_child_frames(false);
    layout_variant_header();
    dirty_ = false;
}

void FrameMovementEditor::apply_changes() {
    if (!document_ || animation_id_.empty()) return;

    sync_active_variant_frames();

    auto payload_dump = document_->animation_payload(animation_id_);
    nlohmann::json payload = nlohmann::json::object();
    if (payload_dump.has_value()) {
        payload = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (!payload.is_object()) {
            payload = nlohmann::json::object();
        }
    }

    if (variants_.empty()) {
        MovementVariant variant;
        variant.name = "Primary";
        variant.primary = true;
        variant.frames = frames_;
        variants_.push_back(std::move(variant));
    }

    payload["movement"] = serialize_frames_to_json(variants_.front().frames);

    if (variants_.size() > 1) {
        nlohmann::json variants_json = nlohmann::json::array();
        for (size_t i = 1; i < variants_.size(); ++i) {
            nlohmann::json entry = nlohmann::json::object();
            entry["name"] = variants_[i].name;
            entry["movement"] = serialize_frames_to_json(variants_[i].frames);
            variants_json.push_back(std::move(entry));
        }
        payload["movement_variants"] = std::move(variants_json);
    } else {
        payload.erase("movement_variants");
    }

    document_->replace_animation_payload(animation_id_, payload.dump());
    if (totals_panel_) totals_panel_->set_frames(frames_);
}

void FrameMovementEditor::ensure_children() {
    if (!canvas_) {
        canvas_ = std::make_unique<MovementCanvas>();
    }
    if (!totals_panel_) {
        totals_panel_ = std::make_unique<TotalsPanel>();
        totals_panel_->set_selected_index(&selected_index_);
        totals_panel_->set_on_selection_changed([this](int index) {
            selected_index_ = clamp_index(index, static_cast<int>(frames_.size()));
            synchronize_selection();
        });
    } else {
        totals_panel_->set_selected_index(&selected_index_);
    }
    if (!properties_panel_) {
        properties_panel_ = std::make_unique<FramePropertiesPanel>();
        properties_panel_->set_frames(&frames_);
        properties_panel_->set_selected_index(&selected_index_);
        properties_panel_->set_on_frame_changed([this]() { mark_dirty(); });
    } else {
        properties_panel_->set_frames(&frames_);
        properties_panel_->set_selected_index(&selected_index_);
    }
    update_layout();
}

void FrameMovementEditor::update_layout() {
    if (bounds_.w <= 0 || bounds_.h <= 0) return;

    header_rect_ = SDL_Rect{bounds_.x, bounds_.y, bounds_.w, kVariantTabHeight + kVariantHeaderPadding * 2};

    const int content_top = header_rect_.y + header_rect_.h;
    const int content_height = std::max(0, bounds_.h - header_rect_.h);
    const int canvas_width = std::max(0, bounds_.w - kSidePanelWidth - kPanelPadding * 3);
    const int canvas_height = std::max(0, content_height - kTotalsHeight - kPanelPadding * 3);

    SDL_Rect canvas_bounds{bounds_.x + kPanelPadding, content_top + kPanelPadding, canvas_width, canvas_height};
    SDL_Rect totals_bounds{canvas_bounds.x, canvas_bounds.y + canvas_bounds.h + kPanelPadding, canvas_width, kTotalsHeight};
    SDL_Rect properties_bounds{canvas_bounds.x + canvas_width + kPanelPadding, content_top + kPanelPadding,
                               kSidePanelWidth, content_height - 2 * kPanelPadding};

    if (canvas_) canvas_->set_bounds(canvas_bounds);
    if (totals_panel_) totals_panel_->set_bounds(totals_bounds);
    if (properties_panel_) properties_panel_->set_bounds(properties_bounds);

    layout_variant_header();
}

void FrameMovementEditor::synchronize_selection() {
    selected_index_ = clamp_index(selected_index_, static_cast<int>(frames_.size()));
    if (canvas_) canvas_->set_selected_index(selected_index_);
    if (properties_panel_) properties_panel_->refresh_from_selection();
}

void FrameMovementEditor::mark_dirty() {
    sync_active_variant_frames();
    dirty_ = true;
    if (canvas_) {
        canvas_->set_frames(frames_, true);
        canvas_->set_selected_index(selected_index_);
    }
    if (totals_panel_) totals_panel_->set_frames(frames_);
}

void FrameMovementEditor::layout_variant_header() {
    if (variants_.size() != variant_tabs_.size()) {
        variant_tabs_.assign(variants_.size(), VariantTabState{});
    }

    if (header_rect_.w <= 0 || header_rect_.h <= 0) {
        add_button_rect_ = SDL_Rect{0, 0, 0, 0};
        return;
    }

    int x = header_rect_.x + kVariantHeaderPadding;
    int y = header_rect_.y + kVariantHeaderPadding;

    for (size_t i = 0; i < variants_.size(); ++i) {
        VariantTabState& tab = variant_tabs_[i];
        tab.rect = SDL_Rect{x, y, kVariantTabWidth, kVariantTabHeight};
        tab.close_visible = !variants_[i].primary;
        if (tab.close_visible) {
            tab.close_rect = SDL_Rect{tab.rect.x + tab.rect.w - kVariantCloseSize - 4,
                                      tab.rect.y + (tab.rect.h - kVariantCloseSize) / 2, kVariantCloseSize, kVariantCloseSize};
        } else {
            tab.close_rect = SDL_Rect{0, 0, 0, 0};
        }
        x += kVariantTabWidth + kVariantTabSpacing;
    }

    add_button_rect_ = SDL_Rect{x, y, kVariantTabHeight, kVariantTabHeight};
}

void FrameMovementEditor::render_variant_header(SDL_Renderer* renderer) const {
    if (!renderer || header_rect_.w <= 0 || header_rect_.h <= 0) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    dm_draw::DrawBeveledRect(
        renderer,
        header_rect_,
        DMStyles::CornerRadius(),
        DMStyles::BevelDepth(),
        DMStyles::PanelBG(),
        DMStyles::HighlightColor(),
        DMStyles::ShadowColor(),
        false,
        DMStyles::HighlightIntensity(),
        DMStyles::ShadowIntensity());

    const DMButtonStyle& active_style = DMStyles::AccentButton();
    const DMButtonStyle& inactive_style = DMStyles::HeaderButton();

    for (size_t i = 0; i < variants_.size(); ++i) {
        const MovementVariant& variant = variants_[i];
        const VariantTabState& tab = variant_tabs_[i];
        bool is_active = static_cast<int>(i) == active_variant_index_;
        const DMButtonStyle& style = is_active ? active_style : inactive_style;

        SDL_Color button_color = style.bg;
        if (tab.pressed) {
            button_color = style.press_bg;
        } else if (tab.hovered) {
            button_color = style.hover_bg;
        }
        SDL_SetRenderDrawColor(renderer, button_color.r, button_color.g, button_color.b, button_color.a);
        SDL_RenderFillRect(renderer, &tab.rect);
        SDL_SetRenderDrawColor(renderer, style.border.r, style.border.g, style.border.b, style.border.a);
        SDL_RenderDrawRect(renderer, &tab.rect);

        SDL_Rect text_rect = tab.rect;
        if (tab.close_visible) {
            text_rect.w = std::max(0, tab.close_rect.x - tab.rect.x - 4);
        }
        render_tab_text(renderer, variant.name, text_rect, style.text);

        if (tab.close_visible) {
            SDL_Color close_bg = style.bg;
            if (tab.close_pressed) {
                close_bg = style.press_bg;
            } else if (tab.close_hovered) {
                close_bg = style.hover_bg;
            }
            SDL_SetRenderDrawColor(renderer, close_bg.r, close_bg.g, close_bg.b, close_bg.a);
            SDL_RenderFillRect(renderer, &tab.close_rect);
            SDL_SetRenderDrawColor(renderer, style.border.r, style.border.g, style.border.b, style.border.a);
            SDL_RenderDrawRect(renderer, &tab.close_rect);
            render_tab_text(renderer, "×", tab.close_rect, style.text);
        }
    }

    SDL_Color add_color = add_button_pressed_ ? active_style.press_bg : (add_button_hovered_ ? active_style.hover_bg : active_style.bg);
    SDL_SetRenderDrawColor(renderer, add_color.r, add_color.g, add_color.b, add_color.a);
    SDL_RenderFillRect(renderer, &add_button_rect_);
    SDL_SetRenderDrawColor(renderer, active_style.border.r, active_style.border.g, active_style.border.b, active_style.border.a);
    SDL_RenderDrawRect(renderer, &add_button_rect_);
    render_tab_text(renderer, "+", add_button_rect_, active_style.text);
}

bool FrameMovementEditor::handle_variant_header_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEMOTION: {
            SDL_Point p{e.motion.x, e.motion.y};
            add_button_hovered_ = SDL_PointInRect(&p, &add_button_rect_) != 0;
            bool consumed = add_button_hovered_;
            for (size_t i = 0; i < variant_tabs_.size(); ++i) {
                VariantTabState& tab = variant_tabs_[i];
                tab.hovered = SDL_PointInRect(&p, &tab.rect) != 0;
                if (tab.close_visible) {
                    tab.close_hovered = SDL_PointInRect(&p, &tab.close_rect) != 0;
                } else {
                    tab.close_hovered = false;
                }
                consumed = consumed || tab.hovered || tab.close_hovered;
            }
            return consumed;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            SDL_Point p{e.button.x, e.button.y};
            if (SDL_PointInRect(&p, &add_button_rect_)) {
                add_button_pressed_ = true;
                return true;
            }
            for (auto& tab : variant_tabs_) {
                if (tab.close_visible && SDL_PointInRect(&p, &tab.close_rect)) {
                    tab.close_pressed = true;
                    return true;
                }
                if (SDL_PointInRect(&p, &tab.rect)) {
                    tab.pressed = true;
                    return true;
                }
            }
            return false;
        }
        case SDL_MOUSEBUTTONUP: {
            if (e.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            SDL_Point p{e.button.x, e.button.y};
            bool handled = false;
            if (add_button_pressed_) {
                bool inside = SDL_PointInRect(&p, &add_button_rect_) != 0;
                add_button_pressed_ = false;
                if (inside) {
                    add_new_variant();
                    handled = true;
                }
            }
            for (size_t i = 0; i < variant_tabs_.size(); ++i) {
                VariantTabState& tab = variant_tabs_[i];
                if (tab.close_pressed) {
                    bool inside_close = tab.close_visible && SDL_PointInRect(&p, &tab.close_rect) != 0;
                    tab.close_pressed = false;
                    if (inside_close) {
                        delete_variant(static_cast<int>(i));
                        handled = true;
                        break;
                    }
                }
                if (tab.pressed) {
                    bool inside_tab = SDL_PointInRect(&p, &tab.rect) != 0;
                    tab.pressed = false;
                    if (inside_tab) {
                        set_active_variant(static_cast<int>(i), false);
                        handled = true;
                    }
                }
            }
            return handled;
        }
        default:
            break;
    }
    return false;
}

void FrameMovementEditor::set_active_variant(int index, bool preserve_view) {
    if (index < 0 || index >= static_cast<int>(variants_.size())) {
        return;
    }
    if (index == active_variant_index_) {
        return;
    }

    sync_active_variant_frames();
    active_variant_index_ = index;
    frames_ = variants_[active_variant_index_].frames;
    selected_index_ = 0;
    update_child_frames(preserve_view);
    layout_variant_header();
    dirty_ = false;
}

void FrameMovementEditor::update_child_frames(bool preserve_view) {
    if (canvas_) {
        canvas_->set_frames(frames_, preserve_view);
        canvas_->set_selected_index(selected_index_);
    }
    if (totals_panel_) {
        totals_panel_->set_frames(frames_);
    }
    if (properties_panel_) {
        properties_panel_->set_frames(&frames_);
        properties_panel_->refresh_from_selection();
    }
}

void FrameMovementEditor::sync_active_variant_frames() {
    if (active_variant_index_ < 0 || active_variant_index_ >= static_cast<int>(variants_.size())) {
        return;
    }
    variants_[active_variant_index_].frames = frames_;
}

void FrameMovementEditor::add_new_variant() {
    sync_active_variant_frames();

    MovementVariant variant;
    variant.primary = false;
    variant.name = generate_variant_name();
    variant.frames = default_variant_frames();

    variants_.push_back(std::move(variant));
    active_variant_index_ = static_cast<int>(variants_.size() - 1);
    frames_ = variants_.back().frames;
    selected_index_ = 0;
    variant_tabs_.resize(variants_.size());
    update_child_frames(false);
    layout_variant_header();
    apply_changes();
    dirty_ = false;
}

void FrameMovementEditor::delete_variant(int index) {
    if (index <= 0 || index >= static_cast<int>(variants_.size())) {
        return;
    }

    variants_.erase(variants_.begin() + index);
    if (variants_.empty()) {
        MovementVariant variant;
        variant.name = "Primary";
        variant.primary = true;
        variant.frames = default_variant_frames();
        variants_.push_back(std::move(variant));
    }

    if (active_variant_index_ >= static_cast<int>(variants_.size())) {
        active_variant_index_ = static_cast<int>(variants_.size()) - 1;
    }
    frames_ = variants_[active_variant_index_].frames;
    selected_index_ = 0;
    variant_tabs_.resize(variants_.size());
    update_child_frames(false);
    layout_variant_header();
    apply_changes();
    dirty_ = false;
}

std::string FrameMovementEditor::generate_variant_name() const {
    int suffix = 1;
    while (true) {
        std::string candidate = "Alternative " + std::to_string(suffix);
        bool exists = false;
        for (const auto& variant : variants_) {
            if (variant.name == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++suffix;
    }
}

}

