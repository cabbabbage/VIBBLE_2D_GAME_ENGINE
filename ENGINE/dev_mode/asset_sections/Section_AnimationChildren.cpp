#include "Section_AnimationChildren.hpp"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_log.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "animation_editor_window/AnimationDocument.hpp"
#include "animation_editor_window/PanelLayoutConstants.hpp"
#include "animation_editor_window/string_utils.hpp"
#include "asset/asset_info.hpp"
#include "asset_info_ui.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "search_assets.hpp"
#include "utils/input.hpp"

namespace {

constexpr int kRowSpacing = 6;
constexpr int kDefaultSearchHeight = 260;
constexpr SDL_Color kRowBackground{32, 38, 44, 255};
constexpr SDL_Color kRowBorder{64, 72, 80, 255};
constexpr SDL_Color kPlaceholderBg{58, 66, 74, 255};
constexpr SDL_Color kPlaceholderText{180, 190, 200, 255};

using animation_editor::strings::has_numeric_stem;

void render_text(SDL_Renderer* renderer,
                 const DMLabelStyle& style,
                 const std::string& text,
                 int x,
                 int y,
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

class AnimationChildrenPanel {
  public:
    using StatusCallback = std::function<void(const std::string&)>;

    AnimationChildrenPanel();
    ~AnimationChildrenPanel();

    void set_document(std::shared_ptr<AnimationDocument> document);
    void set_info(std::shared_ptr<AssetInfo> info);
    void set_manifest_store(devmode::core::ManifestStore* store);
    void set_animation_id(const std::string& animation_id);
    void set_bounds(const SDL_Rect& bounds);
    void set_status_callback(StatusCallback callback);
    void set_on_children_changed(std::function<void()> callback);
    void set_layout_dirty_callback(std::function<void()> callback);

    void update();
    void render(SDL_Renderer* renderer) const;
    void render_overlays(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);

    int preferred_height(int width) const;

    bool allow_out_of_bounds_pointer_events() const;

  private:
    void refresh_from_document();
    std::vector<std::string> current_children() const;
    std::vector<std::string> read_local_children(const nlohmann::json& payload) const;
    std::vector<std::string> resolve_inherited_children(const nlohmann::json& payload, int depth = 0) const;
    void commit_children();
    void refresh_local_children_from_source();
    void add_child_entry(const std::string& entry);
    void remove_child_entry(size_t index);
    void ensure_search_panel();
    void open_search_panel();
    void close_search_panel();
    void position_search_panel() const;
    void update_layout() const;
    void request_layout() const;
    void request_status(const std::string& message) const;
    bool point_inside(const SDL_Rect& rect, int x, int y) const;

    struct PreviewEntry {
        SDL_Renderer* renderer = nullptr;
        std::shared_ptr<SDL_Texture> texture;
        std::filesystem::path frame_path;
        std::filesystem::file_time_type last_write_time{};
    };

    SDL_Texture* acquire_child_icon(SDL_Renderer* renderer, const std::string& child_id) const;
    std::optional<std::filesystem::path> resolve_frame_path(const nlohmann::json& animation_json,
                                                            const std::string& asset_name,
                                                            const std::string& animation_key) const;
    std::filesystem::path resolve_candidate_path(const std::filesystem::path& candidate,
                                                 const std::string& asset_name) const;
    std::filesystem::path detect_repo_root() const;
    static std::filesystem::path ensure_png_in_folder(const std::filesystem::path& folder, int frame_count);

  private:
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<AssetInfo> info_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    SDL_Rect bounds_{0, 0, 0, 0};
    StatusCallback status_callback_;
    std::function<void()> layout_dirty_callback_;

    std::vector<std::string> local_children_;
    std::vector<std::string> inherited_children_;
    std::vector<std::string> display_children_;
    std::vector<std::string> inherited_message_lines_;
    std::string inherited_source_id_;
    bool inherits_children_ = false;

    std::unique_ptr<DMButton> add_button_;

    mutable bool layout_dirty_ = true;
    mutable SDL_Rect header_rect_{0, 0, 0, 0};
    mutable SDL_Rect message_rect_{0, 0, 0, 0};
    mutable SDL_Rect list_rect_{0, 0, 0, 0};
    mutable SDL_Rect add_button_rect_{0, 0, 0, 0};
    mutable std::vector<SDL_Rect> row_rects_;
    mutable std::vector<SDL_Rect> remove_rects_;
    mutable std::vector<SDL_Rect> icon_rects_;

    std::string payload_signature_;
    mutable std::filesystem::path cached_repo_root_;

    mutable std::unordered_map<std::string, PreviewEntry> preview_cache_;

    std::unique_ptr<SearchAssets> search_assets_;
    mutable SDL_Rect search_anchor_rect_{0, 0, 0, 0};
    mutable int search_panel_height_ = 0;

    std::function<void()> children_changed_callback_{};

    int row_height_ = 44;
    int icon_size_ = 36;
};

AnimationChildrenPanel::AnimationChildrenPanel() {
    add_button_ = std::make_unique<DMButton>("Add Child", &DMStyles::CreateButton(), 140, DMButton::height());
}

AnimationChildrenPanel::~AnimationChildrenPanel() = default;

void AnimationChildrenPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    cached_repo_root_.clear();
    payload_signature_.clear();
    refresh_from_document();
}

void AnimationChildrenPanel::set_info(std::shared_ptr<AssetInfo> info) {
    info_ = std::move(info);
    cached_repo_root_.clear();
    payload_signature_.clear();
    refresh_from_document();
}

void AnimationChildrenPanel::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
    if (search_assets_) {
        search_assets_->set_manifest_store(store);
    }
    preview_cache_.clear();
}

void AnimationChildrenPanel::set_animation_id(const std::string& /*animation_id*/) {
    payload_signature_.clear();
    refresh_from_document();
}

void AnimationChildrenPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
    position_search_panel();
}

void AnimationChildrenPanel::set_status_callback(StatusCallback callback) { status_callback_ = std::move(callback); }

void AnimationChildrenPanel::set_on_children_changed(std::function<void()> callback) {
    children_changed_callback_ = std::move(callback);
}

void AnimationChildrenPanel::set_layout_dirty_callback(std::function<void()> callback) {
    layout_dirty_callback_ = std::move(callback);
}

void AnimationChildrenPanel::update() {
    std::string signature;
    if (info_) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& name : info_->animation_children) {
            arr.push_back(name);
        }
        signature = arr.dump();
    }
    if (signature != payload_signature_) {
        payload_signature_ = signature;
        refresh_from_document();
    }

    if (search_assets_ && search_assets_->visible()) {
        Input dummy;
        search_assets_->update(dummy);
        position_search_panel();
    }
}

bool AnimationChildrenPanel::handle_event(const SDL_Event& e) {
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

int AnimationChildrenPanel::preferred_height(int width) const {
    (void)width;
    const int padding = kPanelPadding;
    int height = padding * 2;

    height += DMButton::height();

    const bool search_visible = search_assets_ && search_assets_->visible();
    if (search_visible) {
        const int overlay_height = search_panel_height_ > 0 ? search_panel_height_ : kDefaultSearchHeight;
        height += DMSpacing::small_gap();
        height += overlay_height;
    }

    if (inherits_children_ && !inherited_message_lines_.empty()) {
        height += DMSpacing::small_gap();
        height += static_cast<int>(inherited_message_lines_.size()) * label_height();
    } else if (display_children_.empty()) {
        height += DMSpacing::small_gap();
        height += label_height();
    }

    if (!display_children_.empty()) {
        height += DMSpacing::small_gap();
        height += static_cast<int>(display_children_.size()) * row_height_;
        if (display_children_.size() > 1) {
            height += (static_cast<int>(display_children_.size()) - 1) * kRowSpacing;
        }
    }

    return height;
}

bool AnimationChildrenPanel::allow_out_of_bounds_pointer_events() const {
    return search_assets_ && search_assets_->visible();
}

void AnimationChildrenPanel::render(SDL_Renderer* renderer) const {
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
}

void AnimationChildrenPanel::render_overlays(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }
    if (!search_assets_ || !search_assets_->visible()) {
        return;
    }

    SDL_bool had_clip = SDL_RenderIsClipEnabled(renderer);
    SDL_Rect previous_clip{0, 0, 0, 0};
    if (had_clip) {
        SDL_RenderGetClipRect(renderer, &previous_clip);
    }
    SDL_RenderSetClipRect(renderer, nullptr);
    search_assets_->render(renderer);
    if (had_clip) {
        SDL_RenderSetClipRect(renderer, &previous_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
}

void AnimationChildrenPanel::update_layout() const {
    if (!layout_dirty_) {
        return;
    }
    auto* self = const_cast<AnimationChildrenPanel*>(this);
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
    } else {
        self->add_button_rect_ = SDL_Rect{0, 0, 0, 0};
    }

    const bool search_visible = search_assets_ && search_assets_->visible();
    int overlay_height = 0;
    int overlay_width = 0;
    if (search_visible) {
        overlay_height = search_panel_height_ > 0 ? search_panel_height_ : kDefaultSearchHeight;
        overlay_width = width;
        if (add_button_rect_.w > 0) {
            overlay_width = std::max(add_button_rect_.w * 2, std::min(width, 320));
        } else if (width > 0) {
            overlay_width = std::max(width / 2, std::min(width, 320));
        }
        if (overlay_width <= 0) {
            overlay_width = width;
        }
        if (overlay_width > width) {
            overlay_width = width;
        }
        int overlay_x = x;
        if (add_button_rect_.w > 0 && overlay_width > 0) {
            overlay_x = std::clamp(add_button_rect_.x + add_button_rect_.w - overlay_width,
                                   x,
                                   x + width - overlay_width);
        }
        y += DMSpacing::small_gap();
        self->search_anchor_rect_ = SDL_Rect{overlay_x, y, overlay_width, overlay_height};
        y += overlay_height;
    } else {
        self->search_anchor_rect_ = SDL_Rect{x, y + DMSpacing::small_gap(), width, 0};
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

void AnimationChildrenPanel::refresh_from_document() {
    local_children_.clear();
    inherited_children_.clear();
    display_children_.clear();
    inherited_message_lines_.clear();
    inherited_source_id_.clear();
    inherits_children_ = false;

    if (!info_ && !document_) {
        request_layout();
        return;
    }
    local_children_ = current_children();
    display_children_ = local_children_;
    request_layout();
}

std::vector<std::string> AnimationChildrenPanel::current_children() const {
    if (document_) {
        return document_->animation_children();
    }
    if (info_) {
        return info_->animation_children;
    }
    return {};
}

std::vector<std::string> AnimationChildrenPanel::read_local_children(const nlohmann::json& payload) const {
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

std::vector<std::string> AnimationChildrenPanel::resolve_inherited_children(const nlohmann::json& payload, int depth) const {
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

void AnimationChildrenPanel::commit_children() {
    if (!info_) {
        return;
    }

    bool committed = false;
    try {
        if (document_) {
            document_->replace_animation_children(local_children_);
            document_->save_to_file(true);
            local_children_ = document_->animation_children();
        }

        info_->set_animation_children(local_children_);
        committed = info_->commit_manifest();
    } catch (const std::exception& ex) {
        request_status(std::string("Error committing children: ") + ex.what());
        committed = false;
    } catch (...) {
        request_status("Error committing children: unknown failure");
        committed = false;
    }
    if (!committed && manifest_store_) {
        std::string manifest_key;
        if (auto resolved = manifest_store_->resolve_asset_name(info_->name)) {
            manifest_key = *resolved;
        } else {
            std::string target = info_->name;
            std::string target_lower;
            target_lower.reserve(target.size());
            for (char c : target) target_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            for (const auto& view : manifest_store_->assets()) {
                if (!view || !view.data || !view.data->is_object()) continue;
                const auto& asset_json = *view.data;
                std::string asset_name = asset_json.value("asset_name", view.name);
                std::string candidate = asset_name.empty() ? view.name : asset_name;
                std::string cand_lower;
                cand_lower.reserve(candidate.size());
                for (char c : candidate) cand_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (cand_lower == target_lower) { manifest_key = view.name; break; }
                auto dir_it = asset_json.find("asset_directory");
                if (dir_it != asset_json.end() && dir_it->is_string()) {
                    try {
                        std::filesystem::path dir = dir_it->get<std::string>();
                        std::string folder = dir.filename().string();
                        std::string folder_lower;
                        folder_lower.reserve(folder.size());
                        for (char ch : folder) folder_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                        if (folder_lower == target_lower) { manifest_key = view.name; break; }
                    } catch (...) {
                    }
                }
            }
        }

        if (!manifest_key.empty()) {
            auto session = manifest_store_->begin_asset_edit(manifest_key, true);
            if (session) {
                try {
                    nlohmann::json payload = session.data();
                    payload["animation_children"] = nlohmann::json::array();
                    for (const auto& child : local_children_) {
                        payload["animation_children"].push_back(child);
                    }
                    session.data() = payload;
                    committed = session.commit();
                    manifest_store_->flush();
                } catch (const std::exception& ex) {
                    request_status(std::string("Error saving manifest: ") + ex.what());
                    committed = false;
                } catch (...) {
                    request_status("Error saving manifest: unknown failure");
                    committed = false;
                }
            }
        }
    }

    refresh_from_document();

    if (children_changed_callback_) {
        children_changed_callback_();
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& child : local_children_) {
        arr.push_back(child);
    }
    payload_signature_ = arr.dump();

    if (!committed) {
        request_status("Failed to commit animation children; changes may not persist.");
    }
}

void AnimationChildrenPanel::refresh_local_children_from_source() {
    local_children_ = current_children();
    if (!inherits_children_) {
        display_children_ = local_children_;
    }
}

void AnimationChildrenPanel::add_child_entry(const std::string& entry) {
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
    refresh_local_children_from_source();
    if (std::find(local_children_.begin(), local_children_.end(), value) != local_children_.end()) {
        request_status("Child '" + value + "' already in list.");
        return;
    }
    local_children_.push_back(value);
    // Persist immediately so the manifest reflects the new child before any rebuilds.
    if (info_) {
        info_->set_animation_children(local_children_);
        (void)info_->commit_manifest();
    }
    if (!inherits_children_) {
        display_children_ = local_children_;
    }
    commit_children();
    request_layout();
    request_status("Added child '" + value + "'.");
}

void AnimationChildrenPanel::remove_child_entry(size_t index) {
    refresh_local_children_from_source();
    if (index >= local_children_.size()) {
        return;
    }
    std::string removed = local_children_[index];
    local_children_.erase(local_children_.begin() + static_cast<long>(index));
    if (info_) {
        info_->set_animation_children(local_children_);
        (void)info_->commit_manifest();
    }
    if (!inherits_children_) {
        display_children_ = local_children_;
    }
    commit_children();
    request_layout();
    request_status("Removed child '" + removed + "'.");
}

void AnimationChildrenPanel::ensure_search_panel() {
    if (!search_assets_) {
        search_assets_ = std::make_unique<SearchAssets>(manifest_store_);
        search_assets_->set_embedded_mode(true);
        search_assets_->set_screen_dimensions(std::max(bounds_.w, 320), std::max(bounds_.h, 240));
    }
    if (manifest_store_) {
        search_assets_->set_manifest_store(manifest_store_);
    }
}

void AnimationChildrenPanel::open_search_panel() {
    ensure_search_panel();
    if (!search_assets_) {
        return;
    }
    position_search_panel();
    search_assets_->open([this](const std::string& selection) {
        add_child_entry(selection);
        close_search_panel();
    });
    if (search_panel_height_ <= 0) {
        search_panel_height_ = search_assets_->rect().h > 0 ? search_assets_->rect().h : kDefaultSearchHeight;
    }
    request_layout();
}

void AnimationChildrenPanel::close_search_panel() {
    if (search_assets_) {
        search_assets_->close();
    }
    search_panel_height_ = 0;
    request_layout();
}

void AnimationChildrenPanel::position_search_panel() const {
    if (!search_assets_) {
        return;
    }
    const int padding = kPanelPadding;
    constexpr int kMinSearchWidth = 280;
    constexpr int kMinSearchHeight = 240;
    constexpr int kMaxSearchHeight = 420;

    SDL_Rect target = search_anchor_rect_;
    if (target.w > 0 && target.h > 0) {
        int max_width = std::max(0, bounds_.w - padding * 2);
        if (max_width > 0) {
            target.w = std::min(target.w, max_width);
        }
        if (target.w < kMinSearchWidth) {
            target.w = max_width > 0 ? std::min(max_width, kMinSearchWidth) : kMinSearchWidth;
        }
        target.h = std::clamp(target.h, kMinSearchHeight, kMaxSearchHeight);

        int min_x = bounds_.x + padding;
        int max_x = bounds_.x + bounds_.w - padding - target.w;
        if (max_x < min_x) {
            max_x = min_x;
        }
        int min_y = bounds_.y + padding;
        int max_y = bounds_.y + bounds_.h - padding - target.h;
        if (max_y < min_y) {
            max_y = min_y;
        }

        target.x = std::clamp(target.x, min_x, max_x);
        target.y = std::clamp(target.y, min_y, max_y);

        search_assets_->set_embedded_rect(target);
        auto* self = const_cast<AnimationChildrenPanel*>(this);
        SDL_Rect applied = search_assets_->rect();
        self->search_panel_height_ = applied.h > 0 ? applied.h : target.h;
        return;
    }

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

    SDL_Rect fallback{x, y, width, height};
    search_assets_->set_embedded_rect(fallback);
    auto* self = const_cast<AnimationChildrenPanel*>(this);
    SDL_Rect applied = search_assets_->rect();
    self->search_panel_height_ = applied.h > 0 ? applied.h : fallback.h;
}

void AnimationChildrenPanel::request_layout() const {
    auto* self = const_cast<AnimationChildrenPanel*>(this);
    self->layout_dirty_ = true;
    if (layout_dirty_callback_) {
        layout_dirty_callback_();
    }
}

void AnimationChildrenPanel::request_status(const std::string& message) const {
    if (status_callback_) {
        status_callback_(message);
    }
}

bool AnimationChildrenPanel::point_inside(const SDL_Rect& rect, int x, int y) const {
    if (rect.w <= 0 || rect.h <= 0) return false;
    SDL_Point pt{x, y};
    return SDL_PointInRect(&pt, &rect);
}

SDL_Texture* AnimationChildrenPanel::acquire_child_icon(SDL_Renderer* renderer, const std::string& child_id) const {
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

std::optional<std::filesystem::path> AnimationChildrenPanel::resolve_frame_path(const nlohmann::json& animation_json,
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

std::filesystem::path AnimationChildrenPanel::resolve_candidate_path(const std::filesystem::path& candidate,
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

std::filesystem::path AnimationChildrenPanel::detect_repo_root() const {
    if (!cached_repo_root_.empty()) {
        return cached_repo_root_;
    }
    std::filesystem::path start;
    if (info_) {
        start = std::filesystem::path(info_->asset_dir_path());
    } else if (document_) {
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

std::filesystem::path AnimationChildrenPanel::ensure_png_in_folder(const std::filesystem::path& folder, int frame_count) {
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

namespace {

class AnimationChildrenPanelWidget : public Widget {
  public:
    explicit AnimationChildrenPanelWidget(animation_editor::AnimationChildrenPanel* panel) : panel_(panel) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        if (panel_) {
            panel_->set_bounds(rect_);
        }
    }

    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        return panel_ ? panel_->preferred_height(w) : 0;
    }

    bool handle_event(const SDL_Event& e) override {
        if (!panel_) return false;
        return panel_->handle_event(e);
    }

    void render(SDL_Renderer* renderer) const override {
        if (!panel_) return;
        panel_->set_bounds(rect_);
        panel_->render(renderer);
        panel_->render_overlays(renderer);
    }

    bool wants_full_row() const override { return true; }

  private:
    animation_editor::AnimationChildrenPanel* panel_ = nullptr;
    SDL_Rect rect_{0, 0, 0, 0};
};

}  // namespace

Section_AnimationChildren::Section_AnimationChildren() : DockableCollapsible("Animation Children", false) {
    set_visible_height(360);
}

Section_AnimationChildren::~Section_AnimationChildren() = default;

void Section_AnimationChildren::set_manifest_store(devmode::core::ManifestStore* store) {
    manifest_store_ = store;
    if (children_panel_) {
        children_panel_->set_manifest_store(manifest_store_);
    }
}

void Section_AnimationChildren::build() {
    widgets_.clear();
    children_widget_ = nullptr;
    DockableCollapsible::Rows rows;

    if (!info_) {
        auto placeholder = std::make_unique<ReadOnlyTextBoxWidget>(
            "",
            "No asset selected. Select an asset to manage animation children.");
        rows.push_back({placeholder.get()});
        widgets_.push_back(std::move(placeholder));
        set_rows(rows);
        return;
    }

    if (!children_panel_) {
        children_panel_ = std::make_unique<animation_editor::AnimationChildrenPanel>();
        children_panel_->set_layout_dirty_callback([this]() { this->rebuild_rows(); });
        children_panel_->set_on_children_changed([this]() {
            if (ui_) {
                ui_->sync_animation_children();
            }
        });
    }
    children_panel_->set_document(document_);
    children_panel_->set_info(info_);
    children_panel_->set_manifest_store(manifest_store_);
    children_panel_->set_status_callback([](const std::string& msg) { SDL_Log("%s", msg.c_str()); });

    auto widget = std::make_unique<AnimationChildrenPanelWidget>(children_panel_.get());
    children_widget_ = widget.get();
    widgets_.push_back(std::move(widget));
    rows.push_back({children_widget_});

    set_rows(rows);
}

void Section_AnimationChildren::update(const Input& input, int screen_w, int screen_h) {
    if (children_panel_) {
        children_panel_->update();
    }
    DockableCollapsible::update(input, screen_w, screen_h);
}

bool Section_AnimationChildren::handle_event(const SDL_Event& e) {
    if (children_panel_ && children_panel_->allow_out_of_bounds_pointer_events()) {
        return children_panel_->handle_event(e) || DockableCollapsible::handle_event(e);
    }
    return DockableCollapsible::handle_event(e);
}

void Section_AnimationChildren::rebuild_rows() {
    if (!children_widget_) {
        return;
    }
    DockableCollapsible::Rows rows;
    rows.push_back({children_widget_});
    set_rows(rows);
}
