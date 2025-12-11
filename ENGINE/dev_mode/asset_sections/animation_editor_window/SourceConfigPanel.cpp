#include "SourceConfigPanel.hpp"

#include <SDL.h>
#include <SDL_log.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <regex>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "AnimationDocument.hpp"
#include "PreviewProvider.hpp"

#include "AsyncTaskQueue.hpp"
#include "dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "string_utils.hpp"
#include "utils/string_utils.hpp"
#include "asset/animation.hpp"

#include "utils/stb_image.h"
#include "utils/stb_image_write.h"

namespace animation_editor {

namespace {

bool case_insensitive_equals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

using vibble::strings::to_lower_copy;

bool has_extension_ci(const std::filesystem::path& path, std::string_view ext) {
    return case_insensitive_equals(path.extension().string(), std::string(ext));
}

int safe_to_int(const nlohmann::json& value, int fallback) {
    if (value.is_number_integer()) return value.get<int>();
    if (value.is_number_float()) return static_cast<int>(value.get<double>());
    if (value.is_string()) {
        try {
            return std::stoi(value.get<std::string>());
        } catch (...) {
        }
    }
    return fallback;
}

}

SourceConfigPanel::SourceConfigPanel() {

}

void SourceConfigPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    cached_asset_root_valid_ = false;
    if (preview_provider_) {
        preview_provider_->set_document(document_);
    }
    reload_from_document();
}

void SourceConfigPanel::set_override_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_provider_ = std::move(provider);
    if (preview_provider_ && document_) {
        preview_provider_->set_document(document_);
    }
    animation_start_time_ = 0;
}

void SourceConfigPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    reload_from_document();
}

void SourceConfigPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_controls();
}

void SourceConfigPanel::set_task_queue(std::shared_ptr<AsyncTaskQueue> tasks) {
    task_queue_ = std::move(tasks);
}

void SourceConfigPanel::set_folder_picker(PathPicker picker) { folder_picker_ = std::move(picker); }

void SourceConfigPanel::set_animation_picker(AnimationPicker picker) {
    animation_picker_ = std::move(picker);
    if (use_animation_reference_) {
        layout_controls();
    }
}

void SourceConfigPanel::set_gif_picker(PathPicker picker) { gif_picker_ = std::move(picker); }

void SourceConfigPanel::set_png_sequence_picker(MultiPathPicker picker) { png_sequence_picker_ = std::move(picker); }

void SourceConfigPanel::set_status_callback(std::function<void(const std::string&)> callback) {
    status_callback_ = std::move(callback);
}

void SourceConfigPanel::update() {
    if (task_queue_) {
        task_queue_->update();
        busy_indicator_ = task_queue_->is_busy();
    } else {
        busy_indicator_ = false;
    }

    if (use_animation_reference_ && document_) {
        auto ids = document_->animation_ids();
        std::sort(ids.begin(), ids.end());
        std::string sig;
        sig.reserve(ids.size() * 8);
        for (const auto& id : ids) {
            if (!sig.empty()) sig.push_back('|');
            sig.append(id);
        }
        if (sig != animation_ids_signature_) {
            animation_ids_signature_.swap(sig);
            int previous_index = animation_index_;
            std::string previously_selected = (previous_index >= 0 && previous_index < static_cast<int>(animation_options_.size()))
                                              ? animation_options_[previous_index]
                                              : std::string{};
            refresh_animation_options();

            if (!previously_selected.empty() && !animation_options_.empty()) {
                auto it = std::find(animation_options_.begin(), animation_options_.end(), previously_selected);
                if (it != animation_options_.end()) {
                    animation_index_ = static_cast<int>(std::distance(animation_options_.begin(), it));
                    if (animation_dropdown_) animation_dropdown_->set_selected(animation_index_);
                }
            }
        }
    }

    if (use_animation_reference_ && animation_dropdown_ && !animation_options_.empty()) {
        int idx = std::clamp(animation_dropdown_->selected(), 0, static_cast<int>(animation_options_.size()) - 1);

        std::string current_target = strings::trim_copy(current_source_.name.value_or(current_source_.path));
        const std::string& desired = animation_options_[idx];
        if (idx != animation_index_ || strings::trim_copy(desired) != current_target) {
            apply_animation_selection();
        }
    }
}

void SourceConfigPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    if (bounds_.w <= 0 || bounds_.h <= 0) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    if (use_animation_reference_) {
        if (animation_dropdown_) animation_dropdown_->render(renderer);
        if (pick_animation_button_) pick_animation_button_->render(renderer);

        render_animation_preview(renderer);
    } else {
        for (const auto& button : frame_buttons_) {
            if (button) button->render(renderer);
        }
    }

    if (busy_indicator_) {
        SDL_Rect indicator = bounds_;
        indicator.y = indicator.y + indicator.h - 6;
        indicator.h = 6;
        SDL_SetRenderDrawColor(renderer, 0xc0, 0x9a, 0x2b, 255);
        SDL_RenderFillRect(renderer, &indicator);
    }
}

bool SourceConfigPanel::handle_event(const SDL_Event& e) {
    if (bounds_.w <= 0 || bounds_.h <= 0) return false;

    bool consumed = false;
    if (!consumed && use_animation_reference_ && animation_dropdown_) {
        if (animation_dropdown_->handle_event(e)) {
            apply_animation_selection();
            consumed = true;
        }
    }

    if (!consumed && use_animation_reference_ && pick_animation_button_ && pick_animation_button_->handle_event(e)) {
        consumed = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            import_from_animation();
        }
    }

    if (!consumed && !use_animation_reference_) {
        for (size_t i = 0; i < frame_buttons_.size(); ++i) {
            auto& button = frame_buttons_[i];
            if (button && button->handle_event(e)) {
                consumed = true;
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    switch (i) {
                        case 0:
                            import_from_gif();
                            break;
                        case 1:
                            import_from_folder();
                            break;
                        case 2:
                            import_from_png_sequence();
                            break;
                        default:
                            break;
                    }
                }
                break;
            }
        }
    }

    return consumed;
}

int SourceConfigPanel::preferred_height(int) const {
    const int padding = 6;
    int height = padding;
    if (use_animation_reference_) {
        height += DMDropdown::height();
        if (animation_picker_) {
            height += padding + DMButton::height();
        }
    } else {
        const int button_count = static_cast<int>(frame_buttons_.size());
        if (button_count > 0) {
            height += button_count * DMButton::height();
            height += std::max(0, button_count - 1) * padding;
        }
    }
    height += padding;
    return height;
}

bool SourceConfigPanel::allow_out_of_bounds_pointer_events() const {
    return use_animation_reference_ && animation_dropdown_ && animation_dropdown_->expanded();
}

SourceConfigPanel::SourceMode SourceConfigPanel::source_mode() const {
    return use_animation_reference_ ? SourceMode::kAnimation : SourceMode::kFrames;
}

void SourceConfigPanel::set_source_mode(SourceMode mode) {
    bool wants_animation = (mode == SourceMode::kAnimation);
    if (use_animation_reference_ == wants_animation) {
        return;
    }

    if (wants_animation) {
        refresh_animation_options();
        if (animation_options_.empty() && !animation_picker_) {
            update_status("No other animations available to link. Create or duplicate an animation first.");
            return;
        }
    }

    use_animation_reference_ = wants_animation;

    if (use_animation_reference_) {
        refresh_animation_options();
        if (!animation_options_.empty()) {
            if (!animation_dropdown_) {
                int idx = animation_index_;
                if (idx < 0 && !animation_options_.empty()) {
                    idx = 0;
                }
                if (idx >= 0 && !animation_options_.empty()) {
                    idx = std::min(idx, static_cast<int>(animation_options_.size()) - 1);
                }
                animation_dropdown_ = std::make_unique<DMDropdown>("Source Animation", animation_options_, std::max(0, idx));
                animation_index_ = animation_dropdown_->selected();
            } else {
                int idx = animation_index_;
                if (!animation_options_.empty()) {
                    idx = std::clamp(idx, 0, static_cast<int>(animation_options_.size()) - 1);
                } else {
                    idx = 0;
                }
                animation_dropdown_->set_selected(idx);
                animation_index_ = animation_dropdown_->selected();
            }
            if (animation_dropdown_ && !animation_options_.empty()) {
                clean_output_frames();
                apply_animation_selection();
            }
        } else if (animation_picker_) {
            if (!pick_animation_button_) {
                pick_animation_button_ = std::make_unique<DMButton>("Pick Animation", &DMStyles::AccentButton(), 120, DMButton::height());
            }

        }
    } else {
        animation_index_ = -1;
        animation_dropdown_.reset();
        pick_animation_button_.reset();
    }

    layout_controls();

    if (on_source_changed_) on_source_changed_(animation_id_);
}

std::vector<std::string> SourceConfigPanel::summary_badges() const {
    std::vector<std::string> badges;
    badges.reserve(4);
    badges.push_back(use_animation_reference_ ? std::string{"Animation"} : std::string{"Frames"});

    if (use_animation_reference_) {
        std::string target = strings::trim_copy(current_source_.name.value_or(current_source_.path));
        if (target.empty() && animation_index_ >= 0 && animation_index_ < static_cast<int>(animation_options_.size())) {
            target = animation_options_[animation_index_];
        }
        if (target.empty()) {
            target = "Unassigned";
        }
        badges.push_back(std::move(target));
    } else {
        std::string kind = strings::trim_copy(current_source_.kind);
        if (!kind.empty() && to_lower_copy(kind) != std::string{"folder"}) {
            badges.push_back(kind);
        }
        std::string display_path = strings::trim_copy(current_source_.path);
        if (display_path.empty()) {
            display_path = "Unassigned";
        }
        badges.push_back(std::move(display_path));
    }

    int frames = std::max(1, frame_count_);
    badges.push_back(std::to_string(frames) + (frames == 1 ? " frame" : " frames"));
    return badges;
}

void SourceConfigPanel::reload_from_document() {
    if (reloading_) return;
    reloading_ = true;

    payload_ = nlohmann::json::object();
    payload_loaded_ = false;
    current_source_ = SourceConfig{};
    frame_count_ = 1;

    if (!document_ || animation_id_.empty()) {
        reloading_ = false;
        return;
    }

    if (auto payload_text = document_->animation_payload(animation_id_)) {
        nlohmann::json parsed = nlohmann::json::parse(*payload_text, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) {
            payload_ = parsed;
            payload_loaded_ = true;
            current_source_ = parse_source(payload_);
            if (payload_.contains("number_of_frames")) {
                frame_count_ = std::max(1, safe_to_int(payload_["number_of_frames"], 1));
            }
        }
    }

    cached_asset_root_valid_ = false;

    use_animation_reference_ = (current_source_.kind == std::string("animation"));
    refresh_animation_options();

    if (use_animation_reference_ && animation_options_.empty()) {
        use_animation_reference_ = false;
        current_source_.kind = "folder";
        current_source_.path = animation_id_;
        current_source_.name.reset();

        apply_source_config(current_source_);
    }

    if (use_animation_reference_) {
        std::string target = current_source_.name.value_or(std::string{});
        if (target.empty()) target = current_source_.path;
        animation_index_ = -1;
        for (size_t i = 0; i < animation_options_.size(); ++i) {
            if (animation_options_[i] == target) { animation_index_ = static_cast<int>(i); break; }
        }
        if (animation_index_ < 0 && !animation_options_.empty()) animation_index_ = 0;
    } else {
        animation_index_ = -1;
    }
    layout_controls();
    reloading_ = false;
}

void SourceConfigPanel::ensure_payload_loaded() {
    if (!payload_loaded_) {
        reload_from_document();
        if (!payload_loaded_) {
            payload_ = nlohmann::json::object();
            payload_loaded_ = true;
            current_source_ = SourceConfig{};
            frame_count_ = 1;
        }
    }
}

void SourceConfigPanel::commit_payload(bool refresh_document) {
    if (!document_ || animation_id_.empty()) return;
    ensure_payload_loaded();
    std::string dump = payload_.dump();
    document_->replace_animation_payload(animation_id_, dump);
    if (refresh_document) {
        reload_from_document();
    }
}

void SourceConfigPanel::apply_source_config(const SourceConfig& config) {
    ensure_payload_loaded();
    const std::string previous_kind = current_source_.kind;
    current_source_ = config;
    payload_["source"] = build_source_json(config);
    update_number_of_frames();
    if (previous_kind != std::string("animation") && config.kind == std::string("animation")) {
        clear_derived_fields();
    }
    commit_payload();

    if (previous_kind != std::string("animation") && config.kind == std::string("animation")) {
        clean_output_frames();
    }
}

void SourceConfigPanel::clear_derived_fields() {
    ensure_payload_loaded();
    payload_.erase("movement");
    payload_.erase("movement_total");
    payload_.erase("audio");
    payload_.erase("speed_factor");
    payload_.erase("rnd_start");
}

void SourceConfigPanel::update_number_of_frames() {
    ensure_payload_loaded();
    int frames = compute_frame_count(current_source_);
    if (frames <= 0) frames = 1;
    frame_count_ = frames;
    payload_["number_of_frames"] = frames;
}

int SourceConfigPanel::compute_frame_count(const SourceConfig& config) const {
    std::unordered_set<std::string> visited;
    visited.insert(animation_id_);
    return compute_frame_count_recursive(config, visited);
}

int SourceConfigPanel::compute_frame_count_recursive(const SourceConfig& config,
                                                     std::unordered_set<std::string>& visited) const {
    std::string kind = to_lower_copy(config.kind);
    if (kind == "animation") {
        std::string target = strings::trim_copy(config.name.value_or(config.path));
        if (target.empty()) return 1;
        if (visited.count(target) != 0) return 1;
        visited.insert(target);
        auto payload_opt = animation_payload(target);
        if (!payload_opt.has_value()) return 1;
        const nlohmann::json& payload = *payload_opt;
        if (payload.contains("number_of_frames")) {
            int frames = safe_to_int(payload["number_of_frames"], 1);
            if (frames > 0) return frames;
        }
        SourceConfig nested = parse_source(payload);
        return compute_frame_count_recursive(nested, visited);
    }

    if (kind == "spritesheet") {
        int cols = safe_to_int(config.extras.value("cols", 0), 0);
        int rows = safe_to_int(config.extras.value("rows", 0), 0);
        int frames = safe_to_int(config.extras.value("frames", 0), 0);
        if (cols > 0 && rows > 0) {
            long long total = static_cast<long long>(cols) * static_cast<long long>(rows);
            if (total > 0 && total <= std::numeric_limits<int>::max()) {
                return static_cast<int>(total);
            }
        }
        if (frames > 0) return frames;
        return 1;
    }

    return count_frames_in_folder(config.path);
}

int SourceConfigPanel::count_frames_in_folder(const std::string& relative_path) const {
    std::filesystem::path root = resolve_asset_root();
    if (root.empty()) return 1;
    std::filesystem::path folder = relative_path.empty() ? root : (root / relative_path);
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        return 1;
    }

    static const std::array<std::string, 5> kExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".webp"};
    int count = 0;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = to_lower_copy(entry.path().extension().string());
            if (std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end()) {
                ++count;
            }
        }
    } catch (const std::exception& ex) {
        SDL_Log("SourceConfigPanel: failed counting frames in %s: %s", folder.string().c_str(), ex.what());
        return 1;
    }
    return std::max(count, 1);
}

std::optional<nlohmann::json> SourceConfigPanel::animation_payload(const std::string& id) const {
    if (!document_) return std::nullopt;
    auto payload_text = document_->animation_payload(id);
    if (!payload_text.has_value()) return std::nullopt;
    nlohmann::json parsed = nlohmann::json::parse(*payload_text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
    return parsed;
}

bool SourceConfigPanel::animation_is_frame_based(const std::string& id) const {
    auto payload = animation_payload(id);
    if (!payload) {
        return false;
    }
    SourceConfig config = parse_source(*payload);
    return to_lower_copy(config.kind) != std::string{"animation"};
}

SourceConfigPanel::SourceConfig SourceConfigPanel::parse_source(const nlohmann::json& payload) const {
    SourceConfig config;
    if (!payload.contains("source") || !payload["source"].is_object()) {
        return config;
    }

    const nlohmann::json& src = payload["source"];
    config.kind = src.value("kind", std::string{"folder"});
    config.path = src.value("path", std::string{});
    if (src.contains("name") && !src["name"].is_null()) {
        try {
            config.name = src["name"].get<std::string>();
        } catch (...) {
            config.name = std::nullopt;
        }
    } else {
        config.name = std::nullopt;
    }

    config.extras = nlohmann::json::object();
    for (auto it = src.begin(); it != src.end(); ++it) {
        if (it.key() == "kind" || it.key() == "path" || it.key() == "name") continue;
        config.extras[it.key()] = *it;
    }
    return config;
}

nlohmann::json SourceConfigPanel::build_source_json(const SourceConfig& config) const {
    nlohmann::json src = nlohmann::json::object({{"kind", config.kind}, {"path", config.path}});
    if (config.name.has_value()) {
        src["name"] = *config.name;
    } else {
        src["name"] = nullptr;
    }
    for (auto it = config.extras.begin(); it != config.extras.end(); ++it) {
        if (it.key() == "kind" || it.key() == "path" || it.key() == "name") continue;
        src[it.key()] = *it;
    }
    return src;
}

std::filesystem::path SourceConfigPanel::resolve_asset_root() const {
    if (cached_asset_root_valid_) return cached_asset_root_;
    cached_asset_root_ = std::filesystem::path{};
    if (document_) {
        const std::filesystem::path& root = document_->asset_root();
        if (!root.empty()) {
            cached_asset_root_ = root;
        } else {
            const std::filesystem::path& info_path = document_->info_path();
            if (!info_path.empty()) {
                cached_asset_root_ = info_path.parent_path();
            }
        }
    }
    cached_asset_root_valid_ = true;
    return cached_asset_root_;
}

std::filesystem::path SourceConfigPanel::animation_output_directory() const {
    std::filesystem::path root = resolve_asset_root();
    if (root.empty() || animation_id_.empty()) return {};
    return root / animation_id_;
}

bool SourceConfigPanel::prepare_output_directory(std::filesystem::path* out_dir) const {
    if (!out_dir) return false;
    std::filesystem::path dir = animation_output_directory();
    if (dir.empty()) {
        update_status("Asset root unavailable");
        return false;
    }
    try {
        std::filesystem::create_directories(dir);
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (has_extension_ci(entry.path(), ".png")) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
    } catch (const std::exception& ex) {
        SDL_Log("SourceConfigPanel: failed preparing %s: %s", dir.string().c_str(), ex.what());
        update_status("Failed to prepare output directory");
        return false;
    }

    *out_dir = dir;
    return true;
}

bool SourceConfigPanel::clean_output_frames() const {
    std::filesystem::path dir = animation_output_directory();
    if (dir.empty()) {
        return false;
    }
    try {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            return true;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            if (has_extension_ci(entry.path(), ".png")) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
        return true;
    } catch (const std::exception& ex) {
        SDL_Log("SourceConfigPanel: failed cleaning %s: %s", dir.string().c_str(), ex.what());
        return false;
    }
}

std::vector<std::filesystem::path> SourceConfigPanel::collect_png_files(const std::filesystem::path& folder) const {
    std::vector<std::filesystem::path> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            if (has_extension_ci(entry.path(), ".png")) {
                files.push_back(entry.path());
            }
        }
    } catch (const std::exception& ex) {
        SDL_Log("SourceConfigPanel: failed listing %s: %s", folder.string().c_str(), ex.what());
    }
    return files;
}

std::vector<std::filesystem::path> SourceConfigPanel::normalize_sequence(const std::vector<std::filesystem::path>& files) const {
    std::vector<std::filesystem::path> normalized = files;

    auto numeric_key = [](const std::filesystem::path& path) {
        std::string stem = path.stem().string();
        try {
            return std::make_tuple(0, std::stoi(stem), to_lower_copy(stem));
        } catch (...) {
            std::smatch match;
            static const std::regex kNumber{"(\\d+)", std::regex::icase};
            if (std::regex_search(stem, match, kNumber)) {
                try {
                    return std::make_tuple(0, std::stoi(match.str(1)), to_lower_copy(stem));
                } catch (...) {
                }
            }
        }
        return std::make_tuple(1, 0, to_lower_copy(stem));
};

    std::sort(normalized.begin(), normalized.end(), [&](const auto& lhs, const auto& rhs) {
        return numeric_key(lhs) < numeric_key(rhs);
    });
    return normalized;
}

void SourceConfigPanel::copy_sequence_to_output(const std::vector<std::filesystem::path>& files,
                                                const std::filesystem::path& out_dir) const {
    std::vector<std::filesystem::path> copied;
    copied.reserve(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        std::filesystem::path dst = out_dir / (std::to_string(i) + ".png");
        try {
            std::filesystem::copy_file(files[i], dst, std::filesystem::copy_options::overwrite_existing);
            copied.push_back(dst);
        } catch (const std::exception& ex) {
            SDL_Log("SourceConfigPanel: failed copying %s -> %s: %s", files[i].string().c_str(), dst.string().c_str(), ex.what());
        }
    }

}

void SourceConfigPanel::layout_controls() {
    const int padding = 6;
    const int inner_w = std::max(0, bounds_.w - padding * 2);
    int x = bounds_.x + padding;
    int y = bounds_.y + padding;

    if (use_animation_reference_) {
        refresh_animation_options();
        if (!animation_options_.empty()) {
            if (!animation_dropdown_) {
                int idx = animation_index_;
                if (idx < 0 && !animation_options_.empty()) {
                    idx = 0;
                }
                if (idx >= 0 && !animation_options_.empty()) {
                    idx = std::min(idx, static_cast<int>(animation_options_.size()) - 1);
                }
                animation_dropdown_ = std::make_unique<DMDropdown>("Source Animation", animation_options_, std::max(0, idx));
                animation_index_ = animation_dropdown_->selected();
            } else {

                int idx = animation_dropdown_->selected();
                if (!animation_options_.empty()) {
                    idx = std::clamp(idx, 0, static_cast<int>(animation_options_.size()) - 1);
                } else {
                    idx = 0;
                }
                animation_dropdown_->set_selected(idx);
                animation_index_ = idx;
            }

            animation_dropdown_rect_ = SDL_Rect{x, y, inner_w, DMDropdown::height()};
            if (animation_dropdown_) animation_dropdown_->set_rect(animation_dropdown_rect_);
            y += DMDropdown::height();
            pick_animation_button_.reset();
        } else if (animation_picker_) {
            animation_dropdown_.reset();
            if (!pick_animation_button_) {
                pick_animation_button_ = std::make_unique<DMButton>("Pick Animation", &DMStyles::AccentButton(), inner_w, DMButton::height());
            }
            SDL_Rect button_rect{x, y, inner_w, DMButton::height()};
            pick_animation_button_->set_rect(button_rect);
            animation_dropdown_rect_ = button_rect;
            y += DMButton::height();
        } else {
            animation_dropdown_rect_ = SDL_Rect{bounds_.x, bounds_.y, 0, 0};
            animation_dropdown_.reset();
            pick_animation_button_.reset();
        }

        for (auto& rect : frame_button_rects_) {
            rect = SDL_Rect{bounds_.x, bounds_.y, 0, 0};
        }
    } else {
        animation_dropdown_rect_ = SDL_Rect{bounds_.x, bounds_.y, 0, 0};
        animation_dropdown_.reset();
        pick_animation_button_.reset();

        const std::array<const char*, 3> labels = {"Upload GIF", "Upload Folder", "Upload PNG Sequence"};
        const std::array<const DMButtonStyle*, 3> styles = {&DMStyles::AccentButton(), &DMStyles::HeaderButton(), &DMStyles::HeaderButton()};

        for (size_t i = 0; i < frame_buttons_.size(); ++i) {
            if (!frame_buttons_[i]) {
                frame_buttons_[i] = std::make_unique<DMButton>(labels[i], styles[i], inner_w, DMButton::height());
            } else {
                frame_buttons_[i]->set_text(labels[i]);
                frame_buttons_[i]->set_style(styles[i]);
            }
            frame_button_rects_[i] = SDL_Rect{x, y, inner_w, DMButton::height()};
            frame_buttons_[i]->set_rect(frame_button_rects_[i]);
            y += DMButton::height();
            if (i + 1 < frame_buttons_.size()) {
                y += padding;
            }
        }
    }
}

void SourceConfigPanel::update_status(const std::string& message) const {
    status_message_ = message;
    SDL_Log("SourceConfigPanel[%s]: %s", animation_id_.c_str(), message.c_str());
    if (status_callback_) {
        try {
            status_callback_(message);
        } catch (...) {
        }
    }
}

void SourceConfigPanel::refresh_animation_options() {
    std::vector<std::string> new_options;
    if (document_) {
        auto ids = document_->animation_ids();
        for (const auto& id : ids) {
            if (id == animation_id_) {
                continue;
            }
            new_options.push_back(id);
        }
    }

    if (new_options == animation_options_) {

        if (animation_dropdown_ && !animation_options_.empty()) {
            animation_index_ = std::clamp(animation_dropdown_->selected(), 0, static_cast<int>(animation_options_.size()) - 1);
        }
        return;
    }

    animation_options_ = std::move(new_options);

    if (animation_options_.empty()) {
        animation_index_ = -1;
    } else {
        std::string desired = strings::trim_copy(current_source_.name.value_or(current_source_.path));
        if (desired.empty() && animation_index_ >= 0 && animation_index_ < static_cast<int>(animation_options_.size())) {
            desired = animation_options_[animation_index_];
        }

        int new_index = -1;
        if (!desired.empty()) {
            auto it = std::find(animation_options_.begin(), animation_options_.end(), desired);
            if (it != animation_options_.end()) {
                new_index = static_cast<int>(std::distance(animation_options_.begin(), it));
            }
        }
        if (new_index < 0) {
            new_index = 0;
        }
        animation_index_ = new_index;
    }

    if (animation_dropdown_) {
        animation_dropdown_.reset();
    }
}

void SourceConfigPanel::apply_animation_selection() {
    if (!use_animation_reference_ || !animation_dropdown_ || animation_options_.empty()) return;
    int idx = animation_dropdown_->selected();
    idx = std::clamp(idx, 0, static_cast<int>(animation_options_.size()) - 1);
    const std::string& target = animation_options_[idx];
    if (target.empty() || target == animation_id_) return;

    std::string current_target = strings::trim_copy(current_source_.name.value_or(current_source_.path));
    if (animation_index_ == idx && strings::trim_copy(target) == current_target) {
        return;
    }

    animation_index_ = idx;

    SourceConfig config;
    config.kind = "animation";
    config.path.clear();
    config.name = target;
    apply_source_config(config);
    animation_start_time_ = SDL_GetTicks();
    update_status("Linked frames from animation '" + target + "'");
    if (on_source_changed_) on_source_changed_(animation_id_);
}

void SourceConfigPanel::commit_animation_dropdown_selection() {
    apply_animation_selection();
}

void SourceConfigPanel::import_from_folder() {
    if (!folder_picker_) {
        update_status("Folder picker not configured");
        return;
    }
    std::optional<std::filesystem::path> folder = folder_picker_();
    if (!folder.has_value() || folder->empty()) {
        update_status("Folder selection cancelled");
        return;
    }
    if (!std::filesystem::exists(*folder) || !std::filesystem::is_directory(*folder)) {
        update_status("Selected folder is invalid");
        return;
    }

    std::vector<std::filesystem::path> files = normalize_sequence(collect_png_files(*folder));
    if (files.empty()) {
        update_status("No PNG files found in folder");
        return;
    }

    std::filesystem::path out_dir;
    if (!prepare_output_directory(&out_dir)) return;

    copy_sequence_to_output(files, out_dir);

    SourceConfig config;
    config.kind = "folder";
    config.path = animation_id_;
    config.name.reset();
    apply_source_config(config);
    update_status("Imported frames from folder");
}

void SourceConfigPanel::import_from_animation() {
    if (!animation_picker_) {
        update_status("Animation picker not configured");
        return;
    }
    std::optional<std::string> selection = animation_picker_();
    if (!selection.has_value()) {
        update_status("Animation selection cancelled");
        return;
    }
    std::string target = strings::trim_copy(*selection);
    if (target.empty()) {
        update_status("Animation selection empty");
        return;
    }
    if (target == animation_id_) {
        update_status("Cannot reference same animation");
        return;
    }
    if (!animation_payload(target).has_value()) {
        update_status("Target animation not found");
        return;
    }

    SourceConfig config;
    config.kind = "animation";
    config.path.clear();
    config.name = target;
    apply_source_config(config);
    animation_start_time_ = SDL_GetTicks();
    refresh_animation_options();
    layout_controls();
    if (animation_dropdown_ && animation_index_ >= 0 && animation_index_ < static_cast<int>(animation_options_.size())) {
        animation_dropdown_->set_selected(animation_index_);
    }
    update_status("Linked frames from animation '" + target + "'");
    if (on_source_changed_) on_source_changed_(animation_id_);
}

void SourceConfigPanel::import_from_gif() {
    if (!gif_picker_) {
        update_status("GIF picker not configured");
        return;
    }
    std::optional<std::filesystem::path> file = gif_picker_();
    if (!file.has_value() || file->empty()) {
        update_status("GIF selection cancelled");
        return;
    }
    if (!std::filesystem::exists(*file) || !std::filesystem::is_regular_file(*file)) {
        update_status("Selected GIF is invalid");
        return;
    }

    std::vector<unsigned char> bytes;
    try {
        std::ifstream in(*file, std::ios::binary);
        if (!in) {
            update_status("Failed to open GIF file");
            return;
        }
        in.seekg(0, std::ios::end);
        std::streamsize sz = in.tellg();
        in.seekg(0, std::ios::beg);
        if (sz <= 0) {
            update_status("GIF file is empty");
            return;
        }
        bytes.resize(static_cast<size_t>(sz));
        if (!in.read(reinterpret_cast<char*>(bytes.data()), sz)) {
            update_status("Failed reading GIF file");
            return;
        }
    } catch (const std::exception& ex) {
        SDL_Log("SourceConfigPanel: failed reading GIF %s: %s", file->string().c_str(), ex.what());
        update_status("Failed reading GIF");
        return;
    }

    int x = 0, y = 0, z = 0, comp = 0;
    int* delays = nullptr;
    stbi_uc* data = stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays, &x, &y, &z, &comp, STBI_rgb_alpha);
    if (!data || x <= 0 || y <= 0 || z <= 0) {
        if (data) stbi_image_free(data);
        if (delays) stbi_image_free(delays);
        update_status("Failed to decode GIF frames");
        return;
    }

    std::filesystem::path out_dir;
    if (!prepare_output_directory(&out_dir)) {
        stbi_image_free(data);
        if (delays) stbi_image_free(delays);
        return;
    }

    const int channels = 4;
    const int stride = x * channels;
    std::vector<std::filesystem::path> written;
    written.reserve(static_cast<size_t>(z));
    for (int i = 0; i < z; ++i) {
        std::filesystem::path dst = out_dir / (std::to_string(i) + ".png");
        const stbi_uc* frame = data + static_cast<size_t>(i) * static_cast<size_t>(x) * static_cast<size_t>(y) * channels;
        int ok = 0;
        try {
            ok = stbi_write_png(dst.string().c_str(), x, y, channels, frame, stride);
        } catch (...) {
            ok = 0;
        }
        if (ok) {
            written.push_back(dst);
        } else {
            SDL_Log("SourceConfigPanel: failed writing frame %d to %s", i, dst.string().c_str());
        }
    }

    stbi_image_free(data);
    if (delays) stbi_image_free(delays);

    SourceConfig config;
    config.kind = "folder";
    config.path = animation_id_;
    config.name.reset();
    apply_source_config(config);
    update_status("Imported GIF frames");
}

void SourceConfigPanel::import_from_png_sequence() {
    if (!png_sequence_picker_) {
        update_status("PNG picker not configured");
        return;
    }
    std::vector<std::filesystem::path> files = png_sequence_picker_();
    if (files.empty()) {
        update_status("PNG selection cancelled");
        return;
    }

    std::vector<std::filesystem::path> filtered;
    filtered.reserve(files.size());
    for (const auto& file : files) {
        if (has_extension_ci(file, ".png")) {
            filtered.push_back(file);
        }
    }
    if (filtered.empty()) {
        update_status("No PNG files selected");
        return;
    }

    std::filesystem::path out_dir;
    if (!prepare_output_directory(&out_dir)) return;

    copy_sequence_to_output(normalize_sequence(filtered), out_dir);

    SourceConfig config;
    config.kind = "folder";
    config.path = animation_id_;
    config.name.reset();
    apply_source_config(config);
    update_status("Imported PNG sequence");
}

void SourceConfigPanel::render_animation_preview(SDL_Renderer* renderer) const {
    if (!renderer || !preview_provider_ || !document_ || !use_animation_reference_ ||
        animation_index_ < 0 || animation_index_ >= static_cast<int>(animation_options_.size())) {
        return;
    }

    const std::string& selected_animation_id = animation_options_[animation_index_];

    auto payload_opt = animation_payload(selected_animation_id);
    if (!payload_opt) return;

    const auto& payload = *payload_opt;
    std::string source_kind = "folder";
    if (payload.contains("source") && payload["source"].is_object()) {
        source_kind = payload["source"].value("kind", std::string{"folder"});
    }

    if (source_kind == "animation") {

        return;
    }

    constexpr float effective_fps = static_cast<float>(kBaseAnimationFps);
    const float frame_time_ms = 1000.0f / effective_fps;
    bool reverse = payload.value("reverse_source", false);
    bool flip_x = payload.value("flipped_source", false);
    bool flip_y = false;

    if (payload.contains("derived_modifiers") && payload["derived_modifiers"].is_object()) {
        const auto& modifiers = payload["derived_modifiers"];
        reverse = modifiers.value("reverse", reverse);
        flip_x = modifiers.value("flipX", flip_x);
        flip_y = modifiers.value("flipY", false);
    }

    int num_frames = safe_to_int(payload.value("number_of_frames", nlohmann::json(1)), 1);
    if (num_frames <= 0) num_frames = 1;

    if (animation_start_time_ == 0) {
        animation_start_time_ = SDL_GetTicks();
    }

    Uint32 elapsed_ms = SDL_GetTicks() - animation_start_time_;

    int raw_frame = static_cast<int>((elapsed_ms % static_cast<int>(frame_time_ms * num_frames)) / frame_time_ms);
    if (raw_frame >= num_frames) raw_frame = num_frames - 1;

    int current_frame = reverse ? (num_frames - 1 - raw_frame) : raw_frame;

    if (current_frame < 0) current_frame = 0;
    if (current_frame >= num_frames) current_frame = num_frames - 1;

    SDL_Texture* frame_texture = preview_provider_->get_frame_texture(renderer, selected_animation_id, current_frame);
    if (!frame_texture) return;

    int preview_y = animation_dropdown_rect_.y + animation_dropdown_rect_.h + 8;
    int max_width = bounds_.w - 16;
    int max_height = bounds_.h - preview_y - 16;

    if (max_width <= 0 || max_height <= 0) return;

    int tex_w, tex_h;
    SDL_QueryTexture(frame_texture, nullptr, nullptr, &tex_w, &tex_h);

    if (tex_w <= 0 || tex_h <= 0) return;

    float scale_x = static_cast<float>(max_width) / tex_w;
    float scale_y = static_cast<float>(max_height) / tex_h;
    float scale = std::min(std::min(scale_x, scale_y), 1.0f);

    int draw_w = static_cast<int>(tex_w * scale);
    int draw_h = static_cast<int>(tex_h * scale);

    int center_x = bounds_.x + bounds_.w / 2;
    int draw_x = center_x - draw_w / 2;
    int draw_y = preview_y;

    SDL_Rect dst_rect{draw_x, draw_y, draw_w, draw_h};

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(renderer, &prev_clip);

    bool had_clip = (prev_clip.w > 0 && prev_clip.h > 0 && prev_clip.x >= 0 && prev_clip.y >= 0);

    SDL_Rect clip_rect = {bounds_.x, bounds_.y, bounds_.w, bounds_.h};
    SDL_RenderSetClipRect(renderer, &clip_rect);

    SDL_RendererFlip flip_flags = SDL_FLIP_NONE;
    if (flip_x) flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_HORIZONTAL);
    if (flip_y) flip_flags = static_cast<SDL_RendererFlip>(flip_flags | SDL_FLIP_VERTICAL);

    SDL_RenderCopyEx(renderer, frame_texture, nullptr, &dst_rect, 0.0, nullptr, flip_flags);

    if (had_clip) {
        SDL_RenderSetClipRect(renderer, &prev_clip);
    } else {
        SDL_RenderSetClipRect(renderer, nullptr);
    }
}

}
