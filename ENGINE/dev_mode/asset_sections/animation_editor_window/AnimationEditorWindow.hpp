#pragma once

#include <SDL.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/widgets.hpp"
#include "frame_editor/FrameEditor.hpp"

#include <nlohmann/json_fwd.hpp>

class AssetInfo;
class Input;
class DMButton;

namespace animation_editor {

class AnimationDocument;
class AnimationListPanel;
class AnimationInspectorPanel;
class PreviewProvider;
class CroppingService;
class AsyncTaskQueue;
class AudioImporter;
class AnimationListContextMenu;

using DMButton = ::DMButton;

class AnimationEditorWindow {
  public:
    AnimationEditorWindow();
    ~AnimationEditorWindow();

    void set_visible(bool visible);
    bool is_visible() const { return visible_; }
    void toggle_visible();

    void set_bounds(const SDL_Rect& bounds);
    const SDL_Rect& bounds() const { return bounds_; }

    void set_info(const std::shared_ptr<AssetInfo>& info);
    void clear_info();

    void set_manifest_store(devmode::core::ManifestStore* store);

    void update(const Input& input, int screen_w, int screen_h);
    void render(SDL_Renderer* renderer) const;
    bool handle_event(const SDL_Event& e);
    void focus_animation(const std::string& animation_id);

    void set_on_document_saved(std::function<void()> callback);

    // Wiring to in-world frame editor session
    void set_assets(class Assets* assets) { assets_ = assets; }
    void set_target_asset(class Asset* asset) { target_asset_ = asset; }

  private:
    void layout_children();
    void ensure_layout() const;
    void configure_list_panel();
    void configure_inspector_panel();
    void select_animation(const std::optional<std::string>& animation_id, bool from_user);
    void ensure_selection_valid();
    void handle_list_context_menu(const std::string& animation_id, const SDL_Point& location);
    void prompt_rename_animation(const std::string& animation_id);
    void set_animation_as_start(const std::string& animation_id);
    void duplicate_animation(const std::string& animation_id);
    void delete_animation_with_confirmation(const std::string& animation_id);
    void render_background(SDL_Renderer* renderer) const;
    void render_header(SDL_Renderer* renderer) const;
    void render_status(SDL_Renderer* renderer) const;
    void render_inspector(SDL_Renderer* renderer) const;
    void render_frame_editor_overlay(SDL_Renderer* renderer) const;
    bool handle_header_event(const SDL_Event& e);
    void set_status_message(const std::string& message, int frames = 300);
    void open_frame_editor(const std::string& animation_id);
    void close_frame_editor();
    void update_corner_button();
    void create_animation_via_prompt();
    void reload_document();
    void process_auto_save();
    void close_manifest_transaction();
    bool persist_manifest_payload(const nlohmann::json& payload, bool finalize = false);
    std::optional<std::string> resolve_manifest_key(const AssetInfo& info) const;

    std::optional<std::filesystem::path> pick_folder() const;
    std::optional<std::filesystem::path> pick_gif() const;
    std::vector<std::filesystem::path> pick_png_sequence() const;
    std::optional<std::string> pick_animation_reference() const;
    std::optional<std::filesystem::path> pick_audio_file() const;

    void handle_controller_button_click();
    void update_controller_button_label();
    bool does_controller_exist() const;
    std::string sanitize_asset_name(const std::string& name) const;
    std::string generate_controller_key(const std::string& asset_name) const;
    std::string generate_class_name(const std::string& asset_name) const;
    void add_controller();
    void open_controller();

  private:
    bool visible_ = false;
    SDL_Rect bounds_{0, 0, 0, 0};
    std::weak_ptr<AssetInfo> info_;
    std::filesystem::path asset_root_path_;
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::shared_ptr<CroppingService> cropping_service_;
    std::shared_ptr<AsyncTaskQueue> task_queue_;
    std::shared_ptr<AudioImporter> audio_importer_;
    std::unique_ptr<AnimationListPanel> list_panel_;
    std::unique_ptr<AnimationInspectorPanel> inspector_panel_;
    std::unique_ptr<FrameEditor> frame_editor_;
    std::unique_ptr<AnimationListContextMenu> list_context_menu_;
    std::unique_ptr<DMButton> header_corner_button_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<DMButton> controller_button_;
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect list_rect_{0, 0, 0, 0};
    SDL_Rect inspector_rect_{0, 0, 0, 0};
    SDL_Rect status_rect_{0, 0, 0, 0};
    SDL_Rect frame_editor_rect_{0, 0, 0, 0};

    SDL_Rect frame_editor_modal_rect_{0, 0, 0, 0};
    SDL_Rect frame_editor_modal_header_rect_{0, 0, 0, 0};
    std::string status_message_;
    int status_timer_frames_ = 0;
    bool frame_editor_visible_ = false;
    std::string frame_editor_animation_id_;
    std::optional<std::string> selected_animation_id_;
    mutable bool layout_dirty_ = true;
    bool auto_save_pending_ = false;
    int auto_save_timer_frames_ = 0;
    std::function<void()> on_document_saved_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    devmode::core::ManifestStore::AssetTransaction manifest_transaction_;
    std::string manifest_asset_key_;
    bool using_manifest_store_ = false;

    // For in-world frame editor session
    class Assets* assets_ = nullptr;
    class Asset* target_asset_ = nullptr;
};

}
