#include "SourceConfigPanel.hpp"

#include <SDL.h>
#include <SDL_log.h>
#include <SDL_ttf.h>

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

#define private public
#include "AnimationDocument.hpp"
#undef private

#include "AsyncTaskQueue.hpp"
#include "CroppingService.hpp"
#include "dm_styles.hpp"

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

std::string to_lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool has_extension(const std::filesystem::path& path, std::string_view ext) {
    return case_insensitive_equals(path.extension().string(), std::string(ext));
}

std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](unsigned char ch) { return !is_space(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](unsigned char ch) { return !is_space(ch); }).base(), value.end());
    return value;
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

void render_button_label(SDL_Renderer* renderer, const SDL_Rect& rect, const std::string& text) {
    if (!renderer || text.empty()) {
        return;
    }

    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        int x = rect.x + std::max(0, (rect.w - surface->w) / 2);
        int y = rect.y + std::max(0, (rect.h - surface->h) / 2);
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

}  // namespace

SourceConfigPanel::SourceConfigPanel() {
    buttons_[0].label = "Import From Folder";
    buttons_[1].label = "Link From Animation";
    buttons_[2].label = "Import From GIF";
    buttons_[3].label = "Import PNG Sequence";
}

void SourceConfigPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    cached_asset_root_valid_ = false;
    reload_from_document();
}

void SourceConfigPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    reload_from_document();
}

void SourceConfigPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_buttons();
}

void SourceConfigPanel::set_services(std::shared_ptr<CroppingService> cropping, std::shared_ptr<AsyncTaskQueue> tasks) {
    cropping_service_ = std::move(cropping);
    task_queue_ = std::move(tasks);
}

void SourceConfigPanel::set_folder_picker(PathPicker picker) { folder_picker_ = std::move(picker); }

void SourceConfigPanel::set_animation_picker(AnimationPicker picker) { animation_picker_ = std::move(picker); }

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
}

void SourceConfigPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) return;

    SDL_Rect panel_bounds = bounds_;
    SDL_SetRenderDrawColor(renderer, 0x2d, 0x34, 0x36, 255);
    SDL_RenderFillRect(renderer, &panel_bounds);

    SDL_SetRenderDrawColor(renderer, 0x44, 0x52, 0x54, 255);
    SDL_RenderDrawRect(renderer, &panel_bounds);

    for (size_t i = 0; i < buttons_.size(); ++i) {
        const auto& button = buttons_[i];
        SDL_Rect rect = button.rect;
        if (rect.w <= 0 || rect.h <= 0) continue;

        bool hovered = static_cast<int>(i) == hover_button_;
        SDL_Color fill{static_cast<Uint8>(hovered ? 0x5c : 0x3a), static_cast<Uint8>(hovered ? 0x78 : 0x4b),
                       static_cast<Uint8>(hovered ? 0xa8 : 0x63), 255};
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderDrawColor(renderer, 0x18, 0x1f, 0x20, 255);
        SDL_RenderDrawRect(renderer, &rect);

        render_button_label(renderer, rect, button.label);
    }

    // Simple status indicator bar at bottom.
    if (busy_indicator_) {
        SDL_Rect indicator = bounds_;
        indicator.y = indicator.y + indicator.h - 6;
        indicator.h = 6;
        SDL_SetRenderDrawColor(renderer, 0xc0, 0x9a, 0x2b, 255);
        SDL_RenderFillRect(renderer, &indicator);
    }
}

bool SourceConfigPanel::handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEMOTION: {
            hover_button_ = hit_test_buttons(e.motion.x, e.motion.y);
            return hover_button_ >= 0;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (e.button.button != SDL_BUTTON_LEFT) return false;
            int index = hit_test_buttons(e.button.x, e.button.y);
            if (index >= 0) {
                return activate_button(index);
            }
            return false;
        }
        default:
            break;
    }
    return false;
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

    auto it = document_->animations_.find(animation_id_);
    if (it != document_->animations_.end()) {
        nlohmann::json parsed = nlohmann::json::parse(it->second, nullptr, false);
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
    current_source_ = config;
    payload_["source"] = build_source_json(config);
    update_number_of_frames();
    commit_payload();
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
        std::string target = trim_copy(config.name.value_or(config.path));
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
    auto it = document_->animations_.find(id);
    if (it == document_->animations_.end()) return std::nullopt;
    nlohmann::json parsed = nlohmann::json::parse(it->second, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return std::nullopt;
    return parsed;
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
        if (!document_->info_path_.empty()) {
            cached_asset_root_ = document_->info_path_.parent_path();
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
            if (has_extension(entry.path(), ".png")) {
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

std::vector<std::filesystem::path> SourceConfigPanel::collect_png_files(const std::filesystem::path& folder) const {
    std::vector<std::filesystem::path> files;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            if (has_extension(entry.path(), ".png")) {
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
    post_copy_process(copied);
}

void SourceConfigPanel::post_copy_process(const std::vector<std::filesystem::path>& out_files) const {
    if (out_files.empty()) return;
    if (!cropping_service_) return;
    try {
        cropping_service_->compute_union_bounds(out_files);
        cropping_service_->crop_images_with_bounds(out_files);
    } catch (const std::exception& ex) {
        SDL_Log("SourceConfigPanel: cropping failed: %s", ex.what());
    }
}

void SourceConfigPanel::layout_buttons() {
    int padding = 6;
    int button_height = std::max(24, (bounds_.h - padding * 5) / 4);
    int width = std::max(32, bounds_.w - padding * 2);
    int x = bounds_.x + padding;
    int y = bounds_.y + padding;
    for (auto& button : buttons_) {
        button.rect = SDL_Rect{x, y, width, button_height};
        y += button_height + padding;
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

int SourceConfigPanel::hit_test_buttons(int x, int y) const {
    for (size_t i = 0; i < buttons_.size(); ++i) {
        const SDL_Rect& rect = buttons_[i].rect;
        if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool SourceConfigPanel::activate_button(int index) {
    switch (index) {
        case 0:
            import_from_folder();
            return true;
        case 1:
            import_from_animation();
            return true;
        case 2:
            import_from_gif();
            return true;
        case 3:
            import_from_png_sequence();
            return true;
        default:
            break;
    }
    return false;
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
    std::string target = trim_copy(*selection);
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
    update_status("Linked frames from animation '" + target + "'");
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

    // GIF decoding is not implemented in this tooling build.
    SDL_Log("SourceConfigPanel: GIF import requested for %s, but decoding is not supported in this build.",
            file->string().c_str());
    update_status("GIF import not supported");
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
        if (has_extension(file, ".png")) {
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

}  // namespace animation_editor

