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

#include <nlohmann/json_fwd.hpp>

class Asset;
class Assets;
class AssetInfo;
class Input;
class DMButton;

namespace animation_editor {

using ::Asset;
using ::Assets;
using ::AssetInfo;

class AnimationDocument;
class AnimationListPanel;
class AnimationInspectorPanel;
class PreviewProvider;
class AsyncTaskQueue;
class AudioImporter;
class AnimationListContextMenu;

using DMButton = ::DMButton;
using DMCheckbox = ::DMCheckbox;
using DMDropdown = ::DMDropdown;

class AnimationEditorWindow {
  public:
    AnimationEditorWindow();
    ~AnimationEditorWindow();

    void set_visible(bool visible, bool process_close = true);
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
    void set_on_animation_properties_changed(std::function<void(const std::string&, const nlohmann::json&)> callback);

    std::shared_ptr<AnimationDocument> document() const { return document_; }

    void set_assets(Assets* assets) { assets_ = assets; }
    void set_target_asset(Asset* asset) { target_asset_ = asset; }
    void on_live_frame_editor_closed(const std::string& animation_id);

  private:
    void handle_document_saved();
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
    bool handle_header_event(const SDL_Event& e);
    void set_status_message(const std::string& message, int frames = 300);
    void open_frame_editor(const std::string& animation_id);
    Asset* resolve_frame_editor_asset();
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
    void apply_speed_multiplier_from_dropdown();
    void apply_crop_frames_toggle();
    void sync_header_controls();
    float parse_speed_multiplier(const nlohmann::json& payload) const;
    bool parse_crop_frames(const nlohmann::json& payload) const;
    void persist_header_metadata(float speed_multiplier, bool crop_frames);
    std::vector<float> speed_multiplier_options() const;
    bool rebuild_animation_from_sources(const std::shared_ptr<AssetInfo>& info, const std::string& animation_id);
    bool rebuild_animation_via_pipeline(const std::shared_ptr<AssetInfo>& info, const std::string& animation_id);
    bool rebuild_all_animations_via_pipeline(const std::shared_ptr<AssetInfo>& info);

  private:
    bool visible_ = false;
    SDL_Rect bounds_{0, 0, 0, 0};
    std::weak_ptr<AssetInfo> info_;
    std::filesystem::path asset_root_path_;
    std::shared_ptr<AnimationDocument> document_;
    std::shared_ptr<PreviewProvider> preview_provider_;
    std::shared_ptr<AsyncTaskQueue> task_queue_;
    std::shared_ptr<AudioImporter> audio_importer_;
    std::unique_ptr<AnimationListPanel> list_panel_;
    std::unique_ptr<AnimationInspectorPanel> inspector_panel_;
    std::unique_ptr<AnimationListContextMenu> list_context_menu_;
    std::unique_ptr<DMButton> add_button_;
    std::unique_ptr<DMButton> build_button_;
    std::unique_ptr<DMButton> controller_button_;
    std::unique_ptr<DMDropdown> speed_dropdown_;
    std::unique_ptr<DMCheckbox> crop_checkbox_;
    SDL_Rect header_rect_{0, 0, 0, 0};
    SDL_Rect list_rect_{0, 0, 0, 0};
    SDL_Rect inspector_rect_{0, 0, 0, 0};
    SDL_Rect status_rect_{0, 0, 0, 0};
    std::string status_message_;
    int status_timer_frames_ = 0;
    bool live_frame_editor_session_active_ = false;
    std::optional<std::string> selected_animation_id_;
    mutable bool layout_dirty_ = true;
    bool auto_save_pending_ = false;
    int auto_save_timer_frames_ = 0;
    std::function<void()> on_document_saved_;
    std::function<void(const std::string&, const nlohmann::json&)> on_animation_properties_changed_;
    devmode::core::ManifestStore* manifest_store_ = nullptr;
    devmode::core::ManifestStore::AssetTransaction manifest_transaction_;
    std::string manifest_asset_key_;
    bool using_manifest_store_ = false;

    Assets* assets_ = nullptr;
    Asset* target_asset_ = nullptr;

};

}
