#include "AssetsManager.hpp"

#include "utils/ranged_color.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "asset/asset_types.hpp"
#include "audio/audio_engine.hpp"
#include "dev_mode/dev_controls.hpp"
#include "render/scene_renderer.hpp"
#include "render/image_effect_settings.hpp"
#include "world/chunk.hpp"
#include "render_pipeline/ScalingLogic.hpp"
#include "render_pipeline/render_asset/shading/RenderShadingStages.hpp"
#include "map_generation/room.hpp"
#include "utils/area.hpp"
#include "utils/input.hpp"
#include "utils/range_util.hpp"
#include "utils/text_style.hpp"
#include "utils/map_grid_settings.hpp"
#include "utils/transform_smoothing_settings.hpp"
#include "utils/quick_task_popup.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <execution>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <unordered_set>
#include <array>
#include <SDL.h>
#include <SDL_ttf.h>

namespace {

void dev_mode_trace(const std::string& message) {
    try {
        std::ofstream log("dev_mode_trace.log", std::ios::app);
        log << message << '\n';
    } catch (...) {

    }
}

struct SDLSurfaceDeleter {
    void operator()(SDL_Surface* surface) const {
        if (surface) {
            SDL_FreeSurface(surface);
        }
    }
};

TTF_Font* scaling_notice_font() {
    static std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(nullptr, TTF_CloseFont);
    if (!font) {
        const TextStyle& style = TextStyles::MediumMain();
        font.reset(style.open_font());
    }
    return font.get();
}

constexpr int kQualityOptions[] = {100, 75, 50, 25, 10};
constexpr int kMinRenderQuality = kQualityOptions[sizeof(kQualityOptions) / sizeof(kQualityOptions[0]) - 1];
constexpr std::size_t kNonPlayerParallelThreshold = 4;

int align_render_quality_percent(int percent) {
    int best = kQualityOptions[0];
    int best_diff = std::abs(percent - best);
    for (int option : kQualityOptions) {
        const int diff = std::abs(percent - option);
        if (diff < best_diff) {
            best_diff = diff;
            best = option;
        }
    }
    return best;
}

int halved_render_quality_percent(int percent) {
    if (percent <= kMinRenderQuality) {
        return kMinRenderQuality;
    }
    const int halved = static_cast<int>(std::lround(percent * 0.5));
    return std::max(kMinRenderQuality, align_render_quality_percent(halved));
}

struct AssetWorldBounds {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
};

bool compute_asset_world_bounds(const Asset* asset,
                                float camera_scale,
                                AssetWorldBounds& bounds) {
    if (!asset || !asset->info) {
        return false;
    }

    if (const auto& tiling = asset->tiling_info(); tiling && tiling->is_valid()) {
        bounds.left   = static_cast<float>(tiling->coverage.x);
        bounds.top    = static_cast<float>(tiling->coverage.y);
        bounds.right  = bounds.left + static_cast<float>(tiling->coverage.w);
        bounds.bottom = bounds.top + static_cast<float>(tiling->coverage.h);
        return true;
    }

    const int base_w = std::max(1, asset->info->original_canvas_width);
    const int base_h = std::max(1, asset->info->original_canvas_height);
    float scale_factor = 1.0f;
    if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
        scale_factor = asset->info->scale_factor;
    }

    const float width  = static_cast<float>(base_w) * scale_factor * camera_scale;
    const float height = static_cast<float>(base_h) * scale_factor * camera_scale;
    const float half_w = width * 0.5f;
    const float bottom = static_cast<float>(asset->pos.y);

    bounds.left   = static_cast<float>(asset->pos.x) - half_w;
    bounds.right  = static_cast<float>(asset->pos.x) + half_w;
    bounds.bottom = bottom;
    bounds.top    = bottom - height;
    return true;
}

}

Assets::Assets(std::vector<std::unique_ptr<Asset>>&& loaded,
               AssetLibrary& library,
               Asset*,
               std::vector<Room*> rooms,
               int screen_width_,
               int screen_height_,
               int screen_center_x,
               int screen_center_y,
               int map_radius,
               SDL_Renderer* renderer,
               const std::string& map_id,
               const nlohmann::json& map_manifest,
               std::string content_root,
               world::Grid&& world_grid)
    : camera_(
          screen_width_,
          screen_height_,
          Area(
              "starting_camera",
              std::vector<SDL_Point>{

                  SDL_Point{-100,-100},
                  SDL_Point{ 100,-100},
                  SDL_Point{ 100,100},
                  SDL_Point{-100, 100}
              },
              0)
      ),
      screen_width(screen_width_),
      screen_height(screen_height_),
      world_grid_(std::move(world_grid)),
      library_(library),
      map_id_(map_id),
      map_path_(std::move(content_root))
{
    perf_counter_frequency_ = static_cast<double>(SDL_GetPerformanceFrequency());
    last_frame_counter_     = SDL_GetPerformanceCounter();
    map_info_json_ = map_manifest;
    if (!map_info_json_.is_object()) {
        map_info_json_ = nlohmann::json::object();
    }

    hydrate_map_info_sections();
    load_camera_settings_from_json();

    InitializeAssets::initialize(*this, std::move(loaded), std::move(rooms), screen_width_, screen_height_, screen_center_x, screen_center_y, map_radius);

    finder_ = new CurrentRoomFinder(rooms_, player);
    if (finder_) {
        camera_.set_up_rooms(finder_);
    }

    auto current_room = [&]() -> Room* {
        if (finder_) {
            return finder_->getCurrentRoom();
        }
        return nullptr;
    };
    Room* intro_room = current_room();

    SDL_Point intro_center{screen_center_x, screen_center_y};
    if (player) {
        intro_center = SDL_Point{player->pos.x, player->pos.y};
    } else if (Room* room = intro_room) {
        if (room->room_area) {
            intro_center = room->room_area->get_center();
        }
    }
    camera_.set_screen_center(intro_center);
    if (player) {
        last_known_player_pos_ = SDL_Point{player->pos.x, player->pos.y};
        last_player_pos_valid_ = true;
    } else {
        last_player_pos_valid_ = false;
    }

    double intro_zoom = camera_.default_zoom_for_room(intro_room);
    if (!std::isfinite(intro_zoom) || intro_zoom <= 0.0) {
        intro_zoom = 1.0;
    }
    camera_.set_scale(static_cast<float>(intro_zoom));

    scene = new SceneRenderer(renderer, this, screen_width_, screen_height_, map_info_json_, map_id_);
    if (scene) {
        scene->set_dark_mask_enabled(render_dark_mask_enabled_);
    }
    apply_map_light_config();
    apply_map_grid_settings(map_grid_settings_, false);

    // Mark that we should defer the first active-assets rebuild
    // until the grid has computed active chunks on the first update.
    pending_initial_rebuild_ = true;
    logged_initial_rebuild_warning_ = false;
    moving_assets_for_grid_.clear();
    moving_assets_for_grid_.reserve(all.size());
    pending_static_grid_registration_.clear();
    movement_commands_buffer_.clear();
    movement_commands_buffer_.reserve(all.size());
    grid_registration_buffer_.clear();
    grid_registration_buffer_.reserve(4);
    for (Asset* a : all) {
        if (!a) continue;
        a->set_assets(this);
    }
    register_pending_static_assets();

    update_filtered_active_assets();

    // Initialize quick task popup
    quick_task_popup_ = std::make_unique<QuickTaskPopup>();
    if (manifest_store_fallback_) {
        quick_task_popup_->set_manifest_store(manifest_store_fallback_.get());
    }

}

std::vector<const Room::NamedArea*> Assets::current_room_trigger_areas() const {
    std::vector<const Room::NamedArea*> result;
    if (!current_room_) {
        return result;
    }

    const auto is_trigger_string = [](const std::string& value) {
        if (value.empty()) {
            return false;
        }
        std::string lowered;
        lowered.reserve(value.size());
        for (unsigned char ch : value) {
            lowered.push_back(static_cast<char>(std::tolower(ch)));
        }
        if (lowered == "trigger") {
            return true;
        }
        return lowered.find("trigger") != std::string::npos;
};

    for (const auto& entry : current_room_->areas) {
        if (!entry.area) {
            continue;
        }
        if (is_trigger_string(entry.kind) ||
            is_trigger_string(entry.type) ||
            is_trigger_string(entry.name)) {
            result.push_back(&entry);
        }
    }

    return result;
}

void Assets::save_map_info_json() {
    write_camera_settings_to_json();
    if (map_id_.empty()) {
        std::cerr << "[Assets] Unable to persist map manifest entry: map ID is empty.\n";
        return;
    }
    devmode::core::ManifestStore* store = manifest_store();
    if (!store) {
        std::cerr << "[Assets] Unable to persist map manifest entry: manifest store unavailable.\n";
        return;
    }
    if (!store->update_map_entry(map_id_, map_info_json_)) {
        std::cerr << "[Assets] Failed to persist map manifest entry for " << map_id_ << "\n";
    }
}

void Assets::persist_map_info_json() {
    save_map_info_json();
}

void Assets::hydrate_map_info_sections() {
    if (!map_info_json_.is_object()) {
        return;
    }

    const auto ensure_object = [&](const char* key) {
        auto it = map_info_json_.find(key);
        if (it == map_info_json_.end()) {
            map_info_json_[key] = nlohmann::json::object();
            return;
        }
        if (!it->is_object()) {
            std::cerr << "[Assets] map_info." << key << " expected to be an object. Resetting." << "\n";
            *it = nlohmann::json::object();
        }
};

    ensure_object("map_assets_data");
    ensure_object("map_boundary_data");
    ensure_object("rooms_data");
    ensure_object("trails_data");

    ensure_map_grid_settings(map_info_json_);
    map_grid_settings_ = MapGridSettings::from_json(&map_info_json_["map_grid_settings"]);

    auto light_it = map_info_json_.find("map_light_data");
    if (light_it != map_info_json_.end()) {
        if (!light_it->is_object()) {
            std::cerr << "[Assets] map_info.map_light_data expected to be an object. Removing invalid value.\n";
            map_info_json_.erase(light_it);
        } else {
            nlohmann::json& D = *light_it;
            if (!D.contains("radius"))    D["radius"] = 0;
            if (!D.contains("intensity")) D["intensity"] = 255;
            if (!D.contains("update_interval")) D["update_interval"] = 10;
            if (!D.contains("mult"))            D["mult"] = 0.0;
            if (!D.contains("fall_off"))        D["fall_off"] = 100;
            utils::color::RangedColor base_range{{255,255},{255,255},{255,255},{255,255}};
            if (auto parsed = utils::color::ranged_color_from_json(D.value("base_color", nlohmann::json{}))) {
                base_range = *parsed;
            }
            D["base_color"] = utils::color::ranged_color_to_json(base_range);

            if (!D.contains("keys") || !D["keys"].is_array() || D["keys"].empty()) {
                D["keys"] = nlohmann::json::array();
                D["keys"].push_back(nlohmann::json::array({ 0.0, D["base_color"] }));
            } else {
                auto& keys = D["keys"];
                for (auto& entry : keys) {
                    if (entry.is_array() && entry.size() >= 2) {
                        if (auto parsed = utils::color::ranged_color_from_json(entry[1])) {
                            entry[1] = utils::color::ranged_color_to_json(*parsed);
                        }
                    }
                }
            }
            utils::color::RangedColor default_map_color{{0, 0}, {0, 0}, {0, 0}, {255, 255}};
            utils::color::RangedColor map_color =
                utils::color::ranged_color_from_json(D.value("map_color", nlohmann::json{}))
                    .value_or(default_map_color);
            map_color = utils::color::clamp_ranged_color(map_color);
            D["map_color"] = utils::color::ranged_color_to_json(map_color);
            D.erase("min_opacity");
            D.erase("max_opacity");
        }
    }
}

void Assets::load_camera_settings_from_json() {
    if (!map_info_json_.is_object()) {
        return;
    }
    nlohmann::json& camera_settings = map_info_json_["camera_settings"];
    if (!camera_settings.is_object()) {
        camera_settings = nlohmann::json::object();
    }
    camera_.apply_camera_settings(camera_settings);
    camera_settings = camera_.camera_settings_to_json();
    apply_camera_runtime_settings();
}

void Assets::write_camera_settings_to_json() {
    if (!map_info_json_.is_object()) {
        return;
    }
    map_info_json_["camera_settings"] = camera_.camera_settings_to_json();
}

void Assets::on_camera_settings_changed() {
    apply_camera_runtime_settings();
    write_camera_settings_to_json();
    save_map_info_json();
}

void Assets::reload_camera_settings() {
    load_camera_settings_from_json();
}

int Assets::saved_render_quality_percent() const {
    const camera::RealismSettings& settings = camera_.realism_settings();
    const int clamped = std::clamp(settings.render_quality_percent, kMinRenderQuality, kQualityOptions[0]);
    return align_render_quality_percent(clamped);
}

int Assets::effective_render_quality_percent() const {
    int percent = saved_render_quality_percent();
    if (dev_mode && !force_high_quality_rendering_) {
        percent = halved_render_quality_percent(percent);
    }
    return percent;
}

void Assets::apply_camera_runtime_settings() {
    const int effective_percent = effective_render_quality_percent();
    const float quality_cap = static_cast<float>(effective_percent) / 100.0f;
    render_pipeline::ScalingLogic::SetQualityCap(quality_cap);
    if (scene) {
        const bool low_quality = (effective_percent < 100) && !force_high_quality_rendering_;
        scene->set_low_quality_rendering(low_quality);
    }
    const camera::RealismSettings& settings = camera_.realism_settings();
    update_motion_smoothing_settings(settings);
    camera_effects::image_effects::GlobalState effects_state{
        settings.foreground_effects,
        settings.background_effects
    };
    camera_effects::image_effects::set_global_state(effects_state);
}

TransformSmoothingParams Assets::sanitize_smoothing(const TransformSmoothingParams& params) {
    TransformSmoothingParams result = params;
    if (!std::isfinite(result.lerp_rate) || result.lerp_rate < 0.0f) {
        result.lerp_rate = 0.0f;
    }
    if (!std::isfinite(result.spring_frequency) || result.spring_frequency < 0.0f) {
        result.spring_frequency = 0.0f;
    }
    if (!std::isfinite(result.max_step) || result.max_step < 0.0f) {
        result.max_step = 0.0f;
    }
    if (!std::isfinite(result.snap_threshold) || result.snap_threshold < 0.0f) {
        result.snap_threshold = 0.0f;
    }
    switch (result.method) {
    case TransformSmoothingMethod::None:
    case TransformSmoothingMethod::Lerp:
    case TransformSmoothingMethod::CriticallyDampedSpring:
        break;
    default:
        result.method = TransformSmoothingMethod::None;
        break;
    }
    return result;
}

void Assets::update_motion_smoothing_settings(const camera::RealismSettings& settings) {
    constexpr float kMinTau = 1e-4f;
    auto build_translation_params = [&](const camera::RealismSettings& s) {
        TransformSmoothingParams result{};
        result.method = s.motion_smoothing_method;
        switch (result.method) {
        case TransformSmoothingMethod::Lerp:
            result.lerp_rate        = (s.motion_smoothing_tau > kMinTau)
                ? 1.0f / std::max(s.motion_smoothing_tau, kMinTau)
                : 0.0f;
            result.spring_frequency = 0.0f;
            break;
        case TransformSmoothingMethod::CriticallyDampedSpring:
            result.spring_frequency = std::max(0.0f, s.motion_smoothing_spring_frequency);
            result.lerp_rate        = 0.0f;
            break;
        case TransformSmoothingMethod::None:
        default:
            result.method = TransformSmoothingMethod::None;
            result.lerp_rate = result.spring_frequency = 0.0f;
            break;
        }
        result.max_step       = std::max(0.0f, s.motion_smoothing_max_step);
        result.snap_threshold = std::max(0.0f, s.motion_smoothing_snap_threshold);
        return sanitize_smoothing(result);
    };

    TransformSmoothingParams desired_motion = build_translation_params(settings);
    const bool smoothing_enabled = settings.smooth_motion_zoom &&
        desired_motion.method != TransformSmoothingMethod::None;

    auto params_equal = [](const TransformSmoothingParams& a, const TransformSmoothingParams& b) {
        constexpr float kEpsilon = 1e-4f;
        auto close = [kEpsilon](float x, float y) {
            return std::fabs(x - y) <= kEpsilon;
        };
        return a.method == b.method &&
            close(a.lerp_rate, b.lerp_rate) &&
            close(a.spring_frequency, b.spring_frequency) &&
            close(a.max_step, b.max_step) &&
            close(a.snap_threshold, b.snap_threshold);
    };

    if (!smoothing_cache_initialized_) {
        cached_enabled_translation_params_ = sanitize_smoothing(transform_smoothing::asset_translation_params());
        cached_enabled_scale_params_       = sanitize_smoothing(transform_smoothing::asset_scale_params());
        cached_enabled_alpha_params_       = sanitize_smoothing(transform_smoothing::asset_alpha_params());
        last_camera_motion_params_         = sanitize_smoothing(transform_smoothing::camera_center_params());
        last_asset_translation_params_     = cached_enabled_translation_params_;
        last_asset_scale_params_           = cached_enabled_scale_params_;
        last_asset_alpha_params_           = cached_enabled_alpha_params_;
        smoothing_cache_initialized_       = true;
    }

    if (smoothing_enabled) {
        cached_enabled_translation_params_ = desired_motion;
        if (cached_enabled_scale_params_.method == TransformSmoothingMethod::None) {
            cached_enabled_scale_params_ = sanitize_smoothing(transform_smoothing::asset_scale_params());
        }
        if (cached_enabled_alpha_params_.method == TransformSmoothingMethod::None) {
            cached_enabled_alpha_params_ = sanitize_smoothing(transform_smoothing::asset_alpha_params());
        }
    } else {
        cached_enabled_translation_params_ = desired_motion;
    }

    TransformSmoothingParams translation_to_apply = smoothing_enabled
        ? cached_enabled_translation_params_
        : TransformSmoothingParams{};
    TransformSmoothingParams scale_to_apply = smoothing_enabled
        ? cached_enabled_scale_params_
        : TransformSmoothingParams{};
    TransformSmoothingParams alpha_to_apply = smoothing_enabled
        ? cached_enabled_alpha_params_
        : TransformSmoothingParams{};

    if (!smoothing_enabled) {
        translation_to_apply.method = TransformSmoothingMethod::None;
        translation_to_apply.lerp_rate = translation_to_apply.spring_frequency = 0.0f;
        translation_to_apply.max_step = translation_to_apply.snap_threshold = 0.0f;
        scale_to_apply.method = TransformSmoothingMethod::None;
        scale_to_apply.lerp_rate = scale_to_apply.spring_frequency = 0.0f;
        scale_to_apply.max_step = scale_to_apply.snap_threshold = 0.0f;
        alpha_to_apply.method = TransformSmoothingMethod::None;
        alpha_to_apply.lerp_rate = alpha_to_apply.spring_frequency = 0.0f;
        alpha_to_apply.max_step = alpha_to_apply.snap_threshold = 0.0f;
    }

    translation_to_apply = sanitize_smoothing(translation_to_apply);
    scale_to_apply       = sanitize_smoothing(scale_to_apply);
    alpha_to_apply       = sanitize_smoothing(alpha_to_apply);

    const bool motion_changed       = !params_equal(desired_motion, last_camera_motion_params_);
    const bool translation_changed  = !params_equal(translation_to_apply, last_asset_translation_params_);
    const bool scale_changed        = !params_equal(scale_to_apply, last_asset_scale_params_);
    const bool alpha_changed        = !params_equal(alpha_to_apply, last_asset_alpha_params_);

    if (motion_changed) {
        transform_smoothing::set_camera_center_params(desired_motion);
        transform_smoothing::set_camera_zoom_params(desired_motion);
        last_camera_motion_params_ = desired_motion;
    }
    if (translation_changed) {
        transform_smoothing::set_asset_translation_params(translation_to_apply);
        last_asset_translation_params_ = translation_to_apply;
        if (smoothing_enabled) {
            cached_enabled_translation_params_ = translation_to_apply;
        }
    }
    if (scale_changed) {
        transform_smoothing::set_asset_scale_params(scale_to_apply);
        last_asset_scale_params_ = scale_to_apply;
        if (smoothing_enabled) {
            cached_enabled_scale_params_ = scale_to_apply;
        }
    }
    if (alpha_changed) {
        transform_smoothing::set_asset_alpha_params(alpha_to_apply);
        last_asset_alpha_params_ = alpha_to_apply;
        if (smoothing_enabled) {
            cached_enabled_alpha_params_ = alpha_to_apply;
        }
    }

    if (motion_changed || translation_changed || scale_changed || alpha_changed) {
        for (Asset* asset : all) {
            if (!asset) {
                continue;
            }
            asset->set_smoothing_params(translation_to_apply, scale_to_apply, alpha_to_apply);
        }
    }
}

void Assets::apply_map_light_config() {
    if (!scene) {
        return;
    }
    if (!map_info_json_.is_object()) {
        return;
    }
    auto it = map_info_json_.find("map_light_data");
    if (it != map_info_json_.end() && it->is_object()) {
        scene->apply_map_light_config(*it);
    }
}

bool Assets::on_map_light_changed() {
    apply_map_light_config();
    save_map_info_json();
    return true;
}

void Assets::set_update_map_light_enabled(bool enabled) {
    if (scene) {
        scene->set_update_map_light_enabled(enabled);
    }
}

bool Assets::update_map_light_enabled() const {
    return scene ? scene->update_map_light_enabled() : true;
}

Assets::~Assets() {
    movement_commands_buffer_.clear();
    grid_registration_buffer_.clear();

    // Legacy tiling caches removed.

    if (input) {
        input->clear_screen_to_world_mapper();
    }
    delete scene;
    scene = nullptr;
    delete finder_;
    delete dev_controls_;
    // quick_task_popup_ is unique_ptr, auto-deleted
}

AssetLibrary& Assets::library() {
    return library_;
}

const AssetLibrary& Assets::library() const {
    return library_;
}

void Assets::set_rooms(std::vector<Room*> rooms) {
    rooms_ = std::move(rooms);
    notify_rooms_changed();
}

std::vector<Room*>& Assets::rooms() {
    return rooms_;
}

const std::vector<Room*>& Assets::rooms() const {
    return rooms_;
}

void Assets::notify_rooms_changed() {
    ++rooms_generation_;
    if (finder_) {
        finder_->setRooms(rooms_);
    }
    if (dev_controls_) {
        dev_controls_->set_rooms(&rooms_, rooms_generation_);
    }
}

void Assets::refresh_active_asset_lists() {
    rebuild_active_assets_if_needed();
    // Keep dev tools behavior by delegating to the helper
    update_audio_camera_metrics();
    update_filtered_active_assets();
}

void Assets::update_audio_camera_metrics() {
    // Read current camera focus so audio stays consistent when dev mode toggles
    SDL_Point camera_focus = camera_.get_screen_center();
    auto update_audio_metrics = [&](Asset* asset) {
        if (!asset) return;
        const float dx = static_cast<float>(asset->pos.x - camera_focus.x);
        const float dy = static_cast<float>(asset->pos.y - camera_focus.y);
        asset->distance_from_camera = std::sqrt(dx * dx + dy * dy);
        asset->angle_from_camera = std::atan2(dy, dx);
    };

    if (player) {
        update_audio_metrics(player);
    }
    for (Asset* asset : active_assets) {
        update_audio_metrics(asset);
    }

    // Tick audio engine so positional audio tracks the player in runtime mode
    AudioEngine::instance().update();
}

void Assets::refresh_filtered_active_assets() {
    update_filtered_active_assets();
}

void Assets::update_filtered_active_assets() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        filtered_active_assets = active_assets;
        dev_controls_->filter_active_assets(filtered_active_assets);
        return;
    }

    filtered_active_assets.clear();
}

void Assets::reset_dev_controls_current_room_cache() {
    dev_controls_last_room_ = nullptr;
}

void Assets::sync_dev_controls_current_room(Room* room, bool force_refresh) {
    if (!dev_controls_) {
        return;
    }
    if (!force_refresh && dev_controls_last_room_ == room) {
        return;
    }
    dev_controls_last_room_ = room;
    dev_controls_->set_current_room(room, force_refresh);
}

void Assets::ensure_dev_controls() {
    if (dev_controls_) {
        return;
    }

    const char* msg_create = "[Assets] Creating Dev Controls";
    std::cout << msg_create << "\n";
    dev_mode_trace(msg_create);

    DevControls* created = nullptr;
    try {
        created = new DevControls(this, screen_width, screen_height);
    } catch (const std::exception& ex) {
        std::cout << "[Assets] Dev Controls constructor threw: " << ex.what() << "\n";
        dev_mode_trace(std::string{"[Assets] Dev Controls constructor threw: "} + ex.what());
        created = nullptr;
    } catch (...) {
        std::cout << "[Assets] Dev Controls constructor threw unknown error\n";
        dev_mode_trace("[Assets] Dev Controls constructor threw unknown error");
        created = nullptr;
    }

    if (!created) {
        const char* msg_fail = "[Assets] Failed to allocate Dev Controls";
        std::cout << msg_fail << "\n";
        dev_mode_trace(msg_fail);
        return;
    }

    dev_controls_ = created;
    const char* msg_constructed = "[Assets] Dev Controls constructed, wiring context";
    std::cout << msg_constructed << "\n";
    dev_mode_trace(msg_constructed);

    try {
        reset_dev_controls_current_room_cache();

        dev_mode_trace("[Assets] Dev Controls -> set_player");
        dev_controls_->set_player(player);
        dev_mode_trace("[Assets] Dev Controls -> set_active_assets");
        dev_controls_->set_active_assets(filtered_active_assets);
        dev_mode_trace("[Assets] Dev Controls -> sync_current_room");
        sync_dev_controls_current_room(current_room_, true);
        dev_mode_trace("[Assets] Dev Controls -> set_screen_dimensions");
        dev_controls_->set_screen_dimensions(screen_width, screen_height);
        dev_mode_trace("[Assets] Dev Controls -> set_rooms");
        dev_controls_->set_rooms(&rooms_, rooms_generation_);
        dev_mode_trace("[Assets] Dev Controls -> set_input");
        dev_controls_->set_input(input);
        dev_mode_trace("[Assets] Dev Controls -> set_map_info");
        dev_controls_->set_map_info(&map_info_json_, [this]() { return on_map_light_changed(); });
        dev_mode_trace("[Assets] Dev Controls -> set_map_context");
        dev_controls_->set_map_context(&map_info_json_, map_path_);
        dev_mode_trace("[Assets] Dev Controls wiring complete");
    } catch (const std::exception& ex) {
        std::cout << "[Assets] Failed to wire Dev Controls: " << ex.what() << "\n";
        dev_mode_trace(std::string{"[Assets] Failed to wire Dev Controls: "} + ex.what());
        // Roll back on failure
        delete dev_controls_;
        dev_controls_ = nullptr;
    } catch (...) {
        std::cout << "[Assets] Failed to wire Dev Controls: unknown error\n";
        dev_mode_trace("[Assets] Failed to wire Dev Controls: unknown error");
        delete dev_controls_;
        dev_controls_ = nullptr;
    }
}

void Assets::set_input(Input* m) {
    if (input && input != m) {
        input->clear_screen_to_world_mapper();
    }

    input = m;

    if (input) {
        input->set_screen_to_world_mapper([this](SDL_Point screen) {
            SDL_FPoint mapped = camera_.screen_to_map(screen);
            return SDL_Point{static_cast<int>(std::lround(mapped.x)), static_cast<int>(std::lround(mapped.y))};
        });
    }

    if (dev_controls_) {
        dev_controls_->set_input(m);
        if (dev_controls_->is_enabled()) {
            dev_controls_->set_player(player);
            dev_controls_->set_active_assets(filtered_active_assets);
            sync_dev_controls_current_room(current_room_);
            dev_controls_->set_screen_dimensions(screen_width, screen_height);
            dev_controls_->set_rooms(&rooms_, rooms_generation_);
            dev_controls_->set_map_context(&map_info_json_, map_path_);
        }
    }
}

void Assets::update(const Input& input)
{
    const std::uint64_t now_counter = SDL_GetPerformanceCounter();
    float dt = 1.0f / 60.0f;
    if (last_frame_counter_ != 0 && perf_counter_frequency_ > 0.0) {
        const double elapsed = static_cast<double>(now_counter - last_frame_counter_) / perf_counter_frequency_;
        if (std::isfinite(elapsed) && elapsed > 0.0) {
            dt = static_cast<float>(std::clamp(elapsed, 0.0, 0.25));
        }
    }
    last_frame_counter_    = now_counter;
    last_frame_dt_seconds_ = dt;

    const bool ctrl_down = input.isScancodeDown(SDL_SCANCODE_LCTRL) || input.isScancodeDown(SDL_SCANCODE_RCTRL);
    if (scene && ctrl_down && input.wasScancodePressed(SDL_SCANCODE_Q)) {
        scene->toggle_chunk_preview();
        std::cout << "[Assets] Chunk preview "
                  << (scene->chunk_preview_enabled() ? "enabled" : "disabled") << " (Ctrl+Q).\n";
    }

    if (ctrl_down && input.wasScancodePressed(SDL_SCANCODE_B)) {
        asset_boundary_box_display_enabled_ = !asset_boundary_box_display_enabled_;
        std::cout << "[Assets] Asset boundary box display "
                  << (asset_boundary_box_display_enabled_ ? "enabled" : "disabled") << " (Ctrl+B).\n";
    }

    // Quick Task popup Ctrl+T hotkey
    if (ctrl_down && input.wasScancodePressed(SDL_SCANCODE_T) && quick_task_popup_) {
        if (quick_task_popup_->is_open()) {
            quick_task_popup_->close();
        } else {
            quick_task_popup_->open();
        }
        std::cout << "[Assets] Quick Task popup "
                  << (quick_task_popup_->is_open() ? "opened" : "closed") << " (Ctrl+T).\n";
    }

    // Update popup
    if (quick_task_popup_) {
        quick_task_popup_->update();
    }

    // Flush any queued removals early in the frame so grid residency,
    // lighting caches, and selections stay in sync before other work.
    if (process_removals()) {
        mark_active_assets_dirty();
        rebuild_active_assets_if_needed();
        update_filtered_active_assets();
        if (dev_controls_ && dev_controls_->is_enabled()) {
            dev_controls_->set_active_assets(filtered_active_assets);
        }
    }

    Room* detected_room = finder_ ? finder_->getCurrentRoom() : nullptr;
    Room* active_room = detected_room;
    if (dev_controls_ && dev_controls_->is_enabled()) {
        active_room = dev_controls_->resolve_current_room(detected_room);
    }
    const bool room_changed = (current_room_ != active_room);
    current_room_ = active_room;

    dx = dy = 0;

    int start_px = player ? player->pos.x : 0;
    int start_py = player ? player->pos.y : 0;

    if (!dev_mode) {
        // Update player first and capture any movement
        if (player) player->update();
    }

    bool player_moved = false;
    if (player) {
        dx = player->pos.x - start_px;
        dy = player->pos.y - start_py;
        const bool moved_during_update = (dx != 0 || dy != 0);
        SDL_Point current_player_pos{player->pos.x, player->pos.y};
        const bool moved_since_last_frame =
            !last_player_pos_valid_ ||
            current_player_pos.x != last_known_player_pos_.x ||
            current_player_pos.y != last_known_player_pos_.y;

        last_known_player_pos_ = current_player_pos;
        last_player_pos_valid_ = true;

        player_moved = moved_during_update || moved_since_last_frame;
        if (!dev_mode && moved_during_update) {
            // Queue player movement for batched grid update
            movement_commands_buffer_.push_back(GridMovementCommand{
                player,
                SDL_Point{start_px, start_py},
                SDL_Point{player->pos.x, player->pos.y}
            });
        }
    } else {
        last_player_pos_valid_ = false;
    }

    // Update non-player assets and accumulate movement commands
    if (!dev_mode) {
        // Ensure we have a current buffer of non-player assets to update
        rebuild_non_player_update_buffer_if_needed();

        for (Asset* asset : non_player_update_buffer_) {
            if (!asset) continue;
            SDL_Point previous_pos{asset->pos.x, asset->pos.y};
            asset->update();
            if (previous_pos.x != asset->pos.x || previous_pos.y != asset->pos.y) {
                movement_commands_buffer_.push_back(GridMovementCommand{
                    asset,
                    previous_pos,
                    SDL_Point{asset->pos.x, asset->pos.y}
                });
            }
        }
    }

    // Apply any batched grid movement commands so chunk membership is accurate
    if (!movement_commands_buffer_.empty()) {
        for (const GridMovementCommand& cmd : movement_commands_buffer_) {
            if (!cmd.asset) continue;
            world_grid_.move_asset(cmd.asset, cmd.previous, cmd.current);
            cmd.asset->cache_grid_residency(SDL_Point{cmd.asset->pos.x, cmd.asset->pos.y});
        }
        movement_commands_buffer_.clear();
        // Clear any auxiliary buffers related to grid updates/registration
        moving_assets_for_grid_.clear();
        grid_registration_buffer_.clear();
    }

    const bool zoom_animation_active = camera_.zooming_;
    const bool camera_refresh_needed = room_changed || player_moved || zoom_animation_active;
    camera_.update_zoom(current_room_, finder_, player, camera_refresh_needed, last_frame_dt_seconds_);

    update_max_asset_dimensions();

    // Clear last-cycle culled overlay info
    culled_debug_rects_.clear();

    // Update the world grid using the current camera view before
    // filling the visible candidate buffer so chunk queries are fresh,
    // and parallax conversions are accurate for renderers/editors.
    world_grid_.update_active_chunks(screen_world_rect(), 0);
    world_grid_.update_parallax(camera_, last_frame_dt_seconds_);

    auto& new_active_assets = visible_candidate_buffer_;
    new_active_assets.clear();
    new_active_assets.reserve(active_assets.size() + 32);
    const std::size_t estimated_candidates =
        std::min<std::size_t>(all.size(), new_active_assets.capacity());
    active_candidate_lookup_.clear();
    active_candidate_lookup_.reserve(estimated_candidates);

    SDL_Rect screen_pixels{0, 0, screen_width, screen_height};
    const bool screen_has_area = screen_pixels.w > 0 && screen_pixels.h > 0;
    const float screen_left   = static_cast<float>(screen_pixels.x);
    const float screen_top    = static_cast<float>(screen_pixels.y);
    const float screen_right  = static_cast<float>(screen_pixels.x + screen_pixels.w);
    const float screen_bottom = static_cast<float>(screen_pixels.y + screen_pixels.h);
    const auto intersects_screen = [&](const SDL_FRect& rect) {
        if (!screen_has_area) {
            return false;
        }
        const float rect_right  = rect.x + rect.w;
        const float rect_bottom = rect.y + rect.h;
        return !(rect_right <= screen_left ||
                 screen_right <= rect.x ||
                 rect_bottom <= screen_top ||
                 screen_bottom <= rect.y);
    };

    auto push_debug_rect = [&](const SDL_FRect& rect) {
        SDL_Rect debug{
            static_cast<int>(std::floor(rect.x)),
            static_cast<int>(std::floor(rect.y)),
            static_cast<int>(std::ceil(rect.w)),
            static_cast<int>(std::ceil(rect.h))
        };
        if (debug.w < 0) {
            debug.w = 0;
        }
        if (debug.h < 0) {
            debug.h = 0;
        }
        culled_debug_rects_.push_back(debug);
    };

    auto push_active_asset = [&](Asset* asset) {
        if (!asset) {
            return;
        }
        if (!active_candidate_lookup_.insert(asset).second) {
            return;
        }
        new_active_assets.push_back(asset);
    };

    auto consider_asset = [&](Asset* asset) {
        if (!asset || !asset->info) {
            return;
        }
        SDL_FRect screen_bounds{};
        if (!asset_bounds_in_screen_space(asset, screen_bounds)) {
            return;
        }
        if (!intersects_screen(screen_bounds)) {
            push_debug_rect(screen_bounds);
            return;
        }
        push_active_asset(asset);
    };

    const auto& chunks = world_grid_.active_chunks();
    if (chunks.empty()) {
        for (Asset* asset : all) {
            consider_asset(asset);
        }
    } else {
        for (const world::Chunk* chunk : chunks) {
            if (!chunk) {
                continue;
            }
            for (Asset* asset : chunk->assets) {
                consider_asset(asset);
            }
        }
    }

    // Fallback: assets whose expanded bounds intersect the screen might live in chunks that
    // weren't activated (for example, lights with offsets/radii much larger than the sprite).
    // Catch those by testing all remaining assets directly.
    if (screen_has_area) {
        for (Asset* asset : all) {
            if (!asset) {
                continue;
            }
            if (active_candidate_lookup_.find(asset) != active_candidate_lookup_.end()) {
                continue;
            }
            SDL_FRect screen_bounds{};
            if (!asset_bounds_in_screen_space(asset, screen_bounds)) {
                continue;
            }
            if (!intersects_screen(screen_bounds)) {
                continue;
            }
            push_active_asset(asset);
        }
    }

    std::sort(new_active_assets.begin(),
              new_active_assets.end(),
              [](const Asset* lhs, const Asset* rhs) {
                  if (lhs == rhs) {
                      return false;
                  }
                  if (!lhs || !rhs) {
                      return rhs != nullptr;
                  }
                  if (lhs->z_index == rhs->z_index) {
                      return lhs < rhs;
                  }
                  return lhs->z_index < rhs->z_index;
              });

    std::vector<Asset*> new_light_assets;
    std::vector<Asset*> new_static_lights;
    std::vector<Asset*> new_moving_lights;
    new_light_assets.reserve(new_active_assets.size());
    new_static_lights.reserve(new_active_assets.size());
    new_moving_lights.reserve(new_active_assets.size());

    for (Asset* asset : new_active_assets) {
        if (!asset || !asset->info) {
            continue;
        }
        const auto& info = asset->info;
        if (info->light_sources.empty()) {
            continue;
        }
        new_light_assets.push_back(asset);
        if (info->moving_asset) {
            new_moving_lights.push_back(asset);
        } else {
            new_static_lights.push_back(asset);
        }
    }

    const bool static_changed = (new_static_lights != active_static_light_assets_);
    const bool moving_changed = (new_moving_lights != active_moving_light_assets_);

    if (static_changed) {
        notify_light_map_static_assets_changed();
    }

    if (moving_changed) {
        scratch_moving_light_lookup_.clear();
        for (Asset* asset : new_moving_lights) {
            scratch_moving_light_lookup_.insert(asset);
            if (active_moving_light_lookup_.find(asset) == active_moving_light_lookup_.end()) {
                notify_light_map_asset_moved(asset);
            }
        }

        for (Asset* asset : active_moving_light_assets_) {
            if (scratch_moving_light_lookup_.find(asset) == scratch_moving_light_lookup_.end()) {
                notify_light_map_asset_moved(asset);
            }
        }

        active_moving_light_lookup_.swap(scratch_moving_light_lookup_);
        scratch_moving_light_lookup_.clear();
    }

    active_assets.swap(new_active_assets);
    new_active_assets.clear();
    active_light_assets_        = std::move(new_light_assets);
    active_static_light_assets_ = std::move(new_static_lights);
    active_moving_light_assets_ = std::move(new_moving_lights);
    active_assets_dirty_.store(false, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
    rebuild_non_player_update_buffer_if_needed();

    // Update per-asset camera-relative audio metrics and audio engine each frame
    update_audio_camera_metrics();

    // Keep dev-mode filtered lists in sync with the freshly rebuilt visibility
    // state so overlays/highlights render correctly.
    update_filtered_active_assets();
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->set_active_assets(filtered_active_assets);
        sync_dev_controls_current_room(current_room_);
        dev_controls_->update(input);
        // Update dev-mode UI panels (e.g., asset library, camera panel) after world state
        // has been processed so their layouts and button states stay in sync.
        dev_controls_->update_ui(input);
    }

    // Apply pending static registrations/removals before querying the grid.
    register_pending_static_assets();
    if (process_removals()) {
        mark_active_assets_dirty();
        rebuild_active_assets_if_needed();
        update_filtered_active_assets();
        if (dev_controls_ && dev_controls_->is_enabled()) {
            dev_controls_->set_active_assets(filtered_active_assets);
        }
    }

    // World grid was refreshed earlier, prior to building visibility.

    if (!suppress_render_ && scene) {
        scene->render();
    }

    render_overlays(renderer());
}

void Assets::rebuild_non_player_update_buffer_if_needed() {
    if (!non_player_update_buffer_dirty_.load(std::memory_order_acquire)) {
        return;
    }

#if defined(__cpp_lib_execution)
    const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const bool can_parallelize = hardware_threads > 1 && active_assets.size() >= kNonPlayerParallelThreshold;
    if (can_parallelize) {
        std::vector<Asset*> rebuilt(active_assets.size());
        std::atomic_size_t next_index{0};
        std::for_each(std::execution::par_unseq,
                      active_assets.begin(),
                      active_assets.end(),
                      [&](Asset* asset) {
                          if (asset && asset != player) {
                              const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
                              rebuilt[index] = asset;
                          }
                      });
        const std::size_t final_count = next_index.load(std::memory_order_relaxed);
        rebuilt.resize(final_count);
        non_player_update_buffer_ = std::move(rebuilt);
        non_player_update_buffer_dirty_.store(false, std::memory_order_release);
        return;
    }
#endif

    non_player_update_buffer_.clear();
    non_player_update_buffer_.reserve(active_assets.size());
    for (Asset* asset : active_assets) {
        if (asset && asset != player) {
            non_player_update_buffer_.push_back(asset);
        }
    }
    non_player_update_buffer_dirty_.store(false, std::memory_order_release);
}

void Assets::invalidate_max_asset_dimensions() {
    max_asset_dimensions_dirty_ = true;
}

void Assets::update_max_asset_dimensions() {
    const float camera_scale = std::max(0.0001f, camera_.get_scale());
    bool zoom_changed = cached_zoom_level_ <= 0.0f;
    if (!zoom_changed && cached_zoom_level_ > 0.0f) {
        const float delta = std::fabs(camera_scale - cached_zoom_level_) / std::max(cached_zoom_level_, 0.0001f);
        zoom_changed = delta > 0.05f;
    }
    if (!max_asset_dimensions_dirty_ && !zoom_changed) {
        return;
    }

    cached_zoom_level_ = camera_scale;
    max_asset_dimensions_dirty_ = false;

    float max_height = 0.0f;
    float max_width  = 0.0f;
    for (Asset* asset : all) {
        if (!asset || !asset->info) {
            continue;
        }
        if (asset->info->tillable) {
            continue;
        }
        float scale_factor = 1.0f;
        if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
            scale_factor = asset->info->scale_factor;
        }
        const float width =
            static_cast<float>(std::max(1, asset->info->original_canvas_width)) * scale_factor * camera_scale;
        const float height =
            static_cast<float>(std::max(1, asset->info->original_canvas_height)) * scale_factor * camera_scale;
        max_width  = std::max(max_width, width);
        max_height = std::max(max_height, height);
    }

    if (max_width <= 0.0f) {
        max_width = static_cast<float>(screen_width);
    }
    if (max_height <= 0.0f) {
        max_height = static_cast<float>(screen_height);
    }

    max_asset_width_world_  = max_width;
    max_asset_height_world_ = max_height;
}

SDL_Rect Assets::screen_world_rect() const {
    const Area view = camera_.get_camera_area();
    auto [minx, miny, maxx, maxy] = view.get_bounds();
    SDL_Rect rect{
        minx,
        miny,
        std::max(0, maxx - minx),
        std::max(0, maxy - miny)
    };
    return rect;
}

int Assets::audio_effect_max_distance_world() const {
    const_cast<Assets*>(this)->update_max_asset_dimensions();
    const float horizontal_padding = std::max(0.0f, max_asset_width_world_ * 1.5f);
    const float bottom_padding     = std::max(0.0f, max_asset_height_world_);
    const float radius             = std::max(horizontal_padding, bottom_padding);
    return std::max(1, static_cast<int>(std::ceil(radius)));
}

// Enable/disable developer mode. In dev mode we may lower
// effective render quality (unless forced high quality is set)
// and ensure DevControls are available.
void Assets::set_dev_mode(bool mode) {
    if (dev_mode == mode) {
        return;
    }

    if (mode) {
        bool enabled_ok = false;
        try {
            ensure_dev_controls();
            if (dev_controls_) {
                dev_controls_->set_enabled(true);
                enabled_ok = true;
            }
        } catch (const std::exception& ex) {
            std::cerr << "[Assets] Failed to enable Dev Mode: " << ex.what() << "\n";
            enabled_ok = false;
        } catch (...) {
            std::cerr << "[Assets] Failed to enable Dev Mode: unknown error\n";
            enabled_ok = false;
        }

        if (enabled_ok) {
            dev_mode = true;
            show_dev_notice("Dev Mode enabled (Ctrl+D to toggle)", 2000);
        } else {
            // Ensure we remain in a consistent runtime state on failure
            dev_mode = false;
            if (dev_controls_) {
                try { dev_controls_->set_enabled(false); } catch (...) {}
            }
            show_dev_notice("Dev Mode failed to enable", 2000);
        }
    } else {
        // Disabling is simple and should not throw; guard just in case.
        try {
            if (dev_controls_) {
                dev_controls_->set_enabled(false);
            }
        } catch (...) {
        }
        dev_mode = false;
        show_dev_notice("Dev Mode disabled", 1500);
    }

    // Update renderer quality caps based on (possibly changed) mode
    apply_camera_runtime_settings();
}

// Force high quality rendering (disables quality halving in dev mode).
void Assets::set_force_high_quality_rendering(bool enable) {
    if (force_high_quality_rendering_ == enable) {
        return;
    }
    force_high_quality_rendering_ = enable;
    apply_camera_runtime_settings();
}

// Toggle the dark mask (dynamic darkness overlay)
void Assets::set_render_dark_mask_enabled(bool enabled) {
    if (render_dark_mask_enabled_ == enabled) {
        return;
    }
    render_dark_mask_enabled_ = enabled;
    if (scene) {
        scene->set_dark_mask_enabled(enabled);
    }
}

// Temporarily suppress heavy rendering work (e.g., during menus or smooth camera transitions).
void Assets::set_render_suppressed(bool suppressed) {
    if (suppress_render_ == suppressed) {
        return;
    }
    suppress_render_ = suppressed;

    if (scene) {
        if (suppressed) {
            // Reduce load while suppressed
            scene->set_low_quality_rendering(true);
            scene->set_update_map_light_enabled(false);
        } else {
            // Restore lighting updates and quality policy
            scene->set_update_map_light_enabled(true);
            apply_camera_runtime_settings();
        }
    }
}

// Accessors used by UI/engine code
const std::vector<Asset*>& Assets::getActive() const {
    return active_assets;
}

const std::vector<Asset*>& Assets::getFilteredActiveAssets() const {
    return filtered_active_assets;
}

void Assets::initialize_active_assets(SDL_Point /*center*/) {
    // Seed the active lists conservatively with all assets; subsequent
    // updates will refine visibility, ordering, and lighting state.
    active_assets.clear();
    active_assets.reserve(all.size());
    for (Asset* a : all) {
        if (a) {
            active_assets.push_back(a);
        }
    }

    // Build light asset subsets
    std::vector<Asset*> new_light_assets;
    std::vector<Asset*> new_static_lights;
    std::vector<Asset*> new_moving_lights;
    new_light_assets.reserve(active_assets.size());
    new_static_lights.reserve(active_assets.size());
    new_moving_lights.reserve(active_assets.size());
    for (Asset* asset : active_assets) {
        if (!asset || !asset->info) {
            continue;
        }
        if (asset->info->light_sources.empty()) {
            continue;
        }
        new_light_assets.push_back(asset);
        if (asset->info->moving_asset) {
            new_moving_lights.push_back(asset);
        } else {
            new_static_lights.push_back(asset);
        }
    }

    active_light_assets_        = std::move(new_light_assets);
    active_static_light_assets_ = std::move(new_static_lights);
    active_moving_light_assets_ = std::move(new_moving_lights);
    active_assets_dirty_.store(false, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
}

void Assets::mark_active_assets_dirty() {
    active_assets_dirty_.store(true, std::memory_order_release);
}

Asset* Assets::spawn_asset(const std::string& name, SDL_Point world_pos) {
    // Lookup metadata
    std::shared_ptr<AssetInfo> info = library_.get(name);
    if (!info) {
        return nullptr;
    }

    // Determine owning room label (if available)
    std::string owning_room = map_id_;
    if (current_room_) {
        owning_room = current_room_->room_name;
    }

    // Build a minimal area carrying the owning room name; geometry not required here
    Area spawn_area(owning_room, /*resolution*/ 0);

    // Choose a basic depth; prefer info->z_threshold if it exists
    int depth = 0;
    try {
        depth = info->z_threshold;
    } catch (...) {
        depth = 0;
    }

    // Create and wire the asset
    auto uptr = std::make_unique<Asset>(info, spawn_area, world_pos, depth, nullptr, std::string{}, std::string{}, map_grid_settings_.spacing());
    Asset* raw = uptr.get();
    if (!raw) {
        return nullptr;
    }
    raw->set_assets(this);
    raw->set_camera(&camera_);
    raw->finalize_setup();

    // Register with grid and containers
    world_grid_.register_asset(raw);
    owned_assets.emplace_back(std::move(uptr));
    all.push_back(raw);

    // Invalidate caches and mark dirty so next cycle rebuilds active lists
    invalidate_max_asset_dimensions();
    mark_active_assets_dirty();
    mark_non_player_update_buffer_dirty();

    // Return the raw pointer for immediate usage by caller
    return raw;
}

const std::vector<Asset*>& Assets::get_selected_assets() const {
    static std::vector<Asset*> empty;
    if (dev_controls_ && dev_controls_->is_enabled()) {
        return dev_controls_->get_selected_assets();
    }
    return empty;
}

const std::vector<Asset*>& Assets::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    if (dev_controls_ && dev_controls_->is_enabled()) {
        return dev_controls_->get_highlighted_assets();
    }
    return empty;
}

Asset* Assets::get_hovered_asset() const {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        return dev_controls_->get_hovered_asset();
    }
    return nullptr;
}

void Assets::notify_light_map_asset_moved(const Asset* /*asset*/) {
    // Minimal implementation: leave as no-op. The light map will be
    // refreshed on next frame via SceneRenderer.
}

void Assets::notify_light_map_static_assets_changed() {
    // Minimal implementation: leave as no-op.
}

void Assets::track_asset_for_grid(Asset* asset) {
    if (!asset) {
        return;
    }
    world_grid_.register_asset(asset);
}

void Assets::untrack_asset_for_grid(Asset* asset) {
    if (!asset) {
        return;
    }
    world_grid_.unregister_asset(asset);
}

void Assets::register_pending_static_assets() {
    if (pending_static_grid_registration_.empty()) {
        return;
    }
    for (Asset* asset : pending_static_grid_registration_) {
        if (asset) {
            world_grid_.register_asset(asset);
        }
    }
    pending_static_grid_registration_.clear();
}

bool Assets::rebuild_active_assets_if_needed() {
    const bool dirty = active_assets_dirty_.load(std::memory_order_acquire) || pending_initial_rebuild_;
    if (!dirty) {
        return false;
    }
    pending_initial_rebuild_ = false;
    active_assets_dirty_.store(false, std::memory_order_release);
    initialize_active_assets(camera_.get_screen_center());
    return true;
}

bool Assets::asset_bounds_in_screen_space(const Asset* asset, SDL_FRect& out_rect) const {
    if (!asset || !asset->info) {
        return false;
    }
    const Asset::BoundsSquare& base = asset->base_bounds_local();
    if (!base.valid()) {
        return false;
    }

    float world_x = asset->smoothed_translation_x();
    float world_y = asset->smoothed_translation_y();
    if (dev_mode) {
        world_x = static_cast<float>(asset->pos.x);
        world_y = static_cast<float>(asset->pos.y);
    }

    float asset_scale = asset->smoothed_scale();
    if (!std::isfinite(asset_scale) || asset_scale <= 0.0f) {
        asset_scale = 1.0f;
    }

    float local_center_x = base.center_x;
    if (asset->flipped) {
        local_center_x = -local_center_x;
    }
    const float local_center_y = base.center_y;
    const float scaled_half    = base.half_size * asset_scale;

    const float world_center_x = world_x + local_center_x * asset_scale;
    const float world_center_y = world_y + local_center_y * asset_scale;

    const float left_world   = world_center_x - scaled_half;
    const float right_world  = world_center_x + scaled_half;
    const float top_world    = world_center_y - scaled_half;
    const float bottom_world = world_center_y + scaled_half;

    SDL_FPoint top_left_screen = camera_.map_to_screen_f(SDL_FPoint{left_world, top_world});
    SDL_FPoint bottom_right_screen = camera_.map_to_screen_f(SDL_FPoint{right_world, bottom_world});

    const float width  = bottom_right_screen.x - top_left_screen.x;
    const float height = bottom_right_screen.y - top_left_screen.y;
    if (!(width > 0.0f) || !(height > 0.0f)) {
        return false;
    }
    if (!std::isfinite(top_left_screen.x) || !std::isfinite(top_left_screen.y) ||
        !std::isfinite(width) || !std::isfinite(height)) {
        return false;
    }

    // Base sprite bounds in screen space
    SDL_FRect sprite_rect{
        top_left_screen.x,
        top_left_screen.y,
        width,
        height
    };

    // Expand to include owned light sources so visibility/culling accounts for
    // both the asset pixels and any emitted light textures.
    // Treat layers as a single combined asset boundary.
    SDL_FRect combined = sprite_rect;

    if (asset->info && !asset->info->light_sources.empty()) {
        const int   base_w_px = std::max(1, asset->info->original_canvas_width);
        const int   base_h_px = std::max(1, asset->info->original_canvas_height);
        const float sx        = combined.w / static_cast<float>(base_w_px);
        const float sy        = combined.h / static_cast<float>(base_h_px);

        // Bottom-center anchor in screen space (matches light renderer anchor)
        const float base_center_x = combined.x + combined.w * 0.5f;
        const float base_center_y = combined.y + combined.h;

        auto expand_to_include = [&](const SDL_FRect& r) {
            const float left   = std::min(combined.x, r.x);
            const float top    = std::min(combined.y, r.y);
            const float right  = std::max(combined.x + combined.w, r.x + r.w);
            const float bottom = std::max(combined.y + combined.h, r.y + r.h);
            combined.x = left;
            combined.y = top;
            combined.w = std::max(0.0f, right - left);
            combined.h = std::max(0.0f, bottom - top);
        };

        for (const auto& light : asset->info->light_sources) {
            const int raw_radius = light.radius;
            if (raw_radius <= 0) {
                continue;
            }

            // Apply horizontal flip to offset when the asset is flipped
            const float off_x = static_cast<float>(asset->flipped ? -light.offset_x : light.offset_x);
            const float off_y = static_cast<float>(light.offset_y);

            // Screen-space center of the light relative to the asset
            const float cx = base_center_x + off_x * sx;
            const float cy = base_center_y + off_y * sy;

            // Scale radius according to current on-screen asset scaling
            const float rx = std::max(1.0f, static_cast<float>(raw_radius) * sx);
            const float ry = std::max(1.0f, static_cast<float>(raw_radius) * sy);

            SDL_FRect light_rect{
                cx - rx,
                cy - ry,
                rx * 2.0f,
                ry * 2.0f
            };

            expand_to_include(light_rect);
        }
    }

    out_rect = combined;
    return true;
}

void Assets::schedule_removal(Asset* a) {
    if (!a) {
        return;
    }
    std::lock_guard<std::mutex> lock(removal_queue_mutex_);
    removal_queue.push_back(a);
}

bool Assets::process_removals() {
    std::vector<Asset*> pending_removals;
    {
        std::lock_guard<std::mutex> lock(removal_queue_mutex_);
        if (removal_queue.empty()) {
            return false;
        }
        pending_removals.swap(removal_queue);
    }

    if (pending_removals.empty()) {
        return false;
    }

    for (Asset* asset : pending_removals) {
        render_pipeline::shading::ClearShadowStateFor(asset);

        untrack_asset_for_grid(asset);
        world_grid_.unregister_asset(asset);
        if (asset) {
            asset->clear_grid_residency_cache();
        }
        if (asset && asset->info && !asset->info->light_sources.empty()) {
            if (asset->info->moving_asset) {
                notify_light_map_asset_moved(asset);
            } else {
                notify_light_map_static_assets_changed();
            }
        }
    }

    std::unordered_set<Asset*> removal_lookup(pending_removals.begin(), pending_removals.end());

    for (auto it = owned_assets.begin(); it != owned_assets.end();) {
        if (removal_lookup.count(it->get()) > 0) {
            it = owned_assets.erase(it);
        } else {
            ++it;
        }
    }

    auto erase_ptrs = [&removal_lookup](auto& vec) {
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [&removal_lookup](auto* candidate) {
                               return removal_lookup.count(candidate) > 0;
                           }),
            vec.end());
};

    erase_ptrs(all);
    erase_ptrs(active_assets);
    erase_ptrs(active_light_assets_);
    erase_ptrs(active_static_light_assets_);
    erase_ptrs(active_moving_light_assets_);
    erase_ptrs(filtered_active_assets);
    erase_ptrs(moving_assets_for_grid_);
    erase_ptrs(pending_static_grid_registration_);
    mark_non_player_update_buffer_dirty();

    for (Asset* asset : pending_removals) {
        active_moving_light_lookup_.erase(asset);
        scratch_moving_light_lookup_.erase(asset);
    }

    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->clear_selection();
    }

    invalidate_max_asset_dimensions();
    
    return true;
}

void Assets::render_overlays(SDL_Renderer* renderer) {
    if (renderer && dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->render_overlays(renderer);
    }

    // Render quick task popup
    if (quick_task_popup_) {
        quick_task_popup_->render(renderer);
    }

    if (!renderer) {
        return;
    }

    // Draw debug overlays for culled objects (if any)
    if (!culled_debug_rects_.empty()) {
        SDL_BlendMode prev_mode = SDL_BLENDMODE_NONE;
        SDL_GetRenderDrawBlendMode(renderer, &prev_mode);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 160);
        for (const SDL_Rect& r : culled_debug_rects_) {
            SDL_RenderDrawRect(renderer, &r);
        }
        SDL_SetRenderDrawBlendMode(renderer, prev_mode);
    }

    if (asset_boundary_box_display_enabled_) {
        const std::vector<Asset*>& overlay_assets =
            (dev_controls_ && dev_controls_->is_enabled()) ? filtered_active_assets : active_assets;
        if (!overlay_assets.empty()) {
            SDL_BlendMode previous_mode = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(renderer, &previous_mode);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 255, 180, 200);
            for (Asset* asset : overlay_assets) {
                if (!asset) {
                    continue;
                }
                SDL_FRect screen_rect;
                if (!asset_bounds_in_screen_space(asset, screen_rect)) {
                    continue;
                }
                SDL_Rect draw_rect{
                    static_cast<int>(std::floor(screen_rect.x)),
                    static_cast<int>(std::floor(screen_rect.y)),
                    static_cast<int>(std::ceil(screen_rect.w)),
                    static_cast<int>(std::ceil(screen_rect.h))
                };
                if (draw_rect.w <= 0 || draw_rect.h <= 0) {
                    continue;
                }
                SDL_RenderDrawRect(renderer, &draw_rect);
            }
            SDL_SetRenderDrawBlendMode(renderer, previous_mode);
        }
    }

    if (dev_notice_) {
        const Uint32 now = SDL_GetTicks();
        if (now >= dev_notice_->expiry_ms) {
            dev_notice_->texture.reset();
            dev_notice_.reset();
        }
    }

    if (!dev_notice_) {
        return;
    }

    DevNotice& notice = *dev_notice_;

    if (!notice.texture || notice.dirty) {
        TTF_Font* font = scaling_notice_font();
        if (!font) {
            return;
        }

        SDL_Color color{255, 255, 255, 255};
        std::unique_ptr<SDL_Surface, SDLSurfaceDeleter> surface( TTF_RenderUTF8_Blended(font, notice.message.c_str(), color));
        if (!surface) {
            return;
        }

        SDL_Texture* rebuilt_texture = SDL_CreateTextureFromSurface(renderer, surface.get());
        if (!rebuilt_texture) {
            return;
        }

        notice.texture.reset(rebuilt_texture);
        notice.texture_width = surface->w;
        notice.texture_height = surface->h;
        notice.dirty = false;
    }

    SDL_Texture* texture = notice.texture.get();
    if (!texture) {
        return;
    }

    const int padding_x = 16;
    const int padding_y = 10;
    SDL_Rect dest{0, 0, notice.texture_width, notice.texture_height};
    dest.x = (screen_width - dest.w) / 2;
    dest.x = std::clamp(dest.x, 0, std::max(0, screen_width - dest.w));
    dest.y = std::max(10, screen_height / 10);

    SDL_Rect background{
        dest.x - padding_x,
        dest.y - padding_y,
        dest.w + padding_x * 2,
        dest.h + padding_y * 2
};

    background.x = std::clamp(background.x, 0, std::max(0, screen_width - background.w));
    background.y = std::clamp(background.y, 0, std::max(0, screen_height - background.h));
    dest.x = background.x + (background.w - dest.w) / 2;
    dest.y = background.y + (background.h - dest.h) / 2;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_RenderFillRect(renderer, &background);

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
}

// Legacy chunk tiling implementation removed.

SDL_Renderer* Assets::renderer() const {
    return scene ? scene->get_renderer() : nullptr;
}

bool Assets::scene_light_map_only_mode() const {
    // If we have a scene, report its light-map-only mode state.
    // In that mode, SceneRenderer performs SDL_RenderPresent itself.
    return scene ? scene->light_map_only_mode() : false;
}

std::optional<Asset::TilingInfo> Assets::compute_tiling_for_asset(const Asset* asset) const {
    if (!asset || !asset->info) {
        return std::nullopt;
    }
    if (!asset->info->tillable) {
        return std::nullopt;
    }

    // Determine grid step
    int step = map_grid_settings_.spacing();
    // Fallback: scaled canvas dimension if spacing not configured
    if (step <= 0) {
        const int raw_w = std::max(1, asset->info->original_canvas_width);
        const int raw_h = std::max(1, asset->info->original_canvas_height);
        double scale = 1.0;
        if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
            scale = static_cast<double>(asset->info->scale_factor);
        }
        step = std::max(1, static_cast<int>(std::lround(static_cast<double>(std::max(raw_w, raw_h)) * scale)));
    }
    step = std::max(1, step);

    // Compute the world footprint of the non-tiled sprite (bottom-center anchor)
    const SDL_Point world_pos{ asset->pos.x, asset->pos.y };
    const int base_w = std::max(1, asset->info->original_canvas_width);
    const int base_h = std::max(1, asset->info->original_canvas_height);
    double scale = 1.0;
    if (std::isfinite(asset->info->scale_factor) && asset->info->scale_factor > 0.0f) {
        scale = static_cast<double>(asset->info->scale_factor);
    }
    const int scaled_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * scale)));

    const int left   = world_pos.x - (scaled_w / 2);
    const int top    = world_pos.y - scaled_h;
    const int right  = left + scaled_w;
    const int bottom = world_pos.y;

    auto align_down = [](int value, int step_) {
        if (step_ <= 0) return value;
        const double scaled = std::floor(static_cast<double>(value) / static_cast<double>(step_));
        return static_cast<int>(scaled * static_cast<double>(step_));
    };
    auto align_up = [](int value, int step_) {
        if (step_ <= 0) return value;
        const double scaled = std::ceil(static_cast<double>(value) / static_cast<double>(step_));
        return static_cast<int>(scaled * static_cast<double>(step_));
    };

    const int origin_x = align_down(left, step);
    const int origin_y = align_down(top, step);
    const int limit_x  = align_up(right, step);
    const int limit_y  = align_up(bottom, step);

    Asset::TilingInfo tiling{};
    tiling.enabled    = true;
    tiling.tile_size  = SDL_Point{ step, step };
    tiling.grid_origin = SDL_Point{ origin_x, origin_y };
    tiling.anchor = SDL_Point{ align_down(world_pos.x, step) + step / 2,
                               align_down(world_pos.y, step) + step / 2 };

    const int coverage_w = std::max(step, limit_x - origin_x);
    const int coverage_h = std::max(step, limit_y - origin_y);
    tiling.coverage = SDL_Rect{ origin_x, origin_y, coverage_w, coverage_h };

    return tiling.is_valid() ? std::optional<Asset::TilingInfo>(tiling) : std::nullopt;
}

Asset* Assets::find_asset_by_name(const std::string& name) const {
    if (name.empty()) {
        return nullptr;
    }
    for (Asset* asset : active_assets) {
        if (asset && asset->info && asset->info->name == name) {
            return asset;
        }
    }
    for (Asset* asset : all) {
        if (asset && asset->info && asset->info->name == name) {
            return asset;
        }
    }
    return nullptr;
}

bool Assets::contains_asset(const Asset* asset) const {
    if (!asset) {
        return false;
    }

    if (std::find(all.begin(), all.end(), asset) != all.end()) {
        return true;
    }

    return std::any_of(owned_assets.begin(), owned_assets.end(), [asset](const auto& candidate) {
        return candidate.get() == asset;
    });
}

Global_Light_Source* Assets::map_light_source() {
    if (!scene) {
        return nullptr;
    }
    return &scene->map_light_source();
}

const Global_Light_Source* Assets::map_light_source() const {
    return const_cast<Assets*>(this)->map_light_source();
}

LightMap* Assets::light_map() {
    return scene ? scene->light_map() : nullptr;
}

const LightMap* Assets::light_map() const {
    return scene ? scene->light_map() : nullptr;
}

void Assets::force_shaded_assets_rerender() {
    std::unordered_set<Asset*> visited;
    auto flush_asset = [&](Asset* asset) {
        if (!asset || visited.count(asset) > 0) {
            return;
        }
        visited.insert(asset);
        asset->clear_render_caches();
        if (asset->get_final_texture()) {
            asset->set_final_texture(nullptr);
        }
};

    for (Asset* asset : all) {
        flush_asset(asset);
    }
    for (const auto& owned : owned_assets) {
        flush_asset(owned.get());
    }
    for (Asset* asset : active_assets) {
        flush_asset(asset);
    }

    active_assets_dirty_.store(true, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
}

bool Assets::apply_lighting_grid_subdivide(int subdivisions) {
    (void)subdivisions;
    return false;
}

void Assets::apply_map_grid_settings(const MapGridSettings& settings, bool persist_json) {
    MapGridSettings sanitized = settings;
    sanitized.clamp();

    const bool chunk_changed = sanitized.r_chunk != map_grid_settings_.r_chunk;
    map_grid_settings_ = sanitized;

    if (persist_json) {
        nlohmann::json& section = map_info_json_["map_grid_settings"];
        sanitized.apply_to_json(section);
    }

    world_grid_.set_chunk_resolution(std::max(0, sanitized.r_chunk));
    world_grid_.set_parallax_resolution(std::max(0, sanitized.resolution));

    if (chunk_changed) {
        for (Asset* asset : all) {
            if (!asset) {
                continue;
            }
            asset->clear_grid_residency_cache();
        }
    }

    for (Asset* asset : all) {
        if (!asset || asset->has_grid_residency_cache()) {
            continue;
        }
        world_grid_.register_asset(asset);
        asset->cache_grid_residency(SDL_Point{asset->pos.x, asset->pos.y});
    }

    if (chunk_changed) {
        update_max_asset_dimensions();
        world_grid_.update_active_chunks(screen_world_rect(), 0);
        force_shaded_assets_rerender();
    }
}

int Assets::map_grid_chunk_resolution() const {
    return std::max(0, map_grid_settings_.r_chunk);
}

void Assets::set_map_light_panel_visible(bool visible) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->set_map_light_panel_visible(visible);
    }
}

bool Assets::is_map_light_panel_visible() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_map_light_panel_visible();
}

void Assets::toggle_asset_library() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->toggle_asset_library();
    }
}

void Assets::open_asset_library() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_asset_library();
    }
}

void Assets::close_asset_library() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->close_asset_library();
    }
}

bool Assets::is_asset_library_open() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_asset_library_open();
}

void Assets::toggle_room_config() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->toggle_room_config();
    }
}

void Assets::close_room_config() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->close_room_config();
    }
}

bool Assets::is_room_config_open() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_room_config_open();
}

std::shared_ptr<AssetInfo> Assets::consume_selected_asset_from_library() {
    if (!dev_controls_ || !dev_controls_->is_enabled()) return nullptr;
    return dev_controls_->consume_selected_asset_from_library();
}

void Assets::open_asset_info_editor(const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_asset_info_editor(info);
    }
}

void Assets::open_asset_info_editor_for_asset(Asset* a) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_asset_info_editor_for_asset(a);
    }
}

void Assets::finalize_asset_drag(Asset* a, const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->finalize_asset_drag(a, info);
    }
}

void Assets::close_asset_info_editor() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->close_asset_info_editor();
    }
}

bool Assets::is_asset_info_editor_open() const {
    return dev_controls_ && dev_controls_->is_enabled() && dev_controls_->is_asset_info_editor_open();
}

bool Assets::is_asset_info_lighting_section_expanded() const {
    // Reuse dev controls' lighting-section visibility logic
    // Only true when DevControls are enabled, in RoomEditor mode,
    // and the AssetInfo Lighting section is expanded
    return dev_controls_ && dev_controls_->is_enabled() &&
           dev_controls_->is_asset_info_lighting_section_expanded();
}

void Assets::clear_editor_selection() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->clear_selection();
    }
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    // Handle quick task popup first (if open)
    if (quick_task_popup_ && quick_task_popup_->is_open()) {
        if (quick_task_popup_->handle_event(e)) {
            // Ensure underlying input state does not react to this event
            if (input) {
                input->consumeEvent(e);
            }
            return; // Popup consumed the event
        }
    }

    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->handle_sdl_event(e);
    }
}

void Assets::focus_camera_on_asset(Asset* a, double zoom_factor, int duration_steps) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->focus_camera_on_asset(a, zoom_factor, duration_steps);
    }
}

void Assets::begin_area_edit_for_selected_asset(const std::string& area_name) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->begin_area_edit_for_selected_asset(area_name);
    }
}

void Assets::begin_room_area_edit(const std::string& area_name) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        // New helper to start room-scoped Area edit
        // We rely on DevControls to resolve current room
        // and open AreaOverlayEditor in room mode
        try {
            // Implemented in DevControls
            dev_controls_->begin_room_area_edit(area_name);
        } catch (...) {
        }
    }
}

void Assets::begin_frame_editor_session(Asset* asset,
                                        std::shared_ptr<animation_editor::AnimationDocument> document,
                                        std::shared_ptr<animation_editor::PreviewProvider> preview,
                                        const std::string& animation_id,
                                        animation_editor::AnimationEditorWindow* host_to_toggle) {
    ensure_dev_controls();
    if (dev_controls_) {
        dev_controls_->begin_frame_editor_session(asset, std::move(document), std::move(preview), animation_id, host_to_toggle);
    }
}

devmode::core::ManifestStore* Assets::manifest_store() {
    if (dev_controls_) {
        auto& store = dev_controls_->manifest_store();
        return &store;
    }
    if (!manifest_store_fallback_) {
        manifest_store_fallback_ = std::make_unique<devmode::core::ManifestStore>();
    }
    return manifest_store_fallback_.get();
}

const devmode::core::ManifestStore* Assets::manifest_store() const {
    return const_cast<Assets*>(this)->manifest_store();
}

void Assets::notify_spawn_group_config_changed(const nlohmann::json& entry) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->notify_spawn_group_config_changed(entry);
    }
}

void Assets::notify_spawn_group_removed(const std::string& spawn_id) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->notify_spawn_group_removed(spawn_id);
    }
}

void Assets::show_dev_notice(const std::string& message, Uint32 duration_ms) {
    if (message.empty()) {
        if (dev_notice_) {
            dev_notice_->texture.reset();
            dev_notice_.reset();
        }
        return;
    }

    if (!dev_notice_) {
        dev_notice_.emplace();
    }

    dev_notice_->message = message;
    dev_notice_->expiry_ms = SDL_GetTicks() + duration_ms;
    dev_notice_->texture.reset();
    dev_notice_->texture_width = 0;
    dev_notice_->texture_height = 0;
    dev_notice_->dirty = true;
}

void Assets::set_editor_current_room(Room* room) {
    current_room_ = room;
    if (dev_controls_) {
        sync_dev_controls_current_room(room, true);
    }
}

void Assets::open_animation_editor_for_asset(const std::shared_ptr<AssetInfo>& info) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->open_animation_editor_for_asset(info);
    }
}
