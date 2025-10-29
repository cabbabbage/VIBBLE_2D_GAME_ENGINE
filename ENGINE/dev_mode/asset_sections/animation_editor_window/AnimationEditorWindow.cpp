#include "AnimationEditorWindow.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <iostream>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "AnimationDocument.hpp"
#include "AnimationInspectorPanel.hpp"
#include "AnimationListContextMenu.hpp"
#include "AnimationListPanel.hpp"
#include "AsyncTaskQueue.hpp"
#include "AudioImporter.hpp"
#include "CroppingService.hpp"
#include "PreviewProvider.hpp"
#include "frame_editor/FrameEditor.hpp"
#include "string_utils.hpp"
#include "ui/tinyfiledialogs.h"
#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#  include <shobjidl.h>
#  include <shlwapi.h>
#endif
#include "utils/input.hpp"

#include "asset/asset_info.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dm_icons.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"

namespace {

using animation_editor::AnimationEditorWindow;

constexpr int kAutoSaveDelayFrames = 12;

void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer || text.empty()) return;

    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

std::vector<std::filesystem::path> split_paths(const std::string& raw) {
    std::vector<std::filesystem::path> paths;
    size_t start = 0;
    while (start < raw.size()) {
        size_t pos = raw.find('|', start);
        std::string token = raw.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        token = animation_editor::strings::trim_copy(token);
        if (!token.empty()) {
            paths.emplace_back(token);
        }
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return paths;
}

std::string default_audio_subdir() { return "audio"; }

}

namespace animation_editor {

AnimationEditorWindow::AnimationEditorWindow() {
    document_ = std::make_shared<AnimationDocument>();
    preview_provider_ = std::make_shared<PreviewProvider>();
    preview_provider_->set_document(document_);
    cropping_service_ = std::make_shared<CroppingService>();
    task_queue_ = std::make_shared<AsyncTaskQueue>();
    audio_importer_ = std::make_shared<AudioImporter>();
    list_panel_ = std::make_unique<AnimationListPanel>();
    list_panel_->set_document(document_);
    list_panel_->set_preview_provider(preview_provider_);
    configure_list_panel();
    inspector_panel_ = std::make_unique<AnimationInspectorPanel>();
    inspector_panel_->set_document(document_);
    inspector_panel_->set_preview_provider(preview_provider_);
    configure_inspector_panel();
    list_context_menu_ = std::make_unique<AnimationListContextMenu>();

    header_corner_button_ =
        std::make_unique<DMButton>(std::string(DMIcons::Close()), &DMStyles::DeleteButton(), DMButton::height(), DMButton::height());
    add_button_ = std::make_unique<DMButton>("Add Animation", &DMStyles::CreateButton(), 160, DMButton::height());
    layout_dirty_ = true;
    update_corner_button();
}

AnimationEditorWindow::~AnimationEditorWindow() = default;

void AnimationEditorWindow::set_visible(bool visible) {
    if (!visible && visible_) {
        if (document_ && document_->consume_dirty_flag()) {
            auto_save_pending_ = true;
            auto_save_timer_frames_ = 0;
        }
        auto_save_timer_frames_ = 0;
        process_auto_save();
        if (list_context_menu_) {
            list_context_menu_->close();
        }
    }
    visible_ = visible;
}

void AnimationEditorWindow::toggle_visible() { set_visible(!visible_); }

void AnimationEditorWindow::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
    layout_children();
}

void AnimationEditorWindow::set_info(const std::shared_ptr<AssetInfo>& info) {
    close_manifest_transaction();
    info_ = info;

    if (!document_) {
        document_ = std::make_shared<AnimationDocument>();
    }

    if (!info) {
        clear_info();
        return;
    }

    asset_root_path_.clear();
    try {
        std::filesystem::path candidate = info->asset_dir_path();
        if (!candidate.empty()) {
            asset_root_path_ = candidate;
        }
    } catch (...) {
        asset_root_path_.clear();
    }
    if (asset_root_path_.empty() && !info->name.empty()) {
        asset_root_path_ = std::filesystem::path("SRC") / info->name;
    }

    using_manifest_store_ = false;
    manifest_asset_key_.clear();
    manifest_transaction_ = {};

    if (!manifest_store_) {
        std::cerr << "[AnimationEditor] Manifest store unavailable; animations will not persist for '"
                  << info->name << "'\n";
        document_->load_from_manifest(nlohmann::json::object(), asset_root_path_, {});
    } else if (auto key = resolve_manifest_key(*info)) {
        manifest_asset_key_ = *key;
        manifest_transaction_ = manifest_store_->begin_asset_transaction(manifest_asset_key_, true);
        if (manifest_transaction_) {
            using_manifest_store_ = true;
            nlohmann::json snapshot = manifest_transaction_.data();
            document_->load_from_manifest(snapshot,
                                          asset_root_path_,
                                          [this](const nlohmann::json& payload) {
                                              this->persist_manifest_payload(payload);
                                          });
        } else {
            std::cerr << "[AnimationEditor] Failed to open manifest transaction for '"
                      << manifest_asset_key_ << "'\n";
            manifest_asset_key_.clear();
            document_->load_from_manifest(nlohmann::json::object(), asset_root_path_, {});
        }
    } else {
        std::cerr << "[AnimationEditor] Unable to resolve manifest key for '" << info->name << "'\n";
        document_->load_from_manifest(nlohmann::json::object(), asset_root_path_, {});
    }

    document_->consume_dirty_flag();
    preview_provider_->set_document(document_);
    configure_list_panel();
    configure_inspector_panel();
    if (list_panel_) list_panel_->set_preview_provider(preview_provider_);
    if (inspector_panel_) inspector_panel_->set_preview_provider(preview_provider_);
    if (audio_importer_) {
        std::filesystem::path audio_root = asset_root_path_.empty() ? std::filesystem::path{}
                                                                   : asset_root_path_ / default_audio_subdir();
        audio_importer_->set_asset_root(audio_root);
    }
    if (list_panel_) list_panel_->set_document(document_);
    if (inspector_panel_) inspector_panel_->set_document(document_);
    ensure_selection_valid();
    std::string asset_label = info->name.empty() ? std::string("asset") : info->name;
    set_status_message("Loaded " + asset_label, 240);
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
}

void AnimationEditorWindow::clear_info() {
    info_.reset();
    asset_root_path_.clear();
    close_manifest_transaction();
    frame_editor_visible_ = false;
    frame_editor_animation_id_.clear();
    update_corner_button();
    document_->load_from_manifest(nlohmann::json::object(), std::filesystem::path{}, {});
    document_->consume_dirty_flag();
    preview_provider_->invalidate_all();
    if (list_panel_) list_panel_->set_preview_provider(preview_provider_);
    if (list_panel_) list_panel_->set_document(document_);
    if (inspector_panel_) inspector_panel_->set_preview_provider(preview_provider_);
    if (inspector_panel_) inspector_panel_->set_document(document_);
    configure_list_panel();
    configure_inspector_panel();
    select_animation(std::nullopt, false);
    set_status_message("Select an asset to configure animations.", 240);
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
}

void AnimationEditorWindow::layout_children() {
    layout_dirty_ = false;
    const int padding = DMSpacing::panel_padding();
    const int header_gap = DMSpacing::small_gap();
    const int button_gap = DMSpacing::small_gap();
    const int header_height = DMButton::height() + header_gap * 2;
    header_rect_ = SDL_Rect{bounds_.x, bounds_.y, bounds_.w, header_height};

    int y = header_rect_.y + header_gap;
    int left_x = header_rect_.x + padding;

    if (header_corner_button_) {
        int width = header_corner_button_->rect().w;
        header_corner_button_->set_rect(SDL_Rect{left_x, y, width, DMButton::height()});
        left_x += width + button_gap;
    }
    if (add_button_) {
        add_button_->set_rect(SDL_Rect{left_x, y, add_button_->rect().w, DMButton::height()});
    }

    const int status_padding = DMSpacing::panel_padding();
    int status_height = DMStyles::Label().font_size + status_padding * 2;
    status_rect_ = SDL_Rect{bounds_.x, bounds_.y + bounds_.h - status_height, bounds_.w, status_height};

    int list_y = header_rect_.y + header_rect_.h + header_gap;
    int list_height = std::max(0, status_rect_.y - list_y - header_gap);
    int available_width = std::max(0, bounds_.w - padding * 2);
    int sidebar_width = std::min(320, available_width);
    int inspector_gap = DMSpacing::panel_padding();
    if (available_width < sidebar_width + inspector_gap) {
        inspector_gap = DMSpacing::small_gap();
    }
    list_rect_ = SDL_Rect{bounds_.x + padding, list_y, sidebar_width, list_height};
    int inspector_x = list_rect_.x + list_rect_.w + inspector_gap;
    int inspector_w = std::max(0, bounds_.x + bounds_.w - padding - inspector_x);
    inspector_rect_ = SDL_Rect{inspector_x, list_y, inspector_w, list_height};
    if (list_panel_) list_panel_->set_bounds(list_rect_);
    if (inspector_panel_) inspector_panel_->set_bounds(inspector_rect_);

    frame_editor_rect_ = SDL_Rect{bounds_.x + padding, bounds_.y + padding,
                                  std::max(0, bounds_.w - padding * 2), std::max(0, bounds_.h - padding * 2)};

    if (frame_editor_visible_) {

        const int modal_outer_margin = DMSpacing::panel_padding();
        const int modal_max_w = std::min(bounds_.w - modal_outer_margin * 2, 1200);
        const int modal_max_h = std::min(bounds_.h - modal_outer_margin * 2, 800);
        const int modal_w = std::max(640, modal_max_w);
        const int modal_h = std::max(480, modal_max_h);
        frame_editor_modal_rect_ = SDL_Rect{
            bounds_.x + (bounds_.w - modal_w) / 2, bounds_.y + (bounds_.h - modal_h) / 2, modal_w, modal_h};

        const int modal_header_gap = DMSpacing::small_gap();
        const int modal_header_h = DMButton::height() + modal_header_gap * 2;
        frame_editor_modal_header_rect_ = SDL_Rect{frame_editor_modal_rect_.x,
                                                   frame_editor_modal_rect_.y,
                                                   frame_editor_modal_rect_.w,
                                                   modal_header_h};

        const int modal_inner_pad = DMSpacing::panel_padding();
        const int content_x = frame_editor_modal_rect_.x + modal_inner_pad;
        const int content_y = frame_editor_modal_header_rect_.y + frame_editor_modal_header_rect_.h + modal_inner_pad;
        const int content_w = std::max(0, frame_editor_modal_rect_.w - modal_inner_pad * 2);
        const int content_h = std::max(0, frame_editor_modal_rect_.y + frame_editor_modal_rect_.h - content_y - modal_inner_pad);
        frame_editor_rect_ = SDL_Rect{content_x, content_y, content_w, content_h};
    } else {

        frame_editor_modal_rect_ = SDL_Rect{0, 0, 0, 0};
        frame_editor_modal_header_rect_ = SDL_Rect{0, 0, 0, 0};
    }

    if (frame_editor_) frame_editor_->set_bounds(frame_editor_rect_);
}

void AnimationEditorWindow::configure_list_panel() {
    if (!list_panel_) return;
    list_panel_->set_document(document_);
    list_panel_->set_preview_provider(preview_provider_);
    list_panel_->set_on_selection_changed([this](const std::optional<std::string>& animation_id) {
        this->select_animation(animation_id, true);
    });
    list_panel_->set_on_context_menu([this](const std::string& animation_id, const SDL_Point& location) {
        this->handle_list_context_menu(animation_id, location);
    });
    list_panel_->set_selected_animation_id(selected_animation_id_);
}

void AnimationEditorWindow::configure_inspector_panel() {
    if (!inspector_panel_) return;
    inspector_panel_->set_document(document_);
    inspector_panel_->set_preview_provider(preview_provider_);
    inspector_panel_->set_source_services(cropping_service_, task_queue_);
    inspector_panel_->set_source_folder_picker([this]() { return this->pick_folder(); });
    inspector_panel_->set_source_animation_picker([this]() { return this->pick_animation_reference(); });
    inspector_panel_->set_source_gif_picker([this]() { return this->pick_gif(); });
    inspector_panel_->set_source_png_sequence_picker([this]() { return this->pick_png_sequence(); });
    inspector_panel_->set_source_status_callback([this](const std::string& message) { this->set_status_message(message); });
    inspector_panel_->set_frame_edit_callback([this](const std::string& id) { this->open_frame_editor(id); });
    inspector_panel_->set_navigate_to_animation_callback([this](const std::string& id) {
        this->select_animation(std::optional<std::string>{id}, true);
    });
    inspector_panel_->set_audio_importer(audio_importer_);
    inspector_panel_->set_audio_file_picker([this]() { return this->pick_audio_file(); });
    if (selected_animation_id_) {
        inspector_panel_->set_animation_id(*selected_animation_id_);
    }
}

void AnimationEditorWindow::select_animation(const std::optional<std::string>& animation_id, bool from_user) {
    if (selected_animation_id_ == animation_id) {
        if (list_panel_) {
            list_panel_->set_selected_animation_id(selected_animation_id_);
        }
        return;
    }

    selected_animation_id_ = animation_id;
    if (list_panel_) {
        list_panel_->set_selected_animation_id(selected_animation_id_);
    }
    if (inspector_panel_ && selected_animation_id_) {
        inspector_panel_->set_animation_id(*selected_animation_id_);
    }

    if (from_user) {
        if (selected_animation_id_) {
            set_status_message("Selected animation '" + *selected_animation_id_ + "'.", 150);
        } else {
            set_status_message("No animation selected.", 120);
        }
    }
}

void AnimationEditorWindow::ensure_selection_valid() {
    if (!document_) {
        if (selected_animation_id_) {
            select_animation(std::nullopt, false);
        }
        return;
    }

    auto ids = document_->animation_ids();
    if (ids.empty()) {
        select_animation(std::nullopt, false);
        return;
    }

    if (selected_animation_id_) {
        if (std::find(ids.begin(), ids.end(), *selected_animation_id_) != ids.end()) {
            if (list_panel_) {
                list_panel_->set_selected_animation_id(selected_animation_id_);
            }
            return;
        }
    }

    std::optional<std::string> candidate;
    if (auto start = document_->start_animation()) {
        if (std::find(ids.begin(), ids.end(), *start) != ids.end()) {
            candidate = *start;
        }
    }
    if (!candidate) {
        candidate = ids.front();
    }
    select_animation(candidate, false);
}

void AnimationEditorWindow::handle_list_context_menu(const std::string& animation_id, const SDL_Point& location) {
    if (!document_) {
        return;
    }
    if (!list_context_menu_) {
        list_context_menu_ = std::make_unique<AnimationListContextMenu>();
    }

    select_animation(std::make_optional(animation_id), false);
    std::vector<AnimationListContextMenu::Option> options;
    options.push_back(AnimationListContextMenu::Option{
        "Rename...",
        [this, animation_id]() { this->prompt_rename_animation(animation_id); },
    });
    options.push_back(AnimationListContextMenu::Option{
        "Set as start",
        [this, animation_id]() { this->set_animation_as_start(animation_id); },
    });
    options.push_back(AnimationListContextMenu::Option{
        "Duplicate",
        [this, animation_id]() { this->duplicate_animation(animation_id); },
    });
    options.push_back(AnimationListContextMenu::Option{
        "Delete",
        [this, animation_id]() { this->delete_animation_with_confirmation(animation_id); },
    });

    list_context_menu_->open(bounds_, location, std::move(options));
    set_status_message("Context menu for '" + animation_id + "'.", 90);
}

void AnimationEditorWindow::update(const Input& input, int, int) {
    if (!visible_) return;

    auto& mutable_input = const_cast<Input&>(input);
    mutable_input.consumeAllMouseButtons();
    mutable_input.consumeMotion();
    mutable_input.consumeScroll();

    ensure_layout();

    if (task_queue_) task_queue_->update();
    if (list_panel_) list_panel_->update();
    ensure_selection_valid();
    if (inspector_panel_) {
        inspector_panel_->set_bounds(inspector_rect_);
        if (selected_animation_id_) {
            inspector_panel_->update();
        }
    }
    if (frame_editor_visible_ && frame_editor_) {
        frame_editor_->set_bounds(frame_editor_rect_);
        frame_editor_->update();
    }

    if (document_ && document_->consume_dirty_flag()) {
        auto_save_pending_ = true;
        auto_save_timer_frames_ = kAutoSaveDelayFrames;
    }

    process_auto_save();

    if (status_timer_frames_ > 0) {
        --status_timer_frames_;
        if (status_timer_frames_ == 0) {
            status_message_.clear();
        }
    }
}

void AnimationEditorWindow::render(SDL_Renderer* renderer) const {
    if (!visible_ || !renderer) return;

    ensure_layout();

    render_background(renderer);
    render_header(renderer);
    if (list_panel_) list_panel_->render(renderer);
    render_inspector(renderer);
    render_status(renderer);
    if (list_context_menu_ && list_context_menu_->is_open()) {
        list_context_menu_->render(renderer);
    }
    if (frame_editor_visible_) {
        render_frame_editor_overlay(renderer);
    }

    DMDropdown::render_active_options(renderer);
}

bool AnimationEditorWindow::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    ensure_layout();

    // If any dropdown is currently active (expanded), give it global priority
    // so option clicks outside local widgets are captured reliably.
    if (auto* active_dd = DMDropdown::active_dropdown()) {
        if (active_dd->handle_event(e)) {
            return true;
        }
    }

    // First, route to modal frame editor if visible
    if (frame_editor_visible_ && frame_editor_) {

        if (frame_editor_->handle_event(e)) {
            return true;
        }
    }

    // Then, give any open context menu first chance
    if (list_context_menu_ && list_context_menu_->is_open()) {
        if (list_context_menu_->handle_event(e)) {
            return true;
        }

        if (e.type == SDL_MOUSEBUTTONDOWN) {
            SDL_Point p{e.button.x, e.button.y};
            SDL_Rect menu_bounds = list_context_menu_->bounds();
            if (!SDL_PointInRect(&p, &menu_bounds)) {
                list_context_menu_->close();
            }
        }

        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            list_context_menu_->close();
            return true;
        }
    }

    // Route events to the inspector before header/list so dropdown overlays get priority
    if (inspector_panel_ && selected_animation_id_ && inspector_panel_->handle_event(e)) {
        return true;
    }

    // Header (close / add) comes after inspector so overlays can capture clicks
    if (handle_header_event(e)) {
        return true;
    }

    if (list_panel_ && list_panel_->handle_event(e)) {
        return true;
    }

    if (e.type == SDL_KEYDOWN) {
        if (frame_editor_visible_ && e.key.keysym.sym == SDLK_ESCAPE) {
            close_frame_editor();
            return true;
        }
        if (!frame_editor_visible_ && e.key.keysym.sym == SDLK_ESCAPE) {
            visible_ = false;
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) { p.x = e.motion.x; p.y = e.motion.y; }
        else { p.x = e.button.x; p.y = e.button.y; }
        if (frame_editor_visible_) {

            if (!SDL_PointInRect(&p, &frame_editor_modal_rect_)) {
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    close_frame_editor();
                }
                return true;
            }
            return true;
        } else if (SDL_PointInRect(&p, &bounds_)) {
            return true;
        }
    }

    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};
        if (frame_editor_visible_) {

            return true;
        }
        if (SDL_PointInRect(&p, &bounds_)) {
            return true;
        }
        return true;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        if (list_context_menu_) {
            list_context_menu_->close();
        }
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        return true;
    }

    return false;
}

void AnimationEditorWindow::prompt_rename_animation(const std::string& animation_id) {
    if (!document_) return;

    const char* input = tinyfd_inputBox("Rename Animation", "Enter new animation identifier", animation_id.c_str());
    if (!input) {
        set_status_message("Rename cancelled.", 120);
        return;
    }

    std::string desired = animation_editor::strings::trim_copy(input);
    if (desired.empty()) {
        set_status_message("Animation name cannot be empty.", 180);
        return;
    }

    auto before_ids = document_->animation_ids();
    document_->rename_animation(animation_id, desired);
    auto after_ids = document_->animation_ids();

    std::string new_id = animation_id;
    for (const auto& id : after_ids) {
        if (std::find(before_ids.begin(), before_ids.end(), id) == before_ids.end()) {
            new_id = id;
            break;
        }
    }

    preview_provider_->invalidate(animation_id);
    if (new_id != animation_id) {
        preview_provider_->invalidate(new_id);
    }

    select_animation(std::make_optional(new_id), false);
    set_status_message("Renamed animation to '" + new_id + "'.", 240);
    if (list_context_menu_) {
        list_context_menu_->close();
    }
}

void AnimationEditorWindow::set_animation_as_start(const std::string& animation_id) {
    if (!document_) return;
    document_->set_start_animation(animation_id);
    set_status_message("Set '" + animation_id + "' as start animation.", 180);
    if (list_context_menu_) {
        list_context_menu_->close();
    }
}

void AnimationEditorWindow::duplicate_animation(const std::string& animation_id) {
    if (!document_) return;

    auto before_ids = document_->animation_ids();
    document_->create_animation(animation_id);
    auto after_ids = document_->animation_ids();

    std::optional<std::string> created_id;
    for (const auto& id : after_ids) {
        if (std::find(before_ids.begin(), before_ids.end(), id) == before_ids.end()) {
            created_id = id;
            break;
        }
    }

    if (created_id) {
        if (auto payload = document_->animation_payload(animation_id)) {
            document_->replace_animation_payload(*created_id, *payload);
            preview_provider_->invalidate(*created_id);
        }
        select_animation(created_id, false);
        set_status_message("Duplicated animation to '" + *created_id + "'.", 240);
    } else {
        set_status_message("Failed to duplicate animation.", 180);
    }

    if (list_context_menu_) {
        list_context_menu_->close();
    }
}

void AnimationEditorWindow::delete_animation_with_confirmation(const std::string& animation_id) {
    if (!document_) return;

    std::string message = "Delete animation '" + animation_id + "'? This cannot be undone.";
    int result = tinyfd_messageBox("Delete Animation", message.c_str(), "yesno", "warning", 0);
    if (result != 1) {
        set_status_message("Deletion cancelled.", 120);
        if (list_context_menu_) {
            list_context_menu_->close();
        }
        return;
    }

    document_->delete_animation(animation_id);
    preview_provider_->invalidate(animation_id);
    set_status_message("Deleted animation '" + animation_id + "'.", 240);
    if (list_context_menu_) {
        list_context_menu_->close();
    }
    ensure_selection_valid();
}

void AnimationEditorWindow::set_on_document_saved(std::function<void()> callback) {
    on_document_saved_ = std::move(callback);
}

void AnimationEditorWindow::ensure_layout() const {
    if (layout_dirty_) {
        const_cast<AnimationEditorWindow*>(this)->layout_children();
    }
}

void AnimationEditorWindow::render_background(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
}

void AnimationEditorWindow::render_header(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, header_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelHeader(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    std::string title = "Animation Editor";
    if (auto info_ptr = info_.lock()) {
        std::string name = info_ptr->name;
        if (name.empty()) {
            name = asset_root_path_.filename().string();
        }
        if (!name.empty()) {
            title += " â€” ";
            title += name;
        }
    } else if (!asset_root_path_.empty()) {
        title += " â€” ";
        title += asset_root_path_.filename().string();
    }

    if (!frame_editor_visible_ && header_corner_button_) header_corner_button_->render(renderer);
    if (add_button_) add_button_->render(renderer);

    int label_x = header_rect_.x + DMSpacing::panel_padding();
    if (!frame_editor_visible_ && header_corner_button_) {
        label_x = std::max(label_x, header_corner_button_->rect().x + header_corner_button_->rect().w + DMSpacing::small_gap());
    }
    if (add_button_) {
        label_x = std::max(label_x, add_button_->rect().x + add_button_->rect().w + DMSpacing::small_gap());
    }
    render_label(renderer, title, label_x, header_rect_.y + DMSpacing::small_gap());
}

void AnimationEditorWindow::render_status(SDL_Renderer* renderer) const {
    if (status_message_.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, status_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    render_label(renderer, status_message_, status_rect_.x + DMSpacing::panel_padding(), status_rect_.y + DMSpacing::panel_padding());
}

void AnimationEditorWindow::render_inspector(SDL_Renderer* renderer) const {
    if (!renderer) return;
    if (inspector_rect_.w <= 0 || inspector_rect_.h <= 0) {
        return;
    }

    if (inspector_panel_ && selected_animation_id_) {
        inspector_panel_->render(renderer);
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect(renderer, inspector_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    std::string message = "Select an animation to edit.";
    int text_x = inspector_rect_.x + DMSpacing::panel_padding();
    int text_y = inspector_rect_.y + DMSpacing::panel_padding();
    render_label(renderer, message, text_x, text_y);
}

void AnimationEditorWindow::render_frame_editor_overlay(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(renderer, 10, 10, 14, 192);
    SDL_RenderFillRect(renderer, &bounds_);

    ensure_layout();

    const SDL_Color bg = DMStyles::PanelBG();
    const SDL_Color hi = DMStyles::HighlightColor();
    const SDL_Color sh = DMStyles::ShadowColor();
    const SDL_Color border = DMStyles::Border();
    dm_draw::DrawBeveledRect( renderer, frame_editor_modal_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), bg, hi, sh, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
    dm_draw::DrawRoundedOutline(renderer, frame_editor_modal_rect_, DMStyles::CornerRadius(), 1, border);

    dm_draw::DrawBeveledRect( renderer, frame_editor_modal_header_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelHeader(), hi, sh, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    if (header_corner_button_) {
        const int pad = DMSpacing::panel_padding();
        const int y = frame_editor_modal_header_rect_.y + DMSpacing::small_gap();
        const int w = header_corner_button_->rect().w;
        header_corner_button_->set_style(&DMStyles::HeaderButton());
        header_corner_button_->set_rect(SDL_Rect{frame_editor_modal_header_rect_.x + pad, y, w, DMButton::height()});
        header_corner_button_->render(renderer);
    }

    std::string title = "Frame Editor";
    if (!frame_editor_animation_id_.empty()) {
        title += " â€” ";
        title += frame_editor_animation_id_;
    }
    int label_x = frame_editor_modal_header_rect_.x + DMSpacing::panel_padding();
    if (header_corner_button_) {
        label_x = std::max(label_x, header_corner_button_->rect().x + header_corner_button_->rect().w + DMSpacing::small_gap());
    }
    render_label(renderer, title, label_x, frame_editor_modal_header_rect_.y + DMSpacing::small_gap());

    if (frame_editor_) {
        frame_editor_->set_bounds(frame_editor_rect_);
        frame_editor_->render(renderer);
    }
}

bool AnimationEditorWindow::handle_header_event(const SDL_Event& e) {
    bool consumed = false;
    auto handle_button = [&](const std::unique_ptr<DMButton>& button, auto&& callback) {
        if (button && button->handle_event(e)) {
            callback();
            consumed = true;
        }
};

    handle_button(header_corner_button_, [this]() {
        if (frame_editor_visible_) {
            close_frame_editor();
        } else {
            visible_ = false;
        }
    });
    if (!frame_editor_visible_) {
        handle_button(add_button_, [this]() { create_animation_via_prompt(); });
    }
    return consumed;
}

void AnimationEditorWindow::set_status_message(const std::string& message, int frames) {
    status_message_ = message;
    status_timer_frames_ = std::max(frames, 0);
}

void AnimationEditorWindow::open_frame_editor(const std::string& animation_id) {
    if (!frame_editor_) {
        frame_editor_ = std::make_unique<FrameEditor>();
        frame_editor_->set_close_callback([this]() { this->close_frame_editor(); });
    }
    frame_editor_animation_id_ = animation_id;
    frame_editor_->set_preview_provider(preview_provider_);
    frame_editor_->set_document(document_);
    frame_editor_->set_animation_id(animation_id);
    frame_editor_->set_bounds(frame_editor_rect_);
    frame_editor_visible_ = true;
    update_corner_button();
}

void AnimationEditorWindow::close_frame_editor() {
    frame_editor_visible_ = false;
    frame_editor_animation_id_.clear();
    set_status_message("Movement updated.", 180);
    update_corner_button();
}

void AnimationEditorWindow::update_corner_button() {
    if (!header_corner_button_) {
        return;
    }
    if (frame_editor_visible_) {
        header_corner_button_->set_text(u8"\u2190");
        header_corner_button_->set_style(&DMStyles::HeaderButton());
    } else {
        header_corner_button_->set_text(std::string(DMIcons::Close()));
        header_corner_button_->set_style(&DMStyles::DeleteButton());
    }
}

void AnimationEditorWindow::create_animation_via_prompt() {
    const char* input = tinyfd_inputBox("Create Animation", "Enter new animation identifier", "animation");
    if (!input) return;
    std::string name = animation_editor::strings::trim_copy(input);
    // If no name was provided (e.g., spurious callback), do not create a default.
    if (name.empty()) {
        return;
    }
    document_->create_animation(name);
    preview_provider_->invalidate_all();
    select_animation(std::make_optional(name), false);
    set_status_message("Created animation '" + name + "'.", 240);
}

void AnimationEditorWindow::reload_document() {
    auto info_ptr = info_.lock();
    if (!info_ptr || !manifest_store_) {
        close_manifest_transaction();
        document_->load_from_manifest(nlohmann::json::object(), asset_root_path_, {});
        using_manifest_store_ = false;
    } else {
        close_manifest_transaction();
        if (auto key = resolve_manifest_key(*info_ptr)) {
            manifest_asset_key_ = *key;
            manifest_transaction_ = manifest_store_->begin_asset_transaction(manifest_asset_key_, true);
            if (manifest_transaction_) {
                using_manifest_store_ = true;
                nlohmann::json snapshot = manifest_transaction_.data();
                document_->load_from_manifest(snapshot,
                                              asset_root_path_,
                                              [this](const nlohmann::json& payload) {
                                                  this->persist_manifest_payload(payload);
                                              });
            } else {
                std::cerr << "[AnimationEditor] Failed to reopen manifest transaction for '"
                          << manifest_asset_key_ << "'\n";
                manifest_asset_key_.clear();
                document_->load_from_manifest(nlohmann::json::object(), asset_root_path_, {});
                using_manifest_store_ = false;
            }
        } else {
            std::cerr << "[AnimationEditor] Unable to resolve manifest key during reload\n";
            document_->load_from_manifest(nlohmann::json::object(), asset_root_path_, {});
            using_manifest_store_ = false;
        }
    }

    document_->consume_dirty_flag();
    preview_provider_->invalidate_all();
    if (list_panel_) list_panel_->set_document(document_);
    if (inspector_panel_) inspector_panel_->set_document(document_);
    configure_list_panel();
    configure_inspector_panel();
    ensure_selection_valid();
    set_status_message("Reloaded animations.", 240);
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
}

void AnimationEditorWindow::process_auto_save() {
    if (!auto_save_pending_ || !document_) {
        return;
    }

    if (auto_save_timer_frames_ > 0) {
        --auto_save_timer_frames_;
        return;
    }

    document_->save_to_file();
    if (using_manifest_store_) {
        set_status_message("Animations auto-saved.", 180);
    }
    if (on_document_saved_) {
        on_document_saved_();
    }
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
}

void AnimationEditorWindow::set_manifest_store(devmode::core::ManifestStore* store) {
    if (manifest_store_ == store) {
        return;
    }
    close_manifest_transaction();
    manifest_store_ = store;
    if (auto info_ptr = info_.lock()) {
        set_info(info_ptr);
    }
}

void AnimationEditorWindow::close_manifest_transaction() {
    if (manifest_transaction_) {
        manifest_transaction_.cancel();
        manifest_transaction_ = {};
    }
    manifest_asset_key_.clear();
    using_manifest_store_ = false;
}

bool AnimationEditorWindow::persist_manifest_payload(const nlohmann::json& payload, bool finalize) {
    if (!manifest_store_ || manifest_asset_key_.empty()) {
        return false;
    }
    if (!manifest_transaction_) {
        manifest_transaction_ = manifest_store_->begin_asset_transaction(manifest_asset_key_, true);
        if (!manifest_transaction_) {
            return false;
        }
        using_manifest_store_ = true;
    }

    manifest_transaction_.data() = payload;
    bool committed = finalize ? manifest_transaction_.finalize() : manifest_transaction_.save();
    if (committed) {
        manifest_store_->flush();
    }
    return committed;
}

std::optional<std::string> AnimationEditorWindow::resolve_manifest_key(const AssetInfo& info) const {
    if (!manifest_store_) {
        return std::nullopt;
    }

    std::vector<std::string> candidates;
    if (!info.name.empty()) {
        candidates.push_back(info.name);
    }
    try {
        std::filesystem::path dir = info.asset_dir_path();
        if (!dir.empty()) {
            candidates.push_back(dir.filename().string());
            candidates.push_back(dir.lexically_normal().generic_string());
        }
    } catch (...) {
    }

    auto to_lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
};

    std::unordered_set<std::string> seen;
    for (const auto& candidate : candidates) {
        if (candidate.empty()) continue;
        if (!seen.insert(candidate).second) continue;
        if (auto resolved = manifest_store_->resolve_asset_name(candidate)) {
            return resolved;
        }
    }

    std::string desired_dir;
    try {
        std::filesystem::path dir = info.asset_dir_path();
        if (!dir.empty()) {
            desired_dir = dir.lexically_normal().generic_string();
        }
    } catch (...) {
        desired_dir.clear();
    }

    std::string desired_name_lower = to_lower(info.name);
    for (const auto& view : manifest_store_->assets()) {
        if (!view || !view.data || !view.data->is_object()) {
            continue;
        }
        const auto& asset_json = *view.data;
        auto dir_it = asset_json.find("asset_directory");
        if (dir_it != asset_json.end() && dir_it->is_string()) {
            try {
                std::filesystem::path dir = dir_it->get<std::string>();
                if (!desired_dir.empty() && dir.lexically_normal().generic_string() == desired_dir) {
                    return view.name;
                }
            } catch (...) {
            }
        }
        if (!desired_name_lower.empty()) {
            std::string manifest_name = asset_json.value("asset_name", view.name);
            if (!manifest_name.empty() && to_lower(manifest_name) == desired_name_lower) {
                return view.name;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> AnimationEditorWindow::pick_folder() const {
    std::string default_path = asset_root_path_.empty() ? std::string{} : asset_root_path_.string();
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::optional<std::filesystem::path> picked;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD options = 0;
        if (SUCCEEDED(pfd->GetOptions(&options))) {
            options |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM;
            pfd->SetOptions(options);
        }
        pfd->SetTitle(L"Upload Folder");
        if (!default_path.empty()) {
            IShellItem* psi = nullptr;
            std::wstring wpath(default_path.begin(), default_path.end());
            if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&psi)))) {
                pfd->SetFolder(psi);
                psi->Release();
            }
        }
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    picked = std::filesystem::path(std::wstring(psz));
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return picked;
#else
    const char* result = tinyfd_selectFolderDialog("Select Animation Folder", default_path.empty() ? nullptr : default_path.c_str());
    if (!result || std::string(result).empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(result);
#endif
}

std::optional<std::filesystem::path> AnimationEditorWindow::pick_gif() const {
    std::string default_path = asset_root_path_.empty() ? std::string{} : asset_root_path_.string();
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::optional<std::filesystem::path> picked;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD options = 0;
        if (SUCCEEDED(pfd->GetOptions(&options))) {
            options |= FOS_FORCEFILESYSTEM;
            pfd->SetOptions(options);
        }
        pfd->SetTitle(L"Upload GIF");
        COMDLG_FILTERSPEC filters[] = { {L"GIF Image", L"*.gif"} };
        pfd->SetFileTypes(1, filters);
        pfd->SetDefaultExtension(L"gif");
        if (!default_path.empty()) {
            IShellItem* psi = nullptr;
            std::wstring wpath(default_path.begin(), default_path.end());
            if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&psi)))) {
                pfd->SetFolder(psi);
                psi->Release();
            }
        }
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    picked = std::filesystem::path(std::wstring(psz));
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return picked;
#else
    const char* filters[] = {"*.gif"};
    const char* result = tinyfd_openFileDialog("Import GIF", default_path.c_str(), 1, filters, "GIF Image", 0);
    if (!result || std::string(result).empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(result);
#endif
}

std::vector<std::filesystem::path> AnimationEditorWindow::pick_png_sequence() const {
    std::string default_path = asset_root_path_.empty() ? std::string{} : asset_root_path_.string();
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::vector<std::filesystem::path> picked;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD options = 0;
        if (SUCCEEDED(pfd->GetOptions(&options))) {
            options |= FOS_FORCEFILESYSTEM; // single selection by default
            pfd->SetOptions(options);
        }
        pfd->SetTitle(L"Upload PNG");
        COMDLG_FILTERSPEC filters[] = { {L"PNG Images", L"*.png"} };
        pfd->SetFileTypes(1, filters);
        pfd->SetDefaultExtension(L"png");
        if (!default_path.empty()) {
            IShellItem* psi = nullptr;
            std::wstring wpath(default_path.begin(), default_path.end());
            if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&psi)))) {
                pfd->SetFolder(psi);
                psi->Release();
            }
        }
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    picked.emplace_back(std::wstring(psz));
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return picked;
#else
    const char* filters[] = {"*.png"};
    const char* result = tinyfd_openFileDialog("Upload PNG", default_path.c_str(), 1, filters, "PNG Images", 0);
    if (!result || std::string(result).empty()) {
        return {};
    }
    return split_paths(result);
#endif
}

std::optional<std::string> AnimationEditorWindow::pick_animation_reference() const {
    if (!document_) return std::nullopt;
    auto ids = document_->animation_ids();
    if (ids.empty()) return std::nullopt;

    std::ostringstream oss;
    oss << "Available animations:\n";
    for (const auto& id : ids) {
        oss << " - " << id << "\n";
    }

    const char* result = tinyfd_inputBox("Select Animation", oss.str().c_str(), ids.front().c_str());
    if (!result) return std::nullopt;
    std::string choice = animation_editor::strings::trim_copy(result);
    if (choice.empty()) return std::nullopt;
    if (std::find(ids.begin(), ids.end(), choice) == ids.end()) return std::nullopt;
    return choice;
}

std::optional<std::filesystem::path> AnimationEditorWindow::pick_audio_file() const {
    std::string default_path;
    if (!asset_root_path_.empty()) {
        default_path = (asset_root_path_ / default_audio_subdir()).string();
    }
#ifdef _WIN32
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileDialog* pfd = nullptr;
    std::optional<std::filesystem::path> picked;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD options = 0;
        if (SUCCEEDED(pfd->GetOptions(&options))) {
            options |= FOS_FORCEFILESYSTEM;
            pfd->SetOptions(options);
        }
        pfd->SetTitle(L"Select Audio Clip");
        COMDLG_FILTERSPEC filters[] = {
            {L"Audio Files", L"*.wav;*.ogg;*.mp3"},
            {L"WAV", L"*.wav"},
            {L"OGG", L"*.ogg"},
            {L"MP3", L"*.mp3"}
        };
        pfd->SetFileTypes(4, filters);
        pfd->SetDefaultExtension(L"wav");
        if (!default_path.empty()) {
            IShellItem* psi = nullptr;
            std::wstring wpath(default_path.begin(), default_path.end());
            if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr, IID_PPV_ARGS(&psi)))) {
                pfd->SetFolder(psi);
                psi->Release();
            }
        }
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(pfd->GetResult(&item))) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    picked = std::filesystem::path(std::wstring(psz));
                    CoTaskMemFree(psz);
                }
                item->Release();
            }
        }
        pfd->Release();
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return picked;
#else
    const char* filters[] = {"*.wav", "*.ogg", "*.mp3"};
    const char* result = tinyfd_openFileDialog("Select Audio Clip", default_path.c_str(), 3, filters, "Audio Files", 0);
    if (!result || std::string(result).empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(result);
#endif
}

}

