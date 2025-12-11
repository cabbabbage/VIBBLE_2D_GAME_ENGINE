#include "frame_editor_session.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <utility>

#include "animation_update/animation_update.hpp"
#include "animation_update/child_attachment_math.hpp"
#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "asset/animation_frame_variant.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/asset_sections/animation_editor_window/AnimationDocument.hpp"
#include "dev_mode/asset_sections/animation_editor_window/AnimationEditorWindow.hpp"
#include "dev_mode/asset_sections/animation_editor_window/PreviewProvider.hpp"
#include "dev_mode/dev_mode_utils.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/animation_runtime_refresh.hpp"
#include "render/scaling_logic.hpp"
#include "dev_mode/widgets.hpp"
#include "render/warped_screen_grid.hpp"
#include "utils/grid.hpp"
#include "utils/input.hpp"

FrameEditorSession::FrameEditorSession() = default;
FrameEditorSession::~FrameEditorSession() = default;

namespace {
    constexpr int   kNavPreviewHeight = 96;
    constexpr int   kNavSliderGap = 12;
    constexpr int   kNavSpacing = 12;
    constexpr int   kDirectoryPanelMinWidth = 360;
    constexpr int   kMovementTotalsFieldWidth = 120;
    constexpr int   kSmoothCheckboxMinWidth = 110;
    constexpr int   kCurveCheckboxMinWidth = 110;
    constexpr int   kShowAnimCheckboxMinWidth = 120;
    constexpr int   kChildrenFieldWidth = 110;
    constexpr int   kChildVisibilityCheckboxMinWidth = 120;
    constexpr int   kShowChildCheckboxMinWidth = 140;
    constexpr int   kChildDropdownMinWidth = 200;
    constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;
    constexpr float kRadToDeg = 180.0f / static_cast<float>(M_PI);
    constexpr float kHitboxRotateHandleRadius = 12.0f;
    constexpr float kAttackNodeRadius = 12.0f;

    int nav_header_height_px(bool has_dropdown) {
        return has_dropdown ? DMDropdown::height() : DMButton::height();
    }

    bool animation_supports_frame_editing(animation_editor::AnimationDocument* document,
                                          const std::string& animation_id) {
        if (!document || animation_id.empty()) {
            return false;
        }
        const auto ids = document->animation_ids();
        if (std::find(ids.begin(), ids.end(), animation_id) == ids.end()) {
            return false;
        }
        auto payload = document->animation_payload(animation_id);
        if (!payload.has_value()) {
            return true;
        }
        nlohmann::json parsed = nlohmann::json::parse(*payload, nullptr, false);
        return !parsed.is_discarded();
    }

    const Animation* pick_preview_animation(const std::shared_ptr<AssetInfo>& info) {
        if (!info) {
            return nullptr;
        }
        if (!info->start_animation.empty()) {
            auto it = info->animations.find(info->start_animation);
            if (it != info->animations.end()) {
                return &it->second;
            }
        }
        if (!info->animations.empty()) {
            return &info->animations.begin()->second;
        }
        return nullptr;
    }

    SDL_FPoint sample_quadratic_by_arclen(const SDL_FPoint& p0,
                                          const SDL_FPoint& p1,
                                          const SDL_FPoint& p2,
                                          float ratio) {
        const float t = std::clamp(ratio, 0.0f, 1.0f);
        auto lerp = [](const SDL_FPoint& a, const SDL_FPoint& b, float t) {
            return SDL_FPoint{
                a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
};
        SDL_FPoint a = lerp(p0, p1, t);
        SDL_FPoint b = lerp(p1, p2, t);
        return lerp(a, b, t);
    }

    struct LabelFontHandle {
        TTF_Font* font = nullptr;
        bool owns = false;
        ~LabelFontHandle() {
            if (owns && font) {
                TTF_CloseFont(font);
            }
        }
};

    LabelFontHandle acquire_label_font() {
        LabelFontHandle handle;
        const DMLabelStyle& label_style = DMStyles::Label();
        handle.font = devmode::utils::load_font(label_style.font_size);
        if (!handle.font) {
            handle.font = label_style.open_font();
            handle.owns = handle.font != nullptr;
        }
        return handle;
    }

    float dist_sq(const SDL_FPoint& a, const SDL_FPoint& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    SDL_Point round_point(SDL_FPoint p) {
        return SDL_Point{static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
    }

    SDL_Point measure_label_size(const std::string& text) {
        SDL_Point size{0, 0};
        if (text.empty()) {
            return size;
        }
        auto font_handle = acquire_label_font();
        if (!font_handle.font) {
            return size;
        }
        if (TTF_SizeUTF8(font_handle.font, text.c_str(), &size.x, &size.y) != 0) {
            size = SDL_Point{0, 0};
        }
        return size;
    }

    void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
        if (!renderer || text.empty()) {
            return;
        }
        const DMLabelStyle& label_style = DMStyles::Label();
        auto font_handle = acquire_label_font();
        if (!font_handle.font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font_handle.font, text.c_str(), label_style.color);
        if (!surface) {
            return;
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst{x, y, surface->w, surface->h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    }

    const char* mode_display_name(FrameEditorSession::Mode mode) {
        switch (mode) {
            case FrameEditorSession::Mode::Movement:        return "Movement";
            case FrameEditorSession::Mode::StaticChildren:  return "Children (Static)";
            case FrameEditorSession::Mode::AsyncChildren:   return "Children (Async)";
            case FrameEditorSession::Mode::AttackGeometry:  return "Attack Geometry";
            case FrameEditorSession::Mode::HitGeometry:     return "Hit Geometry";
        }
        return "Unknown";
    }

    bool is_children_mode(FrameEditorSession::Mode mode) {
        return mode == FrameEditorSession::Mode::StaticChildren ||
               mode == FrameEditorSession::Mode::AsyncChildren;
    }
}

void FrameEditorSession::begin(Assets* assets,
                               Asset* asset,
                               std::shared_ptr<animation_editor::AnimationDocument> document,
                               std::shared_ptr<animation_editor::PreviewProvider> preview,
                               const std::string& animation_id,
                               animation_editor::AnimationEditorWindow* host_to_toggle,
                               std::function<void()> on_end_callback) {
    if (!assets || !asset || !document || animation_id.empty()) {
        return;
    }

    if (!assets->contains_asset(asset)) {
        return;
    }
    assets_ = assets;
    target_ = asset;
    document_ = std::move(document);
    preview_ = std::move(preview);
    animation_id_ = animation_id;
    host_ = host_to_toggle;
    on_end_ = std::move(on_end_callback);
    edited_animation_ids_.clear();
    if (!snap_resolution_override_ && assets_) {
        snap_resolution_r_ = vibble::grid::clamp_resolution(std::max(0, assets_->map_grid_settings().resolution));
    }

    WarpedScreenGrid& cam = assets_->getView();
    prev_realism_enabled_ = cam.realism_enabled();
    prev_parallax_enabled_ = cam.parallax_enabled();
    prev_asset_hidden_ = target_->is_hidden();

    load_animation_data(animation_id_);

    assets_->focus_camera_on_asset(target_, 0.85, 18);

    show_animation_ = true;
    show_child_ = true;
    smooth_enabled_ = false;
    curve_enabled_ = false;
    selected_hitbox_type_index_ = 1;
    selected_attack_type_index_ = 1;
    selected_attack_vector_indices_.fill(-1);
    hitbox_dragging_ = false;
    active_hitbox_handle_ = HitHandle::None;
    hitbox_drag_moved_ = false;
    attack_dragging_ = false;
    active_attack_handle_ = AttackHandle::None;
    attack_drag_moved_ = false;
    target_->set_hidden(false);
    scroll_offset_ = 0;
    dragging_scrollbar_thumb_ = false;
    child_dropdown_options_cache_.clear();
    animation_dropdown_options_cache_.clear();
    last_applied_show_asset_state_ = show_animation_;
    child_hidden_cache_.clear();
    cache_child_hidden_states();

    ensure_widgets();
    refresh_hitbox_form();
    refresh_attack_form();
    refresh_hitbox_form();

    {
        int sw = 0, sh = 0;
        if (assets_ && assets_->renderer()) {
            SDL_GetRendererOutputSize(assets_->renderer(), &sw, &sh);
        }
        const WarpedScreenGrid& cam = assets_->getView();
        SDL_Point anchor_world = animation_update::detail::bottom_middle_for(*target_, target_->pos);
        SDL_FPoint anchor_screen_f = cam.map_to_screen_f(SDL_FPoint{ static_cast<float>(anchor_world.x), static_cast<float>(anchor_world.y) });
        SDL_Point anchor_screen = round_point(anchor_screen_f);

        DirectoryPanelMetrics dir_metrics = build_directory_panel_metrics();
        const int dir_w = dir_metrics.width;
        const int dir_h = dir_metrics.height;
        const int nav_h = 90;
        const int nav_w = 560;

        int tool_w = 0;
        int tool_h = 0;
        if (mode_ == Mode::Movement) {
            MovementToolboxMetrics metrics = build_movement_toolbox_metrics();
            tool_w = metrics.width;
            tool_h = metrics.height;
        } else if (is_children_mode(mode_)) {
            ChildrenToolboxMetrics metrics = build_children_toolbox_metrics();
            tool_w = metrics.width;
            tool_h = metrics.height;
        } else if (mode_ == Mode::HitGeometry || mode_ == Mode::AttackGeometry) {
            tool_w = 360;
            tool_h = 230;
        }
        if (tool_w <= 0) {
            tool_w = 320;
        }
        if (tool_h <= 0) {
            tool_h = DMButton::height() + DMSpacing::small_gap() * 2;
        }

        nav_pos_.x = anchor_screen.x - nav_w / 2;
        nav_pos_.y = anchor_screen.y + 280;

        dir_pos_.x = anchor_screen.x - dir_w / 2;
        dir_pos_.y = anchor_screen.y - 200 - dir_h;

        toolbox_pos_.x = anchor_screen.x - 400 - tool_w / 2;
        toolbox_pos_.y = sh / 2 - tool_h / 2;

        auto clamp_panel_pos = [&](int& x, int& y, int w, int h) {
            if (sw > 0 && sh > 0) {
                x = std::clamp(x, 0, std::max(0, sw - w));
                y = std::clamp(y, 0, std::max(0, sh - h));
            }
};
        clamp_panel_pos(nav_pos_.x, nav_pos_.y, nav_w, nav_h);
        clamp_panel_pos(dir_pos_.x, dir_pos_.y, dir_w, dir_h);
        clamp_panel_pos(toolbox_pos_.x, toolbox_pos_.y, tool_w, tool_h);
    }
    active_ = true;
}

void FrameEditorSession::load_animation_data(const std::string& animation_id) {
    if (!document_ || !target_) {
        return;
    }
    animation_id_ = animation_id;
    auto payload_dump = document_->animation_payload(animation_id_);
    last_payload_loaded_ = payload_dump.has_value() && !payload_dump->empty();
    nlohmann::json parsed_payload;
    bool parsed_payload_valid = false;
    if (payload_dump && !payload_dump->empty()) {
        parsed_payload = nlohmann::json::parse(*payload_dump, nullptr, false);
        if (parsed_payload.is_object()) {
            parsed_payload_valid = true;
        } else {
            parsed_payload = nlohmann::json::object();
        }
    }
    frames_ = parse_movement_frames_json(payload_dump.value_or(std::string{}));
    child_assets_ = document_->animation_children();
    ensure_child_mode_size();
    if (target_ && target_->info) {
        target_->info->set_animation_children(child_assets_);
        target_->initialize_animation_children_recursive();
        target_->mark_composite_dirty();
    }
    if (assets_) {
        assets_->mark_active_assets_dirty();
    }
    child_preview_slots_.clear();
    document_payload_cache_.clear();
    document_children_signature_ = document_->animation_children_signature();
    if (payload_dump) {
        document_payload_cache_ = *payload_dump;
    }
    rebuild_child_preview_cache();
    sync_child_frames();
    selected_child_index_ = 0;
    if (frames_.empty()) {
        frames_.push_back(clamp_frame(MovementFrame{}));
    }

    int desired_frames = static_cast<int>(frames_.size());
    if (preview_) {
        desired_frames = preview_->get_frame_count(animation_id_);
    }
    if (desired_frames <= 0) {
        desired_frames = std::max(1, static_cast<int>(frames_.size()));
    }
    if (static_cast<int>(frames_.size()) < desired_frames) {
        const int to_add = desired_frames - static_cast<int>(frames_.size());
        for (int i = 0; i < to_add; ++i) {
            frames_.push_back(clamp_frame(MovementFrame{}));
        }
    } else if (static_cast<int>(frames_.size()) > desired_frames) {
        frames_.resize(desired_frames);
    }

    sync_child_frames();
    if (parsed_payload_valid) {
        apply_child_timelines_from_payload(parsed_payload);
    }

    hydrate_frames_from_animation();
    ensure_child_frames_initialized();
    rebuild_rel_positions();

    selected_index_ = 0;
    scroll_offset_ = 0;
    dragging_scrollbar_thumb_ = false;
    child_dropdown_options_cache_.clear();
    animation_dropdown_options_cache_.clear();
    selected_attack_vector_indices_.fill(-1);
    clamp_attack_selection();

    target_->current_animation = animation_id_;
    update_asset_preview_frame();
    refresh_hitbox_form();
    refresh_attack_form();
    refresh_hitbox_form();
}

void FrameEditorSession::end() {
    if (!active_) return;

    const bool target_alive = target_is_alive();

    persist_changes();

    if (assets_ != nullptr) {
        WarpedScreenGrid& cam = assets_->getView();
        cam.set_realism_enabled(prev_realism_enabled_);
        cam.set_parallax_enabled(prev_parallax_enabled_);

        pan_zoom_.cancel(cam);
    }

    if (target_alive) {
        apply_child_hidden_state(true);
        target_->set_hidden(prev_asset_hidden_);
    } else {
        child_hidden_cache_.clear();
        last_applied_show_asset_state_ = true;
    }

    end_hitbox_drag(false);
    end_attack_drag(false);
    child_hidden_cache_.clear();
    last_applied_show_asset_state_ = true;

    if (pending_save_ && document_) {
        pending_save_ = false;
        document_->save_to_file(false);
    }

    auto saved_host = host_;
    auto saved_animation_id = animation_id_;

    active_ = false;
    assets_ = nullptr;
    target_ = nullptr;
    document_.reset();
    preview_.reset();
    host_ = nullptr;
    animation_id_.clear();
    frames_.clear();
    rel_positions_.clear();
    child_preview_slots_.clear();
    document_payload_cache_.clear();
    document_children_signature_.clear();
    edited_animation_ids_.clear();
    last_payload_loaded_ = false;

    if (saved_host) {
        saved_host->on_live_frame_editor_closed(saved_animation_id);
    }

    if (on_end_) {
        auto cb = std::move(on_end_);
        on_end_ = {};
        cb();
    }
}

void FrameEditorSession::update(const Input& input) {
    if (!active_) return;

    if (!assets_ || !target_ || !assets_->contains_asset(target_)) {
        end();
        return;
    }
    refresh_child_assets_from_document();

    if (assets_) {
        WarpedScreenGrid& cam = assets_->getView();

        ensure_widgets();
        rebuild_layout();
        const bool pan_blocked = true;
        pan_zoom_.handle_input(cam, input, pan_blocked);
    }

    update_asset_preview_frame();

    if (cb_show_anim_) {
        cb_show_anim_->set_value(show_animation_);
    }
    if (cb_show_child_) {
        cb_show_child_->set_value(show_child_);
    }
    if (dd_child_select_) {
        int desired = child_assets_.empty() ? 0 : std::clamp(selected_child_index_, 0, static_cast<int>(child_assets_.size()) - 1);
        if (dd_child_select_->selected() != desired) {
            dd_child_select_->set_selected(desired);
        }
    }
    if (tb_child_name_ && !tb_child_name_->is_editing()) {
        std::string desired = (selected_child_index_ >= 0 && selected_child_index_ < static_cast<int>(child_assets_.size()))
            ? child_assets_[static_cast<std::size_t>(selected_child_index_)]
            : std::string{};
        if (tb_child_name_->value() != desired) {
            tb_child_name_->set_value(desired);
        }
        last_child_name_text_ = tb_child_name_->value();
    }
    if (dd_child_mode_) {
        int desired = child_mode_index(child_mode(selected_child_index_));
        if (dd_child_mode_->selected() != desired) {
            dd_child_mode_->set_selected(desired);
        }
        last_child_mode_index_ = dd_child_mode_->selected();
    }
    if (dd_animation_select_) {
        int desired = 0;
        auto it = std::find(animation_dropdown_options_cache_.begin(), animation_dropdown_options_cache_.end(), animation_id_);
        if (it != animation_dropdown_options_cache_.end()) {
            desired = static_cast<int>(std::distance(animation_dropdown_options_cache_.begin(), it));
        }
        if (dd_animation_select_->selected() != desired) {
            dd_animation_select_->set_selected(desired);
        }
    }
    if (cb_smooth_) {
        cb_smooth_->set_value(smooth_enabled_);
    }
    if (!smooth_enabled_) {
        curve_enabled_ = false;
    }
    if (cb_curve_) {
        cb_curve_->set_value(smooth_enabled_ ? curve_enabled_ : false);
    }
    int total_dx = 0, total_dy = 0;
    for (size_t i = 1; i < frames_.size(); ++i) {
        total_dx += static_cast<int>(std::lround(frames_[i].dx));
        total_dy += static_cast<int>(std::lround(frames_[i].dy));
    }
    const std::string dxs = std::to_string(total_dx);
    const std::string dys = std::to_string(total_dy);
    if (tb_total_dx_ && !tb_total_dx_->is_editing()) {
        if (tb_total_dx_->value() != dxs) tb_total_dx_->set_value(dxs);
        last_totals_dx_text_ = tb_total_dx_->value();
    }
    if (tb_total_dy_ && !tb_total_dy_->is_editing()) {
        if (tb_total_dy_->value() != dys) tb_total_dy_->set_value(dys);
        last_totals_dy_text_ = tb_total_dy_->value();
    }
    if (is_children_mode(mode_)) {
        const ChildFrame* child = current_child_frame();
        auto sync_text_box = [&](DMTextBox* tb, std::string& cache, float value) {
            if (!tb || tb->is_editing()) return;
            std::ostringstream oss;
            oss << static_cast<int>(std::lround(value));
            const std::string text = oss.str();
            if (tb->value() != text) {
                tb->set_value(text);
            }
            cache = tb->value();
};
        if (child) {
            sync_text_box(tb_child_dx_.get(), last_child_dx_text_, child->dx);
            sync_text_box(tb_child_dy_.get(), last_child_dy_text_, child->dy);
            if (tb_child_deg_ && !tb_child_deg_->is_editing()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << child->degree;
                const std::string text = oss.str();
                if (tb_child_deg_->value() != text) {
                    tb_child_deg_->set_value(text);
                }
                last_child_deg_text_ = tb_child_deg_->value();
            }
            if (cb_child_visible_) {
                cb_child_visible_->set_value(child->visible);
                last_child_visible_value_ = child->visible;
            }
            if (cb_child_render_front_) {
                cb_child_render_front_->set_value(child->render_in_front);
                last_child_front_value_ = child->render_in_front;
            }
        } else {
            if (tb_child_dx_ && !tb_child_dx_->is_editing()) tb_child_dx_->set_value("0");
            if (tb_child_dy_ && !tb_child_dy_->is_editing()) tb_child_dy_->set_value("0");
            if (tb_child_deg_ && !tb_child_deg_->is_editing()) tb_child_deg_->set_value("0");
            if (cb_child_visible_) cb_child_visible_->set_value(false);
            if (cb_child_render_front_) cb_child_render_front_->set_value(true);
            last_child_front_value_ = cb_child_render_front_ ? cb_child_render_front_->value() : true;
        }
    }
    if (mode_ == Mode::HitGeometry) {
        if (dd_hitbox_type_ && !hitbox_type_labels_.empty()) {
            int desired = std::clamp(selected_hitbox_type_index_, 0, static_cast<int>(hitbox_type_labels_.size()) - 1);
            if (dd_hitbox_type_->selected() != desired) {
                dd_hitbox_type_->set_selected(desired);
            }
        }
        refresh_hitbox_form();
    } else if (mode_ == Mode::AttackGeometry) {
        if (dd_attack_type_ && !attack_type_labels_.empty()) {
            int desired = std::clamp(selected_attack_type_index_, 0, static_cast<int>(attack_type_labels_.size()) - 1);
            if (dd_attack_type_->selected() != desired) {
                dd_attack_type_->set_selected(desired);
            }
        }
        refresh_attack_form();
    }

    if (target_) {
        target_->set_hidden(!show_animation_);
    }
    sync_child_asset_visibility();
}

bool FrameEditorSession::handle_event(const SDL_Event& e) {
    if (!active_) return false;

    if (!assets_ || !target_ || !assets_->contains_asset(target_)) {
        end();
        return true;
    }
    ensure_widgets();
    rebuild_layout();

    auto clamp_panel_pos = [&](int& x, int& y, int w, int h) {
        int sw = 0, sh = 0;
        if (assets_ && assets_->renderer()) {
            SDL_GetRendererOutputSize(assets_->renderer(), &sw, &sh);
        }
        if (sw > 0 && sh > 0) {
            x = std::clamp(x, 0, std::max(0, sw - w));
            y = std::clamp(y, 0, std::max(0, sh - h));
        }
};

    auto point_in_any_thumb = [&](const SDL_Point& p) -> bool {
        for (const auto& r : thumb_rects_) {
            if (r.w > 0 && r.h > 0 && SDL_PointInRect(&p, &r)) return true;
        }
        return false;
};
    auto point_in_scrollbar = [&](const SDL_Point& p) -> bool {
        return scrollbar_visible_ && SDL_PointInRect(&p, &scrollbar_track_);
};
    auto update_scrollbar_from_mouse = [&](int mouse_x) {
        if (!scrollbar_visible_) return;
        const int thumb_w = scrollbar_thumb_.w;
        int track_min = scrollbar_track_.x;
        int track_max = scrollbar_track_.x + scrollbar_track_.w - thumb_w;
        if (track_max < track_min) track_max = track_min;
        int new_thumb_x = mouse_x - scrollbar_drag_offset_x_;
        new_thumb_x = std::clamp(new_thumb_x, track_min, track_max);
        const float denom = static_cast<float>(track_max - track_min);
        float ratio = 0.0f;
        if (denom > 0.0f) {
            ratio = static_cast<float>(new_thumb_x - track_min) / denom;
        }
        const int max_scroll = max_scroll_offset();
        scroll_offset_ = std::clamp(static_cast<int>(std::round(ratio * static_cast<float>(max_scroll))), 0, max_scroll);
};
    auto point_over_toolbox_widget = [&](const SDL_Point& p) -> bool {
        for (const auto& r : toolbox_widget_rects_) {
            if (r.w > 0 && r.h > 0 && SDL_PointInRect(&p, &r)) {
                return true;
            }
        }
        return false;
};

    if (dragging_dir_ || dragging_toolbox_ || dragging_nav_ || dragging_scrollbar_thumb_) {
        if (e.type == SDL_MOUSEMOTION) {
            bool moved = false;
            if (dragging_dir_) {
                dir_pos_.x = e.motion.x - drag_offset_dir_.x;
                dir_pos_.y = e.motion.y - drag_offset_dir_.y;
                DirectoryPanelMetrics dir_metrics = build_directory_panel_metrics();
                clamp_panel_pos(dir_pos_.x, dir_pos_.y, dir_metrics.width, dir_metrics.height);
                moved = true;
            } else if (dragging_toolbox_) {
                toolbox_pos_.x = e.motion.x - drag_offset_toolbox_.x;
                toolbox_pos_.y = e.motion.y - drag_offset_toolbox_.y;
                const int tool_w = toolbox_rect_.w;
                const int tool_h = toolbox_rect_.h;
                clamp_panel_pos(toolbox_pos_.x, toolbox_pos_.y, tool_w, tool_h);
                moved = true;
            } else if (dragging_nav_) {
                nav_pos_.x = e.motion.x - drag_offset_nav_.x;
                nav_pos_.y = e.motion.y - drag_offset_nav_.y;
                const int nav_w = nav_rect_.w;
                const int nav_h = nav_rect_.h;
                clamp_panel_pos(nav_pos_.x, nav_pos_.y, nav_w, nav_h);
                moved = true;
            } else if (dragging_scrollbar_thumb_) {
                update_scrollbar_from_mouse(e.motion.x);
                moved = true;
            }
            if (moved) {
                rebuild_layout();
            }
            return true;
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            dragging_dir_ = false;
            dragging_toolbox_ = false;
            dragging_nav_ = false;
            dragging_scrollbar_thumb_ = false;
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (point_in_scrollbar(p)) {
            dragging_scrollbar_thumb_ = true;
            scrollbar_drag_offset_x_ = std::clamp(p.x - scrollbar_thumb_.x, 0, scrollbar_thumb_.w);
            update_scrollbar_from_mouse(p.x);
            rebuild_layout();
            return true;
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };

        bool over_dir = SDL_PointInRect(&p, &directory_rect_);
        if (over_dir) {
            bool over_button = false;
            const DMButton* buttons[] = {
                btn_back_.get(), btn_movement_.get(), btn_children_.get(), btn_attack_geometry_.get(), btn_hit_geometry_.get() };
            for (const DMButton* b : buttons) {
                if (!b) continue; const SDL_Rect& r = b->rect();
                if (SDL_PointInRect(&p, &r)) { over_button = true; break; }
            }
            if (!over_button) {
                dragging_dir_ = true;
                drag_offset_dir_ = SDL_Point{ p.x - directory_rect_.x, p.y - directory_rect_.y };
                return true;
            }
        }

        const bool has_toolbox = toolbox_rect_.w > 0 && toolbox_rect_.h > 0;
        if (has_toolbox) {
            const bool over_handle = toolbox_drag_rect_.w > 0 && SDL_PointInRect(&p, &toolbox_drag_rect_);
            if (over_handle || (SDL_PointInRect(&p, &toolbox_rect_) && !point_over_toolbox_widget(p))) {
                dragging_toolbox_ = true;
                drag_offset_toolbox_ = SDL_Point{ p.x - toolbox_rect_.x, p.y - toolbox_rect_.y };
                return true;
            }
        }

        if (SDL_PointInRect(&p, &nav_rect_)) {
            bool over_nav_ctrl = false;
            if (btn_prev_) { const SDL_Rect& r = btn_prev_->rect(); if (SDL_PointInRect(&p, &r)) over_nav_ctrl = true; }
            if (!over_nav_ctrl && btn_next_) { const SDL_Rect& r = btn_next_->rect(); if (SDL_PointInRect(&p, &r)) over_nav_ctrl = true; }
            if (!over_nav_ctrl && dd_animation_select_) {
                const SDL_Rect& r = dd_animation_select_->rect();
                if (SDL_PointInRect(&p, &r)) over_nav_ctrl = true;
            }
            if (!over_nav_ctrl) over_nav_ctrl = point_in_any_thumb(p);
            if (!over_nav_ctrl && point_in_scrollbar(p)) over_nav_ctrl = true;
            const bool is_on_nav_handle = nav_drag_rect_.w > 0 && SDL_PointInRect(&p, &nav_drag_rect_);
            if (is_on_nav_handle || !over_nav_ctrl) {
                dragging_nav_ = true;
                drag_offset_nav_ = SDL_Point{ p.x - nav_rect_.x, p.y - nav_rect_.y };
                return true;
            }
        }
    }

    auto handle_button = [&](std::unique_ptr<DMButton>& btn, auto&& on_click) -> bool {
        if (!btn) return false;
        if (!btn->handle_event(e)) return false;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            on_click();
        }
        return true;
};

    if (handle_button(btn_back_, [this]() {

            this->persist_changes();
            this->end();
        })) return true;
    if (handle_button(btn_movement_, [this]() {

            this->persist_mode_changes(this->mode_);
            this->mode_ = Mode::Movement;
            this->end_hitbox_drag(false);
            this->end_attack_drag(false);
        })) return true;
    if (handle_button(btn_children_, [this]() {
            this->persist_mode_changes(this->mode_);
            this->mode_ = Mode::StaticChildren;
            this->end_hitbox_drag(false);
            this->end_attack_drag(false);
        })) return true;
    if (handle_button(btn_attack_geometry_, [this]() {
            this->persist_mode_changes(this->mode_);
            this->mode_ = Mode::AttackGeometry;
            this->end_hitbox_drag(false);
            this->end_attack_drag(false);
        })) return true;
    if (handle_button(btn_hit_geometry_, [this]() {
            this->persist_mode_changes(this->mode_);
            this->mode_ = Mode::HitGeometry;
            this->end_hitbox_drag(false);
            this->end_attack_drag(false);
        })) return true;
    if (mode_ == Mode::HitGeometry) {
        if (handle_button(btn_hitbox_add_remove_, [this]() {
                const std::string type = this->current_hitbox_type();
                this->end_hitbox_drag(false);
                this->end_attack_drag(false);
                if (this->current_hit_box()) {
                    this->delete_hit_box_for_type(type);
                } else {
                    this->ensure_hit_box_for_type(type);
                }
                this->refresh_hitbox_form();
                this->persist_changes();
            })) return true;
        if (handle_button(btn_hitbox_copy_next_, [this]() {
                this->copy_hit_box_to_next_frame();
                this->refresh_hitbox_form();
            })) return true;
        if (handle_button(btn_apply_all_hit_, [this]() {
                this->apply_current_mode_to_all_frames();
                this->refresh_hitbox_form();
            })) return true;
    } else if (mode_ == Mode::AttackGeometry) {
        if (handle_button(btn_attack_add_remove_, [this]() {
                const std::string type = this->current_attack_type();
                this->end_attack_drag(false);
                this->ensure_attack_vector_for_type(type);
                this->refresh_attack_form();
                this->persist_changes();
            })) return true;
        if (handle_button(btn_attack_delete_, [this]() {
                this->end_attack_drag(false);
                this->delete_current_attack_vector();
                this->refresh_attack_form();
                this->persist_changes();
            })) return true;
        if (handle_button(btn_attack_copy_next_, [this]() {
                this->end_attack_drag(false);
                this->copy_attack_vector_to_next_frame();
                this->refresh_attack_form();
            })) return true;
        if (handle_button(btn_apply_all_attack_, [this]() {
                this->apply_current_mode_to_all_frames();
                this->refresh_attack_form();
            })) return true;
    }

    if (mode_ == Mode::Movement || is_children_mode(mode_)) {
        if (cb_smooth_ && cb_smooth_->handle_event(e)) {
            bool current = cb_smooth_->value();
            if (current != smooth_enabled_) {
                smooth_enabled_ = current;
                if (!smooth_enabled_) {
                    curve_enabled_ = false;
                    if (cb_curve_) {
                        cb_curve_->set_value(false);
                    }
                }
            }
            return true;
        }

        if (smooth_enabled_ && cb_curve_ && cb_curve_->handle_event(e)) {
            bool current = cb_curve_->value();
            if (current != curve_enabled_) {
                curve_enabled_ = current;
            }
            return true;
        }

        auto parse_int = [](const std::string& s, int& out) -> bool {
            try { size_t idx = 0; int v = std::stoi(s, &idx); if (idx == s.size()) { out = v; return true; } } catch (...) {}
            return false;
};
        bool consumed_tb = false;
        if (tb_total_dx_) consumed_tb = tb_total_dx_->handle_event(e) || consumed_tb;
        if (tb_total_dy_) consumed_tb = tb_total_dy_->handle_event(e) || consumed_tb;
        if (tb_total_dx_ && tb_total_dy_) {
            const std::string now_dx = tb_total_dx_->value();
            const std::string now_dy = tb_total_dy_->value();
            if (now_dx != last_totals_dx_text_ || now_dy != last_totals_dy_text_) {
                int dx = 0, dy = 0;
                bool okx = parse_int(now_dx, dx);
                bool oky = parse_int(now_dy, dy);
                last_totals_dx_text_ = now_dx;
                last_totals_dy_text_ = now_dy;
                if (okx && oky) {
                    double cur_dx = 0.0, cur_dy = 0.0;
                    for (size_t i = 1; i < frames_.size(); ++i) {
                        cur_dx += std::isfinite(frames_[i].dx) ? frames_[i].dx : 0.0;
                        cur_dy += std::isfinite(frames_[i].dy) ? frames_[i].dy : 0.0;
                    }
                    const double need_dx = static_cast<double>(dx) - cur_dx;
                    const double need_dy = static_cast<double>(dy) - cur_dy;
                    const size_t last = frames_.size() > 0 ? frames_.size() - 1 : 0;
                    if (last >= 1) {
                        frames_[last].dx = static_cast<float>(std::lround(frames_[last].dx + need_dx));
                        frames_[last].dy = static_cast<float>(std::lround(frames_[last].dy + need_dy));
                        rebuild_rel_positions();
                        persist_changes();
                    }
                }
            }
        }
        if (consumed_tb) return true;

        if (mode_ == Mode::Movement) {
            if (handle_button(btn_apply_all_movement_, [this]() { this->apply_current_mode_to_all_frames(); })) return true;
        }
        if (is_children_mode(mode_)) {
            if (handle_button(btn_apply_all_children_, [this]() { this->apply_current_mode_to_all_frames(); })) return true;
        }

        if (mode_ == Mode::Movement) {

        }
    }

    if (cb_show_anim_ && cb_show_anim_->handle_event(e)) {
        bool current = cb_show_anim_->value();
        if (current != last_show_anim_value_) {
            last_show_anim_value_ = current;
            show_animation_ = current;
            if (target_) target_->set_hidden(!show_animation_);

            sync_child_asset_visibility();
        }
        return true;
    }

    if (is_children_mode(mode_)) {
        if (dd_child_select_ && dd_child_select_->handle_event(e)) {
            int current = dd_child_select_->selected();
            if (child_assets_.empty()) {
                current = 0;
            } else {
                current = std::clamp(current, 0, static_cast<int>(child_assets_.size()) - 1);
            }
            if (current != selected_child_index_) {
                select_child(current);
            }
            return true;
        }

        if (dd_child_mode_ && dd_child_mode_->handle_event(e)) {
            const int selected = std::clamp(dd_child_mode_->selected(), 0, 1);
            set_child_mode(selected_child_index_, selected == 0 ? AnimationChildMode::Static : AnimationChildMode::Async);
            persist_changes();
            return true;
        }

        if (tb_child_name_ && tb_child_name_->handle_event(e)) {
            if (e.type == SDL_KEYUP && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) {
                add_or_rename_child(tb_child_name_->value());
            }
            return true;
        }
        auto handle_child_button = [&](std::unique_ptr<DMButton>& btn, auto&& on_click) -> bool {
            if (!btn) return false;
            if (!btn->handle_event(e)) return false;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                on_click();
            }
            return true;
};
        if (handle_child_button(btn_child_add_, [this]() {
                add_or_rename_child(tb_child_name_ ? tb_child_name_->value() : std::string{});
            })) return true;
        if (handle_child_button(btn_child_remove_, [this]() {
                remove_selected_child();
            })) return true;

        if (cb_show_child_ && cb_show_child_->handle_event(e)) {
            bool current = cb_show_child_->value();
            if (current != last_show_child_value_) {
                last_show_child_value_ = current;
                show_child_ = current;
                sync_child_asset_visibility();
            }
            return true;
        }

        bool consumed_child = false;
        if (tb_child_dx_) consumed_child = tb_child_dx_->handle_event(e) || consumed_child;
        if (tb_child_dy_) consumed_child = tb_child_dy_->handle_event(e) || consumed_child;
        if (tb_child_deg_) consumed_child = tb_child_deg_->handle_event(e) || consumed_child;
        if (cb_child_visible_) consumed_child = cb_child_visible_->handle_event(e) || consumed_child;
        if (cb_child_render_front_) consumed_child = cb_child_render_front_->handle_event(e) || consumed_child;
        if (consumed_child) {
            auto* child = current_child_frame();
            if (child) {
                auto parse_float = [](const std::string& s, float fallback) -> float {
                    try {
                        size_t idx = 0;
                        float v = std::stof(s, &idx);
                        if (idx == s.size()) {
                            return v;
                        }
                    } catch (...) {
                    }
                    return fallback;
};
                bool changed = false;
                bool child_offset_changed = false;
                if (tb_child_dx_) {
                    float new_dx = parse_float(tb_child_dx_->value(), child->dx);
                    if (!std::isnan(new_dx) && child->dx != new_dx) {
                        child->dx = new_dx;
                        changed = true;
                        child_offset_changed = true;
                    }
                }
                if (tb_child_dy_) {
                    float new_dy = parse_float(tb_child_dy_->value(), child->dy);
                    if (!std::isnan(new_dy) && child->dy != new_dy) {
                        child->dy = new_dy;
                        changed = true;
                        child_offset_changed = true;
                    }
                }
                if (tb_child_deg_) {
                    float new_deg = parse_float(tb_child_deg_->value(), child->degree);
                    if (!std::isnan(new_deg) && child->degree != new_deg) {
                        child->degree = new_deg;
                        changed = true;
                    }
                }
                if (cb_child_visible_) {
                    bool vis = cb_child_visible_->value();
                    if (child->visible != vis) {
                        child->visible = vis;
                        changed = true;
                    }
                }
                if (cb_child_render_front_) {
                    bool front = cb_child_render_front_->value();
                    if (child->render_in_front != front) {
                        child->render_in_front = front;
                        changed = true;
                    }
                }
                if (changed) {
                    child->has_data = true;
                    rebuild_rel_positions();
                    const bool should_smooth_child = child_offset_changed &&
                                                     smooth_enabled_ &&
                                                     selected_index_ > 0;
                    if (should_smooth_child) {
                        smooth_child_offsets(selected_child_index_, selected_index_);
                    } else {
                        persist_changes();
                    }
                }
            }
            return true;
        }
    }
    if (mode_ == Mode::HitGeometry) {
        if (dd_hitbox_type_ && dd_hitbox_type_->handle_event(e)) {
            if (!hitbox_type_labels_.empty()) {
                int idx = std::clamp(dd_hitbox_type_->selected(), 0, static_cast<int>(hitbox_type_labels_.size()) - 1);
                if (idx != selected_hitbox_type_index_) {
                    selected_hitbox_type_index_ = idx;
                    refresh_hitbox_form();
                }
            }
            return true;
        }
        bool consumed_hit = false;
        if (tb_hit_center_x_) consumed_hit = tb_hit_center_x_->handle_event(e) || consumed_hit;
        if (tb_hit_center_y_) consumed_hit = tb_hit_center_y_->handle_event(e) || consumed_hit;
        if (tb_hit_width_) consumed_hit = tb_hit_width_->handle_event(e) || consumed_hit;
        if (tb_hit_height_) consumed_hit = tb_hit_height_->handle_event(e) || consumed_hit;
        if (tb_hit_rotation_) consumed_hit = tb_hit_rotation_->handle_event(e) || consumed_hit;
        if (consumed_hit) {
            auto* box = current_hit_box();
            if (!box) {
                box = ensure_hit_box_for_type(current_hitbox_type());
            }
            if (box) {
                auto parse_float = [](const std::string& text, float fallback) -> float {
                    if (text.empty()) return fallback;
                    try {
                        return std::stof(text);
                    } catch (...) {
                        return fallback;
                    }
};
                bool changed = false;
                if (tb_hit_center_x_) {
                    float value = parse_float(tb_hit_center_x_->value(), box->center_x);
                    if (std::isfinite(value) && box->center_x != value) {
                        box->center_x = value;
                        changed = true;
                    }
                }
                if (tb_hit_center_y_) {
                    float value = parse_float(tb_hit_center_y_->value(), box->center_y);
                    if (std::isfinite(value) && box->center_y != value) {
                        box->center_y = value;
                        changed = true;
                    }
                }
                if (tb_hit_width_) {
                    float value = parse_float(tb_hit_width_->value(), box->half_width * 2.0f);
                    if (std::isfinite(value)) {
                        float hw = std::max(1.0f, value * 0.5f);
                        if (std::fabs(hw - box->half_width) > 0.01f) {
                            box->half_width = hw;
                            changed = true;
                        }
                    }
                }
                if (tb_hit_height_) {
                    float value = parse_float(tb_hit_height_->value(), box->half_height * 2.0f);
                    if (std::isfinite(value)) {
                        float hh = std::max(1.0f, value * 0.5f);
                        if (std::fabs(hh - box->half_height) > 0.01f) {
                            box->half_height = hh;
                            changed = true;
                        }
                    }
                }
                if (tb_hit_rotation_) {
                    float value = parse_float(tb_hit_rotation_->value(), box->rotation_degrees);
                    if (std::isfinite(value) && std::fabs(value - box->rotation_degrees) > 0.01f) {
                        box->rotation_degrees = value;
                        changed = true;
                    }
                }
                if (changed) {
                    refresh_hitbox_form();
                    persist_changes();
                }
            }
            return true;
        }
    } else if (mode_ == Mode::AttackGeometry) {
        if (dd_attack_type_ && dd_attack_type_->handle_event(e)) {
            if (!attack_type_labels_.empty()) {
                int idx = std::clamp(dd_attack_type_->selected(), 0, static_cast<int>(attack_type_labels_.size()) - 1);
                if (idx != selected_attack_type_index_) {
                    selected_attack_type_index_ = idx;
                    clamp_attack_selection();
                    refresh_attack_form();
                }
            }
            return true;
        }
        bool consumed_attack = false;
        if (tb_attack_start_x_) consumed_attack = tb_attack_start_x_->handle_event(e) || consumed_attack;
        if (tb_attack_start_y_) consumed_attack = tb_attack_start_y_->handle_event(e) || consumed_attack;
        if (tb_attack_control_x_) consumed_attack = tb_attack_control_x_->handle_event(e) || consumed_attack;
        if (tb_attack_control_y_) consumed_attack = tb_attack_control_y_->handle_event(e) || consumed_attack;
        if (tb_attack_end_x_) consumed_attack = tb_attack_end_x_->handle_event(e) || consumed_attack;
        if (tb_attack_end_y_) consumed_attack = tb_attack_end_y_->handle_event(e) || consumed_attack;
        if (tb_attack_damage_) consumed_attack = tb_attack_damage_->handle_event(e) || consumed_attack;
        if (consumed_attack) {
            auto* vec = current_attack_vector();
            if (!vec) {
                vec = ensure_attack_vector_for_type(current_attack_type());
            }
            if (vec) {
                auto parse_float = [](const std::string& text, float fallback) -> float {
                    if (text.empty()) return fallback;
                    try {
                        return std::stof(text);
                    } catch (...) {
                        return fallback;
                    }
};
                auto parse_int = [](const std::string& text, int fallback) -> int {
                    if (text.empty()) return fallback;
                    try {
                        return std::stoi(text);
                    } catch (...) {
                        return fallback;
                    }
};
                bool changed = false;
                if (tb_attack_start_x_) {
                    float value = parse_float(tb_attack_start_x_->value(), vec->start_x);
                    if (std::isfinite(value) && vec->start_x != value) {
                        vec->start_x = value;
                        changed = true;
                    }
                }
                if (tb_attack_start_y_) {
                    float value = parse_float(tb_attack_start_y_->value(), vec->start_y);
                    if (std::isfinite(value) && vec->start_y != value) {
                        vec->start_y = value;
                        changed = true;
                    }
                }
                if (tb_attack_control_x_) {
                    float value = parse_float(tb_attack_control_x_->value(), vec->control_x);
                    if (std::isfinite(value) && vec->control_x != value) {
                        vec->control_x = value;
                        changed = true;
                    }
                }
                if (tb_attack_control_y_) {
                    float value = parse_float(tb_attack_control_y_->value(), vec->control_y);
                    if (std::isfinite(value) && vec->control_y != value) {
                        vec->control_y = value;
                        changed = true;
                    }
                }
                if (tb_attack_end_x_) {
                    float value = parse_float(tb_attack_end_x_->value(), vec->end_x);
                    if (std::isfinite(value) && vec->end_x != value) {
                        vec->end_x = value;
                        changed = true;
                    }
                }
                if (tb_attack_end_y_) {
                    float value = parse_float(tb_attack_end_y_->value(), vec->end_y);
                    if (std::isfinite(value) && vec->end_y != value) {
                        vec->end_y = value;
                        changed = true;
                    }
                }
                if (tb_attack_damage_) {
                    int dmg = parse_int(tb_attack_damage_->value(), vec->damage);
                    dmg = std::max(0, dmg);
                    if (vec->damage != dmg) {
                        vec->damage = dmg;
                        changed = true;
                    }
                }
                if (changed) {
                    refresh_attack_form();
                    persist_changes();
                }
            }
            return true;
        }
    }

    if (mode_ == Mode::HitGeometry) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (begin_hitbox_drag(SDL_Point{e.button.x, e.button.y})) {
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (hitbox_dragging_) {
                end_hitbox_drag(true);
                return true;
            }
        } else if (e.type == SDL_MOUSEMOTION && hitbox_dragging_) {
            update_hitbox_drag(SDL_Point{e.motion.x, e.motion.y});
            return true;
        }
    } else if (mode_ == Mode::AttackGeometry) {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (begin_attack_drag(SDL_Point{e.button.x, e.button.y})) {
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (attack_dragging_) {
                end_attack_drag(true);
                return true;
            }
        } else if (e.type == SDL_MOUSEMOTION && attack_dragging_) {
            update_attack_drag(SDL_Point{e.motion.x, e.motion.y});
            return true;
        }
    }

    if (dd_animation_select_ && dd_animation_select_->handle_event(e)) {
        if (!animation_dropdown_options_cache_.empty()) {
            int idx = std::clamp(dd_animation_select_->selected(), 0, static_cast<int>(animation_dropdown_options_cache_.size()) - 1);
            const std::string& desired_id = animation_dropdown_options_cache_[idx];
            if (!desired_id.empty() && desired_id != animation_id_) {
                switch_animation(desired_id);
            }
        }
        return true;
    }

    if (handle_button(btn_prev_, [this]() { this->select_frame(std::max(0, this->selected_index_ - 1)); })) return true;
    if (handle_button(btn_next_, [this]() { this->select_frame(this->selected_index_ + 1); })) return true;

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (dragging_dir_ || dragging_nav_ || dragging_scrollbar_thumb_) return true;
        SDL_Point p{e.button.x, e.button.y};
        for (size_t i = 0; i < thumb_rects_.size() && i < thumb_indices_.size(); ++i) {
            if (SDL_PointInRect(&p, &thumb_rects_[i])) {
                select_frame(thumb_indices_[i]);
                return true;
            }
        }
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point sp{e.button.x, e.button.y};

        if (SDL_PointInRect(&sp, &directory_rect_) || SDL_PointInRect(&sp, &nav_rect_) || SDL_PointInRect(&sp, &toolbox_rect_)) {
            return true;
        }
        if (!assets_ || !target_) return false;
        WarpedScreenGrid& cam = assets_->getView();
        SDL_FPoint world_f = cam.screen_to_map(sp);

        SDL_Point anchor_world = animation_update::detail::bottom_middle_for(*target_, target_->pos);

        SDL_Point world_px{ static_cast<int>(std::lround(world_f.x)), static_cast<int>(std::lround(world_f.y)) };
        int snap_r = vibble::grid::clamp_resolution(std::max(0, snap_resolution_r_));
        SDL_Point snapped = vibble::grid::snap_world_to_vertex(world_px, snap_r);
        SDL_FPoint desired_rel{ static_cast<float>(snapped.x - anchor_world.x), static_cast<float>(snapped.y - anchor_world.y) };

        if (is_children_mode(mode_)) {

            if (auto* child = current_child_frame()) {
                const float scale = attachment_scale();
                const float inv_scale = (scale > 0.0001f) ? (1.0f / scale) : 1.0f;
                const float unflipped_x = target_->flipped ? -desired_rel.x : desired_rel.x;
                child->dx = static_cast<float>(std::round(unflipped_x * inv_scale));
                child->dy = static_cast<float>(std::round(desired_rel.y * inv_scale));
                child->has_data = true;
                const bool should_smooth_child = smooth_enabled_ && selected_index_ > 0;
                if (should_smooth_child) {
                    smooth_child_offsets(selected_child_index_, selected_index_);
                } else {
                    persist_changes();
                }
            }
        } else {

            std::vector<SDL_FPoint> base = rel_positions_;
            apply_frame_move_from_base(selected_index_, desired_rel, base);
            rebuild_rel_positions();
            const bool should_smooth = (mode_ == Mode::Movement) && smooth_enabled_ && selected_index_ > 0;
            if (should_smooth) {

                redistribute_frames_after_adjustment(selected_index_);
            } else {
                persist_changes();
            }
        }
        return true;
    }

    if (is_children_mode(mode_) &&
        e.type == SDL_KEYDOWN &&
        (e.key.keysym.sym == SDLK_LEFT || e.key.keysym.sym == SDLK_RIGHT)) {
        if (dd_child_select_ && dd_child_select_->focused()) {
            return true;
        }
        auto child_textbox_editing = [&]() {
            return (tb_child_dx_ && tb_child_dx_->is_editing()) ||
                   (tb_child_dy_ && tb_child_dy_->is_editing()) || (tb_child_deg_ && tb_child_deg_->is_editing());
};
        if (child_textbox_editing()) {
            return true;
        }
        if (auto* child = current_child_frame()) {
            float delta = (e.key.keysym.mod & KMOD_SHIFT) ? 5.0f : 1.0f;
            if (e.key.keysym.sym == SDLK_LEFT) {
                delta = -delta;
            }
            child->degree += delta;
            child->has_data = true;
            persist_changes();
            return true;
        }
    }

    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        SDL_Point sp;
        if (e.type == SDL_MOUSEMOTION) { sp = SDL_Point{ e.motion.x, e.motion.y }; }
        else { sp = SDL_Point{ e.button.x, e.button.y }; }
        if (SDL_PointInRect(&sp, &directory_rect_) || SDL_PointInRect(&sp, &nav_rect_) || SDL_PointInRect(&sp, &toolbox_rect_)) {
            return true;
        }
    }

    return false;
}

void FrameEditorSession::render(SDL_Renderer* renderer) const {
    if (!active_ || !renderer || !assets_ || !target_) return;

    if (!assets_->contains_asset(target_)) return;

    const WarpedScreenGrid& cam = assets_->getView();
    SDL_Point anchor_world = animation_update::detail::bottom_middle_for(*target_, target_->pos);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color path_col = DMStyles::AccentButton().bg;
    SDL_SetRenderDrawColor(renderer, path_col.r, path_col.g, path_col.b, 205);
    for (size_t i = 1; i < rel_positions_.size(); ++i) {
        SDL_FPoint a = cam.map_to_screen_f(SDL_FPoint{ rel_positions_[i-1].x + anchor_world.x,
                                                       rel_positions_[i-1].y + anchor_world.y });
        SDL_FPoint b = cam.map_to_screen_f(SDL_FPoint{ rel_positions_[i].x + anchor_world.x,
                                                       rel_positions_[i].y + anchor_world.y });
        SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(a.x)), static_cast<int>(std::lround(a.y)), static_cast<int>(std::lround(b.x)), static_cast<int>(std::lround(b.y)));
    }

    for (size_t i = 0; i < rel_positions_.size(); ++i) {
        SDL_FPoint p = cam.map_to_screen_f(SDL_FPoint{ rel_positions_[i].x + anchor_world.x,
                                                       rel_positions_[i].y + anchor_world.y });
        const bool is_current = static_cast<int>(i) == selected_index_;
        const int r = is_current ? 6 : 4;
        SDL_Color c = is_current ? DMStyles::AccentButton().hover_bg : devmode::utils::with_alpha(DMStyles::AccentButton().bg, 128);
        SDL_Point cp = round_point(p);
        SDL_Rect dot{ cp.x - r, cp.y - r, r * 2, r * 2 };
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(renderer, &dot);
        SDL_SetRenderDrawColor(renderer, DMStyles::Border().r, DMStyles::Border().g, DMStyles::Border().b, DMStyles::Border().a);
        SDL_RenderDrawRect(renderer, &dot);
    }

    if (is_children_mode(mode_) && show_child_ && !child_assets_.empty() &&
        selected_index_ < static_cast<int>(frames_.size())) {
        const auto& frame = frames_[selected_index_];
        ChildPreviewContext preview_ctx = build_child_preview_context();

        SDL_Point parent_base = asset_anchor_world();
        const float base_adjustment = attachment_scale();
        float variant_scale = target_->current_nearest_variant_scale;
        if (!std::isfinite(variant_scale) || variant_scale <= 0.0f) {
            variant_scale = 1.0f;
        }
        preview_ctx.document_scale = base_adjustment;

        for (std::size_t i = 0; i < child_assets_.size() && i < frame.children.size(); ++i) {
            const auto& child = frame.children[i];

            const float scaled_dx = static_cast<float>(child.dx) * base_adjustment;
            const float scaled_dy = static_cast<float>(child.dy) * base_adjustment;
            const float dx_world = target_->flipped ? -scaled_dx : scaled_dx;
            SDL_FPoint screen = cam.map_to_screen_f(SDL_FPoint{
                dx_world + static_cast<float>(parent_base.x),
                scaled_dy + static_cast<float>(parent_base.y)
            });
            SDL_Point cp = round_point(screen);
            const int marker_r = (static_cast<int>(i) == selected_child_index_) ? 6 : 4;
            SDL_Rect marker{ cp.x - marker_r, cp.y - marker_r, marker_r * 2, marker_r * 2 };
            SDL_Color base = (static_cast<int>(i) == selected_child_index_) ? DMStyles::AccentButton().bg : DMStyles::HeaderButton().bg;
            Uint8 alpha = child.visible ? 220 : 90;
            SDL_SetRenderDrawColor(renderer, base.r, base.g, base.b, alpha);
            SDL_RenderFillRect(renderer, &marker);
            SDL_SetRenderDrawColor(renderer, DMStyles::Border().r, DMStyles::Border().g, DMStyles::Border().b, 255);
            SDL_RenderDrawRect(renderer, &marker);
            render_label(renderer, child_assets_[i], marker.x + marker.w + 4, marker.y - 4);
        }

        const std::size_t preview_count = std::min({ child_assets_.size(), frame.children.size(), child_preview_slots_.size() });
        for (std::size_t i = 0; i < preview_count; ++i) {
            const auto& child = frame.children[i];
            if (!child.visible) continue;
            const auto& slot = child_preview_slots_[i];
            const Animation* preview_anim = slot.animation;
            const AnimationFrame* preview_frame = slot.frame;

            const float scaled_dx = static_cast<float>(child.dx) * base_adjustment;
            const float scaled_dy = static_cast<float>(child.dy) * base_adjustment;
            const float dx_world = target_->flipped ? -scaled_dx : scaled_dx;
            SDL_FPoint child_world{
                static_cast<float>(parent_base.x) + dx_world, static_cast<float>(parent_base.y) + scaled_dy };
            const FrameVariant* variant = (preview_anim && preview_frame) ? preview_anim->get_frame(preview_frame, variant_scale) : nullptr;
            SDL_Texture* tex = variant ? variant->get_base_texture() : slot.texture;
            if (!tex) continue;

            int tex_w = slot.width;
            int tex_h = slot.height;
            if (!variant && tex && (tex_w <= 0 || tex_h <= 0)) {
                SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h);
            } else if (variant) {
                SDL_QueryTexture(tex, nullptr, nullptr, &tex_w, &tex_h);
            }
            if (tex_w <= 0 || tex_h <= 0) continue;

            float final_scale = base_adjustment;
            if (!std::isfinite(final_scale) || final_scale <= 0.0f) {
                final_scale = 1.0f;
            }

            SDL_FRect dst = child_preview_rect(child_world, tex_w, tex_h, preview_ctx, final_scale);
            if (dst.w <= 0.0f || dst.h <= 0.0f) continue;
            SDL_FPoint pivot{ dst.w * 0.5f, dst.h };

            const bool parent_flipped = target_ && target_->flipped;
            const double angle = static_cast<double>(mirrored_child_rotation(parent_flipped, child.degree));
            SDL_RenderCopyExF(renderer, tex, nullptr, &dst, angle, &pivot, parent_flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }

    if (mode_ == Mode::HitGeometry) {
        render_hit_geometry(renderer);
    } else if (mode_ == Mode::AttackGeometry) {
        render_attack_geometry(renderer);
    }

    ensure_widgets();
    rebuild_layout();

    dm_draw::DrawBeveledRect(renderer, directory_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelHeader(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
    {
        std::string mode_text = std::string("Mode: ") + mode_display_name(mode_);
        if (pending_save_) {
            mode_text.append(" *");
        }
        render_label(renderer, mode_text, directory_rect_.x + DMSpacing::small_gap(), directory_rect_.y + DMSpacing::small_gap());
    }
    if (btn_back_) btn_back_->render(renderer);
    if (btn_movement_) btn_movement_->render(renderer);
    if (btn_children_) btn_children_->render(renderer);
    if (btn_attack_geometry_) btn_attack_geometry_->render(renderer);
    if (btn_hit_geometry_) btn_hit_geometry_->render(renderer);

    if (mode_ == Mode::Movement && toolbox_rect_.w > 0 && toolbox_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, toolbox_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        if (cb_smooth_) cb_smooth_->render(renderer);
        if (smooth_enabled_ && cb_curve_) cb_curve_->render(renderer);
        if (cb_show_anim_) cb_show_anim_->render(renderer);
        if (tb_total_dx_) tb_total_dx_->render(renderer);
        if (tb_total_dy_) tb_total_dy_->render(renderer);
        if (btn_apply_all_movement_) btn_apply_all_movement_->render(renderer);
    } else if (is_children_mode(mode_) && toolbox_rect_.w > 0 && toolbox_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, toolbox_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

        if (cb_smooth_) cb_smooth_->render(renderer);
        if (smooth_enabled_ && cb_curve_) cb_curve_->render(renderer);
        if (tb_total_dx_) tb_total_dx_->render(renderer);
        if (tb_total_dy_) tb_total_dy_->render(renderer);
        if (dd_child_select_) dd_child_select_->render(renderer);
        if (cb_show_anim_) cb_show_anim_->render(renderer);
        if (cb_show_child_) cb_show_child_->render(renderer);
        if (tb_child_dx_) tb_child_dx_->render(renderer);
        if (tb_child_dy_) tb_child_dy_->render(renderer);
        if (tb_child_deg_) tb_child_deg_->render(renderer);
        if (cb_child_visible_) cb_child_visible_->render(renderer);
        if (cb_child_render_front_) cb_child_render_front_->render(renderer);
        if (btn_apply_all_children_) btn_apply_all_children_->render(renderer);
    } else if (mode_ == Mode::HitGeometry && toolbox_rect_.w > 0 && toolbox_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, toolbox_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        if (dd_hitbox_type_) dd_hitbox_type_->render(renderer);
        if (btn_hitbox_add_remove_) btn_hitbox_add_remove_->render(renderer);
        if (btn_hitbox_copy_next_) btn_hitbox_copy_next_->render(renderer);
        if (tb_hit_center_x_) tb_hit_center_x_->render(renderer);
        if (tb_hit_center_y_) tb_hit_center_y_->render(renderer);
        if (tb_hit_width_) tb_hit_width_->render(renderer);
        if (tb_hit_height_) tb_hit_height_->render(renderer);
        if (tb_hit_rotation_) tb_hit_rotation_->render(renderer);
        if (btn_apply_all_hit_) btn_apply_all_hit_->render(renderer);
    } else if (mode_ == Mode::AttackGeometry && toolbox_rect_.w > 0 && toolbox_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, toolbox_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        if (dd_attack_type_) dd_attack_type_->render(renderer);
        if (btn_attack_add_remove_) btn_attack_add_remove_->render(renderer);
        if (btn_attack_delete_) btn_attack_delete_->render(renderer);
        if (btn_attack_copy_next_) btn_attack_copy_next_->render(renderer);
        if (tb_attack_start_x_) tb_attack_start_x_->render(renderer);
        if (tb_attack_start_y_) tb_attack_start_y_->render(renderer);
        if (tb_attack_control_x_) tb_attack_control_x_->render(renderer);
        if (tb_attack_control_y_) tb_attack_control_y_->render(renderer);
        if (tb_attack_end_x_) tb_attack_end_x_->render(renderer);
        if (tb_attack_end_y_) tb_attack_end_y_->render(renderer);
        if (tb_attack_damage_) tb_attack_damage_->render(renderer);
        if (btn_apply_all_attack_) btn_apply_all_attack_->render(renderer);
    }

    dm_draw::DrawBeveledRect(renderer, nav_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
    if (dd_animation_select_) {
        dd_animation_select_->render(renderer);
    }

    for (size_t i = 0; i < thumb_rects_.size() && i < thumb_indices_.size(); ++i) {
        const SDL_Rect& r = thumb_rects_[i];
        const int frame_index = thumb_indices_[i];
        SDL_Color border = DMStyles::Border();
        const bool is_current = frame_index == selected_index_;
        if (is_current) {
            border = DMStyles::AccentButton().border;
        }
        SDL_Texture* tex = nullptr;
        if (preview_) {
            tex = preview_->get_frame_texture(renderer, animation_id_, frame_index);
        }
        if (tex) {
            int tw = 0, th = 0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            if (tw > 0 && th > 0) {
                const float sx = std::min(1.0f, static_cast<float>(r.w - 8) / static_cast<float>(tw));
                const float sy = std::min(1.0f, static_cast<float>(r.h - 8) / static_cast<float>(th));
                const float s = std::min(sx, sy);
                int dw = std::max(1, static_cast<int>(std::round(tw * s)));
                int dh = std::max(1, static_cast<int>(std::round(th * s)));
                SDL_Rect dst{ r.x + (r.w - dw)/2, r.y + (r.h - dh)/2, dw, dh };
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
            }
        }
        dm_draw::DrawRoundedOutline(renderer, r, DMStyles::CornerRadius(), 1, border);

        const std::string index_text = std::to_string(frame_index);
        SDL_Point label_size = measure_label_size(index_text);
        if (label_size.x > 0 && label_size.y > 0) {
            const int badge_padding = 3;
            SDL_Rect badge{
                r.x + r.w - label_size.x - badge_padding * 2 - 2,
                r.y + r.h - label_size.y - badge_padding * 2 - 2,
                label_size.x + badge_padding * 2,
                label_size.y + badge_padding * 2
};
            const int min_badge_x = r.x + 2;
            const int min_badge_y = r.y + 2;
            const int max_badge_x = std::max(min_badge_x, r.x + r.w - badge.w - 2);
            const int max_badge_y = std::max(min_badge_y, r.y + r.h - badge.h - 2);
            badge.x = std::clamp(badge.x, min_badge_x, max_badge_x);
            badge.y = std::clamp(badge.y, min_badge_y, max_badge_y);
            SDL_Color bg{0, 0, 0, 180};
            SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(renderer, &badge);
            SDL_Color outline = DMStyles::Border();
            SDL_RenderDrawRect(renderer, &badge);
            render_label(renderer, index_text, badge.x + badge_padding, badge.y + badge_padding);
        }
    }

    if (scrollbar_visible_) {
        SDL_Color track_col = devmode::utils::with_alpha(DMStyles::PanelHeader(), 180);
        SDL_SetRenderDrawColor(renderer, track_col.r, track_col.g, track_col.b, track_col.a);
        SDL_RenderFillRect(renderer, &scrollbar_track_);
        SDL_Color thumb_col = DMStyles::AccentButton().bg;
        SDL_SetRenderDrawColor(renderer, thumb_col.r, thumb_col.g, thumb_col.b, 220);
        SDL_RenderFillRect(renderer, &scrollbar_thumb_);
        SDL_SetRenderDrawColor(renderer, DMStyles::Border().r, DMStyles::Border().g, DMStyles::Border().b, 255);
        SDL_RenderDrawRect(renderer, &scrollbar_thumb_);
    }

    if (btn_prev_) btn_prev_->render(renderer);
    if (btn_next_) btn_next_->render(renderer);

    DMDropdown::render_active_options(renderer);
}

void FrameEditorSession::set_grid_overlay_enabled_transient(bool enabled) {
    (void)enabled;
}

void FrameEditorSession::set_snap_resolution(int r) {
    snap_resolution_r_ = vibble::grid::clamp_resolution(std::max(0, r));
    snap_resolution_override_ = true;
}

void FrameEditorSession::ensure_widgets() const {
    const DMButtonStyle& header = DMStyles::HeaderButton();
    const DMButtonStyle& tab_active = DMStyles::AccentButton();
    const int bw = 96;
    const int bh = DMButton::height();
    if (!btn_back_) btn_back_ = std::make_unique<DMButton>(u8"\u2190 Back", &DMStyles::DeleteButton(), 96, bh);
    if (!btn_movement_) btn_movement_ = std::make_unique<DMButton>("Movement", mode_ == Mode::Movement ? &tab_active : &header, bw, bh);
    if (!btn_children_) btn_children_ = std::make_unique<DMButton>("Children", is_children_mode(mode_) ? &tab_active : &header, bw, bh);
    if (!btn_attack_geometry_) btn_attack_geometry_ = std::make_unique<DMButton>("Attack Geometry", mode_ == Mode::AttackGeometry ? &tab_active : &header, bw, bh);
    if (!btn_hit_geometry_) btn_hit_geometry_ = std::make_unique<DMButton>("Hit Geometry", mode_ == Mode::HitGeometry ? &tab_active : &header, bw, bh);
    if (!btn_prev_) btn_prev_ = std::make_unique<DMButton>("<", &header, 40, 40);
    if (!btn_next_) btn_next_ = std::make_unique<DMButton>(">", &header, 40, 40);
    refresh_animation_dropdown();
    if (!btn_apply_all_movement_) btn_apply_all_movement_ = std::make_unique<DMButton>("Apply To All Frames", &header, 180, DMButton::height());
    if (!btn_apply_all_children_) btn_apply_all_children_ = std::make_unique<DMButton>("Apply To All Frames", &header, 180, DMButton::height());
    if (!btn_apply_all_hit_) btn_apply_all_hit_ = std::make_unique<DMButton>("Apply To All Frames", &header, 180, DMButton::height());
    if (!btn_apply_all_attack_) btn_apply_all_attack_ = std::make_unique<DMButton>("Apply To All Frames", &header, 180, DMButton::height());
    if (!cb_smooth_) cb_smooth_ = std::make_unique<DMCheckbox>("Smooth", smooth_enabled_);
    if (!cb_curve_) cb_curve_ = std::make_unique<DMCheckbox>("Curve", curve_enabled_);
    const bool want_parent_label = is_children_mode(mode_);
    if (!cb_show_anim_ || cb_show_anim_targets_parent_label_ != want_parent_label) {
        const bool current = cb_show_anim_ ? cb_show_anim_->value() : show_animation_;
        cb_show_anim_ = std::make_unique<DMCheckbox>(want_parent_label ? "Show Parent" : "Show Animation", current);
        cb_show_anim_targets_parent_label_ = want_parent_label;
    }
    if (!cb_show_child_) cb_show_child_ = std::make_unique<DMCheckbox>("Show Child", show_child_);
    if (!tb_total_dx_) tb_total_dx_ = std::make_unique<DMTextBox>("Total dX", "0");
    if (!tb_total_dy_) tb_total_dy_ = std::make_unique<DMTextBox>("Total dY", "0");
    if (!tb_child_dx_) tb_child_dx_ = std::make_unique<DMTextBox>("Child dX", "0");
    if (!tb_child_dy_) tb_child_dy_ = std::make_unique<DMTextBox>("Child dY", "0");
    if (!tb_child_deg_) tb_child_deg_ = std::make_unique<DMTextBox>("Rotation", "0");
    if (!cb_child_visible_) cb_child_visible_ = std::make_unique<DMCheckbox>("Visible", true);
    if (!cb_child_render_front_) cb_child_render_front_ = std::make_unique<DMCheckbox>("Render In Front", true);
    if (!dd_child_select_ || child_dropdown_options_cache_ != child_assets_) {
        child_dropdown_options_cache_ = child_assets_;
        int dropdown_index = selected_child_index_;
        if (child_assets_.empty()) {
            dropdown_index = 0;
        } else {
            dropdown_index = std::clamp(dropdown_index, 0, static_cast<int>(child_assets_.size()) - 1);
        }
        dd_child_select_ = std::make_unique<DMDropdown>("Child", child_dropdown_options_cache_, dropdown_index);
    }
    ensure_child_mode_size();
    const std::vector<std::string> child_mode_options = {
        "Static (per-frame)",
        "Async (timeline)"
};
    int desired_mode_index = child_mode_index(child_mode(selected_child_index_));
    dd_child_mode_ = std::make_unique<DMDropdown>("Mode", child_mode_options, std::clamp(desired_mode_index, 0, static_cast<int>(child_mode_options.size()) - 1));
    if (!tb_child_name_) tb_child_name_ = std::make_unique<DMTextBox>("Child Asset", "");
    if (!btn_child_add_) btn_child_add_ = std::make_unique<DMButton>("Add / Rename", &DMStyles::AccentButton(), 140, DMButton::height());
    if (!btn_child_remove_) btn_child_remove_ = std::make_unique<DMButton>("Remove", &DMStyles::DeleteButton(), 120, DMButton::height());
    if (hitbox_type_labels_.size() != kDamageTypeNames.size()) {
        hitbox_type_labels_.clear();
        for (const char* type : kDamageTypeNames) {
            std::string label = type;
            if (!label.empty()) {
                label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            }
            hitbox_type_labels_.push_back(label);
        }
    }
    if (!dd_hitbox_type_ && !hitbox_type_labels_.empty()) {
        dd_hitbox_type_ = std::make_unique<DMDropdown>("Hit Box Type", hitbox_type_labels_, std::clamp(selected_hitbox_type_index_, 0, static_cast<int>(hitbox_type_labels_.size()) - 1));
    }
    if (!btn_hitbox_add_remove_) {
        btn_hitbox_add_remove_ = std::make_unique<DMButton>("Add Hit Box", &DMStyles::AccentButton(), 150, DMButton::height());
    }
    if (!btn_hitbox_copy_next_) {
        btn_hitbox_copy_next_ = std::make_unique<DMButton>("Copy To Next", &header, 150, DMButton::height());
    }
    if (!tb_hit_center_x_) tb_hit_center_x_ = std::make_unique<DMTextBox>("Center X", "0");
    if (!tb_hit_center_y_) tb_hit_center_y_ = std::make_unique<DMTextBox>("Center Y", "0");
    if (!tb_hit_width_) tb_hit_width_ = std::make_unique<DMTextBox>("Width", "0");
    if (!tb_hit_height_) tb_hit_height_ = std::make_unique<DMTextBox>("Height", "0");
    if (!tb_hit_rotation_) tb_hit_rotation_ = std::make_unique<DMTextBox>("Rotation", "0");
    if (attack_type_labels_.size() != kDamageTypeNames.size()) {
        attack_type_labels_.clear();
        for (const char* type : kDamageTypeNames) {
            std::string label = type;
            if (!label.empty()) {
                label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            }
            attack_type_labels_.push_back(label);
        }
    }
    if (!dd_attack_type_ && !attack_type_labels_.empty()) {
        dd_attack_type_ = std::make_unique<DMDropdown>("Attack Type", attack_type_labels_, std::clamp(selected_attack_type_index_, 0, static_cast<int>(attack_type_labels_.size()) - 1));
    }
    if (!btn_attack_add_remove_) {
        btn_attack_add_remove_ = std::make_unique<DMButton>("Add Attack", &DMStyles::AccentButton(), 150, DMButton::height());
    }
    if (!btn_attack_delete_) {
        btn_attack_delete_ = std::make_unique<DMButton>("Delete Attack", &DMStyles::DeleteButton(), 150, DMButton::height());
    }
    if (!btn_attack_copy_next_) {
        btn_attack_copy_next_ = std::make_unique<DMButton>("Copy To Next", &header, 150, DMButton::height());
    }
    if (!tb_attack_start_x_) tb_attack_start_x_ = std::make_unique<DMTextBox>("Start X", "0");
    if (!tb_attack_start_y_) tb_attack_start_y_ = std::make_unique<DMTextBox>("Start Y", "0");
    if (!tb_attack_control_x_) tb_attack_control_x_ = std::make_unique<DMTextBox>("Control X", "0");
    if (!tb_attack_control_y_) tb_attack_control_y_ = std::make_unique<DMTextBox>("Control Y", "0");
    if (!tb_attack_end_x_) tb_attack_end_x_ = std::make_unique<DMTextBox>("End X", "0");
    if (!tb_attack_end_y_) tb_attack_end_y_ = std::make_unique<DMTextBox>("End Y", "0");
    if (!tb_attack_damage_) tb_attack_damage_ = std::make_unique<DMTextBox>("Damage", "0");
    last_show_anim_value_ = show_animation_;
    last_show_child_value_ = show_child_;
    last_totals_dx_text_ = tb_total_dx_->value();
    last_totals_dy_text_ = tb_total_dy_->value();
    last_child_front_value_ = cb_child_render_front_ ? cb_child_render_front_->value() : true;
}

void FrameEditorSession::refresh_animation_dropdown() const {
    if (!document_) {
        dd_animation_select_.reset();
        animation_dropdown_options_cache_.clear();
        return;
    }
    const auto ids = document_->animation_ids();
    std::vector<std::string> eligible;
    eligible.reserve(ids.size());
    for (const auto& id : ids) {
        if (animation_supports_frame_editing(document_.get(), id)) {
            eligible.push_back(id);
        }
    }
    if (!animation_id_.empty() &&
        std::find(eligible.begin(), eligible.end(), animation_id_) == eligible.end()) {
        eligible.insert(eligible.begin(), animation_id_);
    }
    if (eligible.empty()) {
        dd_animation_select_.reset();
        animation_dropdown_options_cache_.clear();
        return;
    }
    if (!dd_animation_select_ || eligible != animation_dropdown_options_cache_) {
        animation_dropdown_options_cache_ = eligible;
        int selected_idx = 0;
        auto it = std::find(animation_dropdown_options_cache_.begin(), animation_dropdown_options_cache_.end(), animation_id_);
        if (it != animation_dropdown_options_cache_.end()) {
            selected_idx = static_cast<int>(std::distance(animation_dropdown_options_cache_.begin(), it));
        }
        dd_animation_select_ = std::make_unique<DMDropdown>("Animation", animation_dropdown_options_cache_, selected_idx);
    }
}

void FrameEditorSession::rebuild_layout() const {
    if (!assets_ || !target_) return;
    const WarpedScreenGrid& cam = assets_->getView();
    const int screen_w = assets_->renderer() ? assets_->getView().get_camera_area().width() : 0;
    (void)screen_w;
    (void)cam;
    DirectoryPanelMetrics dir_metrics = build_directory_panel_metrics();
    directory_rect_ = SDL_Rect{ dir_pos_.x, dir_pos_.y, dir_metrics.width, dir_metrics.height };
    toolbox_widget_rects_.clear();
    const int dir_padding = DMSpacing::small_gap();
    const int button_gap = DMSpacing::small_gap();
    auto button_width = [](const std::unique_ptr<DMButton>& btn) -> int {
        if (!btn) return 0;
        int w = btn->rect().w;
        if (w <= 0) {
            w = btn->preferred_width();
        }
        return w;
};
    auto register_toolbox_widget = [&](const auto* widget) {
        if (!widget) return;
        const SDL_Rect& r = widget->rect();
        if (r.w > 0 && r.h > 0) {
            toolbox_widget_rects_.push_back(r);
        }
};
    int total_button_width = 0;
    auto accumulate_width = [&](const std::unique_ptr<DMButton>& btn) {
        int w = button_width(btn);
        if (w <= 0) return;
        if (total_button_width > 0) {
            total_button_width += button_gap;
        }
        total_button_width += w;
};
    accumulate_width(btn_back_);
    accumulate_width(btn_movement_);
    accumulate_width(btn_children_);
    accumulate_width(btn_attack_geometry_);
    accumulate_width(btn_hit_geometry_);
    int y = directory_rect_.y + dir_metrics.top_padding;
    int x = directory_rect_.x + dir_padding;
    if (total_button_width > 0) {
        const int centered_offset = (directory_rect_.w - total_button_width) / 2;
        x = directory_rect_.x + std::max(dir_padding, centered_offset);
    }
    bool first_button = true;
    auto place_button = [&](std::unique_ptr<DMButton>& btn, auto&& prepare) {
        if (!btn) return;
        int w = button_width(btn);
        if (w <= 0) return;
        if (!first_button) {
            x += button_gap;
        }
        first_button = false;
        prepare(btn.get());
        btn->set_rect(SDL_Rect{ x, y, w, DMButton::height() });
        x += w;
};
    place_button(btn_back_, [](DMButton*) {});
    place_button(btn_movement_, [&](DMButton* btn) {
        btn->set_style(mode_ == Mode::Movement ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
    });
    place_button(btn_children_, [&](DMButton* btn) {
        btn->set_style(is_children_mode(mode_) ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
    });
    place_button(btn_attack_geometry_, [&](DMButton* btn) {
        btn->set_style(mode_ == Mode::AttackGeometry ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
    });
    place_button(btn_hit_geometry_, [&](DMButton* btn) {
        btn->set_style(mode_ == Mode::HitGeometry ? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
    });

    if (mode_ == Mode::Movement) {
        MovementToolboxMetrics metrics = build_movement_toolbox_metrics();
        if (metrics.width <= 0 || metrics.height <= 0) {
            toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, 0, 0 };
            toolbox_drag_rect_ = SDL_Rect{ 0, 0, 0, 0 };
        } else {
            toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, metrics.width, metrics.height };
            const int handle_height = std::max(0, metrics.drag_handle_height);
            const int drag_area_height = std::min(toolbox_rect_.h, handle_height + metrics.padding);
            toolbox_drag_rect_ = SDL_Rect{ toolbox_rect_.x, toolbox_rect_.y, toolbox_rect_.w, drag_area_height };
            int tx = toolbox_rect_.x + metrics.padding;
            const int row_top = toolbox_rect_.y + metrics.padding + handle_height;
            bool first = true;
            auto reserve = [&](int w) -> int {
                if (w <= 0) return tx;
                if (!first) {
                    tx += metrics.gap;
                }
                first = false;
                int x = tx;
                tx += w;
                return x;
};
            if (cb_smooth_) {
                const int w = std::max(metrics.smooth_checkbox_width, DMCheckbox::height());
                const int h = DMCheckbox::height();
                const int y = row_top + (metrics.row_height - h) / 2;
                const int x = reserve(w);
                cb_smooth_->set_rect(SDL_Rect{ x, y, w, h });
                register_toolbox_widget(cb_smooth_.get());
            }
            if (smooth_enabled_ && cb_curve_) {
                const int w = std::max(metrics.curve_checkbox_width, DMCheckbox::height());
                const int h = DMCheckbox::height();
                const int y = row_top + (metrics.row_height - h) / 2;
                const int x = reserve(w);
                cb_curve_->set_rect(SDL_Rect{ x, y, w, h });
                register_toolbox_widget(cb_curve_.get());
            }

            if (cb_show_anim_) {
                const int w = std::max(metrics.show_checkbox_width, DMCheckbox::height());
                const int h = DMCheckbox::height();
                const int y = row_top + (metrics.row_height - h) / 2;
                const int x = reserve(w);
                cb_show_anim_->set_rect(SDL_Rect{ x, y, w, h });
                register_toolbox_widget(cb_show_anim_.get());
            }
            if (tb_total_dx_) {
                const int field_height = metrics.total_dx_height > 0 ? metrics.total_dx_height
                                                                     : tb_total_dx_->height_for_width(metrics.totals_width);
                const int y = row_top + (metrics.row_height - field_height) / 2;
                const int x = reserve(metrics.totals_width);
                tb_total_dx_->set_rect(SDL_Rect{ x, y, metrics.totals_width, field_height });
                register_toolbox_widget(tb_total_dx_.get());
            }
            if (tb_total_dy_) {
                const int field_height = metrics.total_dy_height > 0 ? metrics.total_dy_height
                                                                     : tb_total_dy_->height_for_width(metrics.totals_width);
                const int y = row_top + (metrics.row_height - field_height) / 2;
                const int x = reserve(metrics.totals_width);
                tb_total_dy_->set_rect(SDL_Rect{ x, y, metrics.totals_width, field_height });
                register_toolbox_widget(tb_total_dy_.get());
            }

            if (btn_apply_all_movement_) {
                const int inner_w = std::max(0, toolbox_rect_.w - metrics.padding * 2);
                const int y = row_top + metrics.row_height + metrics.gap;
                btn_apply_all_movement_->set_rect(SDL_Rect{ toolbox_rect_.x + metrics.padding, y, inner_w, DMButton::height() });
                register_toolbox_widget(btn_apply_all_movement_.get());
            }
        }
    } else if (is_children_mode(mode_)) {
        ChildrenToolboxMetrics metrics = build_children_toolbox_metrics();
        if (metrics.width <= 0 || metrics.height <= 0) {
            toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, 0, 0 };
            toolbox_drag_rect_ = SDL_Rect{ 0, 0, 0, 0 };
        } else {
            toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, metrics.width, metrics.height };
            const int handle_height = std::max(0, metrics.drag_handle_height);
            const int drag_area_height = std::min(toolbox_rect_.h, handle_height + metrics.padding);
            toolbox_drag_rect_ = SDL_Rect{ toolbox_rect_.x, toolbox_rect_.y, toolbox_rect_.w, drag_area_height };
            const int content_width = std::max(0, toolbox_rect_.w - metrics.padding * 2);
            int row_cursor = toolbox_rect_.y + metrics.padding + handle_height;
            bool have_previous_row = false;
            auto allocate_row = [&](int row_height) -> int {
                if (row_height <= 0) return -1;
                if (have_previous_row) {
                    row_cursor += metrics.gap;
                }
                have_previous_row = true;
                int top = row_cursor;
                row_cursor += row_height;
                return top;
};

            const int row_left = toolbox_rect_.x + metrics.padding;

            if (dd_child_select_ && metrics.dropdown_row_height > 0) {
                const int row_top = allocate_row(metrics.dropdown_row_height);
                if (row_top >= 0) {
                    dd_child_select_->set_rect(SDL_Rect{
                        row_left,
                        row_top,
                        content_width,
                        metrics.dropdown_row_height
                    });
                    register_toolbox_widget(dd_child_select_.get());
                }
            }

            if (dd_child_mode_ && metrics.mode_row_height > 0) {
                const int row_top = allocate_row(metrics.mode_row_height);
                if (row_top >= 0) {
                    const int w = std::max(metrics.mode_dropdown_width, content_width);
                    dd_child_mode_->set_rect(SDL_Rect{ row_left, row_top, w, metrics.mode_row_height });
                    register_toolbox_widget(dd_child_mode_.get());
                }
            }

            if (metrics.movement_row_height > 0 && (cb_smooth_ || tb_total_dx_ || tb_total_dy_)) {
                const int row_top = allocate_row(metrics.movement_row_height);
                if (row_top >= 0) {
                    int tx = row_left;
                    auto reserve = [&](int w) -> int {
                        if (w <= 0) return tx;
                        int x = tx; tx += w + metrics.gap; return x;
};
                    if (cb_smooth_) {
                        const int w = std::max(metrics.smooth_checkbox_width, DMCheckbox::height());
                        const int h = DMCheckbox::height();
                        const int y = row_top + (metrics.movement_row_height - h) / 2;
                        const int x = reserve(w);
                        cb_smooth_->set_rect(SDL_Rect{ x, y, w, h });
                        register_toolbox_widget(cb_smooth_.get());
                    }
                    if (smooth_enabled_ && cb_curve_ && metrics.curve_checkbox_width > 0) {
                        const int w = std::max(metrics.curve_checkbox_width, DMCheckbox::height());
                        const int h = DMCheckbox::height();
                        const int y = row_top + (metrics.movement_row_height - h) / 2;
                        const int x = reserve(w);
                        cb_curve_->set_rect(SDL_Rect{ x, y, w, h });
                        register_toolbox_widget(cb_curve_.get());
                    }
                    if (tb_total_dx_) {
                        const int field_height = metrics.total_dx_height > 0 ? metrics.total_dx_height : tb_total_dx_->height_for_width(metrics.totals_width);
                        const int y = row_top + (metrics.movement_row_height - field_height) / 2;
                        const int x = reserve(metrics.totals_width);
                        tb_total_dx_->set_rect(SDL_Rect{ x, y, metrics.totals_width, field_height });
                        register_toolbox_widget(tb_total_dx_.get());
                    }
                    if (tb_total_dy_) {
                        const int field_height = metrics.total_dy_height > 0 ? metrics.total_dy_height : tb_total_dy_->height_for_width(metrics.totals_width);
                        const int y = row_top + (metrics.movement_row_height - field_height) / 2;
                        const int x = reserve(metrics.totals_width);
                        tb_total_dy_->set_rect(SDL_Rect{ x, y, metrics.totals_width, field_height });
                        register_toolbox_widget(tb_total_dy_.get());
                    }
                }
            }

            if (metrics.toggle_row_height > 0 && (cb_show_anim_ || cb_show_child_)) {
                const int row_top = allocate_row(metrics.toggle_row_height);
                if (row_top >= 0) {
                    int tx = row_left;
                    auto place_checkbox = [&](DMCheckbox* cb, int width) {
                        if (!cb || width <= 0) return;
                        const int h = DMCheckbox::height();
                        const int y = row_top + (metrics.toggle_row_height - h) / 2;
                        cb->set_rect(SDL_Rect{ tx, y, width, h });
                        register_toolbox_widget(cb);
                        tx += width + metrics.gap;
};
                    place_checkbox(cb_show_anim_.get(), metrics.show_parent_checkbox_width);
                    place_checkbox(cb_show_child_.get(), metrics.show_child_checkbox_width);
                }
            }

            if (metrics.form_row_height > 0 &&
                (tb_child_dx_ || tb_child_dy_ || tb_child_deg_ || cb_child_visible_ || cb_child_render_front_)) {
                const int row_top = allocate_row(metrics.form_row_height);
                if (row_top >= 0) {
                    int tx = row_left;
                    auto reserve = [&](int w) -> int {
                        int x = tx;
                        tx += w + metrics.gap;
                        return x;
};
                    auto place_textbox = [&](DMTextBox* tb, int height) {
                        if (!tb) return;
                        const int w = metrics.textbox_width;
                        const int h = height > 0 ? height : tb->height_for_width(w);
                        const int y = row_top + (metrics.form_row_height - h) / 2;
                        const int x = reserve(w);
                        tb->set_rect(SDL_Rect{ x, y, w, h });
                        register_toolbox_widget(tb);
};
                    place_textbox(tb_child_dx_.get(), metrics.child_dx_height);
                    place_textbox(tb_child_dy_.get(), metrics.child_dy_height);
                    place_textbox(tb_child_deg_.get(), metrics.child_rotation_height);
                    auto place_checkbox = [&](DMCheckbox* cb, int width) {
                        if (!cb || width <= 0) return;
                        const int w = std::max(width, DMCheckbox::height());
                        const int h = DMCheckbox::height();
                        const int y = row_top + (metrics.form_row_height - h) / 2;
                        const int x = reserve(w);
                        cb->set_rect(SDL_Rect{ x, y, w, h });
                        register_toolbox_widget(cb);
};
                    place_checkbox(cb_child_visible_.get(), metrics.child_visible_checkbox_width);
                    place_checkbox(cb_child_render_front_.get(), metrics.child_render_checkbox_width);
                }
            }

            if (metrics.name_row_height > 0 && (tb_child_name_ || btn_child_add_ || btn_child_remove_)) {
                const int row_top = allocate_row(metrics.name_row_height);
                if (row_top >= 0) {
                    int tx = row_left;
                    if (tb_child_name_) {
                        const int h = metrics.name_row_height > 0 ? metrics.name_row_height : tb_child_name_->height_for_width(metrics.name_textbox_width);
                        tb_child_name_->set_rect(SDL_Rect{ tx, row_top, metrics.name_textbox_width, h });
                        register_toolbox_widget(tb_child_name_.get());
                        tx += metrics.name_textbox_width + metrics.gap;
                    }
                    const int button_h = DMButton::height();
                    auto place_btn = [&](std::unique_ptr<DMButton>& btn) {
                        if (!btn) return;
                        btn->set_rect(SDL_Rect{ tx, row_top, metrics.child_action_button_width, button_h });
                        register_toolbox_widget(btn.get());
                        tx += metrics.child_action_button_width + metrics.gap;
};
                    place_btn(btn_child_add_);
                    place_btn(btn_child_remove_);
                }
            }

            if (btn_apply_all_children_) {
                const int apply_top = allocate_row(DMButton::height());
                if (apply_top >= 0) {
                    btn_apply_all_children_->set_rect(SDL_Rect{ row_left, apply_top, content_width, DMButton::height() });
                    register_toolbox_widget(btn_apply_all_children_.get());
                }
            }
        }
    } else if (mode_ == Mode::HitGeometry) {
        const int padding = DMSpacing::small_gap();
        const int gap = DMSpacing::small_gap();
        const int width = 360;
        const int handle_height = DMSpacing::small_gap();
        int content_y = padding + handle_height;
        const int inner_width = width - padding * 2;
        auto place_row = [&](int height) -> SDL_Rect {
            SDL_Rect r{ toolbox_pos_.x + padding, toolbox_pos_.y + content_y, inner_width, height };
            content_y += height + gap;
            return r;
};
        if (dd_hitbox_type_) {
            const int h = DMDropdown::height();
            dd_hitbox_type_->set_rect(place_row(h));
            register_toolbox_widget(dd_hitbox_type_.get());
        }
        if (btn_hitbox_add_remove_ || btn_hitbox_copy_next_) {
            const int row_h = DMButton::height();
            SDL_Rect row = place_row(row_h);
            const int button_width = (row.w - gap) / 2;
            if (btn_hitbox_add_remove_) {
                btn_hitbox_add_remove_->set_rect(SDL_Rect{ row.x, row.y, button_width, row_h });
                register_toolbox_widget(btn_hitbox_add_remove_.get());
            }
            if (btn_hitbox_copy_next_) {
                btn_hitbox_copy_next_->set_rect(SDL_Rect{ row.x + button_width + gap, row.y, button_width, row_h });
                register_toolbox_widget(btn_hitbox_copy_next_.get());
            }
        }
        auto place_pair = [&](DMTextBox* left, DMTextBox* right) {
            if (!left && !right) return;
            const int col_width = (inner_width - gap) / 2;
            const int left_h = left ? left->height_for_width(col_width) : DMTextBox::height();
            const int right_h = right ? right->height_for_width(col_width) : DMTextBox::height();
            const int row_h = std::max(left_h, right_h);
            SDL_Rect row = place_row(row_h);
            if (left) {
                left->set_rect(SDL_Rect{ row.x, row.y, col_width, row_h });
                register_toolbox_widget(left);
            }
            if (right) {
                right->set_rect(SDL_Rect{ row.x + col_width + gap, row.y, col_width, row_h });
                register_toolbox_widget(right);
            }
};
        place_pair(tb_hit_center_x_.get(), tb_hit_center_y_.get());
        place_pair(tb_hit_width_.get(), tb_hit_height_.get());
        if (tb_hit_rotation_) {
            const int rot_height = tb_hit_rotation_->height_for_width(inner_width);
            tb_hit_rotation_->set_rect(place_row(rot_height));
            register_toolbox_widget(tb_hit_rotation_.get());
        }
        if (btn_apply_all_hit_) {
            btn_apply_all_hit_->set_rect(place_row(DMButton::height()));
            register_toolbox_widget(btn_apply_all_hit_.get());
        }
        int total_height = content_y > padding ? content_y - gap + padding : padding * 2;
        toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, width, total_height };
        toolbox_drag_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, width, std::min(total_height, handle_height + padding) };
    } else if (mode_ == Mode::AttackGeometry) {
        const int padding = DMSpacing::small_gap();
        const int gap = DMSpacing::small_gap();
        const int width = 360;
        const int handle_height = DMSpacing::small_gap();
        int content_y = padding + handle_height;
        const int inner_width = width - padding * 2;
        auto place_row = [&](int height) -> SDL_Rect {
            SDL_Rect r{ toolbox_pos_.x + padding, toolbox_pos_.y + content_y, inner_width, height };
            content_y += height + gap;
            return r;
};
        if (dd_attack_type_) {
            const int h = DMDropdown::height();
            dd_attack_type_->set_rect(place_row(h));
            register_toolbox_widget(dd_attack_type_.get());
        }
        if (btn_attack_add_remove_ || btn_attack_delete_ || btn_attack_copy_next_) {
            const int row_h = DMButton::height();
            SDL_Rect row = place_row(row_h);
            int button_count = 0;
            if (btn_attack_add_remove_) ++button_count;
            if (btn_attack_delete_) ++button_count;
            if (btn_attack_copy_next_) ++button_count;
            button_count = std::max(1, button_count);
            const int total_gaps = (button_count - 1) * gap;
            const int button_width = (row.w - total_gaps) / button_count;
            int tx = row.x;
            auto place_btn = [&](DMButton* btn) {
                if (!btn) return;
                btn->set_rect(SDL_Rect{ tx, row.y, button_width, row_h });
                register_toolbox_widget(btn);
                tx += button_width + gap;
};
            place_btn(btn_attack_add_remove_.get());
            place_btn(btn_attack_delete_.get());
            place_btn(btn_attack_copy_next_.get());
        }
        auto place_pair = [&](DMTextBox* left, DMTextBox* right) {
            if (!left && !right) return;
            const int col_width = (inner_width - gap) / 2;
            const int left_h = left ? left->height_for_width(col_width) : DMTextBox::height();
            const int right_h = right ? right->height_for_width(col_width) : DMTextBox::height();
            const int row_h = std::max(left_h, right_h);
            SDL_Rect row = place_row(row_h);
            if (left) {
                left->set_rect(SDL_Rect{ row.x, row.y, col_width, row_h });
                register_toolbox_widget(left);
            }
            if (right) {
                right->set_rect(SDL_Rect{ row.x + col_width + gap, row.y, col_width, row_h });
                register_toolbox_widget(right);
            }
};
        place_pair(tb_attack_start_x_.get(), tb_attack_start_y_.get());
        place_pair(tb_attack_control_x_.get(), tb_attack_control_y_.get());
        place_pair(tb_attack_end_x_.get(), tb_attack_end_y_.get());
        if (tb_attack_damage_) {
            const int dmg_height = tb_attack_damage_->height_for_width(inner_width);
            tb_attack_damage_->set_rect(place_row(dmg_height));
            register_toolbox_widget(tb_attack_damage_.get());
        }
        if (btn_apply_all_attack_) {
            btn_apply_all_attack_->set_rect(place_row(DMButton::height()));
            register_toolbox_widget(btn_apply_all_attack_.get());
        }
        int total_height = content_y > padding ? content_y - gap + padding : padding * 2;
        toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, width, total_height };
        toolbox_drag_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, width, std::min(total_height, handle_height + padding) };
    } else {
        toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, 0, 0 };
        toolbox_drag_rect_ = SDL_Rect{ 0, 0, 0, 0 };
    }

    const int nav_w = 560;
    const int title_h = nav_header_height_px(dd_animation_select_ != nullptr);
    const int nav_vertical_padding = DMSpacing::small_gap() * 2;
    const int nav_drag_handle_height = DMSpacing::small_gap() * 2;
    const int nav_h = title_h + nav_vertical_padding + kNavPreviewHeight + kNavSliderGap + nav_drag_handle_height;
    nav_rect_ = SDL_Rect{ nav_pos_.x, nav_pos_.y, nav_w, nav_h };
    nav_drag_rect_ = SDL_Rect{ nav_rect_.x, nav_rect_.y, nav_rect_.w, std::min(nav_rect_.h, nav_drag_handle_height) };

    const int thumb_h = std::max(1, nav_rect_.h - nav_drag_handle_height - nav_vertical_padding - title_h - kNavSliderGap);
    const int thumb_w = thumb_h;
    const int content_top = nav_rect_.y + nav_drag_handle_height + DMSpacing::small_gap();
    const int thumb_top = content_top + title_h;
    const int btn_size = std::min(thumb_h, DMButton::height() * 2);
    if (btn_prev_) {
        btn_prev_->set_rect(SDL_Rect{ nav_rect_.x + DMSpacing::small_gap(), thumb_top, btn_size, btn_size });
    }
    if (btn_next_) {
        btn_next_->set_rect(SDL_Rect{ nav_rect_.x + nav_rect_.w - DMSpacing::small_gap() - btn_size, thumb_top, btn_size, btn_size });
    }

    const int spacing = kNavSpacing;
    const int viewport_left = (btn_prev_ ? btn_prev_->rect().x + btn_prev_->rect().w + spacing : nav_rect_.x + spacing);
    const int viewport_right = (btn_next_ ? btn_next_->rect().x - spacing : nav_rect_.x + nav_rect_.w - spacing);
    if (dd_animation_select_) {
        const int dropdown_h = DMDropdown::height();
        const int dropdown_w = std::max(120, viewport_right - viewport_left);
        const int dropdown_y = content_top;
        dd_animation_select_->set_rect(SDL_Rect{
            viewport_left,
            dropdown_y,
            std::max(0, dropdown_w),
            dropdown_h
        });
    }
    thumb_viewport_width_ = std::max(0, viewport_right - viewport_left);
    const int per = thumb_w + spacing;
    const int count = static_cast<int>(frames_.size());
    thumb_content_width_ = (per > 0 && count > 0) ? std::max(0, count * per - spacing) : 0;
    clamp_scroll_offset();

    thumb_rects_.clear();
    thumb_indices_.clear();
    const int viewport_right_px = viewport_left + thumb_viewport_width_;
    int current_x = viewport_left - scroll_offset_;
    for (int idx = 0; idx < count; ++idx) {
        SDL_Rect r{ current_x, thumb_top, thumb_w, thumb_h };
        if (thumb_viewport_width_ <= 0 ||
            (r.x + r.w >= viewport_left && r.x <= viewport_right_px)) {
            thumb_rects_.push_back(r);
            thumb_indices_.push_back(idx);
        }
        current_x += per;
    }

    const int scrollbar_height = 8;
    scrollbar_visible_ = thumb_content_width_ > thumb_viewport_width_ && thumb_viewport_width_ > 0;
    if (scrollbar_visible_) {
        scrollbar_track_ = SDL_Rect{
            viewport_left,
            nav_rect_.y + nav_rect_.h - scrollbar_height - spacing,
            thumb_viewport_width_,
            scrollbar_height
};
        const float viewport_ratio = thumb_content_width_ > 0
            ? static_cast<float>(thumb_viewport_width_) / static_cast<float>(thumb_content_width_) : 1.0f;
        int thumb_len = scrollbar_track_.w > 0
            ? std::max(20, static_cast<int>(std::round(static_cast<float>(scrollbar_track_.w) * viewport_ratio))) : scrollbar_track_.w;
        thumb_len = std::min(thumb_len, scrollbar_track_.w);
        const int max_scroll = max_scroll_offset();
        int thumb_x = scrollbar_track_.x;
        if (max_scroll > 0 && scrollbar_track_.w > thumb_len) {
            const float scroll_ratio = static_cast<float>(scroll_offset_) / static_cast<float>(max_scroll);
            thumb_x += static_cast<int>(std::round(scroll_ratio * static_cast<float>(scrollbar_track_.w - thumb_len)));
        }
        scrollbar_thumb_ = SDL_Rect{ thumb_x, scrollbar_track_.y, thumb_len, scrollbar_track_.h };
    } else {
        scrollbar_track_ = SDL_Rect{0, 0, 0, 0};
        scrollbar_thumb_ = SDL_Rect{0, 0, 0, 0};
        scroll_offset_ = 0;
    }
}

FrameEditorSession::DirectoryPanelMetrics FrameEditorSession::build_directory_panel_metrics() const {
    DirectoryPanelMetrics metrics;
    const int padding = DMSpacing::small_gap();
    const int drag_padding = DMSpacing::small_gap();
    const int vertical_padding = DMSpacing::small_gap();
    const int button_gap = DMSpacing::small_gap();
    metrics.top_padding = padding + drag_padding + vertical_padding;
    const int bottom_padding = padding + vertical_padding;
    metrics.height = metrics.top_padding + DMButton::height() + bottom_padding;

    int row_width = 0;
    auto append_button = [&](const std::unique_ptr<DMButton>& btn) {
        if (!btn) return;
        int w = std::max(btn->rect().w, btn->preferred_width());
        if (w <= 0) return;
        if (row_width > 0) {
            row_width += button_gap;
        }
        row_width += w;
};

    append_button(btn_back_);
    append_button(btn_movement_);
    append_button(btn_children_);
    append_button(btn_attack_geometry_);
    append_button(btn_hit_geometry_);

    const int content_width = row_width > 0 ? row_width : 0;
    metrics.width = std::max(kDirectoryPanelMinWidth, content_width + padding * 2);
    return metrics;
}

FrameEditorSession::MovementToolboxMetrics FrameEditorSession::build_movement_toolbox_metrics() const {
    MovementToolboxMetrics metrics;
    metrics.padding = DMSpacing::small_gap();
    metrics.gap = DMSpacing::small_gap();
    metrics.drag_handle_height = DMSpacing::small_gap();
    metrics.totals_width = kMovementTotalsFieldWidth;
    metrics.smooth_checkbox_width = cb_smooth_ ? std::max(kSmoothCheckboxMinWidth, cb_smooth_->preferred_width()) : 0;
    const bool curve_visible = smooth_enabled_ && cb_curve_;
    metrics.curve_checkbox_width = curve_visible ? std::max(kCurveCheckboxMinWidth, cb_curve_->preferred_width()) : 0;
    metrics.show_checkbox_width = cb_show_anim_ ? std::max(kShowAnimCheckboxMinWidth, cb_show_anim_->preferred_width()) : 0;
    metrics.total_dx_height = tb_total_dx_ ? tb_total_dx_->height_for_width(metrics.totals_width) : 0;
    metrics.total_dy_height = tb_total_dy_ ? tb_total_dy_->height_for_width(metrics.totals_width) : 0;
    int max_row_height = 0;
    if (cb_smooth_) max_row_height = std::max(max_row_height, DMCheckbox::height());
    if (curve_visible) max_row_height = std::max(max_row_height, DMCheckbox::height());
    if (cb_show_anim_) max_row_height = std::max(max_row_height, DMCheckbox::height());
    if (tb_total_dx_) max_row_height = std::max(max_row_height, metrics.total_dx_height);
    if (tb_total_dy_) max_row_height = std::max(max_row_height, metrics.total_dy_height);
    metrics.row_height = max_row_height;

    int row_width = 0;
    auto append = [&](int w) {
        if (w <= 0) return;
        if (row_width > 0) {
            row_width += metrics.gap;
        }
        row_width += w;
};
    if (cb_smooth_ && metrics.smooth_checkbox_width > 0) append(metrics.smooth_checkbox_width);
    if (curve_visible && metrics.curve_checkbox_width > 0) append(metrics.curve_checkbox_width);
    if (cb_show_anim_ && metrics.show_checkbox_width > 0) append(metrics.show_checkbox_width);
    if (tb_total_dx_) append(metrics.totals_width);
    if (tb_total_dy_) append(metrics.totals_width);
    if (row_width == 0) {
        metrics.row_height = 0;
        return metrics;
    }
    metrics.width = row_width + metrics.padding * 2;

    metrics.height = metrics.drag_handle_height + metrics.row_height + metrics.gap + DMButton::height() + metrics.padding * 2;
    return metrics;
}

FrameEditorSession::ChildrenToolboxMetrics FrameEditorSession::build_children_toolbox_metrics() const {
    ChildrenToolboxMetrics metrics;
    metrics.padding = DMSpacing::small_gap();
    metrics.gap = DMSpacing::small_gap();
    metrics.drag_handle_height = DMSpacing::small_gap();
    metrics.textbox_width = kChildrenFieldWidth;

    metrics.totals_width = kMovementTotalsFieldWidth;
    metrics.smooth_checkbox_width = cb_smooth_ ? std::max(kSmoothCheckboxMinWidth, cb_smooth_->preferred_width()) : 0;
    const bool curve_visible = smooth_enabled_ && cb_curve_;
    metrics.curve_checkbox_width = curve_visible ? std::max(kCurveCheckboxMinWidth, cb_curve_->preferred_width()) : 0;
    metrics.total_dx_height = tb_total_dx_ ? tb_total_dx_->height_for_width(metrics.totals_width) : 0;
    metrics.total_dy_height = tb_total_dy_ ? tb_total_dy_->height_for_width(metrics.totals_width) : 0;
    int movement_row_height = 0;
    if (cb_smooth_) movement_row_height = std::max(movement_row_height, DMCheckbox::height());
    if (curve_visible) movement_row_height = std::max(movement_row_height, DMCheckbox::height());
    if (tb_total_dx_) movement_row_height = std::max(movement_row_height, metrics.total_dx_height);
    if (tb_total_dy_) movement_row_height = std::max(movement_row_height, metrics.total_dy_height);
    metrics.movement_row_height = movement_row_height;
    metrics.child_dx_height = tb_child_dx_ ? tb_child_dx_->height_for_width(metrics.textbox_width) : 0;
    metrics.child_dy_height = tb_child_dy_ ? tb_child_dy_->height_for_width(metrics.textbox_width) : 0;
    metrics.child_rotation_height = tb_child_deg_ ? tb_child_deg_->height_for_width(metrics.textbox_width) : 0;
    const int max_textbox_height = std::max( metrics.child_dx_height, std::max(metrics.child_dy_height, metrics.child_rotation_height));
    const int checkbox_height = DMCheckbox::height();
    metrics.child_visible_checkbox_width = cb_child_visible_
                                               ? std::max(kChildVisibilityCheckboxMinWidth, cb_child_visible_->preferred_width()) : 0;
    metrics.child_render_checkbox_width = cb_child_render_front_
                                              ? std::max(kChildVisibilityCheckboxMinWidth, cb_child_render_front_->preferred_width()) : 0;
    metrics.mode_dropdown_width = dd_child_mode_ ? kChildDropdownMinWidth : 0;
    metrics.mode_row_height = dd_child_mode_ ? dd_child_mode_->preferred_height(kChildDropdownMinWidth) : 0;
    metrics.name_textbox_width = tb_child_name_ ? std::max(kChildDropdownMinWidth, kChildrenFieldWidth) : 0;
    metrics.name_row_height = tb_child_name_ ? tb_child_name_->height_for_width(metrics.name_textbox_width) : 0;
    metrics.child_action_button_width = std::max({ btn_child_add_ ? btn_child_add_->preferred_width() : 0,
                                                  btn_child_remove_ ? btn_child_remove_->preferred_width() : 0,
                                                  120 });
    metrics.show_parent_checkbox_width = cb_show_anim_
                                             ? std::max(kShowAnimCheckboxMinWidth, cb_show_anim_->preferred_width()) : 0;
    metrics.show_child_checkbox_width = cb_show_child_
                                            ? std::max(kShowChildCheckboxMinWidth, cb_show_child_->preferred_width()) : 0;
    int form_content_height = max_textbox_height;
    if (cb_child_visible_) {
        form_content_height = std::max(form_content_height, checkbox_height);
    }
    if (cb_child_render_front_) {
        form_content_height = std::max(form_content_height, checkbox_height);
    }
    metrics.form_row_height = form_content_height > 0 ? form_content_height : checkbox_height;

    int dropdown_row_width = dd_child_select_ ? kChildDropdownMinWidth : 0;

    int toggle_row_width = 0;
    auto append_toggle = [&](int w) {
        if (w <= 0) return;
        if (toggle_row_width > 0) toggle_row_width += metrics.gap;
        toggle_row_width += w;
};
    append_toggle(metrics.show_parent_checkbox_width);
    append_toggle(metrics.show_child_checkbox_width);

    int form_row_width = 0;
    auto append_form = [&](int w) {
        if (w <= 0) return;
        if (form_row_width > 0) form_row_width += metrics.gap;
        form_row_width += w;
};

    int movement_row_width = 0;
    auto append_movement = [&](int w) {
        if (w <= 0) return;
        if (movement_row_width > 0) movement_row_width += metrics.gap;
        movement_row_width += w;
};
    if (cb_smooth_ && metrics.smooth_checkbox_width > 0) append_movement(metrics.smooth_checkbox_width);
    if (curve_visible && metrics.curve_checkbox_width > 0) append_movement(metrics.curve_checkbox_width);
    if (tb_total_dx_) append_movement(metrics.totals_width);
    if (tb_total_dy_) append_movement(metrics.totals_width);

    if (tb_child_dx_) append_form(metrics.textbox_width);
    if (tb_child_dy_) append_form(metrics.textbox_width);
    if (tb_child_deg_) append_form(metrics.textbox_width);
    if (cb_child_visible_ && metrics.child_visible_checkbox_width > 0) append_form(metrics.child_visible_checkbox_width);
    if (cb_child_render_front_ && metrics.child_render_checkbox_width > 0) append_form(metrics.child_render_checkbox_width);
    if (form_row_width == 0) {
        metrics.form_row_height = 0;
    }

    metrics.toggle_row_height = toggle_row_width > 0 ? checkbox_height : 0;

    int mode_row_width = metrics.mode_dropdown_width;
    int name_row_width = metrics.name_textbox_width > 0
        ? metrics.name_textbox_width + metrics.gap + metrics.child_action_button_width * 2 + metrics.gap
        : 0;
    int content_width = std::max({dropdown_row_width, movement_row_width, toggle_row_width, form_row_width, mode_row_width, name_row_width});
    if (dd_child_select_) {
        const int dropdown_width = std::max(content_width, std::max(kChildDropdownMinWidth, dropdown_row_width));
        metrics.dropdown_row_height = dd_child_select_->preferred_height(std::max(dropdown_width, kChildDropdownMinWidth));
        content_width = std::max(content_width, dropdown_width);
    } else {
        metrics.dropdown_row_height = 0;
    }

    if (dd_child_mode_) {
        metrics.mode_row_height = std::max(metrics.mode_row_height, DMDropdown::height());
    }
    if (tb_child_name_) {
        metrics.name_row_height = std::max(metrics.name_row_height, DMButton::height());
    }

    if (content_width <= 0) {
        metrics.width = 0;
        metrics.height = 0;
        return metrics;
    }

    metrics.width = content_width + metrics.padding * 2;
    metrics.height = metrics.padding * 2;
    bool added_row = false;
    auto add_row = [&](int row_height) {
        if (row_height <= 0) return;
        if (added_row) {
            metrics.height += metrics.gap;
        }
        metrics.height += row_height;
        added_row = true;
};
    add_row(metrics.dropdown_row_height);
    add_row(metrics.mode_row_height);
    add_row(metrics.movement_row_height);
    add_row(metrics.toggle_row_height);
    add_row(metrics.form_row_height);
    add_row(metrics.name_row_height);

    add_row(DMButton::height());
    metrics.height += metrics.drag_handle_height;
    return metrics;
}

animation_update::FrameHitGeometry::HitBox* FrameEditorSession::current_hit_box() {
    if (frames_.empty()) return nullptr;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    const std::string type = current_hitbox_type();
    return frame.hit.find_box(type);
}

const animation_update::FrameHitGeometry::HitBox* FrameEditorSession::current_hit_box() const {
    if (frames_.empty()) return nullptr;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    const auto& frame = frames_[frame_index];
    const std::string type = current_hitbox_type();
    return frame.hit.find_box(type);
}

animation_update::FrameHitGeometry::HitBox* FrameEditorSession::ensure_hit_box_for_type(const std::string& type) {
    if (frames_.empty()) return nullptr;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    if (auto* existing = frame.hit.find_box(type)) {
        return existing;
    }
    animation_update::FrameHitGeometry::HitBox box;
    box.type = type;
    box.center_x = 0.0f;
    box.center_y = 40.0f;
    box.half_width = 40.0f;
    box.half_height = 60.0f;
    box.rotation_degrees = 0.0f;
    frame.hit.boxes.push_back(box);
    return frame.hit.find_box(type);
}

void FrameEditorSession::delete_hit_box_for_type(const std::string& type) {
    if (frames_.empty()) return;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    auto& boxes = frame.hit.boxes;
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [&](const auto& box) {
        return box.type == type;
    }), boxes.end());
}

std::string FrameEditorSession::current_hitbox_type() const {
    int idx = std::clamp(selected_hitbox_type_index_, 0, static_cast<int>(kDamageTypeNames.size()) - 1);
    return std::string{kDamageTypeNames[idx]};
}

void FrameEditorSession::refresh_hitbox_form() const {
    if (mode_ != Mode::HitGeometry) {
        return;
    }
    const auto* box = current_hit_box();
    auto sync_field = [&](DMTextBox* tb, std::string& cache, std::string value) {
        if (!tb || tb->is_editing()) return;
        if (tb->value() != value) {
            tb->set_value(value);
        }
        cache = tb->value();
};
    if (box) {
        sync_field(tb_hit_center_x_.get(), last_hit_center_x_text_, std::to_string(static_cast<int>(std::lround(box->center_x))));
        sync_field(tb_hit_center_y_.get(), last_hit_center_y_text_, std::to_string(static_cast<int>(std::lround(box->center_y))));
        sync_field(tb_hit_width_.get(), last_hit_width_text_, std::to_string(static_cast<int>(std::lround(box->half_width * 2.0f))));
        sync_field(tb_hit_height_.get(), last_hit_height_text_, std::to_string(static_cast<int>(std::lround(box->half_height * 2.0f))));
        if (tb_hit_rotation_ && !tb_hit_rotation_->is_editing()) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << box->rotation_degrees;
            sync_field(tb_hit_rotation_.get(), last_hit_rotation_text_, oss.str());
        }
    } else {
        sync_field(tb_hit_center_x_.get(), last_hit_center_x_text_, "0");
        sync_field(tb_hit_center_y_.get(), last_hit_center_y_text_, "0");
        sync_field(tb_hit_width_.get(), last_hit_width_text_, "0");
        sync_field(tb_hit_height_.get(), last_hit_height_text_, "0");
        sync_field(tb_hit_rotation_.get(), last_hit_rotation_text_, "0");
    }
    if (btn_hitbox_add_remove_) {
        btn_hitbox_add_remove_->set_text(box ? "Delete Hit Box" : "Add Hit Box");
    }
}

animation_update::FrameAttackGeometry::Vector* FrameEditorSession::current_attack_vector() {
    if (frames_.empty()) return nullptr;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    clamp_attack_selection();
    const int vector_index = current_attack_vector_index();
    if (vector_index < 0) return nullptr;
    return frame.attack.vector_at(current_attack_type(), static_cast<std::size_t>(vector_index));
}

const animation_update::FrameAttackGeometry::Vector* FrameEditorSession::current_attack_vector() const {
    if (frames_.empty()) return nullptr;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    const auto& frame = frames_[frame_index];
    const_cast<FrameEditorSession*>(this)->clamp_attack_selection();
    const int vector_index = current_attack_vector_index();
    if (vector_index < 0) return nullptr;
    return frame.attack.vector_at(current_attack_type(), static_cast<std::size_t>(vector_index));
}

animation_update::FrameAttackGeometry::Vector* FrameEditorSession::ensure_attack_vector_for_type(const std::string& type) {
    if (frames_.empty()) return nullptr;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    const std::size_t existing_count = frame.attack.count_for_type(type);
    animation_update::FrameAttackGeometry::Vector vec;
    vec.type = type;
    vec.start_x = 0.0f;
    vec.start_y = 0.0f;
    vec.control_x = 0.0f;
    vec.control_y = 0.0f;
    vec.end_x = 0.0f;
    vec.end_y = 0.0f;
    vec.damage = 0;
    animation_update::FrameAttackGeometry::Vector& created = frame.attack.add_vector(type, vec);
    set_current_attack_vector_index(static_cast<int>(existing_count));
    return &created;
}

void FrameEditorSession::delete_current_attack_vector() {
    if (frames_.empty()) return;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    const int index = current_attack_vector_index();
    if (index < 0) return;
    frame.attack.erase_vector(current_attack_type(), static_cast<std::size_t>(index));
    clamp_attack_selection();
}

std::string FrameEditorSession::current_attack_type() const {
    int idx = std::clamp(selected_attack_type_index_, 0, static_cast<int>(kDamageTypeNames.size()) - 1);
    return std::string{kDamageTypeNames[idx]};
}

int FrameEditorSession::current_attack_vector_index() const {
    const int type_idx = std::clamp(selected_attack_type_index_, 0, static_cast<int>(kDamageTypeNames.size()) - 1);
    return selected_attack_vector_indices_[static_cast<std::size_t>(type_idx)];
}

void FrameEditorSession::set_current_attack_vector_index(int index) {
    const int type_idx = std::clamp(selected_attack_type_index_, 0, static_cast<int>(kDamageTypeNames.size()) - 1);
    selected_attack_vector_indices_[static_cast<std::size_t>(type_idx)] = index;
}

void FrameEditorSession::clamp_attack_selection() {
    if (frames_.empty()) {
        set_current_attack_vector_index(-1);
        return;
    }
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    const std::string type = current_attack_type();
    const std::size_t count = frame.attack.count_for_type(type);
    if (count == 0) {
        set_current_attack_vector_index(-1);
        return;
    }
    int idx = current_attack_vector_index();
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(count)) idx = static_cast<int>(count - 1);
    set_current_attack_vector_index(idx);
}

void FrameEditorSession::refresh_attack_form() const {
    if (mode_ != Mode::AttackGeometry) {
        return;
    }
    const_cast<FrameEditorSession*>(this)->clamp_attack_selection();
    const auto* vec = current_attack_vector();
    auto sync_field = [&](DMTextBox* tb, std::string& cache, const std::string& value) {
        if (!tb || tb->is_editing()) return;
        if (tb->value() != value) {
            tb->set_value(value);
        }
        cache = tb->value();
};
    if (vec) {
        sync_field(tb_attack_start_x_.get(), last_attack_start_x_text_, std::to_string(static_cast<int>(std::lround(vec->start_x))));
        sync_field(tb_attack_start_y_.get(), last_attack_start_y_text_, std::to_string(static_cast<int>(std::lround(vec->start_y))));
        sync_field(tb_attack_control_x_.get(), last_attack_control_x_text_, std::to_string(static_cast<int>(std::lround(vec->control_x))));
        sync_field(tb_attack_control_y_.get(), last_attack_control_y_text_, std::to_string(static_cast<int>(std::lround(vec->control_y))));
        sync_field(tb_attack_end_x_.get(), last_attack_end_x_text_, std::to_string(static_cast<int>(std::lround(vec->end_x))));
        sync_field(tb_attack_end_y_.get(), last_attack_end_y_text_, std::to_string(static_cast<int>(std::lround(vec->end_y))));
        sync_field(tb_attack_damage_.get(), last_attack_damage_text_, std::to_string(vec->damage));
    } else {
        sync_field(tb_attack_start_x_.get(), last_attack_start_x_text_, "0");
        sync_field(tb_attack_start_y_.get(), last_attack_start_y_text_, "0");
        sync_field(tb_attack_control_x_.get(), last_attack_control_x_text_, "0");
        sync_field(tb_attack_control_y_.get(), last_attack_control_y_text_, "0");
        sync_field(tb_attack_end_x_.get(), last_attack_end_x_text_, "0");
        sync_field(tb_attack_end_y_.get(), last_attack_end_y_text_, "0");
        sync_field(tb_attack_damage_.get(), last_attack_damage_text_, "0");
    }
    if (btn_attack_add_remove_) {
        btn_attack_add_remove_->set_text("Add Attack");
    }
    if (btn_attack_delete_) {
        btn_attack_delete_->set_text("Delete Attack");
    }
}

void FrameEditorSession::copy_attack_vector_to_next_frame() {
    if (frames_.empty()) return;
    const int next_index = selected_index_ + 1;
    if (next_index >= static_cast<int>(frames_.size())) {
        return;
    }
    const std::string type = current_attack_type();
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    const auto& src_vectors = frames_[frame_index].attack.vectors;
    std::vector<animation_update::FrameAttackGeometry::Vector> to_copy;
    to_copy.reserve(src_vectors.size());
    for (const auto& v : src_vectors) {
        if (v.type == type) {
            to_copy.push_back(v);
        }
    }
    auto& dest_frame = frames_[next_index];
    auto& dest_vecs = dest_frame.attack.vectors;
    dest_vecs.erase(std::remove_if(dest_vecs.begin(), dest_vecs.end(), [&](const auto& v) {
        return v.type == type;
    }), dest_vecs.end());
    dest_vecs.insert(dest_vecs.end(), to_copy.begin(), to_copy.end());
    persist_changes();
}

void FrameEditorSession::copy_hit_box_to_next_frame() {
    if (frames_.empty()) return;
    const int next_index = selected_index_ + 1;
    if (next_index >= static_cast<int>(frames_.size())) {
        return;
    }
    const std::string type = current_hitbox_type();
    const auto* source = current_hit_box();
    if (!source) return;
    auto& dest_frame = frames_[next_index];
    auto* dest = dest_frame.hit.find_box(type);
    if (!dest) {
        dest_frame.hit.boxes.push_back(*source);
    } else {
        *dest = *source;
    }
    persist_changes();
}

void FrameEditorSession::apply_current_mode_to_all_frames() {
    if (frames_.empty()) return;
    switch (mode_) {
        case Mode::Movement: {
            const int idx = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
            const MovementFrame src = frames_[idx];
            for (size_t i = 1; i < frames_.size(); ++i) {
                frames_[i].dx = src.dx;
                frames_[i].dy = src.dy;
                frames_[i].resort_z = src.resort_z;
            }
            rebuild_rel_positions();
            persist_mode_changes(Mode::Movement);
            persist_changes();
            break;
        }
        case Mode::StaticChildren:
        case Mode::AsyncChildren: {
            if (child_assets_.empty()) break;
            const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
            const auto& frame = frames_[frame_index];
            const int child_index = std::clamp(selected_child_index_, 0, static_cast<int>(frame.children.size()) - 1);
            ChildFrame src = frame.children.empty() ? ChildFrame{} : frame.children[child_index];
            src.has_data = true;
            for (auto& f : frames_) {
                if (child_index >= 0 && child_index < static_cast<int>(f.children.size())) {
                    auto& d = f.children[child_index];
                    d.dx = src.dx;
                    d.dy = src.dy;
                    d.degree = src.degree;
                    d.visible = src.visible;
                    d.render_in_front = src.render_in_front;
                    d.has_data = true;
                }
            }
            persist_mode_changes(mode_);
            persist_changes();
            break;
        }
        case Mode::HitGeometry: {
            const std::string type = current_hitbox_type();
            const auto* source = current_hit_box();
            for (auto& f : frames_) {
                auto& boxes = f.hit.boxes;
                boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [&](const auto& b) { return b.type == type; }), boxes.end());
                if (source) {
                    boxes.push_back(*source);
                }
            }
            refresh_hitbox_form();
            persist_mode_changes(Mode::HitGeometry);
            persist_changes();
            break;
        }
        case Mode::AttackGeometry: {
            const std::string type = current_attack_type();
            const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
            const auto& source_vecs = frames_[frame_index].attack.vectors;
            std::vector<animation_update::FrameAttackGeometry::Vector> type_vecs;
            type_vecs.reserve(source_vecs.size());
            for (const auto& v : source_vecs) {
                if (v.type == type) {
                    type_vecs.push_back(v);
                }
            }
            for (auto& f : frames_) {
                auto& vecs = f.attack.vectors;
                vecs.erase(std::remove_if(vecs.begin(), vecs.end(), [&](const auto& v) { return v.type == type; }), vecs.end());
                vecs.insert(vecs.end(), type_vecs.begin(), type_vecs.end());
            }
            refresh_attack_form();
            persist_mode_changes(Mode::AttackGeometry);
            persist_changes();
            break;
        }
    }
}

float FrameEditorSession::asset_local_scale() const {
    float scale = 1.0f;
    if (target_ && target_->info && std::isfinite(target_->info->scale_factor) && target_->info->scale_factor > 0.0f) {
        scale *= target_->info->scale_factor;
    }
    if (document_) {
        double pct = document_->scale_percentage();
        if (std::isfinite(pct) && pct > 0.0) {
            scale *= static_cast<float>(pct / 100.0);
        }
    }
    return scale;
}

float FrameEditorSession::document_scale_factor() const {
    if (!document_) {
        return 1.0f;
    }
    double pct = document_->scale_percentage();
    if (!std::isfinite(pct) || pct <= 0.0) {
        return 1.0f;
    }
    return static_cast<float>(pct / 100.0);
}

float FrameEditorSession::attachment_scale() const {
    if (!assets_ || !target_) {
        return 1.0f;
    }
    const WarpedScreenGrid& cam = assets_->getView();
    float perspective_scale = 1.0f;
    if (target_->info && target_->info->apply_distance_scaling) {
        if (const auto* gp = cam.grid_point_for_asset(target_)) {
            perspective_scale = std::max(0.0001f, gp->perspective_scale);
        }
    }
    float remainder = target_->current_remaining_scale_adjustment;
    if (!std::isfinite(remainder) || remainder <= 0.0f) {
        remainder = 1.0f;
    }
    float scale = remainder / std::max(0.0001f, perspective_scale);
    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = 1.0f;
    }
    return scale;
}

SDL_Point FrameEditorSession::asset_anchor_world() const {
    if (!target_) {
        return SDL_Point{0, 0};
    }
    return animation_update::detail::bottom_middle_for(*target_, target_->pos);
}

bool FrameEditorSession::screen_to_local(SDL_Point screen, SDL_FPoint& out_local) const {
    if (!assets_ || !target_) return false;
    const WarpedScreenGrid& cam = assets_->getView();
    SDL_FPoint world = cam.screen_to_map(screen);
    SDL_Point anchor = asset_anchor_world();
    const float scale = asset_local_scale();
    if (scale <= 0.0001f) return false;
    out_local.x = (world.x - static_cast<float>(anchor.x)) / scale;
    out_local.y = (static_cast<float>(anchor.y) - world.y) / scale;
    return std::isfinite(out_local.x) && std::isfinite(out_local.y);
}

bool FrameEditorSession::build_hitbox_visual(const animation_update::FrameHitGeometry::HitBox& box,
                                             HitBoxVisual& out) const {
    if (!assets_ || !target_) return false;
    const WarpedScreenGrid& cam = assets_->getView();
    SDL_Point anchor = asset_anchor_world();
    const float scale = asset_local_scale();
    if (scale <= 0.0001f) return false;

    const float cos_r = std::cos(box.rotation_degrees * kDegToRad);
    const float sin_r = std::sin(box.rotation_degrees * kDegToRad);
    auto rotate_vec = [&](SDL_FPoint v) -> SDL_FPoint {
        return SDL_FPoint{
            v.x * cos_r - v.y * sin_r,
            v.x * sin_r + v.y * cos_r
        };
    };
    auto to_screen = [&](SDL_FPoint local) -> SDL_FPoint {
        SDL_FPoint world{
            static_cast<float>(anchor.x) + local.x * scale, static_cast<float>(anchor.y) - local.y * scale };
        return cam.map_to_screen_f(world);
    };

    SDL_FPoint center_local{box.center_x, box.center_y};
    out.center = to_screen(center_local);

    std::array<SDL_FPoint, 4> local_corners = {
        SDL_FPoint{-box.half_width,  box.half_height},
        SDL_FPoint{ box.half_width,  box.half_height},
        SDL_FPoint{ box.half_width, -box.half_height},
        SDL_FPoint{-box.half_width, -box.half_height}
};
    for (std::size_t i = 0; i < local_corners.size(); ++i) {
        SDL_FPoint rotated = rotate_vec(local_corners[i]);
        rotated.x += center_local.x;
        rotated.y += center_local.y;
        out.corners[i] = to_screen(rotated);
    }
    for (std::size_t i = 0; i < out.corners.size(); ++i) {
        const SDL_FPoint& a = out.corners[i];
        const SDL_FPoint& b = out.corners[(i + 1) % 4];
        out.edge_midpoints[i] = SDL_FPoint{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
    }
    SDL_FPoint handle_local{0.0f, box.half_height + (20.0f / std::max(scale, 0.001f))};
    SDL_FPoint rotated_handle = rotate_vec(handle_local);
    rotated_handle.x += center_local.x;
    rotated_handle.y += center_local.y;
    out.rotate_handle = to_screen(rotated_handle);
    return true;
}

void FrameEditorSession::render_hit_geometry(SDL_Renderer* renderer) const {
    if (!renderer || frames_.empty() || mode_ != Mode::HitGeometry) return;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    const auto& frame = frames_[frame_index];
    if (frame.hit.boxes.empty()) return;
    const std::string type = current_hitbox_type();
    for (const auto& box : frame.hit.boxes) {
        HitBoxVisual visual;
        if (!build_hitbox_visual(box, visual)) continue;
        const bool selected = (box.type == type);

        bool hovered_any = false;
        int hovered_edge_index = -1;
        bool hovered_rotate = false;
        if (selected) {
            SDL_Point mp{0, 0};
            SDL_GetMouseState(&mp.x, &mp.y);

            if (!(SDL_PointInRect(&mp, &directory_rect_) ||
                  SDL_PointInRect(&mp, &nav_rect_) ||
                  SDL_PointInRect(&mp, &toolbox_rect_))) {
                const int handle_size = 12;
                auto point_in_rect = [&](const SDL_FPoint& center) {
                    SDL_Rect r{ static_cast<int>(std::round(center.x)) - handle_size / 2,
                                static_cast<int>(std::round(center.y)) - handle_size / 2, handle_size, handle_size };
                    return SDL_PointInRect(&mp, &r) == SDL_TRUE;
                };

                SDL_FPoint mpf{ static_cast<float>(mp.x), static_cast<float>(mp.y) };
                const float rotate_radius = 12.0f;
                if (dist_sq(mpf, visual.rotate_handle) <= rotate_radius * rotate_radius) {
                    hovered_any = true;
                    hovered_rotate = true;
                } else {

                    if (point_in_rect(visual.edge_midpoints[3])) { hovered_any = true; hovered_edge_index = 3; }
                    else if (point_in_rect(visual.edge_midpoints[1])) { hovered_any = true; hovered_edge_index = 1; }
                    else if (point_in_rect(visual.edge_midpoints[0])) { hovered_any = true; hovered_edge_index = 0; }
                    else if (point_in_rect(visual.edge_midpoints[2])) { hovered_any = true; hovered_edge_index = 2; }
                    else {

                        bool inside = false;
                        for (int i = 0, j = 3; i < 4; j = i++) {
                            const SDL_FPoint& a = visual.corners[i];
                            const SDL_FPoint& b = visual.corners[j];
                            const bool intersect = ((a.y > mpf.y) != (b.y > mpf.y)) && (mpf.x < (b.x - a.x) * (mpf.y - a.y) / (b.y - a.y + 0.0001f) + a.x);
                            if (intersect) inside = !inside;
                        }
                        if (inside) {
                            hovered_any = true;
                        }
                    }
                }
            }
        }

        SDL_Color fill = selected ? DMStyles::AccentButton().bg : DMStyles::HeaderButton().bg;
        fill.a = selected ? 90 : 45;
        SDL_Color outline = selected ? DMStyles::AccentButton().border : DMStyles::Border();
        if (selected && hovered_any) {
            outline = SDL_Color{255, 255, 255, 255};
        }
        SDL_Vertex verts[4];
        int indices[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 4; ++i) {
            verts[i].position.x = visual.corners[i].x;
            verts[i].position.y = visual.corners[i].y;
            verts[i].color = fill;
            verts[i].tex_coord = SDL_FPoint{0.0f, 0.0f};
        }
        SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
        SDL_SetRenderDrawColor(renderer, outline.r, outline.g, outline.b, 220);
        for (int i = 0; i < 4; ++i) {
            const SDL_FPoint& a = visual.corners[i];
            const SDL_FPoint& b = visual.corners[(i + 1) % 4];
            SDL_RenderDrawLineF(renderer, a.x, a.y, b.x, b.y);
        }
        if (selected) {

            const int base_handle_size = 10;
            for (int i = 0; i < 4; ++i) {
                const bool is_hovered_handle = (i == hovered_edge_index);
                const int handle_size = is_hovered_handle ? (base_handle_size + 2) : base_handle_size;
                SDL_FRect r{ visual.edge_midpoints[i].x - handle_size * 0.5f,
                             visual.edge_midpoints[i].y - handle_size * 0.5f,
                             static_cast<float>(handle_size), static_cast<float>(handle_size) };
                SDL_Color node_col = is_hovered_handle ? SDL_Color{255, 255, 255, 255}
                                                       : DMStyles::AccentButton().hover_bg;
                SDL_SetRenderDrawColor(renderer, node_col.r, node_col.g, node_col.b, 255);
                SDL_RenderFillRectF(renderer, &r);
            }

            const SDL_FPoint top_mid = visual.edge_midpoints[0];
            SDL_SetRenderDrawColor(renderer, (hovered_rotate ? 255 : DMStyles::AccentButton().hover_bg.r), (hovered_rotate ? 255 : DMStyles::AccentButton().hover_bg.g), (hovered_rotate ? 255 : DMStyles::AccentButton().hover_bg.b), 255);
            SDL_RenderDrawLineF(renderer, top_mid.x, top_mid.y, visual.rotate_handle.x, visual.rotate_handle.y);
            const float radius = 8.0f;
            for (int i = 0; i < 16; ++i) {
                const float a = (static_cast<float>(i) / 16.0f) * 2.0f * static_cast<float>(M_PI);
                const float b = (static_cast<float>(i + 1) / 16.0f) * 2.0f * static_cast<float>(M_PI);
                SDL_RenderDrawLineF(renderer, visual.rotate_handle.x + std::cos(a) * radius, visual.rotate_handle.y + std::sin(a) * radius, visual.rotate_handle.x + std::cos(b) * radius, visual.rotate_handle.y + std::sin(b) * radius);
            }
        }
    }
}

bool FrameEditorSession::begin_hitbox_drag(SDL_Point mouse) {
    if (mode_ != Mode::HitGeometry) return false;
    auto* box = current_hit_box();
    if (!box) return false;
    HitBoxVisual visual;
    if (!build_hitbox_visual(*box, visual)) return false;
    active_hitbox_handle_ = HitHandle::None;
    const int handle_size = 12;
    auto point_in_rect = [&](const SDL_FPoint& center) {
        SDL_Rect r{ static_cast<int>(std::round(center.x)) - handle_size / 2,
                    static_cast<int>(std::round(center.y)) - handle_size / 2, handle_size, handle_size };
        return SDL_PointInRect(&mouse, &r) == SDL_TRUE;
    };
    SDL_FPoint mouse_f{ static_cast<float>(mouse.x), static_cast<float>(mouse.y) };
    const float rotate_radius = kHitboxRotateHandleRadius;
    if (dist_sq(mouse_f, visual.rotate_handle) <= rotate_radius * rotate_radius) {
        active_hitbox_handle_ = HitHandle::Rotate;
    } else {

        if (point_in_rect(visual.edge_midpoints[3])) active_hitbox_handle_ = HitHandle::Left;
        else if (point_in_rect(visual.edge_midpoints[1])) active_hitbox_handle_ = HitHandle::Right;
        else if (point_in_rect(visual.edge_midpoints[0])) active_hitbox_handle_ = HitHandle::Top;
        else if (point_in_rect(visual.edge_midpoints[2])) active_hitbox_handle_ = HitHandle::Bottom;
        else {

            bool inside = false;
            for (int i = 0, j = 3; i < 4; j = i++) {
                const SDL_FPoint& a = visual.corners[i];
                const SDL_FPoint& b = visual.corners[j];
                const bool intersect = ((a.y > mouse_f.y) != (b.y > mouse_f.y)) && (mouse_f.x < (b.x - a.x) * (mouse_f.y - a.y) / (b.y - a.y + 0.0001f) + a.x);
                if (intersect) inside = !inside;
            }
            if (inside) {
                active_hitbox_handle_ = HitHandle::Move;
            }
        }
    }
    if (active_hitbox_handle_ == HitHandle::None) {
        return false;
    }
    hitbox_dragging_ = true;
    hitbox_drag_start_mouse_ = mouse;
    hitbox_drag_start_box_ = *box;
    hitbox_drag_left_ = -box->half_width;
    hitbox_drag_right_ = box->half_width;
    hitbox_drag_top_ = box->half_height;
    hitbox_drag_bottom_ = -box->half_height;
    SDL_FPoint local_mouse{};
    if (!screen_to_local(mouse, local_mouse)) {
        hitbox_dragging_ = false;
        active_hitbox_handle_ = HitHandle::None;
        return false;
    }
    hitbox_drag_grab_offset_.x = local_mouse.x - box->center_x;
    hitbox_drag_grab_offset_.y = local_mouse.y - box->center_y;
    return true;
}

void FrameEditorSession::update_hitbox_drag(SDL_Point mouse) {
    if (!hitbox_dragging_) return;
    auto* box = current_hit_box();
    if (!box) return;
    SDL_FPoint local{};
    if (!screen_to_local(mouse, local)) {
        return;
    }
    constexpr float kMinHalf = 2.0f;
    const float rotation = hitbox_drag_start_box_.rotation_degrees;
    const float cos_r = std::cos(rotation * kDegToRad);
    const float sin_r = std::sin(rotation * kDegToRad);
    auto rotate_to_box = [&](float dx, float dy) -> SDL_FPoint {
        return SDL_FPoint{
            dx * cos_r + dy * sin_r,
            -dx * sin_r + dy * cos_r
        };
    };
    auto rotate_to_world = [&](SDL_FPoint v) -> SDL_FPoint {
        return SDL_FPoint{
            v.x * cos_r - v.y * sin_r,
            v.x * sin_r + v.y * cos_r
        };
    };
    SDL_FPoint delta{ local.x - hitbox_drag_start_box_.center_x,
                      local.y - hitbox_drag_start_box_.center_y };
    SDL_FPoint aligned = rotate_to_box(delta.x, delta.y);
    switch (active_hitbox_handle_) {
        case HitHandle::Move: {
            SDL_FPoint new_center{
                local.x - hitbox_drag_grab_offset_.x,
                local.y - hitbox_drag_grab_offset_.y
};
            box->center_x = new_center.x;
            box->center_y = new_center.y;
            break;
        }
        case HitHandle::Left:
        case HitHandle::Right: {
            float left = hitbox_drag_left_;
            float right = hitbox_drag_right_;
            if (active_hitbox_handle_ == HitHandle::Left) {
                left = std::min(aligned.x, right - kMinHalf * 2.0f);
            } else {
                right = std::max(aligned.x, left + kMinHalf * 2.0f);
            }
            const float width = std::max(kMinHalf * 2.0f, right - left);
            const float center_offset = (right + left) * 0.5f;
            SDL_FPoint offset_world = rotate_to_world(SDL_FPoint{center_offset, 0.0f});
            box->center_x = hitbox_drag_start_box_.center_x + offset_world.x;
            box->center_y = hitbox_drag_start_box_.center_y + offset_world.y;
            box->half_width = width * 0.5f;
            break;
        }
        case HitHandle::Top:
        case HitHandle::Bottom: {
            float bottom = hitbox_drag_bottom_;
            float top = hitbox_drag_top_;
            if (active_hitbox_handle_ == HitHandle::Top) {
                top = std::max(aligned.y, bottom + kMinHalf * 2.0f);
            } else {
                bottom = std::min(aligned.y, top - kMinHalf * 2.0f);
            }
            const float height = std::max(kMinHalf * 2.0f, top - bottom);
            const float center_offset = (top + bottom) * 0.5f;
            SDL_FPoint offset_world = rotate_to_world(SDL_FPoint{0.0f, center_offset});
            box->center_x = hitbox_drag_start_box_.center_x + offset_world.x;
            box->center_y = hitbox_drag_start_box_.center_y + offset_world.y;
            box->half_height = height * 0.5f;
            break;
        }
        case HitHandle::Rotate: {
            SDL_FPoint rel{ local.x - box->center_x, local.y - box->center_y };
            box->rotation_degrees = std::atan2(rel.y, rel.x) * kRadToDeg;
            break;
        }
        case HitHandle::None:
        default:
            break;
    }
    refresh_hitbox_form();
}

void FrameEditorSession::end_hitbox_drag(bool commit) {
    if (!hitbox_dragging_) return;
    hitbox_dragging_ = false;
    active_hitbox_handle_ = HitHandle::None;
    if (commit) {
        persist_changes();
    }
}

bool FrameEditorSession::begin_attack_drag(SDL_Point mp) {
    if (!active_ || !assets_ || !target_ || frames_.empty() || mode_ != Mode::AttackGeometry) return false;

    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    const std::string current_type = current_attack_type();

    const WarpedScreenGrid& cam = assets_->getView();
    SDL_Point anchor = asset_anchor_world();
    const float scale = asset_local_scale();
    auto to_screen = [&](float lx, float ly) -> SDL_FPoint {
        SDL_FPoint world{
            static_cast<float>(anchor.x) + lx * scale, static_cast<float>(anchor.y) - ly * scale };
        return cam.map_to_screen_f(world);
    };

    auto point_hit = [&](SDL_FPoint p, float radius) -> bool {
        const float dx = static_cast<float>(mp.x) - p.x;
        const float dy = static_cast<float>(mp.y) - p.y;
        return dx * dx + dy * dy <= radius * radius;
    };
    const float node_radius = kAttackNodeRadius;

    int type_counter = 0;
    int clicked_vector_index = -1;
    AttackHandle clicked_handle = AttackHandle::None;

    for (const auto& vec : frame.attack.vectors) {
        if (vec.type != current_type) continue;

        SDL_FPoint start_screen = to_screen(vec.start_x, vec.start_y);
        SDL_FPoint control_screen = to_screen(vec.control_x, vec.control_y);
        SDL_FPoint end_screen = to_screen(vec.end_x, vec.end_y);

        if (point_hit(start_screen, node_radius)) {
            clicked_vector_index = type_counter;
            clicked_handle = AttackHandle::Start;
            break;
        } else if (point_hit(control_screen, node_radius)) {
            clicked_vector_index = type_counter;
            clicked_handle = AttackHandle::Control;
            break;
        } else if (point_hit(end_screen, node_radius)) {
            clicked_vector_index = type_counter;
            clicked_handle = AttackHandle::End;
            break;
        }
        ++type_counter;
    }

    if (clicked_vector_index < 0) {
        type_counter = 0;
        constexpr int segments = 16;
        constexpr float segment_hit_radius = 8.0f;

        for (const auto& vec : frame.attack.vectors) {
            if (vec.type != current_type) continue;

            SDL_FPoint start_screen = to_screen(vec.start_x, vec.start_y);
            SDL_FPoint control_screen = to_screen(vec.control_x, vec.control_y);
            SDL_FPoint end_screen = to_screen(vec.end_x, vec.end_y);

            for (int i = 0; i <= segments; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float u = 1.0f - t;
                SDL_FPoint curve_point{
                    u * u * start_screen.x + 2.0f * u * t * control_screen.x + t * t * end_screen.x,
                    u * u * start_screen.y + 2.0f * u * t * control_screen.y + t * t * end_screen.y
};
                if (point_hit(curve_point, segment_hit_radius)) {
                    clicked_vector_index = type_counter;
                    clicked_handle = AttackHandle::Segment;
                    break;
                }
            }
            if (clicked_vector_index >= 0) break;
            ++type_counter;
        }
    }

    if (clicked_vector_index >= 0) {
        set_current_attack_vector_index(clicked_vector_index);
        clamp_attack_selection();
        refresh_attack_form();
        active_attack_handle_ = clicked_handle;
    } else {
        active_attack_handle_ = AttackHandle::None;
        return false;
    }

    const auto* vec = current_attack_vector();
    if (!vec) {
        active_attack_handle_ = AttackHandle::None;
        return false;
    }

    SDL_FPoint mouse_local{};
    if (!screen_to_local(mp, mouse_local)) {
        active_attack_handle_ = AttackHandle::None;
        return false;
    }
    attack_dragging_ = true;
    attack_drag_moved_ = false;
    attack_drag_start_mouse_ = mp;
    attack_drag_start_mouse_local_ = mouse_local;
    attack_drag_start_vector_ = *vec;
    return true;
}

void FrameEditorSession::update_attack_drag(SDL_Point mouse) {
    if (!attack_dragging_) return;
    auto* vec = current_attack_vector();
    if (!vec) return;
    SDL_FPoint local{};
    if (!screen_to_local(mouse, local)) {
        return;
    }
    const float move_threshold = 1.0f;
    if (std::fabs(local.x - attack_drag_start_mouse_local_.x) > move_threshold ||
        std::fabs(local.y - attack_drag_start_mouse_local_.y) > move_threshold) {
        attack_drag_moved_ = true;
    }
    switch (active_attack_handle_) {
        case AttackHandle::Start:
            vec->start_x = local.x;
            vec->start_y = local.y;
            break;
        case AttackHandle::Control:
            vec->control_x = local.x;
            vec->control_y = local.y;
            break;
        case AttackHandle::End:
            vec->end_x = local.x;
            vec->end_y = local.y;
            break;
        case AttackHandle::Segment: {
            SDL_FPoint delta{ local.x - attack_drag_start_mouse_local_.x,
                              local.y - attack_drag_start_mouse_local_.y };
            vec->start_x = attack_drag_start_vector_.start_x + delta.x;
            vec->start_y = attack_drag_start_vector_.start_y + delta.y;
            vec->control_x = attack_drag_start_vector_.control_x + delta.x;
            vec->control_y = attack_drag_start_vector_.control_y + delta.y;
            vec->end_x = attack_drag_start_vector_.end_x + delta.x;
            vec->end_y = attack_drag_start_vector_.end_y + delta.y;
            break;
        }
        case AttackHandle::None:
        default:
            break;
    }
    refresh_attack_form();
}

void FrameEditorSession::end_attack_drag(bool commit) {
    if (!attack_dragging_) return;
    AttackHandle handle = active_attack_handle_;
    attack_dragging_ = false;
    active_attack_handle_ = AttackHandle::None;
    if (!commit) {
        if (auto* vec = current_attack_vector()) {
            *vec = attack_drag_start_vector_;
            refresh_attack_form();
        }
        return;
    }
    if (!attack_drag_moved_ && (handle == AttackHandle::Start || handle == AttackHandle::End)) {
        delete_current_attack_vector();
    }
    refresh_attack_form();
    persist_changes();
}

void FrameEditorSession::apply_frame_move_from_base(int index, SDL_FPoint desired_rel, const std::vector<SDL_FPoint>& base_rel) {
    if (index <= 0) return;
    if (index >= static_cast<int>(frames_.size())) return;
    if (base_rel.size() != frames_.size()) return;

    frames_.front().dx = 0.0f;
    frames_.front().dy = 0.0f;

    SDL_FPoint prev_abs = base_rel[index - 1];
    frames_[index].dx = static_cast<float>(std::round(desired_rel.x - prev_abs.x));
    frames_[index].dy = static_cast<float>(std::round(desired_rel.y - prev_abs.y));

    SDL_FPoint last_abs = desired_rel;
    for (int j = index + 1; j < static_cast<int>(frames_.size()); ++j) {
        const SDL_FPoint desired = base_rel[j];
        frames_[j].dx = static_cast<float>(std::round(desired.x - last_abs.x));
        frames_[j].dy = static_cast<float>(std::round(desired.y - last_abs.y));
        last_abs = desired;
    }
}

void FrameEditorSession::redistribute_frames_from_middle_drag(int adjusted_index) {

    redistribute_frames_after_adjustment(adjusted_index);
}

void FrameEditorSession::redistribute_frames_after_adjustment(int adjusted_index) {
    const size_t count = frames_.size();
    if (count < 3) {
        persist_changes();
        return;
    }
    const int last_index = static_cast<int>(count) - 1;
    if (adjusted_index <= 0) {

        persist_changes();
        return;
    }
    if (rel_positions_.size() != count) {
        rebuild_rel_positions();
    }
    if (rel_positions_.size() != count) {
        persist_changes();
        return;
    }

    const std::vector<SDL_FPoint> original_positions = rel_positions_;
    std::vector<SDL_FPoint> redistributed = original_positions;
    if (curve_enabled_) {
        apply_curved_smoothing(adjusted_index, original_positions, redistributed, last_index);
    } else {
        apply_linear_smoothing(adjusted_index, redistributed, last_index);
    }

    frames_[0].dx = 0.0f;
    frames_[0].dy = 0.0f;
    for (size_t i = 1; i < count; ++i) {
        const SDL_FPoint prev = redistributed[i - 1];
        const SDL_FPoint curr = redistributed[i];
        frames_[i].dx = static_cast<float>(std::round(curr.x - prev.x));
        frames_[i].dy = static_cast<float>(std::round(curr.y - prev.y));
    }
    rebuild_rel_positions();
    persist_changes();
}

void FrameEditorSession::apply_linear_smoothing(int adjusted_index,
                                                std::vector<SDL_FPoint>& redistributed,
                                                int last_index) const {
    if (redistributed.empty()) return;
    if (adjusted_index <= 0) return;
    const SDL_FPoint start = redistributed.front();
    const SDL_FPoint end = redistributed.back();
    const float steps = static_cast<float>(last_index);
    if (steps <= 0.0f) {
        return;
    }
    if (adjusted_index >= 1 && adjusted_index < last_index) {
        const SDL_FPoint anchor = redistributed[adjusted_index];
        const float pre_steps = static_cast<float>(adjusted_index);
        const SDL_FPoint pre_delta{ anchor.x - start.x, anchor.y - start.y };
        for (int j = 1; j < adjusted_index; ++j) {
            const float t = pre_steps > 0.0f ? (static_cast<float>(j) / pre_steps) : 0.0f;
            SDL_FPoint new_pos{ start.x + pre_delta.x * t, start.y + pre_delta.y * t };
            redistributed[j] = new_pos;
        }
        const float post_steps = static_cast<float>(last_index - adjusted_index);
        const SDL_FPoint post_delta{ end.x - anchor.x, end.y - anchor.y };
        for (int j = adjusted_index + 1; j < last_index; ++j) {
            const float u = post_steps > 0.0f ? (static_cast<float>(j - adjusted_index) / post_steps) : 0.0f;
            SDL_FPoint new_pos{ anchor.x + post_delta.x * u, anchor.y + post_delta.y * u };
            redistributed[j] = new_pos;
        }
    } else {
        const SDL_FPoint delta{ end.x - start.x, end.y - start.y };
        for (int j = 1; j < last_index; ++j) {
            const float t = static_cast<float>(j) / steps;
            SDL_FPoint new_pos{ start.x + delta.x * t, start.y + delta.y * t };
            redistributed[j] = new_pos;
        }
    }
}

void FrameEditorSession::apply_curved_smoothing(int adjusted_index,
                                                const std::vector<SDL_FPoint>& original,
                                                std::vector<SDL_FPoint>& redistributed,
                                                int last_index) const {
    if (redistributed.size() < 2) return;
    if (original.size() != redistributed.size()) return;
    if (adjusted_index <= 0) return;

    auto clamp_control = [](const SDL_FPoint& p0, const SDL_FPoint& p2, SDL_FPoint& control) {
        SDL_FPoint midpoint{ (p0.x + p2.x) * 0.5f, (p0.y + p2.y) * 0.5f };
        float dx = control.x - midpoint.x;
        float dy = control.y - midpoint.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        const float span = std::sqrt((p2.x - p0.x) * (p2.x - p0.x) + (p2.y - p0.y) * (p2.y - p0.y));
        const float max_offset = std::clamp(span * 0.45f, 0.0f, 160.0f);
        if (dist > max_offset && dist > 0.0f) {
            const float scale = max_offset / dist;
            control.x = midpoint.x + dx * scale;
            control.y = midpoint.y + dy * scale;
            dist = max_offset;
        }
        if (dist < 1.0f && span > 0.0f) {
            const float nx = -(p2.y - p0.y) / span;
            const float ny = (p2.x - p0.x) / span;
            const float offset = std::min(span * 0.2f, 40.0f);
            control.x = midpoint.x + nx * offset;
            control.y = midpoint.y + ny * offset;
        }
};

    auto place_half = [&](int first_idx, int second_idx) {
        const int segment_count = second_idx - first_idx;
        if (segment_count <= 1) return;
        SDL_FPoint p0 = redistributed[first_idx];
        SDL_FPoint p2 = redistributed[second_idx];
        SDL_FPoint control{ (p0.x + p2.x) * 0.5f, (p0.y + p2.y) * 0.5f };
        const int interior_count = segment_count - 1;
        if (interior_count > 0) {
            int mid_index = first_idx + (segment_count / 2);
            mid_index = std::clamp(mid_index, first_idx + 1, second_idx - 1);
            if (mid_index >= 0 && mid_index < static_cast<int>(original.size())) {
                control = original[mid_index];
            }
        }
        clamp_control(p0, p2, control);
        for (int j = first_idx + 1; j < second_idx; ++j) {
            const float ratio = static_cast<float>(j - first_idx) / static_cast<float>(segment_count);
            redistributed[j] = sample_quadratic_by_arclen(p0, control, p2, ratio);
        }
};

    place_half(0, std::min(adjusted_index, last_index));
    if (adjusted_index < last_index) {
        place_half(adjusted_index, last_index);
    }
}

void FrameEditorSession::smooth_child_offsets(int child_index, int adjusted_index) {
    if (frames_.size() < 3) {
        persist_changes();
        return;
    }
    if (child_index < 0 || adjusted_index <= 0) {
        persist_changes();
        return;
    }
    const std::size_t frame_count = frames_.size();
    if (static_cast<std::size_t>(child_index) >= child_assets_.size()) {
        persist_changes();
        return;
    }
    const int last_index = static_cast<int>(frame_count) - 1;
    if (adjusted_index >= static_cast<int>(frame_count)) {
        persist_changes();
        return;
    }

    std::vector<SDL_FPoint> original(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        const auto& children = frames_[i].children;
        if (static_cast<std::size_t>(child_index) >= children.size()) {
            original[i] = SDL_FPoint{0.0f, 0.0f};
            continue;
        }
        const auto& child = children[child_index];
        original[i] = SDL_FPoint{ child.dx, child.dy };
    }
    std::vector<SDL_FPoint> redistributed = original;
    if (curve_enabled_) {
        apply_curved_smoothing(adjusted_index, original, redistributed, last_index);
    } else {
        apply_linear_smoothing(adjusted_index, redistributed, last_index);
    }
    for (std::size_t i = 0; i < frame_count; ++i) {
        auto& children = frames_[i].children;
        if (static_cast<std::size_t>(child_index) >= children.size()) {
            continue;
        }
        auto& child = children[child_index];
        child.dx = static_cast<float>(std::round(redistributed[i].x));
        child.dy = static_cast<float>(std::round(redistributed[i].y));
        child.has_data = true;
    }
    persist_changes();
}

void FrameEditorSession::rebuild_rel_positions() {
    rel_positions_.clear();
    SDL_FPoint curr{0.0f, 0.0f};
    for (size_t i = 0; i < frames_.size(); ++i) {
        if (i == 0) {
            curr = SDL_FPoint{0.0f, 0.0f};
        } else {
            curr.x += frames_[i].dx;
            curr.y += frames_[i].dy;
        }
        rel_positions_.push_back(curr);
    }
}
void FrameEditorSession::refresh_child_assets_from_document() {
    if (!document_) {
        return;
    }
    const std::string new_signature = document_->animation_children_signature();
    if (new_signature == document_children_signature_) {
        return;
    }
    document_children_signature_ = new_signature;
    std::vector<std::string> names = document_->animation_children();
    if (names == child_assets_) {
        return;
    }
    const std::vector<std::string> previous = child_assets_;
    std::vector<int> remap(previous.size(), -1);
    if (!previous.empty()) {
        std::unordered_map<std::string, int> new_index;
        new_index.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            new_index[names[i]] = static_cast<int>(i);
        }
        for (size_t i = 0; i < previous.size(); ++i) {
            auto it = new_index.find(previous[i]);
            if (it != new_index.end()) {
                remap[i] = it->second;
            }
        }
    }
    child_assets_ = std::move(names);

    std::vector<AnimationChildMode> remapped_modes(child_assets_.size(), AnimationChildMode::Static);
    for (std::size_t i = 0; i < remap.size(); ++i) {
        const int to = remap[i];
        if (to >= 0 && static_cast<std::size_t>(to) < remapped_modes.size() && i < child_modes_.size()) {
            remapped_modes[static_cast<std::size_t>(to)] = child_modes_[i];
        }
    }
    child_modes_ = std::move(remapped_modes);
    std::unordered_set<std::string> previous_lookup(previous.begin(), previous.end());
    std::vector<int> new_child_indices;
    new_child_indices.reserve(child_assets_.size());
    for (std::size_t i = 0; i < child_assets_.size(); ++i) {
        if (previous_lookup.find(child_assets_[i]) == previous_lookup.end()) {
            new_child_indices.push_back(static_cast<int>(i));
        }
    }
    remap_child_indices(remap);
    if (target_ && target_->info) {
        target_->info->set_animation_children(child_assets_);
        target_->initialize_animation_children_recursive();
        target_->mark_composite_dirty();
    }
    if (assets_) {
        assets_->mark_active_assets_dirty();
    }
    sync_child_frames();
    if (!new_child_indices.empty()) {
        for (auto& frame : frames_) {
            if (frame.children.size() < child_assets_.size()) {
                frame.children.resize(child_assets_.size());
            }
            for (int idx : new_child_indices) {
                if (idx < 0 || static_cast<std::size_t>(idx) >= frame.children.size()) {
                    continue;
                }
                auto& child = frame.children[static_cast<std::size_t>(idx)];
                if (!child.has_data) {
                    child.child_index = idx;
                    child.dx = 0.0f;
                    child.dy = 0.0f;
                    child.degree = 0.0f;
                    child.visible = true;
                    child.render_in_front = true;
                    child.has_data = true;
                }
            }
        }
    }
    child_dropdown_options_cache_.clear();
    rebuild_child_preview_cache();
}

void FrameEditorSession::rebuild_child_preview_cache() {
    child_preview_slots_.clear();
    if (!assets_ || child_assets_.empty()) {
        return;
    }
    SDL_Renderer* renderer = assets_->renderer();
    child_preview_slots_.reserve(child_assets_.size());
    float variant_scale = 1.0f;
    if (target_) {
        variant_scale = target_->current_nearest_variant_scale;
        if (!std::isfinite(variant_scale) || variant_scale <= 0.0f) {
            variant_scale = 1.0f;
        }
    }
    auto& library = assets_->library();
    for (const auto& name : child_assets_) {
        ChildPreviewSlot slot;
        slot.asset_name = name;
        if (!name.empty()) {
            slot.info = library.get(name);
            if (slot.info) {
                if (renderer) {
                    slot.info->loadAnimations(renderer);
                }
                slot.animation = pick_preview_animation(slot.info);
                slot.frame = slot.animation ? slot.animation->get_first_frame() : nullptr;
                if (slot.frame) {
                    const FrameVariant* variant = slot.animation->get_frame(slot.frame, variant_scale);
                    slot.texture = variant ? variant->get_base_texture() : nullptr;
                    if (!slot.texture && !slot.frame->variants.empty()) {
                        slot.texture = slot.frame->variants[0].get_base_texture();
                    }
                    if (slot.texture) {
                        if (SDL_QueryTexture(slot.texture, nullptr, nullptr, &slot.width, &slot.height) != 0) {
                            slot.width = 0;
                            slot.height = 0;
                        }
                    }
                }
            }
        }
        child_preview_slots_.push_back(std::move(slot));
    }
}

FrameEditorSession::ChildFrame* FrameEditorSession::current_child_frame() {
    if (frames_.empty() || child_assets_.empty()) {
        return nullptr;
    }
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    auto& frame = frames_[frame_index];
    if (selected_child_index_ < 0 ||
        selected_child_index_ >= static_cast<int>(frame.children.size())) {
        return nullptr;
    }
    return &frame.children[selected_child_index_];
}

const FrameEditorSession::ChildFrame* FrameEditorSession::current_child_frame() const {
    if (frames_.empty() || child_assets_.empty()) {
        return nullptr;
    }
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    const auto& frame = frames_[frame_index];
    if (selected_child_index_ < 0 ||
        selected_child_index_ >= static_cast<int>(frame.children.size())) {
        return nullptr;
    }
    return &frame.children[selected_child_index_];
}

bool FrameEditorSession::target_is_alive() const {
    return assets_ && target_ && assets_->contains_asset(target_);
}

Animation* FrameEditorSession::current_animation_mutable() const {
    if (!target_ || !target_->info) {
        return nullptr;
    }
    if (assets_ && !assets_->contains_asset(target_)) {
        return nullptr;
    }
    auto it = target_->info->animations.find(animation_id_);
    if (it == target_->info->animations.end()) {
        return nullptr;
    }
    return const_cast<Animation*>(&it->second);
}

void FrameEditorSession::hydrate_frames_from_animation() {
    Animation* anim = current_animation_mutable();
    if (!anim || anim->movement_path_count() == 0) {
        return;
    }
    const auto& path = anim->movement_path(anim->default_movement_path_index());
    if (path.empty()) {
        return;
    }
    const std::size_t count = std::min<std::size_t>(frames_.size(), path.size());
    for (std::size_t i = 0; i < count; ++i) {
        const AnimationFrame& src = path[i];
        MovementFrame& dst = frames_[i];
        if (!last_payload_loaded_) {
            dst.dx = static_cast<float>(src.dx);
            dst.dy = static_cast<float>(src.dy);
            dst.resort_z = src.z_resort;
        }

        if (!last_payload_loaded_ && !src.children.empty()) {

            if (dst.children.size() < child_assets_.size()) {
                dst.children.resize(child_assets_.size());
                for (std::size_t k = 0; k < dst.children.size(); ++k) {
                    dst.children[k].child_index = static_cast<int>(k);
                }
            }
            for (const auto& child_src : src.children) {
                if (child_src.child_index < 0 ||
                    static_cast<std::size_t>(child_src.child_index) >= child_assets_.size()) {
                    continue;
                }
                ChildFrame& child = dst.children[static_cast<std::size_t>(child_src.child_index)];
                child.child_index = child_src.child_index;
                child.dx = static_cast<float>(child_src.dx);
                child.dy = static_cast<float>(child_src.dy);
                child.degree = child_src.degree;
                child.visible = child_src.visible;
                child.render_in_front = child_src.render_in_front;
                child.has_data = true;
            }
        }
        if (!last_payload_loaded_ && dst.hit.boxes.empty() && !src.hit_geometry.boxes.empty()) {
            dst.hit.boxes = src.hit_geometry.boxes;
        }
        if (!last_payload_loaded_ && dst.attack.vectors.empty() && !src.attack_geometry.vectors.empty()) {
            dst.attack.vectors = src.attack_geometry.vectors;
        }
    }
}

void FrameEditorSession::apply_frames_to_animation() {
    Animation* anim = current_animation_mutable();
    if (!anim || anim->movement_path_count() == 0) {
        return;
    }

    anim->child_assets() = child_assets_;
    std::unordered_map<std::string, const AnimationChildData*> timeline_by_name;
    const auto& existing_timelines = anim->child_timelines();
    timeline_by_name.reserve(existing_timelines.size());
    for (const auto& descriptor : existing_timelines) {
        if (descriptor.asset_name.empty()) {
            continue;
        }
        if (timeline_by_name.find(descriptor.asset_name) == timeline_by_name.end()) {
            timeline_by_name.emplace(descriptor.asset_name, &descriptor);
        }
    }
    const std::size_t frame_count = frames_.size();
    const std::size_t primary_path_index = anim->default_movement_path_index();
    for (std::size_t path_index = 0; path_index < anim->movement_path_count(); ++path_index) {
        auto& path = anim->movement_path(path_index);
        if (path.empty()) {
            continue;
        }
        if (path.size() < frame_count) {
            const std::size_t prev_size = path.size();
            path.resize(frame_count);
            for (std::size_t i = prev_size; i < path.size(); ++i) {
                path[i].frame_index = static_cast<int>(i);
            }
        }
        const std::size_t copy_count = std::min<std::size_t>(frame_count, path.size());
        for (std::size_t i = 0; i < copy_count; ++i) {
            const MovementFrame& src = frames_[i];
            AnimationFrame& dst = path[i];
            dst.dx = static_cast<int>(std::lround(src.dx));
            dst.dy = static_cast<int>(std::lround(src.dy));
            dst.z_resort = src.resort_z;
            dst.frame_index = static_cast<int>(i);
            dst.children.clear();
            if (!child_assets_.empty()) {
                for (const auto& child_src : src.children) {
                    if (child_src.child_index < 0 ||
                        child_src.child_index >= static_cast<int>(child_assets_.size())) {
                        continue;
                    }
                    AnimationChildFrameData child{};
                    child.child_index = child_src.child_index;
                    child.dx = static_cast<int>(std::lround(child_src.dx));
                    child.dy = static_cast<int>(std::lround(child_src.dy));
                    child.degree = child_src.degree;
                    child.visible = child_src.visible;
                    child.render_in_front = child_src.render_in_front;
                    dst.children.push_back(child);
                }
            }
            dst.hit_geometry.boxes.clear();
            for (const auto& box : src.hit.boxes) {
                if (box.is_empty()) continue;
                dst.hit_geometry.boxes.push_back(box);
            }
            dst.attack_geometry.vectors = src.attack.vectors;
        }
        for (std::size_t i = 0; i < path.size(); ++i) {
            AnimationFrame& dst = path[i];
            dst.frame_index = static_cast<int>(i);
            dst.is_first = (i == 0);
            dst.is_last = (i + 1 == path.size());
            dst.prev = (i > 0) ? &path[i - 1] : nullptr;
            dst.next = (i + 1 < path.size()) ? &path[i + 1] : nullptr;
        }
        if (path_index == primary_path_index) {
            anim->frames.clear();
            anim->frames.reserve(path.size());
            for (auto& frame : path) {
                anim->frames.push_back(&frame);
            }
            anim->total_dx = 0;
            anim->total_dy = 0;
            anim->movment = false;
            for (const auto& frame : path) {
                anim->total_dx += frame.dx;
                anim->total_dy += frame.dy;
                if (frame.dx != 0 || frame.dy != 0) {
                    anim->movment = true;
                }
            }
        }
    }

    std::vector<AnimationChildData> rebuilt_timelines;
    rebuilt_timelines.reserve(child_assets_.size());
    ensure_child_mode_size();
    for (std::size_t child_idx = 0; child_idx < child_assets_.size(); ++child_idx) {
        AnimationChildData descriptor;
        descriptor.asset_name = child_assets_[child_idx];
        const AnimationChildData* previous = nullptr;
        auto prev_it = timeline_by_name.find(descriptor.asset_name);
        if (prev_it != timeline_by_name.end()) {
            previous = prev_it->second;
        }
        descriptor.animation_override = previous ? previous->animation_override : std::string{};
        descriptor.mode = child_mode(static_cast<int>(child_idx));
        if (descriptor.mode == AnimationChildMode::Static) {
            const std::size_t timeline_frame_count = frame_count == 0 ? 1 : frame_count;
            descriptor.frames.clear();
            descriptor.frames.reserve(timeline_frame_count);
            if (frame_count == 0) {
                AnimationChildFrameData sample{};
                sample.child_index = static_cast<int>(child_idx);
                sample.visible = false;
                sample.render_in_front = true;
                descriptor.frames.push_back(sample);
            } else {
                for (const auto& movement_frame : frames_) {
                    descriptor.frames.push_back(build_child_frame_descriptor(movement_frame, child_idx));
                }
            }
        } else {
            if (previous) {
                descriptor.frames = previous->frames;
            }
        }
        rebuilt_timelines.push_back(std::move(descriptor));
    }
    anim->child_timelines() = std::move(rebuilt_timelines);
    anim->refresh_child_start_events();
}

void FrameEditorSession::sync_child_asset_visibility() {
    if (!target_is_alive()) {
        child_hidden_cache_.clear();
        last_applied_show_asset_state_ = show_animation_ && show_child_;
        return;
    }
    const bool desired_show = show_animation_ && show_child_;
    if (desired_show != last_applied_show_asset_state_) {
        if (!desired_show) {
            cache_child_hidden_states();
            apply_child_hidden_state(false);
        } else {
            apply_child_hidden_state(true);
            cache_child_hidden_states();
        }
        last_applied_show_asset_state_ = desired_show;
    } else {
        if (desired_show) {
            cache_child_hidden_states();
        } else {
            apply_child_hidden_state(false);
        }
    }
}

void FrameEditorSession::cache_child_hidden_states() {
    if (!target_is_alive()) return;
    std::function<void(Asset*)> recurse = [&](Asset* parent) {
        if (!parent) return;
        if (assets_ && !assets_->contains_asset(parent)) return;
        for (Asset* child : parent->asset_children) {
            if (!child) continue;
            if (assets_ && !assets_->contains_asset(child)) continue;
            child_hidden_cache_[child] = child->is_hidden();
            recurse(child);
        }
};
    recurse(target_);
}

void FrameEditorSession::apply_child_hidden_state(bool show_children) {
    if (!target_is_alive()) return;
    std::function<void(Asset*)> recurse = [&](Asset* parent) {
        if (!parent) return;
        if (assets_ && !assets_->contains_asset(parent)) return;
        for (Asset* child : parent->asset_children) {
            if (!child) continue;
            if (assets_ && !assets_->contains_asset(child)) continue;
            if (show_children) {
                auto it = child_hidden_cache_.find(child);
                const bool desired = (it != child_hidden_cache_.end()) ? it->second : child->is_hidden();
                child->set_hidden(desired);
            } else {
                child_hidden_cache_.try_emplace(child, child->is_hidden());
                child->set_hidden(true);
            }
            recurse(child);
        }
};
    recurse(target_);
}

void FrameEditorSession::switch_animation(const std::string& animation_id) {
    if (animation_id.empty() || animation_id_ == animation_id) {
        return;
    }
    if (!animation_supports_frame_editing(document_.get(), animation_id)) {
        return;
    }
    persist_changes();
    end_hitbox_drag(false);
    end_attack_drag(false);
    load_animation_data(animation_id);
    child_hidden_cache_.clear();
    last_applied_show_asset_state_ = show_animation_ && show_child_;
    cache_child_hidden_states();
    sync_child_asset_visibility();
    ensure_widgets();
    rebuild_layout();
    ensure_selected_thumb_visible();
}

void FrameEditorSession::select_child(int index) {
    int clamped = child_assets_.empty() ? 0 : std::clamp(index, 0, static_cast<int>(child_assets_.size()) - 1);
    if (clamped == selected_child_index_) {
        return;
    }
    selected_child_index_ = clamped;
    if (dd_child_select_) {
        dd_child_select_->set_selected(clamped);
    }
}

void FrameEditorSession::persist_changes() {
    if (!document_ || animation_id_.empty()) {
        return;
    }
    ensure_child_frames_initialized();

    apply_frames_to_animation();
    if (target_is_alive() && target_->info) {
        target_->info->set_animation_children(child_assets_);
        for (auto& entry : target_->info->animations) {
            entry.second.child_assets() = child_assets_;
        }
        target_->initialize_animation_children_recursive();
        target_->mark_composite_dirty();
    }
    if (assets_) {
        assets_->mark_active_assets_dirty();
    }

    nlohmann::json payload = nlohmann::json::object();
    if (auto j = document_->animation_payload(animation_id_)) {
        payload = nlohmann::json::parse(*j, nullptr, false);
        if (!payload.is_object()) payload = nlohmann::json::object();
    }

    document_->replace_animation_children(child_assets_);
    if (child_assets_.empty()) {
        payload.erase("children");
    } else {
        payload["children"] = child_assets_;
    }
    nlohmann::json movement = nlohmann::json::array();
    nlohmann::json hit_geometry = nlohmann::json::array();
    nlohmann::json attack_geometry = nlohmann::json::array();
    for (size_t i = 0; i < frames_.size(); ++i) {
        const MovementFrame& f = frames_[i];
        int dx = static_cast<int>(std::lround(f.dx));
        int dy = static_cast<int>(std::lround(f.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});

        entry.push_back(f.resort_z);
        if (!child_assets_.empty()) {
            while (entry.size() < 4) {
                entry.push_back(nlohmann::json());
            }
            nlohmann::json child_entries = nlohmann::json::array();
            if (!f.children.empty()) {
                for (const auto& child : f.children) {
                    if (child.child_index < 0 ||
                        child.child_index >= static_cast<int>(child_assets_.size())) {
                        continue;
                    }
                    nlohmann::json child_json = nlohmann::json::array();
                    child_json.push_back(child.child_index);
                    child_json.push_back(static_cast<int>(std::lround(child.dx)));
                    child_json.push_back(static_cast<int>(std::lround(child.dy)));
                    child_json.push_back(static_cast<double>(child.degree));
                    child_json.push_back(child.visible);
                    child_json.push_back(child.render_in_front);
                    child_entries.push_back(std::move(child_json));
                }
            }
            entry.push_back(std::move(child_entries));
        }
        movement.push_back(entry);

        nlohmann::json hit_entry = nlohmann::json::object();
        for (const char* type : kDamageTypeNames) {
            const auto* box = f.hit.find_box(type);
            if (!box || box->is_empty() ||
                !std::isfinite(box->center_x) || !std::isfinite(box->center_y) ||
                !std::isfinite(box->half_width) || !std::isfinite(box->half_height) ||
                !std::isfinite(box->rotation_degrees)) {
                hit_entry[type] = nullptr;
                continue;
            }
            hit_entry[type] = nlohmann::json{
                {"center_x", box->center_x},
                {"center_y", box->center_y},
                {"half_width", box->half_width},
                {"half_height", box->half_height},
                {"rotation", box->rotation_degrees},
                {"type", type}
};
        }
        hit_geometry.push_back(std::move(hit_entry));

        nlohmann::json attack_entry = nlohmann::json::object();
        for (const char* type : kDamageTypeNames) {
            nlohmann::json type_array = nlohmann::json::array();
            for (const auto& vec : f.attack.vectors) {
                if (vec.type != type) continue;
                if (!std::isfinite(vec.start_x) || !std::isfinite(vec.start_y) ||
                    !std::isfinite(vec.end_x) || !std::isfinite(vec.end_y) ||
                    !std::isfinite(vec.control_x) || !std::isfinite(vec.control_y)) {
                    continue;
                }
                type_array.push_back(nlohmann::json{
                    {"start_x", vec.start_x},
                    {"start_y", vec.start_y},
                    {"control_x", vec.control_x},
                    {"control_y", vec.control_y},
                    {"end_x", vec.end_x},
                    {"end_y", vec.end_y},
                    {"damage", vec.damage},
                    {"type", vec.type}
                });
            }
            attack_entry[type] = std::move(type_array);
        }
        attack_geometry.push_back(std::move(attack_entry));
    }
    if (movement.empty()) movement.push_back(nlohmann::json::array({0,0}));
    payload["movement"] = std::move(movement);
    payload["hit_geometry"] = std::move(hit_geometry);
    payload["attack_geometry"] = std::move(attack_geometry);
    if (child_assets_.empty()) {
        payload.erase("child_timelines");
    } else {
        payload["child_timelines"] = build_child_timelines_payload(payload);
    }

    std::string serialized = payload.dump();
    const bool changed = document_payload_cache_.empty() || serialized != document_payload_cache_;
    if (!changed) {
        return;
    }

    if (std::find(edited_animation_ids_.begin(), edited_animation_ids_.end(), animation_id_) == edited_animation_ids_.end()) {
        edited_animation_ids_.push_back(animation_id_);
    }
    pending_save_ = true;
    document_->replace_animation_payload(animation_id_, serialized);
    if (auto normalized = document_->animation_payload(animation_id_)) {
        document_payload_cache_ = *normalized;
    } else {
        document_payload_cache_ = serialized;
    }

    document_->save_to_file(false);
}

void FrameEditorSession::remap_child_indices(const std::vector<int>& remap) {
    if (frames_.empty()) {
        return;
    }
    if (remap.empty()) {
        for (auto& frame : frames_) {
            for (auto& child : frame.children) {
                child.child_index = -1;
            }
        }
        return;
    }
    for (auto& frame : frames_) {
        for (auto& child : frame.children) {
            if (child.child_index < 0 || child.child_index >= static_cast<int>(remap.size())) {
                child.child_index = -1;
                continue;
            }
            child.child_index = remap[child.child_index];
        }
    }
}

ChildPreviewContext FrameEditorSession::build_child_preview_context() const {
    ChildPreviewContext ctx;
    ctx.document_scale = document_scale_factor();
    SDL_Point anchor = asset_anchor_world();
    ctx.anchor_world = SDL_FPoint{ static_cast<float>(anchor.x), static_cast<float>(anchor.y) };
    return ctx;
}

SDL_FRect FrameEditorSession::child_preview_rect(SDL_FPoint child_world,
                                                 int texture_w,
                                                 int texture_h,
                                                 const ChildPreviewContext& ctx,
                                                 float scale_override) const {
    SDL_FRect rect{0.0f, 0.0f, 0.0f, 0.0f};
    if (!assets_ || !target_) {
        return rect;
    }
    if (texture_w <= 0 || texture_h <= 0) {
        return rect;
    }
    float scale = scale_override;
    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = ctx.document_scale;
    }
    if (!std::isfinite(scale) || scale <= 0.0f) {
        scale = 1.0f;
    }
    const float raw_w = static_cast<float>(texture_w) * scale;
    const float raw_h = static_cast<float>(texture_h) * scale;
    const WarpedScreenGrid& cam = assets_->getView();
    const float inv_scale = 1.0f / std::max(0.000001f, cam.get_scale());
    rect.w = raw_w * inv_scale;
    rect.h = raw_h * inv_scale;
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        rect.w = rect.h = 0.0f;
        return rect;
    }
    SDL_FPoint screen_base = cam.map_to_screen_f(SDL_FPoint{
        static_cast<float>(target_->pos.x),
        static_cast<float>(target_->pos.y)
    });
    const float offset_x = child_world.x - static_cast<float>(target_->pos.x);
    const float offset_y = child_world.y - static_cast<float>(target_->pos.y);
    rect.x = screen_base.x + offset_x * inv_scale - rect.w * 0.5f;
    rect.y = screen_base.y + offset_y * inv_scale - rect.h;
    return rect;
}

float FrameEditorSession::mirrored_child_rotation(bool parent_is_flipped, float degree) const {
    return ::mirrored_child_rotation(parent_is_flipped, degree);
}

AnimationChildFrameData FrameEditorSession::build_child_frame_descriptor(const MovementFrame& frame,
                                                                         std::size_t child_index) const {
    AnimationChildFrameData descriptor{};
    descriptor.child_index = static_cast<int>(child_index);
    descriptor.dx = 0;
    descriptor.dy = 0;
    descriptor.degree = 0.0f;
    descriptor.visible = false;
    descriptor.render_in_front = true;
    if (child_index < frame.children.size()) {
        const ChildFrame& child = frame.children[child_index];
        if (child.has_data) {
            descriptor.child_index = (child.child_index >= 0) ? child.child_index : static_cast<int>(child_index);
            descriptor.dx = static_cast<int>(std::lround(child.dx));
            descriptor.dy = static_cast<int>(std::lround(child.dy));
            descriptor.degree = child.degree;
            descriptor.visible = child.visible;
            descriptor.render_in_front = child.render_in_front;
        }
    }
    return descriptor;
}

void FrameEditorSession::persist_mode_changes(Mode mode) {

    (void)mode;
    persist_changes();
}

void FrameEditorSession::select_frame(int index) {
    if (frames_.empty()) return;
    const int clamped = std::clamp(index, 0, static_cast<int>(frames_.size()) - 1);
    if (clamped == selected_index_) return;
    selected_index_ = clamped;
    update_asset_preview_frame();
    ensure_selected_thumb_visible();
    clamp_attack_selection();
    refresh_hitbox_form();
    refresh_attack_form();
}

void FrameEditorSession::update_asset_preview_frame() const {
    if (!target_ || animation_id_.empty()) return;
    target_->current_animation = animation_id_;
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(frames_.size()) && target_->info) {
        auto it = target_->info->animations.find(animation_id_);
        if (it != target_->info->animations.end() &&
            selected_index_ < static_cast<int>(it->second.frames.size())) {
            target_->current_frame = it->second.frames[selected_index_];
        }
    }
}

int FrameEditorSession::max_scroll_offset() const {
    if (thumb_content_width_ <= thumb_viewport_width_) return 0;
    return std::max(0, thumb_content_width_ - thumb_viewport_width_);
}

void FrameEditorSession::clamp_scroll_offset() const {
    const int max_scroll = max_scroll_offset();
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);
}

void FrameEditorSession::ensure_selected_thumb_visible() {
    if (frames_.empty() || thumb_viewport_width_ <= 0) return;

    const int nav_drag_handle_height = DMSpacing::small_gap() * 2;
    const int title_h = nav_header_height_px(dd_animation_select_ != nullptr);
    const int nav_vertical_padding = DMSpacing::small_gap() * 2;
    const int thumb_h = std::max(1, nav_rect_.h - nav_drag_handle_height - nav_vertical_padding - title_h - kNavSliderGap);
    const int thumb_w = thumb_h;
    const int spacing = kNavSpacing;
    const int per = thumb_w + spacing;

    const int left_edge = selected_index_ * per;
    const int desired_scroll = left_edge + (thumb_w / 2) - (thumb_viewport_width_ / 2);
    scroll_offset_ = std::clamp(desired_scroll, 0, max_scroll_offset());
    clamp_scroll_offset();
}

std::vector<int> FrameEditorSession::build_child_index_remap(const std::vector<std::string>& previous,
                                                             const std::vector<std::string>& next) const {
    std::vector<int> remap(previous.size(), -1);
    if (previous.empty() || next.empty()) {
        return remap;
    }
    std::unordered_map<std::string, int> next_lookup;
    next_lookup.reserve(next.size());
    for (size_t i = 0; i < next.size(); ++i) {
        next_lookup[next[i]] = static_cast<int>(i);
    }
    for (size_t i = 0; i < previous.size(); ++i) {
        auto it = next_lookup.find(previous[i]);
        if (it != next_lookup.end()) {
            remap[i] = it->second;
        }
    }
    return remap;
}

void FrameEditorSession::apply_child_list_change(const std::vector<std::string>& next_children) {
    const std::vector<std::string> previous = child_assets_;
    const std::vector<int> remap = build_child_index_remap(previous, next_children);
    child_assets_ = next_children;

    std::vector<AnimationChildMode> next_modes(child_assets_.size(), AnimationChildMode::Static);
    for (std::size_t i = 0; i < remap.size(); ++i) {
        const int to = remap[i];
        if (to >= 0 && static_cast<std::size_t>(to) < next_modes.size()) {
            if (i < child_modes_.size()) {
                next_modes[static_cast<std::size_t>(to)] = child_modes_[i];
            }
        }
    }
    child_modes_ = std::move(next_modes);
    remap_child_indices(remap);
    ensure_child_frames_initialized();
    sync_child_frames();
    rebuild_child_preview_cache();
    selected_child_index_ = std::clamp(selected_child_index_, 0, static_cast<int>(child_assets_.size()) - 1);
    child_dropdown_options_cache_.clear();
    persist_changes();
}

void FrameEditorSession::add_or_rename_child(const std::string& raw_name) {
    auto trim = [](std::string s) {
        auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
};
    std::string name = trim(raw_name);
    if (name.empty()) {
        return;
    }

    for (const auto& existing : child_assets_) {
        if (existing == name) {

            auto it = std::find(child_assets_.begin(), child_assets_.end(), name);
            if (it != child_assets_.end()) {
                select_child(static_cast<int>(std::distance(child_assets_.begin(), it)));
            }
            return;
        }
    }

    if (selected_child_index_ >= 0 && selected_child_index_ < static_cast<int>(child_assets_.size())) {
        std::vector<std::string> next = child_assets_;
        next[static_cast<std::size_t>(selected_child_index_)] = name;
        apply_child_list_change(next);
    } else {
        std::vector<std::string> next = child_assets_;
        next.push_back(name);
        apply_child_list_change(next);
        select_child(static_cast<int>(next.size()) - 1);
    }
}

void FrameEditorSession::remove_selected_child() {
    if (child_assets_.empty()) {
        return;
    }
    if (selected_child_index_ < 0 || selected_child_index_ >= static_cast<int>(child_assets_.size())) {
        return;
    }
    std::vector<std::string> next;
    next.reserve(child_assets_.size() - 1);
    for (std::size_t i = 0; i < child_assets_.size(); ++i) {
        if (static_cast<int>(i) == selected_child_index_) continue;
        next.push_back(child_assets_[i]);
    }
    apply_child_list_change(next);
    select_child(std::clamp(selected_child_index_ - 1, 0, static_cast<int>(next.size()) - 1));
}

void FrameEditorSession::set_child_mode(int child_index, AnimationChildMode mode) {
    ensure_child_mode_size();
    if (child_index < 0 || static_cast<std::size_t>(child_index) >= child_modes_.size()) {
        return;
    }
    if (child_modes_[static_cast<std::size_t>(child_index)] == mode) {
        return;
    }
    child_modes_[static_cast<std::size_t>(child_index)] = mode;
    persist_changes();
}

void FrameEditorSession::render_attack_geometry(SDL_Renderer* renderer) const {
    if (!renderer || frames_.empty() || mode_ != Mode::AttackGeometry) return;
    const int frame_index = std::clamp(selected_index_, 0, static_cast<int>(frames_.size()) - 1);
    const auto& frame = frames_[frame_index];
    if (frame.attack.vectors.empty()) return;

    if (!assets_ || !target_) return;
    const WarpedScreenGrid& cam = assets_->getView();
    SDL_Point anchor = asset_anchor_world();
    const float scale = asset_local_scale();
    if (scale <= 0.0001f) return;

    auto to_screen = [&](float lx, float ly) -> SDL_FPoint {
        SDL_FPoint world{
            static_cast<float>(anchor.x) + lx * scale, static_cast<float>(anchor.y) - ly * scale };
        return cam.map_to_screen_f(world);
    };

    const std::string current_type = current_attack_type();
    int current_type_counter = 0;
    const int selected_idx = current_attack_vector_index();
    for (const auto& vec : frame.attack.vectors) {
        bool selected = false;
        if (vec.type == current_type) {
            selected = (current_type_counter == selected_idx && selected_idx >= 0);
            ++current_type_counter;
        }
        SDL_FPoint start_screen = to_screen(vec.start_x, vec.start_y);
        SDL_FPoint control_screen = to_screen(vec.control_x, vec.control_y);
        SDL_FPoint end_screen = to_screen(vec.end_x, vec.end_y);

        SDL_Color line_color = selected ? DMStyles::AccentButton().bg : DMStyles::HeaderButton().bg;
        SDL_SetRenderDrawColor(renderer, line_color.r, line_color.g, line_color.b, 220);
        constexpr int segments = 16;
        SDL_FPoint prev = start_screen;
        for (int i = 1; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float u = 1.0f - t;
            SDL_FPoint p{
                u * u * start_screen.x + 2.0f * u * t * control_screen.x + t * t * end_screen.x,
                u * u * start_screen.y + 2.0f * u * t * control_screen.y + t * t * end_screen.y
};
            SDL_RenderDrawLineF(renderer, prev.x, prev.y, p.x, p.y);
            prev = p;
        }

        if (selected) {
            SDL_SetRenderDrawColor(renderer, 180, 180, 180, 180);
            SDL_RenderDrawLineF(renderer, start_screen.x, start_screen.y, control_screen.x, control_screen.y);
            SDL_RenderDrawLineF(renderer, control_screen.x, control_screen.y, end_screen.x, end_screen.y);
        }

        auto draw_node = [&](SDL_FPoint p, bool is_selected_node) {
            const float radius = is_selected_node ? 10.0f : 8.0f;
            SDL_Color node_col = is_selected_node ? DMStyles::AccentButton().hover_bg : line_color;
            SDL_SetRenderDrawColor(renderer, node_col.r, node_col.g, node_col.b, 255);
            SDL_FRect r{ p.x - radius, p.y - radius, radius * 2.0f, radius * 2.0f };
            SDL_RenderFillRectF(renderer, &r);
            SDL_SetRenderDrawColor(renderer, DMStyles::Border().r, DMStyles::Border().g, DMStyles::Border().b, 255);
            SDL_RenderDrawRectF(renderer, &r);
        };
        draw_node(start_screen, selected);
        draw_node(end_screen, selected);
        if (selected) {

            const float cr = 6.0f;
            SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
            for (int i = 0; i < 16; ++i) {
                const float a = (static_cast<float>(i) / 16.0f) * 2.0f * static_cast<float>(M_PI);
                const float b = (static_cast<float>(i + 1) / 16.0f) * 2.0f * static_cast<float>(M_PI);
                SDL_RenderDrawLineF(renderer, control_screen.x + std::cos(a) * cr, control_screen.y + std::sin(a) * cr, control_screen.x + std::cos(b) * cr, control_screen.y + std::sin(b) * cr);
            }
        }
    }
}
