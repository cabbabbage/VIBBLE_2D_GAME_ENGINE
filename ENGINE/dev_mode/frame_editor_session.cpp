#include "frame_editor_session.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <utility>
#include <iomanip>

#include "asset/Asset.hpp"
#include "asset/animation.hpp"
#include "core/AssetsManager.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/dev_mode_utils.hpp"
#include "dev_mode/widgets.hpp"
#include "dev_mode/pan_and_zoom.hpp"
#include "render/camera.hpp"
#include "utils/input.hpp"

#include "asset_sections/animation_editor_window/AnimationDocument.hpp"
#include "asset_sections/animation_editor_window/PreviewProvider.hpp"
#include "asset_sections/animation_editor_window/AnimationEditorWindow.hpp"
#include "asset_sections/animation_editor_window/frame_editor/movement/MovementCanvas.hpp" // for helper signatures
#include "animation_update/animation_update.hpp" // bottom middle helper
#include "util/grid.hpp"

using animation_editor::AnimationDocument;
using animation_editor::PreviewProvider;

namespace {

inline SDL_Point round_point(SDL_FPoint p) {
    return SDL_Point{ static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)) };
}

inline SDL_FPoint round_fpoint(SDL_FPoint p) {
    return SDL_FPoint{ static_cast<float>(std::round(p.x)), static_cast<float>(std::round(p.y)) };
}

void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer || text.empty()) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) { TTF_CloseFont(font); return; }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

} // namespace

FrameEditorSession::FrameEditorSession() = default;
FrameEditorSession::~FrameEditorSession() = default;

void FrameEditorSession::begin(Assets* assets,
                               Asset* asset,
                               std::shared_ptr<AnimationDocument> document,
                               std::shared_ptr<PreviewProvider> preview,
                               const std::string& animation_id,
                               animation_editor::AnimationEditorWindow* host_to_toggle,
                               std::function<void()> on_end_callback) {
    if (!assets || !asset || !document || animation_id.empty()) {
        return;
    }
    assets_ = assets;
    target_ = asset;
    document_ = std::move(document);
    preview_ = std::move(preview);
    animation_id_ = animation_id;
    host_ = host_to_toggle;
    on_end_ = std::move(on_end_callback);

    // Snapshot state
    camera& cam = assets_->getView();
    prev_realism_enabled_ = cam.realism_enabled();
    prev_parallax_enabled_ = cam.parallax_enabled();
    prev_asset_hidden_ = target_->is_hidden();

    // Force perspective OFF; grid overlay ON (transient)
    cam.set_realism_enabled(false);
    cam.set_parallax_enabled(false);
    // Grid overlay preference is handled by DevControls when beginning the session.

    // Parse frames from document
    frames_ = parse_movement_frames_json(document_->animation_payload(animation_id_).value_or(std::string{}));
    child_assets_.clear();
    if (document_) {
        if (auto payload_dump = document_->animation_payload(animation_id_)) {
            nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
            if (payload.is_object() && payload.contains("children") && payload["children"].is_array()) {
                for (const auto& entry : payload["children"]) {
                    if (!entry.is_string()) continue;
                    std::string name = entry.get<std::string>();
                    if (name.empty()) continue;
                    child_assets_.push_back(std::move(name));
                }
            }
        }
    }
    sync_child_frames();
    selected_child_index_ = 0;
    if (frames_.empty()) {
        frames_.push_back(clamp_frame(MovementFrame{}));
    }
    // For derived animations that do NOT inherit movement, expand/trim movement frames
    // to match the effective preview frame count so thumbnails and navigation reflect
    // the true number of frames.
    {
        int desired_frames = 0;
        bool derived = false;
        bool inherit_movement = true;
        if (document_) {
            auto payload_dump = document_->animation_payload(animation_id_);
            if (payload_dump.has_value()) {
                nlohmann::json payload = nlohmann::json::parse(*payload_dump, nullptr, false);
                if (payload.is_object() && payload.contains("source") && payload["source"].is_object()) {
                    const auto& src = payload["source"];
                    std::string kind = src.value("kind", std::string{"folder"});
                    derived = (kind == std::string{"animation"});
                    inherit_movement = payload.value("inherit_source_movement", true);
                }
            }
        }
        if (derived && !inherit_movement) {
            if (preview_) {
                desired_frames = preview_->get_frame_count(animation_id_);
            }
            if (desired_frames <= 0) desired_frames = 1;
            if (static_cast<int>(frames_.size()) < desired_frames) {
                const int to_add = desired_frames - static_cast<int>(frames_.size());
                for (int i = 0; i < to_add; ++i) {
                    frames_.push_back(clamp_frame(MovementFrame{}));
                }
            } else if (static_cast<int>(frames_.size()) > desired_frames) {
                frames_.resize(desired_frames);
            }
        }
    }
    // Always keep the first frame zeroed
    frames_.front().dx = 0.0f;
    frames_.front().dy = 0.0f;
    rebuild_rel_positions();

    // Focus camera on asset; start at frame 0 and set asset anim
    assets_->focus_camera_on_asset(target_, 0.85, 18);
    selected_index_ = 0;
    target_->current_animation = animation_id_;
    update_asset_preview_frame();

    // Show animation initially; ensure asset visible
    show_animation_ = true;
    target_->set_hidden(false);

    ensure_widgets();
    // Initialize panel positions relative to the asset for better UX
    {
        int sw = 0, sh = 0;
        if (assets_ && assets_->renderer()) {
            SDL_GetRendererOutputSize(assets_->renderer(), &sw, &sh);
        }
        const camera& cam = assets_->getView();
        SDL_Point anchor_world = animation_update::detail::bottom_middle_for(*target_, target_->pos);
        SDL_FPoint anchor_screen_f = cam.map_to_screen_f(SDL_FPoint{ static_cast<float>(anchor_world.x), static_cast<float>(anchor_world.y) });
        SDL_Point anchor_screen = round_point(anchor_screen_f);

        const int dir_w = 600;
        const int dir_h = DMButton::height() + DMSpacing::small_gap()*2;
        const int nav_h = 90;
        const int nav_w = 560;

        // Compute toolbox width (similar to rebuild_layout)
        const int tool_padding = DMSpacing::small_gap();
        int tool_w = tool_padding * 2;
        const int gaps_between = DMSpacing::small_gap();
        const int line_h = std::max(std::max(DMButton::height(), DMCheckbox::height()), DMTextBox::height());
        const int cb_w = cb_show_anim_ ? std::max(140, cb_show_anim_->preferred_width()) : 0;
        const int tb_w = 120;
        if (btn_smooth_) tool_w += btn_smooth_->rect().w;
        if (cb_show_anim_) tool_w += (tool_w > tool_padding*2 ? gaps_between : 0) + cb_w;
        if (tb_total_dx_) tool_w += gaps_between + tb_w;
        if (tb_total_dy_) tool_w += gaps_between + tb_w;
        const int tool_h = line_h + tool_padding * 2;

        // Position panels relative to asset
        // Frame navigator: 400 pixels below asset, horizontally centered
        nav_pos_.x = anchor_screen.x - nav_w / 2;
        nav_pos_.y = anchor_screen.y + 400;

        // Mode selector: 200 pixels above asset, horizontally centered
        dir_pos_.x = anchor_screen.x - dir_w / 2;
        dir_pos_.y = anchor_screen.y - 200 - dir_h;

        // Tool panel: 400 pixels left of asset, vertically centered on screen
        toolbox_pos_.x = anchor_screen.x - 400 - tool_w / 2;
        toolbox_pos_.y = sh / 2 - tool_h / 2;

        // Clamp to screen bounds
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

void FrameEditorSession::end() {
    if (!active_) return;
    // Restore camera and overlay state
    if (assets_) {
        camera& cam = assets_->getView();
        cam.set_realism_enabled(prev_realism_enabled_);
        cam.set_parallax_enabled(prev_parallax_enabled_);
        // Cancel any transient pan/zoom override
        pan_zoom_.cancel(cam);
    }
    if (target_) {
        target_->set_hidden(prev_asset_hidden_);
    }
    // Reopen animation editor window
    if (host_) {
        host_->set_visible(true);
        host_->focus_animation(animation_id_);
    }
    // Clear session
    active_ = false;
    assets_ = nullptr;
    target_ = nullptr;
    document_.reset();
    preview_.reset();
    host_ = nullptr;
    animation_id_.clear();
    frames_.clear();
    rel_positions_.clear();
    if (on_end_) { auto cb = std::move(on_end_); on_end_ = {}; cb(); }
}

void FrameEditorSession::update(const Input& input) {
    if (!active_) return;
    // Enable mouse wheel zoom on the world camera while editing (panning is blocked to avoid conflicts)
    if (assets_) {
        camera& cam = assets_->getView();
        // Make sure layout is up to date before computing any UI blocking logic (future use)
        ensure_widgets();
        rebuild_layout();
        const bool pan_blocked = true; // disallow left-drag panning here; only wheel zoom
        pan_zoom_.handle_input(cam, input, pan_blocked);
    }
    // Ensure the asset frame reflects selection
    update_asset_preview_frame();
    // Sync checkbox state and totals text boxes
    if (cb_show_anim_) {
        cb_show_anim_->set_value(show_animation_);
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
    if (mode_ == Mode::Children) {
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
        } else {
            if (tb_child_dx_ && !tb_child_dx_->is_editing()) tb_child_dx_->set_value("0");
            if (tb_child_dy_ && !tb_child_dy_->is_editing()) tb_child_dy_->set_value("0");
            if (tb_child_deg_ && !tb_child_deg_->is_editing()) tb_child_deg_->set_value("0");
            if (cb_child_visible_) cb_child_visible_->set_value(false);
        }
    }
    // Ensure asset hidden state follows checkbox
    if (target_) target_->set_hidden(!show_animation_);
}

bool FrameEditorSession::handle_event(const SDL_Event& e) {
    if (!active_) return false;
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

    // Handle dragging (motion and release)
    if (dragging_dir_ || dragging_toolbox_ || dragging_nav_) {
        if (e.type == SDL_MOUSEMOTION) {
            if (dragging_dir_) {
                dir_pos_.x = e.motion.x - drag_offset_dir_.x;
                dir_pos_.y = e.motion.y - drag_offset_dir_.y;
                const int dir_w = 600;
                const int dir_h = DMButton::height() + DMSpacing::small_gap()*2;
                clamp_panel_pos(dir_pos_.x, dir_pos_.y, dir_w, dir_h);
            } else if (dragging_toolbox_) {
                toolbox_pos_.x = e.motion.x - drag_offset_toolbox_.x;
                toolbox_pos_.y = e.motion.y - drag_offset_toolbox_.y;
                const int tool_w = toolbox_rect_.w;
                const int tool_h = toolbox_rect_.h;
                clamp_panel_pos(toolbox_pos_.x, toolbox_pos_.y, tool_w, tool_h);
            } else if (dragging_nav_) {
                nav_pos_.x = e.motion.x - drag_offset_nav_.x;
                nav_pos_.y = e.motion.y - drag_offset_nav_.y;
                const int nav_w = 560;
                const int nav_h = 90;
                clamp_panel_pos(nav_pos_.x, nav_pos_.y, nav_w, nav_h);
            }
            rebuild_layout();
            return true; // consume while dragging
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            dragging_dir_ = false;
            dragging_toolbox_ = false;
            dragging_nav_ = false;
            return true; // consume mouse up at end of drag
        }
    }

    // Begin dragging on mouse down if inside panel backgrounds, avoiding interactive controls
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        // Directory panel drag: avoid buttons inside
        bool over_dir = SDL_PointInRect(&p, &directory_rect_);
        if (over_dir) {
            bool over_button = false;
            const DMButton* buttons[] = {
                btn_back_.get(),
                btn_movement_.get(),
                btn_children_.get(),
                btn_attack_geometry_.get(),
                btn_hit_geometry_.get()
            };
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
        // Toolbox panel drag: avoid interactive controls (buttons/checkbox/textboxes)
        if (mode_ == Mode::Movement && SDL_PointInRect(&p, &toolbox_rect_)) {
            bool over_interactive = false;
            const DMButton* buttons[] = { btn_smooth_.get() };
            for (const DMButton* b : buttons) {
                if (!b) continue; const SDL_Rect& r = b->rect();
                if (SDL_PointInRect(&p, &r)) { over_interactive = true; break; }
            }
            if (!over_interactive && cb_show_anim_) {
                const SDL_Rect& r = cb_show_anim_->rect();
                if (SDL_PointInRect(&p, &r)) over_interactive = true;
            }
            if (!over_interactive && tb_total_dx_) {
                const SDL_Rect& r = tb_total_dx_->rect();
                if (SDL_PointInRect(&p, &r)) over_interactive = true;
            }
            if (!over_interactive && tb_total_dy_) {
                const SDL_Rect& r = tb_total_dy_->rect();
                if (SDL_PointInRect(&p, &r)) over_interactive = true;
            }
            if (!over_interactive) {
                dragging_toolbox_ = true;
                drag_offset_toolbox_ = SDL_Point{ p.x - toolbox_rect_.x, p.y - toolbox_rect_.y };
                return true;
            }
        }
        // Nav panel drag: avoid prev/next buttons and thumbnails
        if (SDL_PointInRect(&p, &nav_rect_)) {
            bool over_nav_ctrl = false;
            if (btn_prev_) { const SDL_Rect& r = btn_prev_->rect(); if (SDL_PointInRect(&p, &r)) over_nav_ctrl = true; }
            if (!over_nav_ctrl && btn_next_) { const SDL_Rect& r = btn_next_->rect(); if (SDL_PointInRect(&p, &r)) over_nav_ctrl = true; }
            if (!over_nav_ctrl) over_nav_ctrl = point_in_any_thumb(p);
            if (!over_nav_ctrl) {
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

    // Directory buttons
    if (handle_button(btn_back_, [this]() { this->end(); })) return true;
    if (handle_button(btn_movement_, [this]() { this->mode_ = Mode::Movement; })) return true;
    if (handle_button(btn_children_, [this]() { this->mode_ = Mode::Children; })) return true;
    if (handle_button(btn_attack_geometry_, [this]() { this->mode_ = Mode::AttackGeometry; })) return true;
    if (handle_button(btn_hit_geometry_, [this]() { this->mode_ = Mode::HitGeometry; })) return true;

    // Movement tool panel widgets
    if (mode_ == Mode::Movement) {
        if (handle_button(btn_smooth_, [this]() { this->smooth_frames(); })) return true;

        // Checkbox toggle
        if (cb_show_anim_ && cb_show_anim_->handle_event(e)) {
            bool current = cb_show_anim_->value();
            if (current != last_show_anim_value_) {
                last_show_anim_value_ = current;
                show_animation_ = current;
                if (target_) target_->set_hidden(!show_animation_);
            }
            return true;
        }

        // Handle totals edit boxes
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
    }

    if (mode_ == Mode::Children) {
        bool consumed_child = false;
        if (tb_child_dx_) consumed_child = tb_child_dx_->handle_event(e) || consumed_child;
        if (tb_child_dy_) consumed_child = tb_child_dy_->handle_event(e) || consumed_child;
        if (tb_child_deg_) consumed_child = tb_child_deg_->handle_event(e) || consumed_child;
        if (cb_child_visible_) consumed_child = cb_child_visible_->handle_event(e) || consumed_child;
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
                if (tb_child_dx_) {
                    float new_dx = parse_float(tb_child_dx_->value(), child->dx);
                    if (!std::isnan(new_dx) && child->dx != new_dx) {
                        child->dx = new_dx;
                        changed = true;
                    }
                }
                if (tb_child_dy_) {
                    float new_dy = parse_float(tb_child_dy_->value(), child->dy);
                    if (!std::isnan(new_dy) && child->dy != new_dy) {
                        child->dy = new_dy;
                        changed = true;
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
                if (changed) {
                    rebuild_rel_positions();
                    persist_changes();
                }
            }
            return true;
        }
    }

    // Navigation
    if (handle_button(btn_prev_, [this]() { this->select_frame(std::max(0, this->selected_index_ - 1)); })) return true;
    if (handle_button(btn_next_, [this]() { this->select_frame(this->selected_index_ + 1); })) return true;
    if (mode_ == Mode::Children) {
        if (handle_button(btn_child_prev_, [this]() { this->select_child(this->selected_child_index_ - 1); })) return true;
        if (handle_button(btn_child_next_, [this]() { this->select_child(this->selected_child_index_ + 1); })) return true;
    }

    // Thumbnails (skip if we were dragging)
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        if (dragging_dir_ || dragging_nav_) return true;
        SDL_Point p{e.button.x, e.button.y};
        for (size_t i = 0; i < thumb_rects_.size(); ++i) {
            if (SDL_PointInRect(&p, &thumb_rects_[i])) {
                select_frame(static_cast<int>(i));
                return true;
            }
        }
    }

    // Map interaction: left-click to set/adjust current frame
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point sp{e.button.x, e.button.y};
        // If inside any of our panels, consume the event so it doesn't leak to dev controls/room editor
        if (SDL_PointInRect(&sp, &directory_rect_) || SDL_PointInRect(&sp, &nav_rect_) || SDL_PointInRect(&sp, &toolbox_rect_)) {
            return true;
        }
        if (!assets_ || !target_) return false;
        camera& cam = assets_->getView();
        SDL_FPoint world_f = cam.screen_to_map(sp);
        // Anchor is bottom-middle of the asset
        SDL_Point anchor_world = animation_update::detail::bottom_middle_for(*target_, target_->pos);
        // Snap absolute click to current map grid resolution before computing relative
        SDL_Point world_px{ static_cast<int>(std::lround(world_f.x)), static_cast<int>(std::lround(world_f.y)) };
        int snap_r = std::max(0, assets_->map_grid_settings().resolution);
        SDL_Point snapped = vibble::grid::snap_world_to_vertex(world_px, vibble::grid::clamp_resolution(snap_r));
        SDL_FPoint desired_rel{ static_cast<float>(snapped.x - anchor_world.x), static_cast<float>(snapped.y - anchor_world.y) };
        // Compute current base rel positions
        std::vector<SDL_FPoint> base = rel_positions_;
        apply_frame_move_from_base(selected_index_, desired_rel, base);
        rebuild_rel_positions();
        persist_changes();
        return true;
    }

    // Swallow pointer events inside our panels to prevent world/editor input from seeing them
    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL) {
        SDL_Point sp;
        if (e.type == SDL_MOUSEMOTION) { sp = SDL_Point{ e.motion.x, e.motion.y }; }
        else if (e.type == SDL_MOUSEWHEEL) { int mx=0,my=0; SDL_GetMouseState(&mx,&my); sp = SDL_Point{mx,my}; }
        else { sp = SDL_Point{ e.button.x, e.button.y }; }
        if (SDL_PointInRect(&sp, &directory_rect_) || SDL_PointInRect(&sp, &nav_rect_) || SDL_PointInRect(&sp, &toolbox_rect_)) {
            return true;
        }
    }

    // Do not consume events outside our panels by default
    return false;
}

void FrameEditorSession::render(SDL_Renderer* renderer) const {
    if (!active_ || !renderer || !assets_ || !target_) return;

    // Compute anchor
    const camera& cam = assets_->getView();
    SDL_Point anchor_world = animation_update::detail::bottom_middle_for(*target_, target_->pos);

    // Draw path lines and points in world space
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color path_col = DMStyles::AccentButton().bg;
    SDL_SetRenderDrawColor(renderer, path_col.r, path_col.g, path_col.b, 205);
    for (size_t i = 1; i < rel_positions_.size(); ++i) {
        SDL_FPoint a = cam.map_to_screen_f(SDL_FPoint{ rel_positions_[i-1].x + anchor_world.x,
                                                       rel_positions_[i-1].y + anchor_world.y });
        SDL_FPoint b = cam.map_to_screen_f(SDL_FPoint{ rel_positions_[i].x + anchor_world.x,
                                                       rel_positions_[i].y + anchor_world.y });
        SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(a.x)), static_cast<int>(std::lround(a.y)),
                                      static_cast<int>(std::lround(b.x)), static_cast<int>(std::lround(b.y)));
    }
    // Points
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

    if (mode_ == Mode::Children && !child_assets_.empty() && selected_index_ < static_cast<int>(frames_.size())) {
        const auto& frame = frames_[selected_index_];
        for (std::size_t i = 0; i < child_assets_.size() && i < frame.children.size(); ++i) {
            const auto& child = frame.children[i];
            SDL_FPoint screen = cam.map_to_screen_f(SDL_FPoint{
                child.dx + anchor_world.x,
                child.dy + anchor_world.y
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
    }

    // Panels
    ensure_widgets();
    rebuild_layout();
    // Directory panel background
    dm_draw::DrawBeveledRect(renderer, directory_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelHeader(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
    if (btn_back_) btn_back_->render(renderer);
    if (btn_movement_) btn_movement_->render(renderer);
    if (btn_children_) btn_children_->render(renderer);
    if (btn_attack_geometry_) btn_attack_geometry_->render(renderer);
    if (btn_hit_geometry_) btn_hit_geometry_->render(renderer);

    // Toolbox panel
    if (mode_ == Mode::Movement && toolbox_rect_.w > 0 && toolbox_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, toolbox_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        if (btn_smooth_) btn_smooth_->render(renderer);
        if (cb_show_anim_) cb_show_anim_->render(renderer);
        if (tb_total_dx_) tb_total_dx_->render(renderer);
        if (tb_total_dy_) tb_total_dy_->render(renderer);
    } else if (mode_ == Mode::Children && toolbox_rect_.w > 0 && toolbox_rect_.h > 0) {
        dm_draw::DrawBeveledRect(renderer, toolbox_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        if (btn_child_prev_) btn_child_prev_->render(renderer);
        if (btn_child_next_) btn_child_next_->render(renderer);
        if (tb_child_dx_) tb_child_dx_->render(renderer);
        if (tb_child_dy_) tb_child_dy_->render(renderer);
        if (tb_child_deg_) tb_child_deg_->render(renderer);
        if (cb_child_visible_) cb_child_visible_->render(renderer);
    }

    // Navigation panel
    dm_draw::DrawBeveledRect(renderer, nav_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
    // Title: show animation name above buttons
    if (!animation_id_.empty()) {
        render_label(renderer, animation_id_, nav_rect_.x + 8, nav_rect_.y + 4);
    }
    if (btn_prev_) btn_prev_->render(renderer);
    if (btn_next_) btn_next_->render(renderer);

    // Thumbnails
    for (size_t i = 0; i < thumb_rects_.size(); ++i) {
        const SDL_Rect& r = thumb_rects_[i];
        SDL_Color border = DMStyles::Border();
        const bool is_current = static_cast<int>(i) == selected_index_;
        if (is_current) {
            border = DMStyles::AccentButton().border;
        }
        SDL_Texture* tex = nullptr;
        if (preview_) {
            tex = preview_->get_frame_texture(renderer, animation_id_, static_cast<int>(i));
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
    }
}

void FrameEditorSession::set_grid_overlay_enabled_transient(bool enabled) {
    (void)enabled; // DevControls handles drawing; we rely on caller to toggle
}

void FrameEditorSession::ensure_widgets() const {
    const DMButtonStyle& header = DMStyles::HeaderButton();
    const DMButtonStyle& tab_active = DMStyles::AccentButton();
    const int bw = 96;
    const int bh = DMButton::height();
    if (!btn_back_) btn_back_ = std::make_unique<DMButton>(u8"\u2190 Back", &DMStyles::DeleteButton(), 96, bh);
    if (!btn_movement_) btn_movement_ = std::make_unique<DMButton>("Movement", mode_ == Mode::Movement ? &tab_active : &header, bw, bh);
    if (!btn_children_) btn_children_ = std::make_unique<DMButton>("Children", mode_ == Mode::Children ? &tab_active : &header, bw, bh);
    if (!btn_attack_geometry_) btn_attack_geometry_ = std::make_unique<DMButton>("Attack Geometry", mode_ == Mode::AttackGeometry ? &tab_active : &header, bw, bh);
    if (!btn_hit_geometry_) btn_hit_geometry_ = std::make_unique<DMButton>("Hit Geometry", mode_ == Mode::HitGeometry ? &tab_active : &header, bw, bh);
    if (!btn_prev_) btn_prev_ = std::make_unique<DMButton>("<", &header, 40, 40);
    if (!btn_next_) btn_next_ = std::make_unique<DMButton>(">", &header, 40, 40);
    if (!btn_smooth_) btn_smooth_ = std::make_unique<DMButton>("Smooth", &DMStyles::AccentButton(), 120, bh);
    if (!cb_show_anim_) cb_show_anim_ = std::make_unique<DMCheckbox>("Show Animation", show_animation_);
    if (!tb_total_dx_) tb_total_dx_ = std::make_unique<DMTextBox>("Total dX", "0");
    if (!tb_total_dy_) tb_total_dy_ = std::make_unique<DMTextBox>("Total dY", "0");
    if (!btn_child_prev_) btn_child_prev_ = std::make_unique<DMButton>("< Child", &header, 96, bh);
    if (!btn_child_next_) btn_child_next_ = std::make_unique<DMButton>("Child >", &header, 96, bh);
    if (!tb_child_dx_) tb_child_dx_ = std::make_unique<DMTextBox>("Child dX", "0");
    if (!tb_child_dy_) tb_child_dy_ = std::make_unique<DMTextBox>("Child dY", "0");
    if (!tb_child_deg_) tb_child_deg_ = std::make_unique<DMTextBox>("Rotation", "0");
    if (!cb_child_visible_) cb_child_visible_ = std::make_unique<DMCheckbox>("Visible", true);
    last_show_anim_value_ = show_animation_;
    last_totals_dx_text_ = tb_total_dx_->value();
    last_totals_dy_text_ = tb_total_dy_->value();
}

void FrameEditorSession::rebuild_layout() const {
    if (!assets_ || !target_) return;
    const camera& cam = assets_->getView();
    const int screen_w = assets_->renderer() ? assets_->getView().get_current_view().width() : 0; // not used for clamp heavily
    (void)screen_w;
    (void)cam; // anchor-based layout replaced by draggable screen-space positions
    const int dir_w = 600;
    const int dir_h = DMButton::height() + DMSpacing::small_gap()*2;
    directory_rect_ = SDL_Rect{ dir_pos_.x, dir_pos_.y, dir_w, dir_h };
    // Place buttons inside
    int x = directory_rect_.x + DMSpacing::small_gap();
    int y = directory_rect_.y + DMSpacing::small_gap();
    if (btn_back_) { btn_back_->set_rect(SDL_Rect{ x, y, btn_back_->rect().w, DMButton::height() }); x += btn_back_->rect().w + DMSpacing::small_gap(); }
    if (btn_movement_) { btn_movement_->set_style(mode_==Mode::Movement? &DMStyles::AccentButton() : &DMStyles::HeaderButton()); btn_movement_->set_rect(SDL_Rect{ x, y, btn_movement_->rect().w, DMButton::height() }); x += btn_movement_->rect().w + DMSpacing::small_gap(); }
    if (btn_children_) { btn_children_->set_style(mode_==Mode::Children? &DMStyles::AccentButton() : &DMStyles::HeaderButton()); btn_children_->set_rect(SDL_Rect{ x, y, btn_children_->rect().w, DMButton::height() }); x += btn_children_->rect().w + DMSpacing::small_gap(); }
    if (btn_attack_geometry_) {
        btn_attack_geometry_->set_style(mode_==Mode::AttackGeometry? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
        btn_attack_geometry_->set_rect(SDL_Rect{ x, y, btn_attack_geometry_->rect().w, DMButton::height() });
        x += btn_attack_geometry_->rect().w + DMSpacing::small_gap();
    }
    if (btn_hit_geometry_) {
        btn_hit_geometry_->set_style(mode_==Mode::HitGeometry? &DMStyles::AccentButton() : &DMStyles::HeaderButton());
        btn_hit_geometry_->set_rect(SDL_Rect{ x, y, btn_hit_geometry_->rect().w, DMButton::height() });
    }

    // Toolbox panel placement
    const int tool_padding = DMSpacing::small_gap();
    if (mode_ == Mode::Movement) {
        int tool_w = tool_padding * 2;
        const int gaps_between = DMSpacing::small_gap();
        const int line_h = std::max(std::max(DMButton::height(), DMCheckbox::height()), DMTextBox::height());
        const int cb_w = cb_show_anim_ ? std::max(140, cb_show_anim_->preferred_width()) : 0;
        const int tb_w = 120;
        if (btn_smooth_) tool_w += btn_smooth_->rect().w;
        if (cb_show_anim_) tool_w += (tool_w > tool_padding*2 ? gaps_between : 0) + cb_w;
        if (tb_total_dx_) tool_w += gaps_between + tb_w;
        if (tb_total_dy_) tool_w += gaps_between + tb_w;
        const int tool_h = line_h + tool_padding * 2;
        toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, tool_w, tool_h };
        int tx = toolbox_rect_.x + tool_padding;
        int ty = toolbox_rect_.y + tool_padding;
        if (btn_smooth_) {
            btn_smooth_->set_rect(SDL_Rect{ tx, ty, btn_smooth_->rect().w, line_h });
            tx += btn_smooth_->rect().w + gaps_between;
        }
        if (cb_show_anim_) {
            cb_show_anim_->set_rect(SDL_Rect{ tx, ty, cb_w, line_h });
            tx += cb_w + gaps_between;
        }
        if (tb_total_dx_) {
            tb_total_dx_->set_rect(SDL_Rect{ tx, ty, tb_w, DMTextBox::height() });
            tx += tb_w + gaps_between;
        }
        if (tb_total_dy_) {
            tb_total_dy_->set_rect(SDL_Rect{ tx, ty, tb_w, DMTextBox::height() });
        }
    } else if (mode_ == Mode::Children) {
        const int gaps_between = DMSpacing::small_gap();
        const int line_h = std::max(DMButton::height(), DMTextBox::height());
        const int tb_w = 110;
        int tool_w = tool_padding * 2;
        if (btn_child_prev_) tool_w += btn_child_prev_->rect().w;
        if (btn_child_next_) tool_w += btn_child_next_->rect().w + gaps_between;
        tool_w += (tb_w * 3) + gaps_between * 4;
        const int tool_h = line_h * 2 + tool_padding * 3;
        toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, tool_w, tool_h };
        int tx = toolbox_rect_.x + tool_padding;
        int ty = toolbox_rect_.y + tool_padding;
        if (btn_child_prev_) {
            btn_child_prev_->set_rect(SDL_Rect{ tx, ty, btn_child_prev_->rect().w, line_h });
            tx += btn_child_prev_->rect().w + gaps_between;
        }
        if (btn_child_next_) {
            btn_child_next_->set_rect(SDL_Rect{ tx, ty, btn_child_next_->rect().w, line_h });
            tx += btn_child_next_->rect().w + gaps_between;
        }
        ty += line_h + gaps_between;
        tx = toolbox_rect_.x + tool_padding;
        if (tb_child_dx_) {
            tb_child_dx_->set_rect(SDL_Rect{ tx, ty, tb_w, DMTextBox::height() });
            tx += tb_w + gaps_between;
        }
        if (tb_child_dy_) {
            tb_child_dy_->set_rect(SDL_Rect{ tx, ty, tb_w, DMTextBox::height() });
            tx += tb_w + gaps_between;
        }
        if (tb_child_deg_) {
            tb_child_deg_->set_rect(SDL_Rect{ tx, ty, tb_w, DMTextBox::height() });
            tx += tb_w + gaps_between;
        }
        if (cb_child_visible_) {
            cb_child_visible_->set_rect(SDL_Rect{ tx, ty, std::max(100, cb_child_visible_->preferred_width()), DMCheckbox::height() });
        }
    } else {
        toolbox_rect_ = SDL_Rect{ toolbox_pos_.x, toolbox_pos_.y, 0, 0 };
    }

    // Navigation panel under tool strip
    const int nav_h = 90;
    const int nav_w = 560;
    nav_rect_ = SDL_Rect{ nav_pos_.x, nav_pos_.y, nav_w, nav_h };
    const int prev_w = 40, next_w = 40;
    if (btn_prev_) btn_prev_->set_rect(SDL_Rect{ nav_rect_.x + 6, nav_rect_.y + (nav_rect_.h - 40)/2, prev_w, 40 });
    if (btn_next_) btn_next_->set_rect(SDL_Rect{ nav_rect_.x + nav_rect_.w - next_w - 6, nav_rect_.y + (nav_rect_.h - 40)/2, next_w, 40 });

    // Thumbs area
    const int title_h = 22;
    const int thumb_h = std::max(1, nav_rect_.h - 16 - title_h);
    const int thumb_w = thumb_h; // square thumbs
    const int spacing = 8;
    int x0 = (btn_prev_ ? btn_prev_->rect().x + btn_prev_->rect().w + spacing : nav_rect_.x + spacing);
    int x1 = (btn_next_ ? btn_next_->rect().x - spacing : nav_rect_.x + nav_rect_.w - spacing);
    int available = std::max(0, x1 - x0);
    int per = thumb_w + spacing;
    int max_visible = per > 0 ? std::max(1, available / per) : 1;
    // Center thumbnails
    int used = max_visible * per - spacing;
    int start_x = x0 + std::max(0, (available - used) / 2);
    thumb_rects_.clear();
    int count = static_cast<int>(frames_.size());
    int visible = std::min(count, max_visible);
    int first_index = std::clamp(selected_index_ - visible/2, 0, std::max(0, count - visible));
    for (int i = 0; i < visible; ++i) {
        int idx = first_index + i;
        SDL_Rect r{ start_x + i * per, nav_rect_.y + 8 + title_h, thumb_w, thumb_h };
        if (idx >= 0 && idx < count) {
            if (static_cast<int>(thumb_rects_.size()) <= idx) thumb_rects_.resize(idx+1);
            thumb_rects_[idx] = r;
        }
    }
    // Ensure we have rects for all frames (non-visible indices will be empty {0,0,0,0})
    if (static_cast<int>(thumb_rects_.size()) < count) thumb_rects_.resize(count);
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

void FrameEditorSession::sync_child_frames() {
    if (child_assets_.empty()) {
        for (auto& frame : frames_) {
            frame.children.clear();
        }
        selected_child_index_ = 0;
        return;
    }
    for (auto& frame : frames_) {
        std::vector<ChildFrame> normalized(child_assets_.size());
        for (std::size_t i = 0; i < normalized.size(); ++i) {
            normalized[i].child_index = static_cast<int>(i);
        }
        for (const auto& existing : frame.children) {
            if (existing.child_index < 0 ||
                existing.child_index >= static_cast<int>(normalized.size())) {
                continue;
            }
            normalized[existing.child_index] = existing;
        }
        frame.children = std::move(normalized);
    }
    if (selected_child_index_ >= static_cast<int>(child_assets_.size())) {
        selected_child_index_ = static_cast<int>(child_assets_.size()) - 1;
    }
    if (selected_child_index_ < 0) {
        selected_child_index_ = 0;
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

void FrameEditorSession::select_child(int index) {
    if (child_assets_.empty()) {
        selected_child_index_ = 0;
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(child_assets_.size()) - 1);
    if (index == selected_child_index_) {
        return;
    }
    selected_child_index_ = index;
}

void FrameEditorSession::persist_changes() {
    if (!document_) return;
    // Serialize primary movement + totals (reuse logic similar to FrameMovementEditor)
    nlohmann::json payload = nlohmann::json::object();
    if (auto j = document_->animation_payload(animation_id_)) {
        payload = nlohmann::json::parse(*j, nullptr, false);
        if (!payload.is_object()) payload = nlohmann::json::object();
    }
    nlohmann::json children_array = nlohmann::json::array();
    for (const auto& child_name : child_assets_) {
        children_array.push_back(child_name);
    }
    payload["children"] = std::move(children_array);
    nlohmann::json movement = nlohmann::json::array();
    for (size_t i = 0; i < frames_.size(); ++i) {
        const MovementFrame& f = frames_[i];
        int dx = static_cast<int>(std::lround(i == 0 ? 0.0f : f.dx));
        int dy = static_cast<int>(std::lround(i == 0 ? 0.0f : f.dy));
        nlohmann::json entry = nlohmann::json::array({dx, dy});
        if (f.resort_z) entry.push_back(f.resort_z);
        if (!child_assets_.empty()) {
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
                    child_entries.push_back(std::move(child_json));
                }
            }
            entry.push_back(std::move(child_entries));
        }
        movement.push_back(entry);
    }
    if (movement.empty()) movement.push_back(nlohmann::json::array({0,0}));
    movement[0][0] = 0; movement[0][1] = 0;
    payload["movement"] = std::move(movement);
    // totals
    int total_dx = 0, total_dy = 0;
    for (size_t i = 1; i < frames_.size(); ++i) {
        total_dx += static_cast<int>(std::lround(frames_[i].dx));
        total_dy += static_cast<int>(std::lround(frames_[i].dy));
    }
    payload["movement_total"] = nlohmann::json{{"dx", total_dx}, {"dy", total_dy}};
    document_->replace_animation_payload(animation_id_, payload.dump());
    // Persist immediately because the AnimationEditorWindow may be hidden during in-world sessions
    document_->save_to_file();
}

void FrameEditorSession::smooth_frames() {
    const size_t n = frames_.size();
    if (n <= 2) return;
    // Compute running total and redistribute evenly across steps (reuse behavior)
    double total_dx = 0.0, total_dy = 0.0;
    for (size_t i = 1; i < n; ++i) {
        total_dx += std::isfinite(frames_[i].dx) ? frames_[i].dx : 0.0;
        total_dy += std::isfinite(frames_[i].dy) ? frames_[i].dy : 0.0;
    }
    const size_t steps = n - 1;
    frames_[0].dx = frames_[0].dy = 0.0f;
    int accum_x = 0, accum_y = 0;
    for (size_t i = 1; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const double target_x = total_dx * t;
        const double target_y = total_dy * t;
        int rounded_x = (i == steps) ? static_cast<int>(std::lround(total_dx)) : static_cast<int>(std::lround(target_x));
        int rounded_y = (i == steps) ? static_cast<int>(std::lround(total_dy)) : static_cast<int>(std::lround(target_y));
        const int dx = rounded_x - accum_x;
        const int dy = rounded_y - accum_y;
        accum_x = rounded_x;
        accum_y = rounded_y;
        frames_[i].dx = static_cast<float>(dx);
        frames_[i].dy = static_cast<float>(dy);
    }
    rebuild_rel_positions();
    persist_changes();
}

void FrameEditorSession::select_frame(int index) {
    index = std::clamp(index, 0, static_cast<int>(frames_.size()) - 1);
    if (index == selected_index_) return;
    selected_index_ = index;
    update_asset_preview_frame();
}

void FrameEditorSession::update_asset_preview_frame() const {
    if (!target_ || !target_->info) return;
    // Switch animation and point to selected frame
    auto it = target_->info->animations.find(animation_id_);
    if (it == target_->info->animations.end()) return;
    Animation& anim = const_cast<Animation&>(it->second);
    target_->current_animation = animation_id_;
    // Iterate to the selected frame
    AnimationFrame* f = anim.get_first_frame();
    int idx = 0;
    while (f && f->next && idx < selected_index_) { f = f->next; ++idx; }
    target_->current_frame = f ? f : anim.get_first_frame();
    target_->static_frame = anim.frames.size() <= 1;
    target_->set_frame_progress(0.0f);
}

FrameEditorSession::MovementFrame FrameEditorSession::clamp_frame(const MovementFrame& in) {
    MovementFrame f = in;
    if (!std::isfinite(f.dx)) f.dx = 0.0f;
    if (!std::isfinite(f.dy)) f.dy = 0.0f;
    return f;
}

std::vector<FrameEditorSession::MovementFrame> FrameEditorSession::parse_movement_frames_json(const std::string& payload_json) {
    std::vector<MovementFrame> frames;
    nlohmann::json payload = nlohmann::json::parse(payload_json, nullptr, false);
    if (!payload.is_object()) {
        frames.push_back(MovementFrame{});
        return frames;
    }
    nlohmann::json movement = nlohmann::json::array();
    if (payload.contains("movement")) movement = payload["movement"];
    if (!movement.is_array() || movement.empty()) {
        frames.push_back(MovementFrame{});
        return frames;
    }
    for (const auto& entry : movement) {
        MovementFrame f{};
        if (entry.is_array()) {
            if (!entry.empty() && entry[0].is_number()) f.dx = static_cast<float>(entry[0].get<double>());
            if (entry.size() > 1 && entry[1].is_number()) f.dy = static_cast<float>(entry[1].get<double>());
            if (entry.size() > 2 && entry[2].is_boolean()) f.resort_z = entry[2].get<bool>();
            if (entry.size() > 4 && entry[4].is_array()) {
                for (const auto& child_entry : entry[4]) {
                    if (!child_entry.is_array() || child_entry.empty()) continue;
                    ChildFrame child;
                    try { child.child_index = child_entry[0].get<int>(); } catch (...) { child.child_index = -1; }
                    if (child_entry.size() > 1 && child_entry[1].is_number()) {
                        child.dx = static_cast<float>(child_entry[1].get<double>());
                    }
                    if (child_entry.size() > 2 && child_entry[2].is_number()) {
                        child.dy = static_cast<float>(child_entry[2].get<double>());
                    }
                    if (child_entry.size() > 3 && child_entry[3].is_number()) {
                        child.degree = static_cast<float>(child_entry[3].get<double>());
                    }
                    if (child_entry.size() > 4) {
                        if (child_entry[4].is_boolean()) {
                            child.visible = child_entry[4].get<bool>();
                        } else if (child_entry[4].is_number_integer()) {
                            child.visible = child_entry[4].get<int>() != 0;
                        }
                    }
                    f.children.push_back(child);
                }
            }
        } else if (entry.is_object()) {
            f.dx = static_cast<float>(entry.value("dx", 0.0));
            f.dy = static_cast<float>(entry.value("dy", 0.0));
            f.resort_z = entry.value("resort_z", false);
            if (entry.contains("children") && entry["children"].is_array()) {
                for (const auto& child_entry : entry["children"]) {
                    if (!child_entry.is_object() && !child_entry.is_array()) continue;
                    ChildFrame child;
                    if (child_entry.is_object()) {
                        child.child_index = child_entry.value("child_index", -1);
                        child.dx = static_cast<float>(child_entry.value("dx", 0.0));
                        child.dy = static_cast<float>(child_entry.value("dy", 0.0));
                        child.degree = static_cast<float>(child_entry.value("degree", 0.0));
                        child.visible = child_entry.value("visible", false);
                    } else if (child_entry.is_array()) {
                        try { child.child_index = child_entry[0].get<int>(); } catch (...) { child.child_index = -1; }
                        if (child_entry.size() > 1 && child_entry[1].is_number()) {
                            child.dx = static_cast<float>(child_entry[1].get<double>());
                        }
                        if (child_entry.size() > 2 && child_entry[2].is_number()) {
                            child.dy = static_cast<float>(child_entry[2].get<double>());
                        }
                        if (child_entry.size() > 3 && child_entry[3].is_number()) {
                            child.degree = static_cast<float>(child_entry[3].get<double>());
                        }
                        if (child_entry.size() > 4) {
                            if (child_entry[4].is_boolean()) {
                                child.visible = child_entry[4].get<bool>();
                            } else if (child_entry[4].is_number_integer()) {
                                child.visible = child_entry[4].get<int>() != 0;
                            }
                        }
                    }
                    f.children.push_back(child);
                }
            }
        }
        frames.push_back(clamp_frame(f));
    }
    if (frames.empty()) frames.push_back(MovementFrame{});
    frames.front().dx = 0.0f; frames.front().dy = 0.0f;
    return frames;
}


