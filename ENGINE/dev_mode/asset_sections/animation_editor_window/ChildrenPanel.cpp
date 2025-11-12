#include "ChildrenPanel.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include <nlohmann/json.hpp>

#include "AnimationDocument.hpp"
#include "PanelLayoutConstants.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "search_assets.hpp"
#include "string_utils.hpp"
#include "utils/input.hpp"

namespace {

constexpr int kRowSpacing = 6;
constexpr SDL_Color kRowBackground{32, 38, 44, 255};
constexpr SDL_Color kRowBorder{64, 72, 80, 255};
constexpr SDL_Color kPlaceholderBg{58, 66, 74, 255};
constexpr SDL_Color kPlaceholderText{180, 190, 200, 255};

using animation_editor::ChildrenPanel;
using animation_editor::strings::has_numeric_stem;

void render_text(SDL_Renderer* renderer, const DMLabelStyle& style, const std::string& text, int x, int y,
                 SDL_Color color) {
    if (!renderer || text.empty()) return;
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

int label_height() {
    const DMLabelStyle& style = DMStyles::Label();
    return style.font_size + DMSpacing::small_gap();
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace animation_editor {

ChildrenPanel::ChildrenPanel() {
    add_button_ = std::make_unique<DMButton>("Add Child", &DMStyles::CreateButton(), 140, DMButton::height());
}

ChildrenPanel::~ChildrenPanel() = default;

void ChildrenPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    cached_repo_root_.clear();
    payload_signature_.clear();
    refresh_from_document();
}

void ChildrenPanel::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
    if (search_assets_) {
        search_assets_->set_manifest_store(store);
    }
    preview_cache_.clear();
}

void ChildrenPanel::set_animation_id(const std::string& animation_id) {
    if (animation_id_ == animation_id) {
        return;
    }
    animation_id_ = animation_id;
    payload_signature_.clear();
    refresh_from_document();
}

void ChildrenPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
    position_search_panel();
}

void ChildrenPanel::set_status_callback(StatusCallback callback) { status_callback_ = std::move(callback); }

void ChildrenPanel::set_layout_dirty_callback(std::function<void()> callback) { layout_dirty_callback_ = std::move(callback); }

void ChildrenPanel::update() {
    if (!document_ || animation_id_.empty()) {
        display_children_.clear();
        if (!payload_signature_.empty()) {
            payload_signature_.clear();
            request_layout();
        }
    } else {
        auto payload_dump = document_->animation_payload(animation_id_);
        std::string signature = payload_dump.has_value() ? *payload_dump : std::string{};
        if (signature != payload_signature_) {
            payload_signature_ = signature;
            refresh_from_document();
        }
    }

    if (search_assets_ && search_assets_->visible()) {
        Input dummy;
        search_assets_->update(dummy);
        position_search_panel();
    }
}

bool ChildrenPanel::handle_event(const SDL_Event& e) {
    update_layout();

    bool handled = false;
    auto handle_add_button = [&]() {
        if (!add_button_) return;
        bool activated = add_button_->handle_event(e);
        if (!activated) return;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            open_search_panel();
        }
        handled = true;
    };

    if (search_assets_ && search_assets_->visible()) {
        if (search_assets_->handle_event(e)) {
            handled = true;
        }
    }

    if (inherits_children_) {
        // No direct interactions except closing search panel
        handle_add_button();
    } else {
        handle_add_button();
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            int mx = 0;
            int my = 0;
            if (e.type == SDL_MOUSEBUTTONUP) {
                mx = e.button.x;
                my = e.button.y;
            }
            for (size_t i = 0; i < remove_rects_.size(); ++i) {
                if (point_inside(remove_rects_[i], mx, my)) {
                    remove_child_entry(i);
                    handled = true;
                    break;
                }
            }
        }
    }

    if (search_assets_ && search_assets_->visible() && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point pt{e.button.x, e.button.y};
        SDL_Rect rect = search_assets_->rect();
        if (!SDL_PointInRect(&pt, &rect) && !point_inside(add_button_rect_, pt.x, pt.y)) {
            close_search_panel();
        }
    }

    return handled;
}

int ChildrenPanel::preferred_height(int width) const {
    (void)width;
    const int padding = kPanelPadding;
    int height = padding * 2 + DMButton::height();

    if (inherits_children_ && !inherited_message_lines_.empty()) {
        height += DMSpacing::small_gap();
        height += static_cast<int>(inherited_message_lines_.size()) * label_height();
    } else if (display_children_.empty()) {
        height += DMSpacing::small_gap();
        height += label_height();
    }

    if (!display_children_.empty()) {
        height += DMSpacing::small_gap();
        if (!inherits_children_) {
            height += static_cast<int>(display_children_.size()) * row_height_;
            if (display_children_.size() > 1) {
                height += (static_cast<int>(display_children_.size()) - 1) * kRowSpacing;
            }
        } else {
            height += static_cast<int>(display_children_.size()) * row_height_;
            if (display_children_.size() > 1) {
                height += (static_cast<int>(display_children_.size()) - 1) * kRowSpacing;
            }
        }
    }

    return height;
}

bool ChildrenPanel::allow_out_of_bounds_pointer_events() const {
    // When the embedded search panel is visible, allow pointer events
    // outside the inspector bounds so the overlay can receive clicks/typing.
    return search_assets_ && search_assets_->visible();
}

void ChildrenPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (bounds_.w <= 0 || bounds_.h <= 0) return;

    update_layout();

    const DMLabelStyle& label_style = DMStyles::Label();
    render_text(renderer, label_style, "Children", header_rect_.x, header_rect_.y, label_style.color);
    if (add_button_) {
        add_button_->render(renderer);
    }

    if (inherits_children_ && !inherited_message_lines_.empty()) {
        int line_y = message_rect_.y;
        for (const auto& line : inherited_message_lines_) {
            render_text(renderer, label_style, line, message_rect_.x, line_y, label_style.color);
            line_y += label_height();
        }
    } else if (display_children_.empty()) {
        render_text(renderer, label_style, "No children configured.", message_rect_.x, message_rect_.y, label_style.color);
    }

    SDL_Color text_color = label_style.color;

    for (size_t i = 0; i < row_rects_.size() && i < display_children_.size(); ++i) {
        const SDL_Rect& row = row_rects_[i];
        SDL_SetRenderDrawColor(renderer, kRowBackground.r, kRowBackground.g, kRowBackground.b, kRowBackground.a);
        SDL_RenderFillRect(renderer, &row);
        SDL_SetRenderDrawColor(renderer, kRowBorder.r, kRowBorder.g, kRowBorder.b, kRowBorder.a);
        SDL_RenderDrawRect(renderer, &row);

        const SDL_Rect& icon_rect = icon_rects_[i];
        SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
        SDL_RenderFillRect(renderer, &icon_rect);

        const std::string& child = display_children_[i];
        if (!child.empty() && child.front() != '#') {
            SDL_Texture* texture = acquire_child_icon(renderer, child);
            if (texture) {
                int tex_w = 0;
                int tex_h = 0;
                SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
                if (tex_w > 0 && tex_h > 0) {
                    float scale = std::min(icon_rect.w / static_cast<float>(tex_w), icon_rect.h / static_cast<float>(tex_h));
                    int draw_w = static_cast<int>(tex_w * scale);
                    int draw_h = static_cast<int>(tex_h * scale);
                    SDL_Rect dst{icon_rect.x + (icon_rect.w - draw_w) / 2,
                                 icon_rect.y + (icon_rect.h - draw_h) / 2,
                                 draw_w,
                                 draw_h};
                    SDL_RenderCopy(renderer, texture, nullptr, &dst);
                }
            } else {
                SDL_SetRenderDrawColor(renderer, kPlaceholderBg.r, kPlaceholderBg.g, kPlaceholderBg.b, kPlaceholderBg.a);
                SDL_RenderFillRect(renderer, &icon_rect);
            }
        } else {
            SDL_SetRenderDrawColor(renderer, kPlaceholderBg.r, kPlaceholderBg.g, kPlaceholderBg.b, kPlaceholderBg.a);
            SDL_RenderFillRect(renderer, &icon_rect);
            render_text(renderer, label_style, "#", icon_rect.x + icon_rect.w / 2 - 4, icon_rect.y + icon_rect.h / 2 - label_style.font_size / 2,
                        kPlaceholderText);
        }

        int text_x = icon_rect.x + icon_rect.w + DMSpacing::item_gap();
        render_text(renderer, label_style, child, text_x, row.y + (row.h - label_style.font_size) / 2, text_color);

        if (!inherits_children_ && i < remove_rects_.size()) {
            const SDL_Rect& remove = remove_rects_[i];
            dm_draw::DrawBeveledRect(renderer,
                                     remove,
                                     DMStyles::CornerRadius(),
                                     1,
                                     DMStyles::DeleteButton().bg,
                                     DMStyles::HighlightColor(),
                                     DMStyles::ShadowColor(),
                                     true,
                                     DMStyles::HighlightIntensity(),
                                     DMStyles::ShadowIntensity());
            render_text(renderer, label_style, "x", remove.x + remove.w / 2 - 4, remove.y + remove.h / 2 - label_style.font_size / 2,
                        DMStyles::DeleteButton().text);
        }
    }

    if (search_assets_ && search_assets_->visible()) {
        search_assets_->render(renderer);
    }
}

void ChildrenPanel::update_layout() const {
    if (!layout_dirty_) {
        return;
    }
    auto* self = const_cast<ChildrenPanel*>(this);
    const int padding = kPanelPadding;
    int x = bounds_.x + padding;
    int y = bounds_.y + padding;
    int width = std::max(0, bounds_.w - padding * 2);

    self->header_rect_ = SDL_Rect{x, y, width, DMButton::height()};
    y += header_rect_.h;

    if (add_button_) {
        SDL_Rect button_rect{header_rect_.x + header_rect_.w - add_button_->preferred_width(),
                             header_rect_.y,
                             add_button_->preferred_width(),
                             DMButton::height()};
        add_button_->set_rect(button_rect);
        self->add_button_rect_ = button_rect;
        self->search_anchor_rect_ = SDL_Rect{button_rect.x,
                                             button_rect.y + button_rect.h + DMSpacing::small_gap(),
                                             std::max(button_rect.w * 2, std::min(bounds_.w, 320)),
                                             260};
    } else {
        self->add_button_rect_ = SDL_Rect{0, 0, 0, 0};
        self->search_anchor_rect_ = SDL_Rect{bounds_.x + padding,
                                             bounds_.y + padding + DMButton::height(),
                                             std::max(bounds_.w / 2, 320),
                                             260};
    }

    y += DMSpacing::small_gap();

    if (inherits_children_ && !inherited_message_lines_.empty()) {
        self->message_rect_ = SDL_Rect{x, y, width, static_cast<int>(inherited_message_lines_.size()) * label_height()};
        y += message_rect_.h + DMSpacing::small_gap();
    } else if (display_children_.empty()) {
        self->message_rect_ = SDL_Rect{x, y, width, label_height()};
        y += message_rect_.h + DMSpacing::small_gap();
    } else {
        self->message_rect_ = SDL_Rect{0, 0, 0, 0};
    }

    if (!display_children_.empty()) {
        self->row_rects_.clear();
        self->remove_rects_.clear();
        self->icon_rects_.clear();
        int list_height = static_cast<int>(display_children_.size()) * row_height_;
        if (display_children_.size() > 1) {
            list_height += (static_cast<int>(display_children_.size()) - 1) * kRowSpacing;
        }
        self->list_rect_ = SDL_Rect{x, y, width, list_height};
        int row_y = y;
        for (size_t i = 0; i < display_children_.size(); ++i) {
            SDL_Rect row{x, row_y, width, row_height_};
            row_rects_.push_back(row);
            SDL_Rect icon{row.x + DMSpacing::item_gap(),
                          row.y + (row_height_ - icon_size_) / 2,
                          icon_size_,
                          icon_size_};
            icon_rects_.push_back(icon);
            SDL_Rect remove{row.x + row.w - DMButton::height() - DMSpacing::item_gap(),
                            row.y + (row_height_ - DMButton::height()) / 2,
                            DMButton::height(),
                            DMButton::height()};
            remove_rects_.push_back(remove);
            row_y += row_height_ + kRowSpacing;
        }
        y += list_height;
    } else {
        self->list_rect_ = SDL_Rect{x, y, width, 0};
        self->row_rects_.clear();
        self->remove_rects_.clear();
        self->icon_rects_.clear();
    }

    self->layout_dirty_ = false;
    position_search_panel();
}

void ChildrenPanel::refresh_from_document() {
    local_children_.clear();
    inherited_children_.clear();
    display_children_.clear();
    inherited_message_lines_.clear();
    inherited_source_id_.clear();
    inherits_children_ = false;

    if (!document_ || animation_id_.empty()) {
        request_layout();
        return;
    }

    auto payload_dump = document_->animation_payload(animation_id_);
    if (!payload_dump.has_value()) {
        request_layout();
        return;
    }

    nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
    if (payload.is_discarded() || !payload.is_object()) {
        payload = nlohmann::json::object();
    }

    local_children_ = read_local_children(payload);

    bool derived = false;
    std::string source_animation;
    if (payload.contains("source") && payload["source"].is_object()) {
        const auto& src = payload["source"];
        derived = strings::to_lower_copy(src.value("kind", std::string{})) == std::string("animation");
        source_animation = src.value("name", std::string{});
        if (source_animation.empty()) {
            source_animation = src.value("path", std::string{});
        }
    }

    bool inherit_movement = payload.value("inherit_source_movement", derived);

    if (derived && inherit_movement && !source_animation.empty()) {
        inherits_children_ = true;
        inherited_source_id_ = source_animation;
        inherited_children_ = resolve_inherited_children(payload);
        display_children_ = inherited_children_;
        inherited_message_lines_.push_back("Children inherit from animation '" + inherited_source_id_ + "'.");
        inherited_message_lines_.push_back("Disable 'Inherit Source Movement' to edit.");
    } else {
        inherits_children_ = false;
        display_children_ = local_children_;
    }

    request_layout();
}

std::vector<std::string> ChildrenPanel::read_local_children(const nlohmann::json& payload) const {
    std::vector<std::string> result;
    if (!payload.is_object()) return result;
    if (!payload.contains("children") || !payload["children"].is_array()) {
        return result;
    }
    for (const auto& entry : payload["children"]) {
        if (!entry.is_string()) continue;
        std::string value = strings::trim_copy(entry.get<std::string>());
        if (value.empty()) continue;
        result.push_back(value);
    }
    return result;
}

std::vector<std::string> ChildrenPanel::resolve_inherited_children(const nlohmann::json& payload, int depth) const {
    if (depth > 8 || !document_) {
        return read_local_children(payload);
    }
    bool derived = false;
    std::string source_animation;
    if (payload.contains("source") && payload["source"].is_object()) {
        const auto& src = payload["source"];
        derived = strings::to_lower_copy(src.value("kind", std::string{})) == std::string("animation");
        source_animation = src.value("name", std::string{});
        if (source_animation.empty()) {
            source_animation = src.value("path", std::string{});
        }
    }
    bool inherit_movement = payload.value("inherit_source_movement", derived);
    if (!derived || !inherit_movement || source_animation.empty()) {
        return read_local_children(payload);
    }

    auto src_payload_dump = document_->animation_payload(source_animation);
    if (!src_payload_dump.has_value()) {
        return read_local_children(payload);
    }
    nlohmann::json src_payload = nlohmann::json::parse(*src_payload_dump, nullptr, false);
    if (src_payload.is_discarded() || !src_payload.is_object()) {
        return read_local_children(payload);
    }
    return resolve_inherited_children(src_payload, depth + 1);
}

void ChildrenPanel::commit_children() {
    if (!document_ || animation_id_.empty()) {
        return;
    }
    nlohmann::json payload = nlohmann::json::object();
    if (auto payload_dump = document_->animation_payload(animation_id_)) {
        payload = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            payload = nlohmann::json::object();
        }
    }
    nlohmann::json children = nlohmann::json::array();
    for (const auto& child : local_children_) {
        children.push_back(child);
    }
    payload["children"] = std::move(children);
    document_->replace_animation_payload(animation_id_, payload.dump());
}

void ChildrenPanel::add_child_entry(const std::string& entry) {
    std::string value = strings::trim_copy(entry);
    if (value.empty()) {
        return;
    }
    if (value.front() != '#') {
        if (manifest_store_) {
            if (auto resolved = manifest_store_->resolve_asset_name(value)) {
                value = *resolved;
            }
        }
    }
    if (std::find(local_children_.begin(), local_children_.end(), value) != local_children_.end()) {
        request_status("Child '" + value + "' already in list.");
        return;
    }
    local_children_.push_back(value);
    if (!inherits_children_) {
        display_children_ = local_children_;
    }
    commit_children();
    request_layout();
    request_status("Added child '" + value + "'.");
}

void ChildrenPanel::remove_child_entry(size_t index) {
    if (index >= local_children_.size()) {
        return;
    }
    std::string removed = local_children_[index];
    local_children_.erase(local_children_.begin() + static_cast<long>(index));
    if (!inherits_children_) {
        display_children_ = local_children_;
    }
    commit_children();
    request_layout();
    request_status("Removed child '" + removed + "'.");
}

void ChildrenPanel::ensure_search_panel() {
    if (!search_assets_) {
        search_assets_ = std::make_unique<SearchAssets>(manifest_store_);
        search_assets_->set_embedded_mode(true);
        search_assets_->set_screen_dimensions(std::max(bounds_.w, 320), std::max(bounds_.h, 240));
    }
    if (manifest_store_) {
        search_assets_->set_manifest_store(manifest_store_);
    }
}

void ChildrenPanel::open_search_panel() {
    ensure_search_panel();
    if (!search_assets_) {
        return;
    }
    // Ensure the embedded panel has a valid rect before opening to avoid
    // zero-sized layout during initial update.
    position_search_panel();
    search_assets_->open([this](const std::string& selection) {
        add_child_entry(selection);
        close_search_panel();
    });
}

void ChildrenPanel::close_search_panel() {
    if (search_assets_) {
        search_assets_->close();
    }
}

void ChildrenPanel::position_search_panel() const {
    if (!search_assets_) {
        return;
    }
    const int padding = kPanelPadding;
    constexpr int kMinSearchWidth = 280;
    constexpr int kMinSearchHeight = 240;
    constexpr int kMaxSearchHeight = 420;

    int width = kMinSearchWidth;
    if (bounds_.w > padding * 2) {
        width = std::min(std::max(bounds_.w / 2, kMinSearchWidth), bounds_.w - padding * 2);
    }
    if (width <= 0) {
        width = kMinSearchWidth;
    }

    int header_bottom = header_rect_.h > 0 ? header_rect_.y + header_rect_.h : bounds_.y + padding;
    int height = std::min(std::max(kMinSearchHeight, bounds_.h / 3), kMaxSearchHeight);
    int available_height = bounds_.h - header_bottom - padding;
    if (available_height > 0) {
        height = std::min(height, available_height);
    }
    if (height <= 0) {
        height = kMinSearchHeight;
    }

    int min_x = bounds_.x + padding;
    int max_x = bounds_.x + bounds_.w - padding - width;
    if (max_x < min_x) {
        max_x = min_x;
    }

    int min_y = bounds_.y + padding;
    int max_y = bounds_.y + bounds_.h - padding - height;
    if (max_y < min_y) {
        max_y = min_y;
    }

    int x = min_x;
    if (add_button_rect_.w > 0 && add_button_rect_.h > 0) {
        int desired_x = add_button_rect_.x + add_button_rect_.w - width;
        x = std::clamp(desired_x, min_x, max_x);
    }

    int y = std::clamp(header_bottom + padding, min_y, max_y);

    // Keep the embedded search overlay wide and anchored below the header so results are visible immediately.
    search_assets_->set_embedded_rect(SDL_Rect{x, y, width, height});
}

void ChildrenPanel::request_layout() const {
    auto* self = const_cast<ChildrenPanel*>(this);
    self->layout_dirty_ = true;
    if (layout_dirty_callback_) {
        layout_dirty_callback_();
    }
}

void ChildrenPanel::request_status(const std::string& message) const {
    if (status_callback_) {
        status_callback_(message);
    }
}

bool ChildrenPanel::point_inside(const SDL_Rect& rect, int x, int y) const {
    if (rect.w <= 0 || rect.h <= 0) return false;
    SDL_Point pt{x, y};
    return SDL_PointInRect(&pt, &rect);
}

SDL_Texture* ChildrenPanel::acquire_child_icon(SDL_Renderer* renderer, const std::string& child_id) const {
    if (!renderer || child_id.empty() || child_id.front() == '#') {
        return nullptr;
    }

    auto cache_it = preview_cache_.find(child_id);
    if (cache_it != preview_cache_.end()) {
        std::error_code ec;
        auto current_write = std::filesystem::last_write_time(cache_it->second.frame_path, ec);
        if (!cache_it->second.frame_path.empty() && cache_it->second.renderer == renderer && !ec &&
            current_write == cache_it->second.last_write_time) {
            return cache_it->second.texture.get();
        }
        preview_cache_.erase(cache_it);
    }

    if (!manifest_store_) {
        return nullptr;
    }

    auto resolved = manifest_store_->resolve_asset_name(child_id);
    std::string asset_key = resolved ? *resolved : child_id;

    auto view = manifest_store_->get_asset(asset_key);
    if (!view || !view.data || !view.data->is_object()) {
        return nullptr;
    }
    const nlohmann::json& asset_json = *view.data;
    if (!asset_json.contains("animations") || !asset_json["animations"].is_object()) {
        return nullptr;
    }
    const nlohmann::json& animations = asset_json["animations"];
    const nlohmann::json* animation = nullptr;
    std::string animation_key;

    if (animations.contains("default")) {
        animation_key = "default";
        animation = &animations["default"];
    } else if (asset_json.contains("start") && asset_json["start"].is_string()) {
        animation_key = asset_json["start"].get<std::string>();
        auto it = animations.find(animation_key);
        if (it != animations.end()) {
            animation = &(*it);
        }
    }

    if (!animation) {
        for (auto it = animations.begin(); it != animations.end(); ++it) {
            if (it.value().is_object()) {
                animation_key = it.key();
                animation = &it.value();
                break;
            }
        }
    }

    if (!animation || !animation->is_object()) {
        return nullptr;
    }

    auto frame_path = resolve_frame_path(*animation, asset_key, animation_key);
    if (!frame_path.has_value()) {
        return nullptr;
    }

    SDL_Surface* surface = IMG_Load(frame_path->string().c_str());
    if (!surface) {
        return nullptr;
    }
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!converted) {
        return nullptr;
    }
    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, converted);
    SDL_FreeSurface(converted);
    if (!raw) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(raw, SDL_BLENDMODE_BLEND);
    auto shared = std::shared_ptr<SDL_Texture>(raw, SDL_DestroyTexture);

    PreviewEntry entry;
    entry.renderer = renderer;
    entry.texture = shared;
    entry.frame_path = *frame_path;
    std::error_code ec;
    entry.last_write_time = std::filesystem::last_write_time(entry.frame_path, ec);
    preview_cache_[child_id] = std::move(entry);
    return shared.get();
}

std::optional<std::filesystem::path> ChildrenPanel::resolve_frame_path(const nlohmann::json& animation_json,
                                                                       const std::string& asset_name,
                                                                       const std::string& animation_key) const {
    std::string source_path;
    if (animation_json.contains("source") && animation_json["source"].is_object()) {
        source_path = animation_json["source"].value("path", std::string{});
    }
    if (source_path.empty()) {
        source_path = animation_key;
    }

    std::filesystem::path candidate = resolve_candidate_path(source_path, asset_name);
    if (candidate.empty()) {
        return std::nullopt;
    }
    int frames = animation_json.value("number_of_frames", 0);
    std::filesystem::path png = ensure_png_in_folder(candidate, frames);
    if (png.empty()) {
        return std::nullopt;
    }
    return png;
}

std::filesystem::path ChildrenPanel::resolve_candidate_path(const std::filesystem::path& candidate,
                                                            const std::string& asset_name) const {
    std::vector<std::filesystem::path> attempts;
    attempts.push_back(candidate);

    std::filesystem::path repo_root = detect_repo_root();
    if (!candidate.is_absolute()) {
        attempts.push_back(repo_root / candidate);
        attempts.push_back(repo_root / "SRC" / asset_name / candidate);
        attempts.push_back(repo_root / "SRC" / "assets" / asset_name / candidate);
    } else {
        attempts.push_back(candidate);
    }

    std::error_code ec;
    for (const auto& attempt : attempts) {
        if (attempt.empty()) continue;
        if (std::filesystem::exists(attempt, ec)) {
            if (std::filesystem::is_directory(attempt, ec)) {
                return attempt;
            }
            return attempt.parent_path();
        }
    }
    return candidate;
}

std::filesystem::path ChildrenPanel::detect_repo_root() const {
    if (!cached_repo_root_.empty()) {
        return cached_repo_root_;
    }
    std::filesystem::path start;
    if (document_) {
        start = document_->asset_root();
    }
    if (start.empty()) {
        start = std::filesystem::current_path();
    }
    auto current = start;
    while (!current.empty()) {
        if (iequals(current.filename().string(), "SRC")) {
            cached_repo_root_ = current.parent_path();
            break;
        }
        current = current.parent_path();
    }
    if (cached_repo_root_.empty()) {
        cached_repo_root_ = std::filesystem::current_path();
    }
    return cached_repo_root_;
}

std::filesystem::path ChildrenPanel::ensure_png_in_folder(const std::filesystem::path& folder, int frame_count) {
    std::error_code ec;
    if (frame_count > 0) {
        for (int i = 0; i < frame_count; ++i) {
            std::filesystem::path attempt = folder / (std::to_string(i) + ".png");
            if (std::filesystem::exists(attempt, ec)) {
                return attempt;
            }
        }
    }
    if (!std::filesystem::exists(folder, ec) || !std::filesystem::is_directory(folder, ec)) {
        return {};
    }
    std::filesystem::path best;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto& path = entry.path();
        if (!has_numeric_stem(path)) continue;
        if (!best.empty()) {
            try {
                if (std::stoi(path.stem().string()) < std::stoi(best.stem().string())) {
                    best = path;
                }
            } catch (...) {
                continue;
            }
        } else {
            best = path;
        }
    }
    return best;
}

}  // namespace animation_editor
