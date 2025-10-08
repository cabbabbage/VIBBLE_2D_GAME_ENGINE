#include "AnimationEditorWindow.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "AnimationDocument.hpp"
#include "AnimationInspectorPanel.hpp"
#include "AnimationListPanel.hpp"
#include "AsyncTaskQueue.hpp"
#include "AudioImporter.hpp"
#include "CroppingService.hpp"
#include "PreviewProvider.hpp"
#include "frame_editor/FrameMovementEditor.hpp"
#include "ui/tinyfiledialogs.h"
#include "utils/input.hpp"

#include "asset/asset_info.hpp"
#include "dev_mode/dm_styles.hpp"
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

std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](unsigned char ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](unsigned char ch) { return !is_space(ch); }).base(), value.end());
    return value;
}

std::vector<std::filesystem::path> split_paths(const std::string& raw) {
    std::vector<std::filesystem::path> paths;
    size_t start = 0;
    while (start < raw.size()) {
        size_t pos = raw.find('|', start);
        std::string token = raw.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        token = trim_copy(token);
        if (!token.empty()) {
            paths.emplace_back(token);
        }
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return paths;
}

std::string default_audio_subdir() { return "audio"; }

}  // namespace

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

    add_button_ = std::make_unique<DMButton>("Add Animation", &DMStyles::CreateButton(), 160, DMButton::height());
    reload_button_ = std::make_unique<DMButton>("Reload", &DMStyles::AccentButton(), 120, DMButton::height());
    close_button_ = std::make_unique<DMButton>("Close", &DMStyles::DeleteButton(), 120, DMButton::height());
    layout_dirty_ = true;
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
    info_ = info;
    if (info) {
        info_path_ = info->info_json_path();
        document_->load_from_file(info_path_);
        document_->consume_dirty_flag();
        preview_provider_->set_document(document_);
        configure_list_panel();
        if (list_panel_) list_panel_->set_preview_provider(preview_provider_);
        if (audio_importer_) {
            std::filesystem::path audio_root = info_path_.empty() ? std::filesystem::path{}
                                                                  : info_path_.parent_path() / default_audio_subdir();
            audio_importer_->set_asset_root(audio_root);
        }
        if (list_panel_) list_panel_->set_document(document_);
        set_status_message("Loaded " + info_path_.filename().string(), 240);
        auto_save_pending_ = false;
        auto_save_timer_frames_ = 0;
    } else {
        clear_info();
    }
}

void AnimationEditorWindow::clear_info() {
    info_.reset();
    info_path_.clear();
    movement_editor_visible_ = false;
    movement_editor_animation_id_.clear();
    document_->load_from_file(std::filesystem::path{});
    document_->consume_dirty_flag();
    preview_provider_->invalidate_all();
    if (list_panel_) list_panel_->set_preview_provider(preview_provider_);
    if (list_panel_) list_panel_->set_document(document_);
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
    int right_x = header_rect_.x + header_rect_.w - padding;

    if (close_button_) {
        int width = close_button_->rect().w;
        right_x -= width;
        close_button_->set_rect(SDL_Rect{right_x, y, width, DMButton::height()});
        right_x -= button_gap;
    }
    if (reload_button_) {
        int width = reload_button_->rect().w;
        right_x -= width;
        reload_button_->set_rect(SDL_Rect{right_x, y, width, DMButton::height()});
        right_x -= button_gap;
    }
    if (add_button_) {
        add_button_->set_rect(SDL_Rect{left_x, y, add_button_->rect().w, DMButton::height()});
    }

    const int status_padding = DMSpacing::panel_padding();
    int status_height = DMStyles::Label().font_size + status_padding * 2;
    status_rect_ = SDL_Rect{bounds_.x, bounds_.y + bounds_.h - status_height, bounds_.w, status_height};

    int list_y = header_rect_.y + header_rect_.h + header_gap;
    int list_height = std::max(0, status_rect_.y - list_y - header_gap);
    list_rect_ = SDL_Rect{bounds_.x + padding, list_y, std::max(0, bounds_.w - padding * 2), list_height};
    if (list_panel_) list_panel_->set_bounds(list_rect_);

    movement_editor_rect_ = SDL_Rect{bounds_.x + padding, bounds_.y + padding,
                                     std::max(0, bounds_.w - padding * 2), std::max(0, bounds_.h - padding * 2)};
    if (movement_editor_) movement_editor_->set_bounds(movement_editor_rect_);
}

void AnimationEditorWindow::configure_list_panel() {
    if (!list_panel_) return;
    list_panel_->set_inspector_configurator([this](AnimationInspectorPanel& inspector) {
        inspector.set_preview_provider(preview_provider_);
        inspector.set_source_services(cropping_service_, task_queue_);
        inspector.set_source_folder_picker([this]() { return this->pick_folder(); });
        inspector.set_source_animation_picker([this]() { return this->pick_animation_reference(); });
        inspector.set_source_gif_picker([this]() { return this->pick_gif(); });
        inspector.set_source_png_sequence_picker([this]() { return this->pick_png_sequence(); });
        inspector.set_source_status_callback([this](const std::string& message) { this->set_status_message(message); });
        inspector.set_movement_edit_callback([this](const std::string& id) { this->open_movement_editor(id); });
        inspector.set_audio_importer(audio_importer_);
        inspector.set_audio_file_picker([this]() { return this->pick_audio_file(); });
    });
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
    if (movement_editor_visible_ && movement_editor_) {
        movement_editor_->set_bounds(movement_editor_rect_);
        movement_editor_->update();
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
    render_status(renderer);
    DMDropdown::render_active_options(renderer);
    if (movement_editor_visible_) {
        render_movement_overlay(renderer);
    }
}

bool AnimationEditorWindow::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    ensure_layout();

    if (movement_editor_visible_ && movement_editor_) {
        if (movement_editor_->handle_event(e)) {
            return true;
        }
    }

    if (handle_header_event(e)) {
        return true;
    }

    if (list_panel_ && list_panel_->handle_event(e)) {
        return true;
    }

    if (e.type == SDL_KEYDOWN) {
        if (movement_editor_visible_ && e.key.keysym.sym == SDLK_ESCAPE) {
            close_movement_editor();
            return true;
        }
        if (!movement_editor_visible_ && e.key.keysym.sym == SDLK_ESCAPE) {
            visible_ = false;
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) {
            p.x = e.motion.x;
            p.y = e.motion.y;
        } else {
            p.x = e.button.x;
            p.y = e.button.y;
        }
        if (SDL_PointInRect(&p, &bounds_)) {
            return true;
        }
    }

    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};
        if (SDL_PointInRect(&p, &bounds_)) {
            return true;
        }
        return true;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
        return true;
    }

    return false;
}

void AnimationEditorWindow::ensure_layout() const {
    if (layout_dirty_) {
        const_cast<AnimationEditorWindow*>(this)->layout_children();
    }
}

void AnimationEditorWindow::render_background(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bounds_);

    const SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bounds_);
}

void AnimationEditorWindow::render_header(SDL_Renderer* renderer) const {
    const SDL_Color header_bg = DMStyles::PanelHeader();
    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, header_bg.a);
    SDL_RenderFillRect(renderer, &header_rect_);

    std::string title = "Animation Editor";
    if (!info_path_.empty()) {
        title += " — ";
        title += info_path_.filename().string();
    }
    render_label(renderer, title, header_rect_.x + DMSpacing::panel_padding(),
                 header_rect_.y + DMSpacing::small_gap());

    if (add_button_) add_button_->render(renderer);
    if (reload_button_) reload_button_->render(renderer);
    if (close_button_) close_button_->render(renderer);
}

void AnimationEditorWindow::render_status(SDL_Renderer* renderer) const {
    if (status_message_.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &status_rect_);

    const SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &status_rect_);

    render_label(renderer, status_message_, status_rect_.x + DMSpacing::panel_padding(),
                 status_rect_.y + DMSpacing::panel_padding());
}

void AnimationEditorWindow::render_movement_overlay(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 10, 10, 14, 192);
    SDL_RenderFillRect(renderer, &bounds_);

    if (movement_editor_) {
        movement_editor_->render(renderer);
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

    handle_button(add_button_, [this]() { create_animation_via_prompt(); });
    handle_button(reload_button_, [this]() { reload_document(); });
    handle_button(close_button_, [this]() { visible_ = false; });
    return consumed;
}

void AnimationEditorWindow::set_status_message(const std::string& message, int frames) {
    status_message_ = message;
    status_timer_frames_ = std::max(frames, 0);
}

void AnimationEditorWindow::open_movement_editor(const std::string& animation_id) {
    if (!movement_editor_) {
        movement_editor_ = std::make_unique<FrameMovementEditor>();
        movement_editor_->set_close_callback([this]() { this->close_movement_editor(); });
    }
    movement_editor_animation_id_ = animation_id;
    movement_editor_->set_document(document_);
    movement_editor_->set_animation_id(animation_id);
    movement_editor_->set_bounds(movement_editor_rect_);
    movement_editor_visible_ = true;
}

void AnimationEditorWindow::close_movement_editor() {
    movement_editor_visible_ = false;
    movement_editor_animation_id_.clear();
    set_status_message("Movement updated.", 180);
}

void AnimationEditorWindow::create_animation_via_prompt() {
    const char* input = tinyfd_inputBox("Create Animation", "Enter new animation identifier", "animation");
    if (!input) return;
    std::string name = trim_copy(input);
    if (name.empty()) {
        name = "animation";
    }
    document_->create_animation(name);
    preview_provider_->invalidate_all();
    set_status_message("Created animation '" + name + "'.", 240);
}

void AnimationEditorWindow::reload_document() {
    if (info_path_.empty()) return;
    document_->load_from_file(info_path_);
    preview_provider_->invalidate_all();
    if (list_panel_) list_panel_->set_document(document_);
    set_status_message("Reloaded animations from disk.", 240);
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
    if (!info_path_.empty()) {
        set_status_message("Animations auto-saved.", 180);
    }
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
}

std::optional<std::filesystem::path> AnimationEditorWindow::pick_folder() const {
    std::string default_path = info_path_.empty() ? std::string{} : info_path_.parent_path().string();
    const char* result = tinyfd_selectFolderDialog("Select Animation Folder",
                                                  default_path.empty() ? nullptr : default_path.c_str());
    if (!result || std::string(result).empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(result);
}

std::optional<std::filesystem::path> AnimationEditorWindow::pick_gif() const {
    std::string default_path = info_path_.empty() ? std::string{} : info_path_.parent_path().string();
    const char* filters[] = {"*.gif"};
    const char* result = tinyfd_openFileDialog("Import GIF", default_path.c_str(), 1, filters, "GIF Image", 0);
    if (!result || std::string(result).empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(result);
}

std::vector<std::filesystem::path> AnimationEditorWindow::pick_png_sequence() const {
    std::string default_path = info_path_.empty() ? std::string{} : info_path_.parent_path().string();
    const char* filters[] = {"*.png"};
    const char* result = tinyfd_openFileDialog("Import PNG Sequence", default_path.c_str(), 1, filters,
                                              "PNG Images", 1);
    if (!result || std::string(result).empty()) {
        return {};
    }
    return split_paths(result);
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
    std::string choice = trim_copy(result);
    if (choice.empty()) return std::nullopt;
    if (std::find(ids.begin(), ids.end(), choice) == ids.end()) return std::nullopt;
    return choice;
}

std::optional<std::filesystem::path> AnimationEditorWindow::pick_audio_file() const {
    std::string default_path = info_path_.empty() ? std::string{} : info_path_.parent_path().string();
    const char* filters[] = {"*.wav", "*.ogg", "*.mp3"};
    const char* result = tinyfd_openFileDialog("Select Audio Clip", default_path.c_str(), 3, filters,
                                              "Audio Files", 0);
    if (!result || std::string(result).empty()) {
        return std::nullopt;
    }
    return std::filesystem::path(result);
}

}  // namespace animation_editor

