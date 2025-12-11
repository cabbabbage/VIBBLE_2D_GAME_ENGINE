#include "AnimationEditorWindow.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>
#include <array>
#include <limits>

#include <nlohmann/json.hpp>

#include "AnimationDocument.hpp"
#include "AnimationInspectorPanel.hpp"
#include "AnimationListContextMenu.hpp"
#include "AnimationListPanel.hpp"
#include "EditorUIPrimitives.hpp"
#include "AsyncTaskQueue.hpp"
#include "AudioImporter.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "PreviewProvider.hpp"
#include "string_utils.hpp"
#include "ui/tinyfiledialogs.h"
#include "utils/rebuild_queue.hpp"
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shobjidl.h>
#  include <shlwapi.h>
#endif
#include "utils/input.hpp"

#include "asset/asset_info.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/asset_paths.hpp"
#include "dev_mode/animation_runtime_refresh.hpp"

namespace {

using animation_editor::AnimationEditorWindow;
namespace fs = std::filesystem;
namespace asset_paths = devmode::asset_paths;

constexpr int kAutoSaveDelayFrames = 12;

fs::path preferred_asset_folder(const std::string& asset_name) {
    if (asset_name.empty()) {
        return asset_paths::assets_root_path();
    }
    return (asset_paths::assets_root_path() / asset_name).lexically_normal();
}

bool path_has_prefix(fs::path path, fs::path prefix) {
    path = path.lexically_normal();
    prefix = prefix.lexically_normal();
    if (prefix.empty()) {
        return false;
    }
    auto pit = prefix.begin();
    auto it = path.begin();
    for (; pit != prefix.end(); ++pit, ++it) {
        if (it == path.end() || *it != *pit) {
            return false;
        }
    }
    return true;
}

bool is_inside_assets_root(const fs::path& path) {
    return path_has_prefix(path, asset_paths::assets_root_path());
}

bool is_inside_src_root(const fs::path& path) {
    return path_has_prefix(path, fs::path("SRC"));
}

void copy_directory_contents(const fs::path& source, const fs::path& destination, const std::string& asset_name) {
    std::error_code ec;
    if (source.empty() || destination.empty()) {
        return;
    }
    if (!fs::exists(source, ec) || !fs::is_directory(source, ec)) {
        return;
    }
    fs::create_directories(destination, ec);
    if (ec) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AnimationEditor] Failed to prepare destination '%s' for '%s': %s", destination.generic_string().c_str(), asset_name.c_str(), ec.message().c_str());
        return;
    }
    for (fs::directory_iterator it(source, ec); !ec && it != fs::directory_iterator(); ++it) {
        const fs::path target = destination / it->path().filename();
        fs::copy(it->path(), target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AnimationEditor] Failed to copy '%s' to '%s' for '%s': %s", it->path().generic_string().c_str(), target.generic_string().c_str(), asset_name.c_str(), ec.message().c_str());
            ec.clear();
        }
    }
}

fs::path ensure_assets_storage(const fs::path& candidate, const AssetInfo& info) {
    const std::string asset_name = info.name;
    if (asset_name.empty()) {
        return candidate.lexically_normal();
    }

    const fs::path preferred = preferred_asset_folder(asset_name);
    fs::path normalized_candidate = candidate.lexically_normal();

    if (normalized_candidate.empty()) {
        normalized_candidate = preferred;
    }

    if (is_inside_assets_root(normalized_candidate)) {
        return normalized_candidate;
    }

    if (!normalized_candidate.empty() && !is_inside_src_root(normalized_candidate)) {
        return normalized_candidate;
    }

    std::error_code ec;
    const bool preferred_exists = fs::exists(preferred, ec);
    ec.clear();
    const bool candidate_exists = !normalized_candidate.empty() && fs::exists(normalized_candidate, ec);
    ec.clear();

    if (!preferred_exists && candidate_exists) {
        const fs::path source = normalized_candidate;
        if (!source.empty() && source != preferred) {
            copy_directory_contents(source, preferred, asset_name);
        }
    }

    fs::create_directories(preferred, ec);
    if (ec) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[AnimationEditor] Failed to create assets directory '%s' for '%s': %s", preferred.generic_string().c_str(), asset_name.c_str(), ec.message().c_str());
        return normalized_candidate.empty() ? preferred : normalized_candidate;
    }

    return preferred;
}

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

bool has_animation_entries(const nlohmann::json& asset_json) {
    if (!asset_json.is_object()) {
        return false;
    }
    auto animations_it = asset_json.find("animations");
    if (animations_it == asset_json.end() || !animations_it->is_object()) {
        return false;
    }
    if (animations_it->contains("animations") && (*animations_it)["animations"].is_object()) {
        return !(*animations_it)["animations"].empty();
    }
    return !animations_it->empty();
}

nlohmann::json build_folder_payload(const std::filesystem::path& folder) {
    try {
        if (folder.empty() || !std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
            return {};
        }
        int frame_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (ext == ".png" || ext == ".gif") {
                ++frame_count;
            }
        }
        if (frame_count == 0) {
            return {};
        }
        nlohmann::json payload = {
            {"loop", true},
            {"locked", false},
            {"reverse_source", false},
            {"flipped_source", false},
            {"rnd_start", false},
            {"source",
                {
                    {"kind", "folder"},
                    {"path", folder.generic_string()},

                    {"name", ""},
                }},
};
        payload["number_of_frames"] = frame_count;
        return payload;
    } catch (...) {
        return {};
    }
}

nlohmann::json snapshot_from_asset_folders(const AssetInfo& info, const std::filesystem::path& asset_root) {
    nlohmann::json snapshot = nlohmann::json::object();
    if (!info.name.empty()) {
        snapshot["asset_name"] = info.name;
    }
    if (!info.type.empty()) {
        snapshot["asset_type"] = info.type;
    }
    if (!asset_root.empty()) {
        snapshot["asset_directory"] = asset_root.generic_string();
    }

    nlohmann::json animations = nlohmann::json::object();
    try {
        if (!asset_root.empty() && std::filesystem::exists(asset_root) && std::filesystem::is_directory(asset_root)) {

            for (const auto& entry : std::filesystem::directory_iterator(asset_root)) {
                if (!entry.is_directory()) {
                    continue;
                }
                std::string anim_id = entry.path().filename().string();
                if (anim_id.empty()) {
                    continue;
                }
                nlohmann::json payload = build_folder_payload(entry.path());
                if (!payload.is_object() || payload.empty()) {
                    continue;
                }
                animations[anim_id] = std::move(payload);
            }

            nlohmann::json root_payload = build_folder_payload(asset_root);
            if (root_payload.is_object() && !root_payload.empty()) {

                std::string preferred_id = "default";
                if (animations.contains(preferred_id)) {

                    preferred_id = "root";
                    if (animations.contains(preferred_id)) {
                        preferred_id = info.name.empty() ? std::string{"main"} : info.name;
                        if (preferred_id.empty()) preferred_id = "main";
                    }
                }
                animations[preferred_id] = std::move(root_payload);
            }
        }
    } catch (...) {
        animations = nlohmann::json::object();
    }

    if (!animations.empty()) {
        snapshot["animations"] = std::move(animations);
        std::string start_id = info.start_animation;
        if (start_id.empty()) {

            if (snapshot["animations"].contains("default")) {
                start_id = "default";
            } else {
                const auto& anims = snapshot["animations"];
                auto it = anims.begin();
                if (it != anims.end()) {
                    start_id = it.key();
                }
            }
        }
        if (!start_id.empty()) {
            snapshot["start"] = start_id;
        }
    }

    return snapshot;
}

nlohmann::json snapshot_from_asset_info(const AssetInfo& info) {
    nlohmann::json snapshot = nlohmann::json::object();
    if (!info.name.empty()) {
        snapshot["asset_name"] = info.name;
    }
    if (!info.type.empty()) {
        snapshot["asset_type"] = info.type;
    }
    try {
        std::filesystem::path dir = info.asset_dir_path();
        if (!dir.empty()) {
            snapshot["asset_directory"] = dir.generic_string();
        }
    } catch (...) {
    }

    nlohmann::json animations = nlohmann::json::object();
    try {
        auto names = info.animation_names();
        for (const auto& anim_id : names) {
            nlohmann::json payload = info.animation_payload(anim_id);
            if (payload.is_object() && !payload.empty()) {
                animations[anim_id] = std::move(payload);
            }
        }
    } catch (...) {
        animations = nlohmann::json::object();
    }

    if (!animations.empty()) {
        snapshot["animations"] = std::move(animations);
        if (!info.start_animation.empty()) {
            snapshot["start"] = info.start_animation;
        }
    }

    return snapshot;
}

}

namespace animation_editor {

AnimationEditorWindow::AnimationEditorWindow() {
    document_ = std::make_shared<AnimationDocument>();
    document_->set_on_saved_callback([this]() { this->handle_document_saved(); });
    preview_provider_ = std::make_shared<PreviewProvider>();
    preview_provider_->set_document(document_);
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

    add_button_ = std::make_unique<DMButton>("Add Animation", &DMStyles::CreateButton(), 160, DMButton::height());
    build_button_ = std::make_unique<DMButton>("Build Now", &DMStyles::CreateButton(), 120, DMButton::height());
    controller_button_ = std::make_unique<DMButton>("Add Controller", &DMStyles::CreateButton(), 140, DMButton::height());
    const auto speeds = speed_multiplier_options();
    const std::vector<std::string> speed_labels = {"0.25x", "0.5x", "1.0x", "2.0x", "4.0x"};
    speed_dropdown_ = std::make_unique<DMDropdown>("Speed Multiplier", speed_labels, 2);
    crop_checkbox_ = std::make_unique<DMCheckbox>("Crop Frames", false);
    layout_dirty_ = true;
}

AnimationEditorWindow::~AnimationEditorWindow() {
    if (document_) {
        document_->set_on_saved_callback(nullptr);
    }
}

void AnimationEditorWindow::set_visible(bool visible, bool process_close) {
    if (!visible && visible_ && process_close) {

        if (document_ && document_->consume_dirty_flag()) {
            auto_save_pending_ = true;
            auto_save_timer_frames_ = 0;
        }
        auto_save_timer_frames_ = 0;
        process_auto_save();

        if (using_manifest_store_ && manifest_transaction_) {

            nlohmann::json dummy;
            persist_manifest_payload(dummy, true);
        }

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
        document_->set_on_saved_callback([this]() { this->handle_document_saved(); });
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
    asset_root_path_ = ensure_assets_storage(asset_root_path_, *info);

    process_auto_save();

    using_manifest_store_ = false;
    manifest_asset_key_.clear();
    manifest_transaction_ = {};

    enum class SnapshotRecoverySource { None, AssetMetadata, AssetFolders, Manifest };
    SnapshotRecoverySource recovery_source = SnapshotRecoverySource::None;

    auto build_folder_snapshot = [&]() -> nlohmann::json { return snapshot_from_asset_folders(*info, asset_root_path_); };
    auto build_info_snapshot   = [&]() -> nlohmann::json { return snapshot_from_asset_info(*info); };

    nlohmann::json snapshot = nlohmann::json::object();
    std::function<void(const nlohmann::json&)> persist_callback;
    bool seed_transaction_with_recovery = false;

    nlohmann::json info_snapshot = build_info_snapshot();

    if (manifest_store_) {
        if (auto key = resolve_manifest_key(*info)) {
            manifest_asset_key_ = *key;
            manifest_transaction_ = manifest_store_->begin_asset_transaction(manifest_asset_key_, true);
            if (manifest_transaction_) {
                using_manifest_store_ = true;
                persist_callback = [this](const nlohmann::json& payload) { this->persist_manifest_payload(payload); };
            } else {
                std::cerr << "[AnimationEditor] Failed to open manifest transaction for '" << manifest_asset_key_ << "'\n";
                manifest_asset_key_.clear();
            }
        } else {
            std::cerr << "[AnimationEditor] Unable to resolve manifest key for '" << info->name << "'\n";
        }
    } else {
        std::cerr << "[AnimationEditor] Manifest store unavailable; animations will not persist for '" << info->name << "'\n";
    }

    if (has_animation_entries(info_snapshot)) {
        snapshot = std::move(info_snapshot);
        recovery_source = SnapshotRecoverySource::AssetMetadata;
        seed_transaction_with_recovery = true;
        std::cerr << "[AnimationEditor] Using animations from AssetInfo for '" << info->name << "'\n";
    } else if (manifest_transaction_) {
        nlohmann::json manifest_data = manifest_transaction_.data();
        if (has_animation_entries(manifest_data)) {
            snapshot = std::move(manifest_data);
            recovery_source = SnapshotRecoverySource::Manifest;
            std::cerr << "[AnimationEditor] Loaded animations from manifest for '" << info->name << "'\n";
        }
    }

    if (!has_animation_entries(snapshot)) {
        nlohmann::json folder_snapshot = build_folder_snapshot();
        if (has_animation_entries(folder_snapshot)) {
            snapshot = std::move(folder_snapshot);
            recovery_source = SnapshotRecoverySource::AssetFolders;
            seed_transaction_with_recovery = true;
            std::cerr << "[AnimationEditor] Recovered animations by scanning folders for '" << info->name << "'\n";
        } else {
            snapshot = nlohmann::json::object();
            std::cerr << "[AnimationEditor] No animations found for '" << info->name << "' (manifest/metadata/folders)\n";
        }
    }

    auto apply_snapshot = [&](const nlohmann::json& payload, SnapshotRecoverySource source) {
        document_->load_from_manifest(payload, asset_root_path_, persist_callback);
        recovery_source = source;
        if (using_manifest_store_ && has_animation_entries(payload)) {
            persist_manifest_payload(payload);
        }
};

    const bool snapshot_was_empty = !has_animation_entries(snapshot);
    document_->load_from_manifest(snapshot, asset_root_path_, persist_callback);
    if (seed_transaction_with_recovery) {
        persist_manifest_payload(snapshot);
    }

    if (document_->animation_ids().empty()) {
        bool recovered = false;

        nlohmann::json metadata_snapshot2 = snapshot_from_asset_info(*info);
        if (has_animation_entries(metadata_snapshot2)) {
            apply_snapshot(metadata_snapshot2, SnapshotRecoverySource::AssetMetadata);
            recovered = true;
        } else {
            nlohmann::json folder_snapshot2 = snapshot_from_asset_folders(*info, asset_root_path_);
            if (has_animation_entries(folder_snapshot2)) {
                apply_snapshot(folder_snapshot2, SnapshotRecoverySource::AssetFolders);
                recovered = true;
            }
        }
        if (!recovered) {

            if (target_asset_ && target_asset_->info) {
                nlohmann::json runtime_snapshot = snapshot_from_asset_info(*target_asset_->info);
                if (has_animation_entries(runtime_snapshot)) {
                    apply_snapshot(runtime_snapshot, SnapshotRecoverySource::AssetMetadata);
                    recovered = true;
                    std::cerr << "[AnimationEditor] Fallback to runtime asset info for '" << info->name << "'\n";
                }
            }
            if (!recovered) {
                recovery_source = SnapshotRecoverySource::None;
            }
        }
    }

    const bool seeded_default = snapshot_was_empty && recovery_source == SnapshotRecoverySource::None &&
                                document_ && document_->animation_ids().size() == 1 && document_->animation_ids().front() == "default";

    if (seeded_default) {
        document_->save_to_file();
    } else if (document_) {
        document_->consume_dirty_flag();
    }
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
    update_controller_button_label();
    std::string asset_label = info->name.empty() ? std::string("asset") : info->name;
    const bool has_any_animations = !document_->animation_ids().empty();
    if (seeded_default) {
        set_status_message("Created default animation for " + asset_label + ".", 300);
    } else {
        switch (recovery_source) {
            case SnapshotRecoverySource::AssetMetadata:
                set_status_message("Recovered animations from asset metadata for " + asset_label + ".", 300);
                break;
            case SnapshotRecoverySource::AssetFolders:
                set_status_message("Recovered animations from asset folders for " + asset_label + ".", 300);
                break;
            default:
                if (has_any_animations) {
                    set_status_message("Loaded " + asset_label, 240);
                } else {
                    set_status_message("No animations found for " + asset_label + ".", 240);
                }
                break;
        }
    }
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
    sync_header_controls();
}

void AnimationEditorWindow::clear_info() {
    info_.reset();
    asset_root_path_.clear();
    close_manifest_transaction();
    live_frame_editor_session_active_ = false;
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
    sync_header_controls();
}

void AnimationEditorWindow::layout_children() {
    layout_dirty_ = false;
    const int padding = DMSpacing::panel_padding();
    const int header_gap = DMSpacing::small_gap();
    const int button_gap = DMSpacing::small_gap();
    const int header_control_height =
        std::max({DMButton::height(), DMDropdown::height(), DMCheckbox::height()});
    const int header_height = header_control_height + header_gap * 2;
    header_rect_ = SDL_Rect{bounds_.x, bounds_.y, bounds_.w, header_height};

    int y = header_rect_.y + header_gap;
    int left_x = header_rect_.x + padding;

    if (add_button_) {
        add_button_->set_rect(SDL_Rect{left_x, y, add_button_->rect().w, DMButton::height()});
        left_x += add_button_->rect().w + button_gap;
    }

    if (build_button_) {
        build_button_->set_rect(SDL_Rect{left_x, y, build_button_->rect().w, DMButton::height()});
        left_x += build_button_->rect().w + button_gap;
    }

    if (controller_button_) {
        controller_button_->set_rect(SDL_Rect{left_x, y, controller_button_->rect().w, DMButton::height()});
        left_x += controller_button_->rect().w + button_gap;
    }

    if (speed_dropdown_) {
        const int dropdown_width = 180;
        speed_dropdown_->set_rect(SDL_Rect{left_x, y, dropdown_width, DMDropdown::height()});
        left_x += dropdown_width + button_gap;
    }

    if (crop_checkbox_) {
        const int checkbox_width = 150;
        crop_checkbox_->set_rect(SDL_Rect{left_x, y, checkbox_width, DMCheckbox::height()});
    }

    const int status_padding = DMSpacing::panel_padding();
    int status_height = DMStyles::Label().font_size + status_padding * 2;
    status_rect_ = SDL_Rect{bounds_.x, bounds_.y + bounds_.h - status_height, bounds_.w, status_height};

    int content_top = header_rect_.y + header_rect_.h + header_gap;
    int content_bottom = status_rect_.y - header_gap;
    int content_height = std::max(0, content_bottom - content_top);
    int available_width = std::max(0, bounds_.w - padding * 2);
    bool stack_vertical = available_width < 640;

    if (stack_vertical) {
        int gap = DMSpacing::panel_padding();
        if (content_height < gap * 2) {
            gap = DMSpacing::small_gap();
        }
        int inspector_height = content_height / 2;
        int list_height = std::max(0, content_height - inspector_height - gap);
        inspector_height = std::max(0, content_height - list_height - gap);

        list_rect_ = SDL_Rect{bounds_.x + padding, content_top, available_width, list_height};
        inspector_rect_ = SDL_Rect{bounds_.x + padding,
                                   list_rect_.y + list_rect_.h + gap,
                                   available_width,
                                   inspector_height};
    } else {
        int sidebar_width = std::clamp(available_width / 3, 260, 420);
        int inspector_gap = DMSpacing::panel_padding();
        if (available_width < sidebar_width + inspector_gap + 320) {
            inspector_gap = DMSpacing::small_gap();
        }
        list_rect_ = SDL_Rect{bounds_.x + padding, content_top, sidebar_width, content_height};
        int inspector_x = list_rect_.x + list_rect_.w + inspector_gap;
        int inspector_w = std::max(0, bounds_.x + bounds_.w - padding - inspector_x);
        inspector_rect_ = SDL_Rect{inspector_x, content_top, inspector_w, content_height};
    }
    if (list_panel_) list_panel_->set_bounds(list_rect_);
    if (inspector_panel_) inspector_panel_->set_bounds(inspector_rect_);

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
    list_panel_->set_on_delete_animation([this](const std::string& animation_id) {
        this->delete_animation_with_confirmation(animation_id);
    });
    list_panel_->set_selected_animation_id(selected_animation_id_);
}

void AnimationEditorWindow::configure_inspector_panel() {
    if (!inspector_panel_) return;
    inspector_panel_->set_document(document_);
    inspector_panel_->set_preview_provider(preview_provider_);
    inspector_panel_->set_task_queue(task_queue_);
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
    inspector_panel_->set_manifest_store(manifest_store_);
    inspector_panel_->set_on_animation_properties_changed(on_animation_properties_changed_);
    if (selected_animation_id_) {
        inspector_panel_->set_animation_id(*selected_animation_id_);
    }
}

void AnimationEditorWindow::select_animation(const std::optional<std::string>& animation_id, bool from_user) {
    if (selected_animation_id_ == animation_id) {
        if (list_panel_) {
            list_panel_->set_selected_animation_id(selected_animation_id_);
        }
        if (inspector_panel_ && selected_animation_id_) {
            inspector_panel_->set_animation_id(*selected_animation_id_);
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

    sync_header_controls();

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

void AnimationEditorWindow::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;

    int mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    if (mouse_x >= bounds_.x && mouse_x < bounds_.x + bounds_.w &&
        mouse_y >= bounds_.y && mouse_y < bounds_.y + bounds_.h) {
        auto& mutable_input = const_cast<Input&>(input);
        mutable_input.consumeAllMouseButtons();
        mutable_input.consumeMotion();
        mutable_input.consumeScroll();
    }

    ensure_layout();

    if (task_queue_) task_queue_->update();
    if (list_panel_) list_panel_->update();
    ensure_selection_valid();
    if (inspector_panel_) {
        if (selected_animation_id_) {
            inspector_panel_->update();
        }
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

    DMDropdown::render_active_options(renderer);
}

bool AnimationEditorWindow::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    ensure_layout();

    if (auto* active_dd = DMDropdown::active_dropdown()) {
        if (active_dd->handle_event(e)) {

            if (inspector_panel_) {
                inspector_panel_->apply_dropdown_selections();
            }
            return true;
        }
    }

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

    if (inspector_panel_ && selected_animation_id_) {
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP ||
            e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEWHEEL) {
            int mx = 0, my = 0;
            if (e.type == SDL_MOUSEMOTION) { mx = e.motion.x; my = e.motion.y; }
            else if (e.type == SDL_MOUSEWHEEL) { SDL_GetMouseState(&mx, &my); }
            else { mx = e.button.x; my = e.button.y; }
            SDL_Point mp{mx, my};
            if (SDL_PointInRect(&mp, &inspector_rect_)) {

                (void)inspector_panel_->handle_event(e);
                return true;
            }
        }

        if (inspector_panel_->handle_event(e)) {
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
        if (e.key.keysym.sym == SDLK_ESCAPE) {
            set_visible(false);
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) { p.x = e.motion.x; p.y = e.motion.y; }
        else { p.x = e.button.x; p.y = e.button.y; }

        if (list_context_menu_ && e.type == SDL_MOUSEBUTTONDOWN) {
            list_context_menu_->close();
        }

        if (SDL_PointInRect(&p, &bounds_)) {
            return true;
        } else {

            return false;
        }
    }

    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};

        if (inspector_panel_ && selected_animation_id_ && SDL_PointInRect(&p, &inspector_rect_)) {
            return inspector_panel_->handle_event(e);
        }

        return false;
    }

    return false;
}

void AnimationEditorWindow::focus_animation(const std::string& animation_id) {
    if (animation_id.empty()) return;
    if (!document_) return;
    auto ids = document_->animation_ids();
    if (std::find(ids.begin(), ids.end(), animation_id) == ids.end()) {
        return;
    }
    select_animation(std::make_optional(animation_id), true);
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

void AnimationEditorWindow::set_on_animation_properties_changed(std::function<void(const std::string&, const nlohmann::json&)> callback) {
    on_animation_properties_changed_ = std::move(callback);
}

void AnimationEditorWindow::handle_document_saved() {
    if (on_document_saved_) {
        on_document_saved_();
    }
}

void AnimationEditorWindow::ensure_layout() const {
    if (layout_dirty_) {
        const_cast<AnimationEditorWindow*>(this)->layout_children();
    }
}

void AnimationEditorWindow::render_background(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    animation_editor::ui::draw_panel_background(renderer, bounds_);
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
            title += " - ";
            title += name;
        }
    } else if (!asset_root_path_.empty()) {
        title += " - ";
        title += asset_root_path_.filename().string();
    }

    if (add_button_) add_button_->render(renderer);
    if (build_button_) build_button_->render(renderer);
    if (controller_button_) controller_button_->render(renderer);
    if (speed_dropdown_) speed_dropdown_->render(renderer);
    if (crop_checkbox_) crop_checkbox_->render(renderer);

    int label_x = header_rect_.x + DMSpacing::panel_padding();
    if (add_button_) {
        label_x = std::max(label_x, add_button_->rect().x + add_button_->rect().w + DMSpacing::small_gap());
    }
    if (build_button_) {
        label_x = std::max(label_x, build_button_->rect().x + build_button_->rect().w + DMSpacing::small_gap());
    }
    if (controller_button_) {
        label_x = std::max(label_x, controller_button_->rect().x + controller_button_->rect().w + DMSpacing::small_gap());
    }
    if (speed_dropdown_) {
        label_x = std::max(label_x, speed_dropdown_->rect().x + speed_dropdown_->rect().w + DMSpacing::small_gap());
    }
    if (crop_checkbox_) {
        label_x = std::max(label_x, crop_checkbox_->rect().x + crop_checkbox_->rect().w + DMSpacing::small_gap());
    }
    render_label(renderer, title, label_x, header_rect_.y + DMSpacing::small_gap());
}

void AnimationEditorWindow::render_status(SDL_Renderer* renderer) const {
    if (status_message_.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    animation_editor::ui::draw_panel_background(renderer, status_rect_);

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
    animation_editor::ui::draw_panel_background(renderer, inspector_rect_);

    std::string message = "Select an animation to edit.";
    int text_x = inspector_rect_.x + DMSpacing::panel_padding();
    int text_y = inspector_rect_.y + DMSpacing::panel_padding();
    render_label(renderer, message, text_x, text_y);
}

bool AnimationEditorWindow::handle_header_event(const SDL_Event& e) {
    bool consumed = false;
    auto handle_button = [&](const std::unique_ptr<DMButton>& button, auto&& callback) {
        if (!button) return;
        bool activated = button->handle_event(e);
        if (!activated) return;

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            callback();
        }
        consumed = true;
};

    handle_button(add_button_, [this]() { create_animation_via_prompt(); });
    handle_button(build_button_, [this]() {
        auto info_ptr = info_.lock();
        if (!info_ptr) {
            set_status_message("No asset selected.", 180);
            return;
        }
        if (!rebuild_all_animations_via_pipeline(info_ptr)) {
            set_status_message("Build failed; see logs.", 240);
        } else {
            set_status_message("Rebuilt all animations.", 240);
        }
    });
    handle_button(controller_button_, [this]() { handle_controller_button_click(); });

    if (!consumed && speed_dropdown_) {
        int before = speed_dropdown_->selected();
        if (speed_dropdown_->handle_event(e)) {
            consumed = true;
            if (speed_dropdown_->selected() != before) {
                apply_speed_multiplier_from_dropdown();
            }
        }
    }

    if (!consumed && crop_checkbox_) {
        bool before = crop_checkbox_->value();
        if (crop_checkbox_->handle_event(e)) {
            consumed = true;
            if (crop_checkbox_->value() != before) {
                apply_crop_frames_toggle();
            }
        }
    }
    return consumed;
}

std::vector<float> AnimationEditorWindow::speed_multiplier_options() const {
    return {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
}

float AnimationEditorWindow::parse_speed_multiplier(const nlohmann::json& payload) const {
    float raw = 1.0f;
    try {
        if (payload.contains("speed_multiplier") && payload["speed_multiplier"].is_number()) {
            raw = payload["speed_multiplier"].get<float>();
        } else if (payload.contains("speed_factor") && payload["speed_factor"].is_number()) {
            raw = payload["speed_factor"].get<float>();
        }
    } catch (...) {
        raw = 1.0f;
    }
    if (!std::isfinite(raw) || raw <= 0.0f) {
        raw = 1.0f;
    }
    const auto options = speed_multiplier_options();
    float best = options.empty() ? 1.0f : options.front();
    float best_diff = std::numeric_limits<float>::max();
    for (float option : options) {
        float diff = std::fabs(option - raw);
        if (diff < best_diff) {
            best_diff = diff;
            best = option;
        }
    }
    return best;
}

bool AnimationEditorWindow::parse_crop_frames(const nlohmann::json& payload) const {
    try {
        auto it = payload.find("crop_frames");
        if (it != payload.end()) {
            if (it->is_boolean()) {
                return it->get<bool>();
            }
            if (it->is_number()) {
                return it->get<double>() != 0.0;
            }
            if (it->is_string()) {
                std::string text = it->get<std::string>();
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                return text == "true" || text == "1" || text == "yes" || text == "on";
            }
        }
    } catch (...) {
    }
    return false;
}

void AnimationEditorWindow::persist_header_metadata(float speed_multiplier, bool crop_frames) {
    if (!document_ || !selected_animation_id_) {
        return;
    }

    nlohmann::json payload = nlohmann::json::object();
    if (auto payload_text = document_->animation_payload(*selected_animation_id_)) {
        nlohmann::json parsed = nlohmann::json::parse(*payload_text, nullptr, false);
        if (parsed.is_object()) {
            payload = parsed;
        }
    }

    payload["speed_multiplier"] = speed_multiplier;
    payload["crop_frames"] = crop_frames;
    if (!crop_frames) {
        payload.erase("crop_bounds");
    }

    document_->replace_animation_payload(*selected_animation_id_, payload.dump());
    nlohmann::json normalized = payload;
    if (auto updated = document_->animation_payload(*selected_animation_id_)) {
        nlohmann::json parsed = nlohmann::json::parse(*updated, nullptr, false);
        if (parsed.is_object()) {
            normalized = parsed;
        }
    }
    if (preview_provider_) {
        preview_provider_->invalidate(*selected_animation_id_);
    }
    if (on_animation_properties_changed_) {
        on_animation_properties_changed_(*selected_animation_id_, normalized);
    }
    auto_save_pending_ = true;
    auto_save_timer_frames_ = 0;
    sync_header_controls();
}

void AnimationEditorWindow::apply_speed_multiplier_from_dropdown() {
    if (!speed_dropdown_) {
        return;
    }
    const auto options = speed_multiplier_options();
    int idx = std::clamp(speed_dropdown_->selected(), 0, static_cast<int>(options.size()) - 1);
    float speed = options.empty() ? 1.0f : options[idx];
    bool crop = crop_checkbox_ ? crop_checkbox_->value() : false;
    persist_header_metadata(speed, crop);
}

void AnimationEditorWindow::apply_crop_frames_toggle() {
    const auto options = speed_multiplier_options();
    float speed = options.size() > 2 ? options[2] : 1.0f;
    if (speed_dropdown_) {
        int idx = std::clamp(speed_dropdown_->selected(), 0, static_cast<int>(options.size()) - 1);
        speed = options.empty() ? 1.0f : options[idx];
    }
    bool crop = crop_checkbox_ ? crop_checkbox_->value() : false;
    persist_header_metadata(speed, crop);
}

void AnimationEditorWindow::sync_header_controls() {
    float speed = 1.0f;
    bool crop = false;
    if (document_ && selected_animation_id_) {
        if (auto payload_text = document_->animation_payload(*selected_animation_id_)) {
            nlohmann::json parsed = nlohmann::json::parse(*payload_text, nullptr, false);
            if (parsed.is_object()) {
                speed = parse_speed_multiplier(parsed);
                crop = parse_crop_frames(parsed);
            }
        }
    }

    const auto options = speed_multiplier_options();
    int idx = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (std::fabs(options[i] - speed) < 1e-3f) {
            idx = static_cast<int>(i);
            break;
        }
    }

    if (speed_dropdown_) {
        speed_dropdown_->set_selected(idx);
    }
    if (crop_checkbox_) {
        crop_checkbox_->set_value(crop);
    }
}

void AnimationEditorWindow::set_status_message(const std::string& message, int frames) {
    status_message_ = message;
    status_timer_frames_ = std::max(frames, 0);
}

Asset* AnimationEditorWindow::resolve_frame_editor_asset() {
    if (target_asset_) {
        return target_asset_;
    }
    if (!assets_) {
        return nullptr;
    }
    auto info = info_.lock();
    if (!info) {
        return nullptr;
    }
    const std::string context_name_lower = animation_editor::strings::to_lower_copy(info->name);
    auto matches_context = [&](Asset* candidate) -> bool {
        if (!candidate || !candidate->info) {
            return false;
        }
        if (candidate->info == info) {
            return true;
        }
        if (context_name_lower.empty() || candidate->info->name.empty()) {
            return false;
        }
        return animation_editor::strings::to_lower_copy(candidate->info->name) == context_name_lower;
};
    auto pick_from = [&](const std::vector<Asset*>& candidates) -> Asset* {
        for (Asset* candidate : candidates) {
            if (matches_context(candidate)) {
                return candidate;
            }
        }
        return nullptr;
};

    if (Asset* hovered = assets_->get_hovered_asset(); hovered && matches_context(hovered)) {
        return hovered;
    }
    if (Asset* from_selection = pick_from(assets_->get_selected_assets())) {
        return from_selection;
    }
    if (Asset* from_highlight = pick_from(assets_->get_highlighted_assets())) {
        return from_highlight;
    }
    if (Asset* from_active = pick_from(assets_->getActive())) {
        return from_active;
    }
    return nullptr;
}

void AnimationEditorWindow::open_frame_editor(const std::string& animation_id) {
    if (animation_id.empty() || !document_) {
        return;
    }
    if (!assets_) {
        set_status_message("Live Frame Editor is only available inside the room editor.", 240);
        return;
    }
    Asset* runtime_asset = resolve_frame_editor_asset();
    if (!runtime_asset) {
        set_status_message("Select an in-room asset to edit frames in-scene.", 240);
        return;
    }
    target_asset_ = runtime_asset;
    live_frame_editor_session_active_ = true;
    assets_->begin_frame_editor_session(runtime_asset, document_, preview_provider_, animation_id, this);
    set_visible(false, false );
}

void AnimationEditorWindow::on_live_frame_editor_closed(const std::string& animation_id) {
    live_frame_editor_session_active_ = false;
    preview_provider_->invalidate_all();
    set_visible(true);
    if (!animation_id.empty()) {
        focus_animation(animation_id);
    }
    set_status_message("Movement updated.", 180);
}

void AnimationEditorWindow::create_animation_via_prompt() {
    const char* input = tinyfd_inputBox("Create Animation", "Enter new animation identifier", "animation");
    if (!input) return;
    std::string name = animation_editor::strings::trim_copy(input);

    if (name.empty()) {
        return;
    }
    if (animation_editor::strings::is_reserved_animation_name(name)) {
        set_status_message("Animation name '" + name + "' is reserved.", 240);
        return;
    }
    document_->create_animation(name);
    preview_provider_->invalidate_all();
    select_animation(std::make_optional(name), false);
    set_status_message("Created animation '" + name + "'.", 240);
}

void AnimationEditorWindow::reload_document() {
    auto info_ptr = info_.lock();
    bool snapshot_was_empty = true;
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
                snapshot_was_empty = !has_animation_entries(snapshot);
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

    const bool seeded_default = snapshot_was_empty && document_ && document_->animation_ids().size() == 1 && document_->animation_ids().front() == "default";

    if (seeded_default) {
        document_->save_to_file();
    } else if (document_) {
        document_->consume_dirty_flag();
    }
    preview_provider_->invalidate_all();
    if (list_panel_) list_panel_->set_document(document_);
    if (inspector_panel_) inspector_panel_->set_document(document_);
    configure_list_panel();
    configure_inspector_panel();
    ensure_selection_valid();
    if (seeded_default) {
        set_status_message("Created default animation.", 240);
    } else {
        set_status_message("Reloaded animations.", 240);
    }
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
    auto_save_pending_ = false;
    auto_save_timer_frames_ = 0;
}

void AnimationEditorWindow::set_manifest_store(devmode::core::ManifestStore* store) {
    if (manifest_store_ == store) {
        return;
    }
    close_manifest_transaction();
    manifest_store_ = store;
    if (inspector_panel_) {
        inspector_panel_->set_manifest_store(store);
    }
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

    nlohmann::json& draft = manifest_transaction_.data();
    if (payload.is_null()) {

    } else if (payload.is_object()) {
        if (!draft.is_object()) {
            draft = nlohmann::json::object();
        }

        for (auto it = payload.begin(); it != payload.end(); ++it) {
            draft[it.key()] = it.value();
        }
    } else {

        draft = payload;
    }
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

void AnimationEditorWindow::handle_controller_button_click() {
    if (does_controller_exist()) {
        open_controller();
    } else {
        add_controller();
    }
}

void AnimationEditorWindow::update_controller_button_label() {
    if (!controller_button_) return;
    if (does_controller_exist()) {
        controller_button_->set_text("Open Controller");
    } else {
        controller_button_->set_text("Add Controller");
    }
}

bool AnimationEditorWindow::does_controller_exist() const {
    auto info_ptr = info_.lock();
    if (!info_ptr) return false;
    std::string sanitized = sanitize_asset_name(info_ptr->name);
    if (sanitized.empty()) return false;
    std::string key = generate_controller_key(sanitized);

    std::filesystem::path controller_dir = "ENGINE/animation_update/custom_controllers";
    std::filesystem::path hpp_path = controller_dir / (key + ".hpp");
    return std::filesystem::exists(hpp_path);
}

std::string AnimationEditorWindow::sanitize_asset_name(const std::string& name) const {
    if (name.empty()) return "";
    std::string sanitized;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            sanitized += c;
        } else {
            sanitized += '_';
        }
    }

    sanitized.erase(0, sanitized.find_first_not_of('_'));
    sanitized.erase(sanitized.find_last_not_of('_') + 1);
    return sanitized;
}

std::string AnimationEditorWindow::generate_controller_key(const std::string& asset_name) const {
    return asset_name + "_controller";
}

std::string AnimationEditorWindow::generate_class_name(const std::string& asset_name) const {
    if (asset_name.empty()) return "";
    std::string class_name = asset_name;

    if (!class_name.empty()) {
        class_name[0] = std::toupper(static_cast<unsigned char>(class_name[0]));
    }
    return class_name + "Controller";
}

void AnimationEditorWindow::add_controller() {
    auto info_ptr = info_.lock();
    if (!info_ptr) {
        set_status_message("No asset selected.", 180);
        return;
    }
    std::string sanitized = sanitize_asset_name(info_ptr->name);
    if (sanitized.empty()) {
        set_status_message("Invalid asset name.", 180);
        return;
    }
    std::string key = generate_controller_key(sanitized);
    std::string class_name = generate_class_name(sanitized);

    std::filesystem::path controller_dir = "ENGINE/animation_update/custom_controllers";
    std::filesystem::path hpp_path = controller_dir / (key + ".hpp");
    std::filesystem::path cpp_path = controller_dir / (key + ".cpp");

    if (std::filesystem::exists(hpp_path)) {
        set_status_message("Controller already exists.", 180);
        update_controller_button_label();
        return;
    }

    std::ostringstream hpp_builder;
    hpp_builder << "#pragma once\n"
                << "#include \"asset/asset_controller.hpp\"\n"
                << "\n"
                << "class Assets;\n"
                << "class Asset;\n"
                << "class Input;\n"
                << "\n"
                << "class " << class_name << " : public AssetController {\n"
                << "public:\n"
                << "    " << class_name << "(Assets* assets, Asset* self);\n"
                << "    ~" << class_name << "() override = default;\n"
                << "\n"
                << "    void init();\n"
                << "\n"
                << "    void update(const Input& in) override;\n"
                << "\n"
                << "private:\n"
                << "    Assets* assets_ = nullptr;\n"
                << "    Asset*  self_   = nullptr;\n"
                << "};\n";
    std::string hpp_content = hpp_builder.str();

    std::ostringstream cpp_builder;
    cpp_builder << "#include \"" << key << ".hpp\"\n"
                << "\n"
                << "#include \"asset/Asset.hpp\"\n"
                << "#include \"asset/animation.hpp\"\n"
                << "#include \"asset/asset_info.hpp\"\n"
                << "#include \"animation_update/animation_update.hpp\"\n"
                << "#include \"utils/range_util.hpp\"\n"
                << "#include <string>\n"
                << "\n"
                << class_name << "::" << class_name << "(Assets* assets, Asset* self)\n"
                << "    : assets_(assets), self_(self) {}\n"
                << "\n"
                << "void " << class_name << "::init() {\n"
                << "    if (!self_ || !self_->info || !self_->anim_) return;\n"
                << "\n"
                << "    const std::string default_anim{ animation_update::detail::kDefaultAnimation };\n"
                << "\n"
                << "    auto it = self_->info->animations.find(default_anim);\n"
                << "    if (it != self_->info->animations.end() && !it->second.frames.empty()) {\n"
                << "        self_->anim_->move(SDL_Point{0, 0}, default_anim);\n"
                << "    }\n"
                << "}\n"
                << "\n"
                << "void " << class_name << "::update(const Input& ) {\n"
                << "    if (!self_ || !self_->info || !self_->anim_) return;\n"
                << "\n"
                << "    const std::string default_anim{ animation_update::detail::kDefaultAnimation };\n"
                << "    auto it = self_->info->animations.find(default_anim);\n"
                << "    if (it == self_->info->animations.end() || it->second.frames.empty()) return;\n"
                << "\n"
                << "    if (self_->current_animation != default_anim || self_->current_frame == nullptr) {\n"
                << "        self_->anim_->move(SDL_Point{0, 0}, default_anim);\n"
                << "    }\n"
                << "}\n";
    std::string cpp_content = cpp_builder.str();

    std::ofstream hpp_file(hpp_path);
    if (!hpp_file) {
        set_status_message("Failed to create .hpp file.", 180);
        return;
    }
    hpp_file << hpp_content;
    hpp_file.close();

    std::ofstream cpp_file(cpp_path);
    if (!cpp_file) {
        set_status_message("Failed to create .cpp file.", 180);
        return;
    }
    cpp_file << cpp_content;
    cpp_file.close();

    info_ptr->custom_controller_key = key;

    set_status_message("Controller created.", 240);
    update_controller_button_label();
}

void AnimationEditorWindow::open_controller() {
    auto info_ptr = info_.lock();
    if (!info_ptr) {
        set_status_message("No asset selected.", 180);
        return;
    }
    std::string sanitized = sanitize_asset_name(info_ptr->name);
    if (sanitized.empty()) {
        set_status_message("Invalid asset name.", 180);
        return;
    }
    std::string key = generate_controller_key(sanitized);
    std::filesystem::path controller_dir = "ENGINE/animation_update/custom_controllers";
    std::filesystem::path hpp_path = controller_dir / (key + ".hpp");
    if (!std::filesystem::exists(hpp_path)) {
        set_status_message("Controller file does not exist.", 180);
        return;
    }

    std::string cmd = "cmd /c start \"\" \"" + hpp_path.string() + "\"";
    int result = std::system(cmd.c_str());
    if (result != 0) {
        set_status_message("Failed to open controller file.", 180);
    } else {
        set_status_message("Opened controller file.", 120);
    }
}

bool AnimationEditorWindow::rebuild_animation_from_sources(const std::shared_ptr<AssetInfo>& info,
                                                           const std::string& animation_id) {
    return rebuild_animation_via_pipeline(info, animation_id);
}

bool AnimationEditorWindow::rebuild_animation_via_pipeline(const std::shared_ptr<AssetInfo>& info,
                                                           const std::string& animation_id) {
    if (!info) {
        set_status_message("No asset selected.", 180);
        return false;
    }
    if (animation_id.empty()) {
        set_status_message("No animation id provided.", 180);
        return false;
    }

    vibble::RebuildQueueCoordinator coordinator;
    coordinator.request_animation(info->name, animation_id);
    if (!coordinator.run_asset_tool()) {
        set_status_message("asset_tool.py failed; see logs for details.", 240);
        return false;
    }

    SDL_Renderer* renderer = assets_ ? assets_->renderer() : nullptr;
    if (!renderer) {
        set_status_message("No renderer available to reload animations.", 240);
        return false;
    }

    info->reload_animations_from_disk();
    info->loadAnimations(renderer);

    auto it = info->animations.find(animation_id);
    if (it == info->animations.end()) {
        set_status_message("Animation not found after rebuild.", 240);
        return false;
    }

    if (!it->second.rebuild_animation(renderer, *info, animation_id)) {
        set_status_message("Failed to rebuild animation textures.", 240);
        return false;
    }

    if (assets_) {
        devmode::refresh_loaded_animation_instances(assets_, info);
    }
    if (preview_provider_) {
        preview_provider_->invalidate_all();
    }
    return true;
}

bool AnimationEditorWindow::rebuild_all_animations_via_pipeline(const std::shared_ptr<AssetInfo>& info) {
    if (!info) {
        set_status_message("No asset selected.", 180);
        return false;
    }

    vibble::RebuildQueueCoordinator coordinator;
    coordinator.request_asset(info->name);
    if (!coordinator.run_asset_tool()) {
        set_status_message("asset_tool.py failed; see logs for details.", 240);
        return false;
    }

    SDL_Renderer* renderer = assets_ ? assets_->renderer() : nullptr;
    if (!renderer) {
        set_status_message("No renderer available to reload animations.", 240);
        return false;
    }

    info->reload_animations_from_disk();
    info->loadAnimations(renderer);

    bool ok = true;
    for (auto& [anim_id, anim] : info->animations) {
        ok = anim.rebuild_animation(renderer, *info, anim_id) && ok;
    }

    if (assets_) {
        devmode::refresh_loaded_animation_instances(assets_, info);
    }
    if (preview_provider_) {
        preview_provider_->invalidate_all();
    }
    return ok;

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
            options |= FOS_FORCEFILESYSTEM;
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
    std::vector<std::string> frame_based;
    frame_based.reserve(ids.size());
    for (const auto& id : ids) {
        if (selected_animation_id_ && id == *selected_animation_id_) {
            continue;
        }
        auto payload_text = document_->animation_payload(id);
        if (!payload_text.has_value()) {
            continue;
        }
        nlohmann::json payload = nlohmann::json::parse(*payload_text, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            continue;
        }
        std::string kind = "folder";
        if (payload.contains("source") && payload["source"].is_object()) {
            kind = payload["source"].value("kind", std::string{"folder"});
        }
        if (animation_editor::strings::to_lower_copy(kind) == std::string{"animation"}) {
            continue;
        }
        frame_based.push_back(id);
    }

    if (frame_based.empty()) return std::nullopt;

    std::ostringstream oss;
    oss << "Animations sourced from frames:\n";
    for (const auto& id : frame_based) {
        oss << " - " << id << "\n";
    }

    const char* result = tinyfd_inputBox("Select Animation", oss.str().c_str(), frame_based.front().c_str());
    if (!result) return std::nullopt;
    std::string choice = animation_editor::strings::trim_copy(result);
    if (choice.empty()) return std::nullopt;

    auto match_it = std::find(frame_based.begin(), frame_based.end(), choice);
    if (match_it == frame_based.end()) {
        std::string lowered = animation_editor::strings::to_lower_copy(choice);
        match_it = std::find_if(frame_based.begin(), frame_based.end(), [&](const std::string& value) {
            return animation_editor::strings::to_lower_copy(value) == lowered;
        });
        if (match_it == frame_based.end()) {
            return std::nullopt;
        }
        choice = *match_it;
    }
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
