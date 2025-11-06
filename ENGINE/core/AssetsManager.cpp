#include "AssetsManager.hpp"

#include "utils/ranged_color.hpp"
#include "asset/initialize_assets.hpp"

#include "find_current_room.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "asset/asset_utils.hpp"
#include "audio/audio_engine.hpp"
#include "dev_mode/dev_controls.hpp"
#include "render/scene_renderer.hpp"
#include "world/chunk.hpp"
#include "render/light_map_manager.hpp"
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

                  SDL_Point{-10,-10},
                  SDL_Point{ 10,-10},
                  SDL_Point{ 10,10},
                  SDL_Point{-10, 10}
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

    scene = new SceneRenderer(renderer, this, screen_width_, screen_height_, map_info_json_, map_id_);
    notify_reactive_shadow_settings_available();
    apply_map_light_config();
    apply_map_grid_settings(map_grid_settings_, false);
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
    ensure_object("map_light_data");
    ensure_object("rooms_data");
    ensure_object("trails_data");

    ensure_map_grid_settings(map_info_json_);
    map_grid_settings_ = MapGridSettings::from_json(&map_info_json_["map_grid_settings"]);

    {
        nlohmann::json& L = map_info_json_["map_light_data"];
        if (!L.is_object()) {
            map_info_json_["map_light_data"] = nlohmann::json::object();
        }
        nlohmann::json& D = map_info_json_["map_light_data"];
        if (!D.contains("radius"))          D["radius"] = 0;
        if (!D.contains("intensity"))       D["intensity"] = 255;
        auto clamp_radius = [](int v) { return std::max(0, std::min(20000, v)); };
        int orbit_radius = 0;
        if (D.contains("orbit_radius")) {
            try {
                orbit_radius = clamp_radius(D["orbit_radius"].get<int>());
            } catch (...) {
                orbit_radius = 0;
            }
        }
        int orbit_x = orbit_radius;
        if (D.contains("orbit_x")) {
            try {
                orbit_x = clamp_radius(D["orbit_x"].get<int>());
            } catch (...) {
                orbit_x = orbit_radius;
            }
        }
        int orbit_y = orbit_x;
        if (D.contains("orbit_y")) {
            try {
                orbit_y = clamp_radius(D["orbit_y"].get<int>());
            } catch (...) {
                orbit_y = orbit_x;
            }
        }
        D["orbit_x"] = orbit_x;
        D["orbit_y"] = orbit_y;
        D["orbit_radius"] = std::max(orbit_x, orbit_y);
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
        D.erase("screen_light");
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
    update_motion_smoothing_settings(camera_.realism_settings());
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
        auto close = [](float x, float y) {
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

    if (input) {
        input->clear_screen_to_world_mapper();
    }
    notify_reactive_shadow_settings_about_to_change();
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

    AudioEngine::instance().update();
    update_filtered_active_assets();
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
    dev_controls_ = new DevControls(this, screen_width, screen_height);
    if (!dev_controls_) {
        const char* msg_fail = "[Assets] Failed to allocate Dev Controls";
        std::cout << msg_fail << "\n";
        dev_mode_trace(msg_fail);
        return;
    }
    const char* msg_constructed = "[Assets] Dev Controls constructed, wiring context";
    std::cout << msg_constructed << "\n";
    dev_mode_trace(msg_constructed);

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

    dev_controls_->refresh_reactive_shadow_settings();
}

void Assets::set_input(Input* m) {
    if (input && input != m) {
        input->clear_screen_to_world_mapper();
    }

    input = m;

    if (input) {
        input->set_screen_to_world_mapper([this](SDL_Point screen, float parallax_x, float parallax_y) {
            SDL_FPoint mapped = camera_.screen_to_map(screen, parallax_x, parallax_y);
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
        if (player) player->update();
    }

    bool player_moved = false;
    if (player) {
        dx = player->pos.x - start_px;
        dy = player->pos.y - start_py;
        player_moved = (dx != 0 || dy != 0);
    }

    const bool zoom_animation_active = camera_.zooming_;
    const bool camera_refresh_needed = room_changed || player_moved || zoom_animation_active;
    camera_.update_zoom(current_room_, finder_, player, camera_refresh_needed, last_frame_dt_seconds_);

    const Area view = camera_.get_camera_area();
    auto [minx, miny, maxx, maxy] = view.get_bounds();
    SDL_Rect cam_rect{minx, miny, std::max(0, maxx - minx), std::max(0, maxy - miny)};
    world_grid_.update_active_chunks(cam_rect, camera_.get_render_distance_world_margin());

    update_active_assets(camera_.get_screen_center());
    const bool rebuilt_active_assets = rebuild_active_assets_if_needed();
    if (rebuilt_active_assets) {
        update_filtered_active_assets();
    }

    AudioEngine& audio_engine = AudioEngine::instance();
    const float effect_max_distance =
        static_cast<float>(std::max(1, camera_.get_render_distance_world_margin()));
    if (!last_audio_effect_max_distance_.has_value() ||
        *last_audio_effect_max_distance_ != effect_max_distance) {
        audio_engine.set_effect_max_distance(effect_max_distance);
        last_audio_effect_max_distance_ = effect_max_distance;
        std::cout << "[Assets] Audio effect max distance updated to " << effect_max_distance << "\n";
    }
    if (!dev_mode) {
        rebuild_non_player_update_buffer_if_needed();

        const std::size_t task_count = non_player_update_buffer_.size();
        if (task_count == 1) {
            non_player_update_buffer_.front()->update();
        } else if (task_count > 1) {
#if defined(__cpp_lib_execution)
            const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
            const bool can_parallelize = hardware_threads > 1 && task_count >= kNonPlayerParallelThreshold;
            if (can_parallelize) {
                std::for_each(std::execution::par_unseq,
                              non_player_update_buffer_.begin(),
                              non_player_update_buffer_.end(),
                              [](Asset* asset) {
                                  if (asset) {
                                      asset->update();
                                  }
                              });
            } else
#endif
            {
                for (Asset* asset : non_player_update_buffer_) {
                    if (asset) {
                        asset->update();
                    }
                }
            }
        }
    }

    register_pending_static_assets();

    if (!moving_assets_for_grid_.empty()) {
        movement_commands_buffer_.clear();
        movement_commands_buffer_.reserve(moving_assets_for_grid_.size());
        grid_registration_buffer_.clear();
        if (grid_registration_buffer_.capacity() < 4) {
            grid_registration_buffer_.reserve(4);
        }

#if defined(__cpp_lib_execution)
        if (moving_assets_for_grid_.size() > 1) {
            std::mutex movement_commands_mutex;
            std::mutex registration_mutex;
            std::for_each(std::execution::par_unseq,
                          moving_assets_for_grid_.begin(),
                          moving_assets_for_grid_.end(),
                          [&](Asset* asset) {
                              if (!asset) {
                                  return;
                              }
                              SDL_Point curr{asset->pos.x, asset->pos.y};
                              if (!asset->has_grid_residency_cache()) {
                                  std::lock_guard<std::mutex> reg_lock(registration_mutex);
                                  grid_registration_buffer_.push_back(asset);
                                  return;
                              }
                              const SDL_Point prev = asset->grid_residency_cache();
                              if (prev.x == curr.x && prev.y == curr.y) {
                                  return;
                              }
                              GridMovementCommand command{asset, prev, curr};
                              std::lock_guard<std::mutex> lock(movement_commands_mutex);
                              movement_commands_buffer_.push_back(command);
                          });
        } else
#endif
        {
            for (Asset* asset : moving_assets_for_grid_) {
                if (!asset) {
                    continue;
                }
                SDL_Point curr{asset->pos.x, asset->pos.y};
                if (!asset->has_grid_residency_cache()) {
                    grid_registration_buffer_.push_back(asset);
                    continue;
                }
                const SDL_Point prev = asset->grid_residency_cache();
                if (prev.x == curr.x && prev.y == curr.y) {
                    continue;
                }
                movement_commands_buffer_.push_back(GridMovementCommand{asset, prev, curr});
            }
        }

        if (!grid_registration_buffer_.empty()) {
            std::sort(grid_registration_buffer_.begin(), grid_registration_buffer_.end());
            grid_registration_buffer_.erase(
                std::unique(grid_registration_buffer_.begin(), grid_registration_buffer_.end()),
                grid_registration_buffer_.end());
            for (Asset* asset : grid_registration_buffer_) {
                if (!asset || asset->has_grid_residency_cache()) {
                    continue;
                }
                SDL_Point curr{asset->pos.x, asset->pos.y};
                world_grid_.register_asset(asset);
                asset->cache_grid_residency(curr);
            }
        }

        for (const GridMovementCommand& command : movement_commands_buffer_) {
            if (!command.asset) {
                continue;
            }
            world_grid_.move_asset(command.asset, command.previous, command.current);
            command.asset->cache_grid_residency(command.current);
        }
    }

    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->set_player(player);
        dev_controls_->set_active_assets(filtered_active_assets);
        sync_dev_controls_current_room(current_room_);
        dev_controls_->set_screen_dimensions(screen_width, screen_height);
        dev_controls_->set_rooms(&rooms_, rooms_generation_);
        dev_controls_->update(input);
        dev_controls_->update_ui(input);
    }

    if (scene && !suppress_render_) scene->render();

    process_removals();
}

void Assets::set_dev_mode(bool mode) {
    const bool changed = (dev_mode != mode);
    dev_mode = mode;

    force_high_quality_rendering_ = false;
    update_scene_render_quality();

    if (dev_mode) {
        if (SDL_Renderer* renderer_ptr = renderer()) {
            library_.ensureAllAnimationsLoaded(renderer_ptr);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[Assets] Dev mode asset cache skipped: renderer unavailable.");
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        if (changed) {
        dev_mode_trace("[Assets] Dev Mode enabled: using low-quality rendering for responsiveness.");
        std::cout << "[Assets] Dev Mode enabled: using low-quality rendering for responsiveness.\n";
    }
        dev_mode_trace("[Assets] Enabling Dev Controls");
        std::cout << "[Assets] Enabling Dev Controls\n";
        ensure_dev_controls();
        if (dev_controls_) {
            dev_mode_trace("[Assets] Dev Controls acquired, toggling on");
            std::cout << "[Assets] Dev Controls acquired, toggling on\n";
            dev_controls_->set_enabled(true);
            dev_controls_->set_player(player);
            dev_controls_->set_active_assets(filtered_active_assets);
            sync_dev_controls_current_room(current_room_, true);
            dev_controls_->set_screen_dimensions(screen_width, screen_height);
            dev_controls_->set_rooms(&rooms_, rooms_generation_);
            dev_controls_->set_input(input);
            dev_controls_->set_map_info(&map_info_json_, [this]() { return on_map_light_changed(); });
            dev_controls_->set_map_context(&map_info_json_, map_path_);
            dev_controls_->resolve_current_room(current_room_);
        }
        refresh_filtered_active_assets();
        dev_mode_trace("[Assets] Dev Controls enabled");
        std::cout << "[Assets] Dev Controls enabled\n";
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");
        if (changed) {
            std::cout << "[Assets] Dev Mode disabled: restoring high render quality.\n";
        }
        if (dev_controls_) {
            dev_mode_trace("[Assets] Disabling Dev Controls");
            std::cout << "[Assets] Disabling Dev Controls\n";
            dev_controls_->set_enabled(false);
            dev_controls_->clear_selection();
            reset_dev_controls_current_room_cache();
        }
        update_filtered_active_assets();
    }
}

void Assets::set_force_high_quality_rendering(bool enable) {
    if (force_high_quality_rendering_ == enable) {
        return;
    }
    force_high_quality_rendering_ = enable;
    update_scene_render_quality();
}

void Assets::update_scene_render_quality() {
    apply_camera_runtime_settings();
}

void Assets::set_render_suppressed(bool suppressed) {
    suppress_render_ = suppressed;
}

const std::vector<Asset*>& Assets::getActive() const {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        return filtered_active_assets;
    }
    return active_assets;
}

const std::vector<Asset*>& Assets::getFilteredActiveAssets() const {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        return filtered_active_assets;
    }
    return active_assets;
}

const std::vector<Asset*>& Assets::get_selected_assets() const {
    static std::vector<Asset*> empty;
    return (dev_controls_ && dev_controls_->is_enabled())
               ? dev_controls_->get_selected_assets() : empty;
}

const std::vector<Asset*>& Assets::get_highlighted_assets() const {
    static std::vector<Asset*> empty;
    return (dev_controls_ && dev_controls_->is_enabled())
               ? dev_controls_->get_highlighted_assets() : empty;
}

Asset* Assets::get_hovered_asset() const {
    return (dev_controls_ && dev_controls_->is_enabled())
               ? dev_controls_->get_hovered_asset() : nullptr;
}

nlohmann::json Assets::save_current_room(std::string ) {

    return nlohmann::json::object();
}

void Assets::addAsset(const std::string& name, SDL_Point g) {
    std::cout << "\n[Assets::addAsset] Request to create asset '" << name
              << "' at grid (" << g.x << ", " << g.y << ")\n";

    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets::addAsset][Error] No asset info found for '" << name << "'\n";
        return;
    }
    std::cout << "[Assets::addAsset] Retrieved AssetInfo '" << info->name
              << "' at " << info << "\n";

    Area spawn_area(name, SDL_Point{g.x, g.y}, 1, 1, "Point", 1, 1, 1);
    std::cout << "[Assets::addAsset] Created Area '" << spawn_area.get_name() << "' at (" << g.x << ", " << g.y << ")\n";

    size_t prev_size = owned_assets.size();

    owned_assets.emplace_back(
        std::make_unique<Asset>(info, spawn_area, SDL_Point{g.x, g.y}, 0, nullptr));

    if (owned_assets.size() <= prev_size) {
        std::cerr << "[Assets::addAsset][Error] owned_assets did not grow!\n";
        return;
    }

    Asset* newAsset = owned_assets.back().get();
    if (!newAsset) {
        std::cerr << "[Assets::addAsset][Error] Asset allocation failed for '" << name << "'\n";
        return;
    }
    std::cout << "[Assets::addAsset][Debug] New Asset allocated at " << newAsset
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>") << ")\n";

    all.push_back(newAsset);
    std::cout << "[Assets::addAsset] all.size() now = " << all.size() << "\n";

    try {
        set_camera_recursive(newAsset, &camera_);
        set_assets_owner_recursive(newAsset, this);
        std::cout << "[Assets::addAsset] View set successfully\n";
        newAsset->finalize_setup();
        std::cout << "[Assets::addAsset] Finalize setup successful\n";
    } catch (const std::exception& e) {
        std::cerr << "[Assets::addAsset][Exception] " << e.what() << "\n";
    }

    register_pending_static_assets();

    initialize_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();
    update_filtered_active_assets();

    std::cout << "[Assets::addAsset] Active assets=" << active_assets.size() << "\n";

    std::cout << "[Assets::addAsset] Successfully added asset '" << name
              << "' at (" << g.x << ", " << g.y << ")\n";
}

Asset* Assets::spawn_asset(const std::string& name, SDL_Point world_pos) {
    std::cout << "\n[Assets::spawn_asset] Request to spawn asset '" << name
              << "' at world (" << world_pos.x << ", " << world_pos.y << ")\n";

    auto info = library_.get(name);
    if (!info) {
        std::cerr << "[Assets::spawn_asset][Error] No asset info found for '" << name << "'\n";
        return nullptr;
    }
    std::cout << "[Assets::spawn_asset] Retrieved AssetInfo '" << info->name
              << "' at " << info << "\n";

    Area spawn_area(name, SDL_Point{world_pos.x, world_pos.y}, 1, 1, "Point", 1, 1, 1);
    std::cout << "[Assets::spawn_asset] Created Area '" << spawn_area.get_name() << "' at (" << world_pos.x << ", " << world_pos.y << ")\n";

    size_t prev_size = owned_assets.size();
    owned_assets.emplace_back( std::make_unique<Asset>(info, spawn_area, world_pos, 0, nullptr));

    if (owned_assets.size() <= prev_size) {
        std::cerr << "[Assets::spawn_asset][Error] owned_assets did not grow!\n";
        return nullptr;
    }

    Asset* newAsset = owned_assets.back().get();
    if (!newAsset) {
        std::cerr << "[Assets::spawn_asset][Error] Asset allocation failed for '" << name << "'\n";
        return nullptr;
    }

    std::cout << "[Assets::spawn_asset][Debug] New Asset allocated at " << newAsset
              << " (info=" << (newAsset->info ? newAsset->info->name : "<null>") << ")\n";

    all.push_back(newAsset);
    std::cout << "[Assets::spawn_asset] all.size() now = " << all.size() << "\n";

    try {
        set_camera_recursive(newAsset, &camera_);
        set_assets_owner_recursive(newAsset, this);
        std::cout << "[Assets::spawn_asset] View set successfully\n";
        newAsset->finalize_setup();
        std::cout << "[Assets::spawn_asset] Finalize setup successful\n";
    } catch (const std::exception& e) {
        std::cerr << "[Assets::spawn_asset][Exception] " << e.what() << "\n";
    }

    register_pending_static_assets();

    initialize_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();
    update_filtered_active_assets();

    std::cout << "[Assets::spawn_asset] Active assets=" << active_assets.size() << "\n";

    std::cout << "[Assets::spawn_asset] Successfully spawned asset '" << name
              << "' at (" << world_pos.x << ", " << world_pos.y << ")\n";

    return newAsset;
}

void Assets::mark_active_assets_dirty() {
    active_assets_dirty_.store(true, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
}

void Assets::notify_light_map_asset_moved(const Asset* asset) {
    if (!asset || !asset->info || asset->info->light_sources.empty()) {
        return;
    }
    if (!asset->info->moving_asset) {
        return;
    }
    if (LightMap* map = light_map()) {
        map->mark_asset_lights_dirty(asset);
    }
}

void Assets::notify_light_map_static_assets_changed() {
    if (LightMap* map = light_map()) {
        map->mark_static_cache_dirty();
    }
}

void Assets::track_asset_for_grid(Asset* asset) {
    if (!asset || !asset->info) {
        return;
    }

    if (asset->info->moving_asset) {
        if (std::find(moving_assets_for_grid_.begin(), moving_assets_for_grid_.end(), asset) == moving_assets_for_grid_.end()) {
            moving_assets_for_grid_.push_back(asset);
        }
        if (!asset->has_grid_residency_cache()) {
            SDL_Point curr{asset->pos.x, asset->pos.y};
            world_grid_.register_asset(asset);
            asset->cache_grid_residency(curr);
        }
        return;
    }

    if (asset->has_grid_residency_cache()) {
        return;
    }

    if (std::find(pending_static_grid_registration_.begin(),
                  pending_static_grid_registration_.end(),
                  asset) == pending_static_grid_registration_.end()) {
        pending_static_grid_registration_.push_back(asset);
    }
}

void Assets::untrack_asset_for_grid(Asset* asset) {
    if (!asset) {
        return;
    }

    auto erase_ptr = [asset](auto& vec) {
        vec.erase(std::remove(vec.begin(), vec.end(), asset), vec.end());
    };

    erase_ptr(moving_assets_for_grid_);
    erase_ptr(pending_static_grid_registration_);
}

void Assets::register_pending_static_assets() {
    if (pending_static_grid_registration_.empty()) {
        return;
    }

    std::vector<Asset*> still_pending;
    still_pending.reserve(pending_static_grid_registration_.size());
    for (Asset* asset : pending_static_grid_registration_) {
        if (!asset) {
            continue;
        }
        if (!asset->has_grid_residency_cache()) {
            SDL_Point curr{asset->pos.x, asset->pos.y};
            world_grid_.register_asset(asset);
            asset->cache_grid_residency(curr);
        }
        if (asset && !asset->has_grid_residency_cache()) {
            still_pending.push_back(asset);
        }
    }
    pending_static_grid_registration_.swap(still_pending);
}

void Assets::notify_reactive_shadow_settings_about_to_change() {
    if (dev_controls_) {
        dev_controls_->clear_reactive_shadow_settings();
    }
}

void Assets::notify_reactive_shadow_settings_available() {
    if (dev_controls_) {
        dev_controls_->refresh_reactive_shadow_settings();
    }
}

void Assets::initialize_active_assets(SDL_Point center) {
    const int radius = active_search_radius();
    active_asset_list_ = std::make_unique<AssetList>(
        all,
        center,
        radius,
        std::vector<std::string>{},
        std::vector<std::string>{},
        std::vector<std::string>{},
        SortMode::ZIndexAsc);
    active_assets_dirty_.store(true, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
}

void Assets::update_active_assets(SDL_Point center) {
    if (!active_asset_list_) {
        initialize_active_assets(center);
        return;
    }

    active_asset_list_->set_center(center);
    active_asset_list_->set_search_radius(active_search_radius());
    active_asset_list_->update();
    active_assets_dirty_.store(true, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
}

bool Assets::rebuild_active_assets_if_needed() {
    if (!active_asset_list_) {
        initialize_active_assets(camera_.get_screen_center());
    }

    if (!active_asset_list_ || !active_assets_dirty_.load(std::memory_order_acquire)) {
        return false;
    }

    std::vector<Asset*> new_active_assets;
    active_asset_list_->full_list(new_active_assets);

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

    active_assets                = std::move(new_active_assets);
    active_light_assets_         = std::move(new_light_assets);
    active_static_light_assets_  = std::move(new_static_lights);
    active_moving_light_assets_  = std::move(new_moving_lights);
    active_assets_dirty_.store(false, std::memory_order_release);
    mark_non_player_update_buffer_dirty();
    rebuild_non_player_update_buffer_if_needed();
    return true;
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

int Assets::active_search_radius() const {
    return std::max(1, camera_.get_render_distance_world_margin());
}

void Assets::schedule_removal(Asset* a) {
    if (!a) {
        return;
    }
    std::lock_guard<std::mutex> lock(removal_queue_mutex_);
    removal_queue.push_back(a);
}

void Assets::process_removals() {
    std::vector<Asset*> pending_removals;
    {
        std::lock_guard<std::mutex> lock(removal_queue_mutex_);
        if (removal_queue.empty()) {
            return;
        }
        pending_removals.swap(removal_queue);
    }

    if (pending_removals.empty()) {
        return;
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
        dev_controls_->set_active_assets(filtered_active_assets);
    }

    initialize_active_assets(camera_.get_screen_center());
    rebuild_active_assets_if_needed();
    update_filtered_active_assets();
}

void Assets::render_overlays(SDL_Renderer* renderer) {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->render_overlays(renderer);
    }

    // Render quick task popup
    if (quick_task_popup_) {
        quick_task_popup_->render(renderer);
    }

    if (!renderer) {
        return;
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

SDL_Renderer* Assets::renderer() const {
    return scene ? scene->get_renderer() : nullptr;
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

render_pipeline::shading::ReactiveShadowSettings* Assets::reactive_shadow_settings() {
    if (!scene) {
        return nullptr;
    }
    return &scene->reactive_shadow_settings();
}

const render_pipeline::shading::ReactiveShadowSettings* Assets::reactive_shadow_settings() const {
    return const_cast<Assets*>(this)->reactive_shadow_settings();
}

LightMap* Assets::light_map() {
    return scene ? scene->light_map() : nullptr;
}

const LightMap* Assets::light_map() const {
    return scene ? scene->light_map() : nullptr;
}

LightMapManager* Assets::light_map_manager() {
    if (!light_map_manager_) {
        light_map_manager_ = std::make_unique<LightMapManager>(this);
    }
    return light_map_manager_.get();
}

const LightMapManager* Assets::light_map_manager() const {
    return const_cast<Assets*>(this)->light_map_manager();
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
    subdivisions = std::max(1, std::min(8, subdivisions));
    bool changed = world_grid_.set_lighting_subdivisions_per_chunk(subdivisions);
    if (changed) {
        if (LightMap* map = light_map()) {
            map->rebuild(nullptr);
        }
        force_shaded_assets_rerender();
    }
    return changed;
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
        const Area view = camera_.get_camera_area();
        auto [minx, miny, maxx, maxy] = view.get_bounds();
        SDL_Rect cam_rect{minx, miny, std::max(0, maxx - minx), std::max(0, maxy - miny)};
        world_grid_.update_active_chunks(cam_rect, camera_.get_render_distance_world_margin());
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

void Assets::clear_editor_selection() {
    if (dev_controls_ && dev_controls_->is_enabled()) {
        dev_controls_->clear_selection();
    }
}

void Assets::handle_sdl_event(const SDL_Event& e) {
    // Handle quick task popup first (if open)
    if (quick_task_popup_ && quick_task_popup_->is_open()) {
        if (quick_task_popup_->handle_event(e)) {
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
