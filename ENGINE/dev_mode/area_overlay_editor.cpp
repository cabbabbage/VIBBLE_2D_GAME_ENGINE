#include "area_overlay_editor.hpp"

#include "DockableCollapsible.hpp"
#include "FloatingPanelLayoutManager.hpp"
#include "widgets.hpp"
#include "draw_utils.hpp"
#include "dm_styles.hpp"

#include "spawn_group_config/SpawnGroupConfig.hpp"

#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "map_generation/room.hpp"
#include "render/camera.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"
#include "util/grid.hpp"
#include "dev_mode/spawn_group_config/spawn_group_utils.hpp"
#include "utils/relative_room_position.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cctype>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

    static bool point_in_poly(int px, int py, const std::vector<SDL_Point>& poly) {
        bool inside = false;
        size_t n = poly.size();
        if (n < 3) return false;
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const int xi = poly[i].x, yi = poly[i].y;
            const int xj = poly[j].x, yj = poly[j].y;
            bool intersect = false;
            if ((yi > py) != (yj > py)) {
                if (yj != yi) {
                    double x_intersect = (xj - xi) * (py - yi) / static_cast<double>(yj - yi) + xi;
                    if (px < x_intersect) {
                        intersect = true;
                    }
                }
            }
            if (intersect) inside = !inside;
        }
        return inside;
    }

    static float compute_reference_screen_height(const Assets* assets, const camera& cam) {
        if (!assets) return 1.0f;
        Asset* player = assets->player;
        if (!player) return 1.0f;

        SDL_Texture* player_final = player->get_final_texture();
        SDL_Texture* player_frame = player->get_current_frame();
        int pw = player->cached_w;
        int ph = player->cached_h;
        if ((pw == 0 || ph == 0) && player_final) {
            SDL_QueryTexture(player_final, nullptr, nullptr, &pw, &ph);
        }
        if ((pw == 0 || ph == 0) && player_frame) {
            SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
        }
        if (pw != 0) player->cached_w = pw;
        if (ph != 0) player->cached_h = ph;

        float scale = cam.get_scale();
        if (scale <= 0.0f) return 1.0f;
        float inv_scale = 1.0f / scale;

        if (ph > 0) {
            float screen_h = static_cast<float>(ph) * inv_scale;
            if (screen_h > 0.0f) return screen_h;
        }
        return 1.0f;
    }

    static bool should_use_room_center_anchor(const std::string& name) {
        auto matches = [](const std::string& value) {
            if (value.empty()) return false;
            std::string lowered = value;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lowered == "trigger" || lowered == "spawning" || lowered == "spawn" ||
                   lowered.find("trigger") != std::string::npos || lowered.find("spawn") != std::string::npos;
};
        return matches(name);
    }

    static std::string trim_copy(std::string value) {
        auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](unsigned char ch) {
            return !is_space(ch);
        }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [&](unsigned char ch) {
            return !is_space(ch);
        }).base(), value.end());
        return value;
    }

    static std::string canonical_area_type(std::string value) {
        value = trim_copy(value);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static bool is_known_area_type(const std::string& value) {
        if (value.empty()) {
            return false;
        }
        static const std::array<const char*, 4> kKnownTypes = {
            "impassable",
            "trigger",
            "child",
            "spawning"
};
        for (const char* known : kKnownTypes) {
            if (value == known) {
                return true;
            }
        }
        return false;
    }

    static double point_to_segment_distance_sq(const SDL_Point& a, const SDL_Point& b, const SDL_Point& p) {
        const double vx = static_cast<double>(b.x - a.x);
        const double vy = static_cast<double>(b.y - a.y);
        const double wx = static_cast<double>(p.x - a.x);
        const double wy = static_cast<double>(p.y - a.y);

        const double c1 = vx * wx + vy * wy;
        if (c1 <= 0.0) {
            const double dx = static_cast<double>(p.x - a.x);
            const double dy = static_cast<double>(p.y - a.y);
            return dx * dx + dy * dy;
        }

        const double c2 = vx * vx + vy * vy;
        if (c2 <= c1) {
            const double dx = static_cast<double>(p.x - b.x);
            const double dy = static_cast<double>(p.y - b.y);
            return dx * dx + dy * dy;
        }

        const double t = (c2 > 0.0) ? (c1 / c2) : 0.0;
        const double proj_x = static_cast<double>(a.x) + t * vx;
        const double proj_y = static_cast<double>(a.y) + t * vy;
        const double dx = static_cast<double>(p.x) - proj_x;
        const double dy = static_cast<double>(p.y) - proj_y;
        return dx * dx + dy * dy;
    }

    static long long orientation(const SDL_Point& a, const SDL_Point& b, const SDL_Point& c) {
        return static_cast<long long>(b.x - a.x) * static_cast<long long>(c.y - a.y) - static_cast<long long>(b.y - a.y) * static_cast<long long>(c.x - a.x);
    }

    static bool on_segment(const SDL_Point& a, const SDL_Point& b, const SDL_Point& c) {
        return std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= c.y && c.y <= std::max(a.y, b.y);
    }

    static bool segments_intersect(const SDL_Point& a1, const SDL_Point& a2,
                                   const SDL_Point& b1, const SDL_Point& b2) {
        const long long o1 = orientation(a1, a2, b1);
        const long long o2 = orientation(a1, a2, b2);
        const long long o3 = orientation(b1, b2, a1);
        const long long o4 = orientation(b1, b2, a2);

        if ((o1 > 0 && o2 < 0 || o1 < 0 && o2 > 0) &&
            (o3 > 0 && o4 < 0 || o3 < 0 && o4 > 0)) {
            return true;
        }

        if (o1 == 0 && on_segment(a1, a2, b1)) return true;
        if (o2 == 0 && on_segment(a1, a2, b2)) return true;
        if (o3 == 0 && on_segment(b1, b2, a1)) return true;
        if (o4 == 0 && on_segment(b1, b2, a2)) return true;

        return false;
    }

    static bool segments_share_vertex(const SDL_Point& a1, const SDL_Point& a2,
                                      const SDL_Point& b1, const SDL_Point& b2) {
        auto equal = [](const SDL_Point& lhs, const SDL_Point& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y;
};
        return equal(a1, b1) || equal(a1, b2) || equal(a2, b1) || equal(a2, b2);
    }

    static int choose_insertion_index(const std::vector<SDL_Point>& points, const SDL_Point& candidate) {
        const std::size_t n = points.size();
        if (n <= 1) {
            return static_cast<int>(n);
        }

        const bool closed = (n >= 3);
        const std::size_t edge_count = closed ? n : (n - 1);
        double best_score = std::numeric_limits<double>::infinity();
        int best_index = -1;

        for (std::size_t i = 0; i < edge_count; ++i) {
            const std::size_t j = closed ? ((i + 1) % n) : (i + 1);
            if (!closed && j >= n) {
                continue;
            }

            const SDL_Point& from = points[i];
            const SDL_Point& to = points[j];

            auto segment_clear = [&](const SDL_Point& s0, const SDL_Point& s1) {
                for (std::size_t k = 0; k < edge_count; ++k) {
                    const std::size_t k_next = closed ? ((k + 1) % n) : (k + 1);
                    if (!closed && k_next >= n) break;
                    if ((k == i && k_next == j) || (k == j && k_next == i)) continue;
                    const SDL_Point& q0 = points[k];
                    const SDL_Point& q1 = points[k_next];
                    if (segments_share_vertex(s0, s1, q0, q1)) continue;
                    if (segments_intersect(s0, s1, q0, q1)) return false;
                }
                return true;
};

            if (!segment_clear(from, candidate)) continue;
            if (!segment_clear(candidate, to)) continue;

            const double distance_sq = point_to_segment_distance_sq(from, to, candidate);
            if (distance_sq < best_score) {
                best_score = distance_sq;
                best_index = static_cast<int>(j);
            }
        }

        if (best_index < 0) {
            return static_cast<int>(n);
        }
        return best_index;
    }
}

constexpr Uint8 kDefaultMaskAlpha = 128;

AreaOverlayEditor::AreaOverlayEditor()
    : mask_alpha_(kDefaultMaskAlpha) {}

AreaOverlayEditor::~AreaOverlayEditor() {
    if (mask_) SDL_FreeSurface(mask_);
    if (mask_tex_) SDL_DestroyTexture(mask_tex_);
    discard_autogen_base();
}

bool AreaOverlayEditor::begin(AssetInfo* info,
                             Asset* asset,
                             const std::string& area_name,
                             const std::string& area_type) {
    if (!assets_ || !info || !asset) return false;

    return begin_at_point(info,
                          SDL_Point{asset->pos.x, asset->pos.y},
                          area_name,
                          asset,
                          area_type);
}

bool AreaOverlayEditor::begin_at_point(AssetInfo* info,
                                       SDL_Point anchor_world,
                                       const std::string& area_name,
                                       Asset* asset,
                                       const std::string& area_type) {
    if (!assets_ || !info) return false;

    const double base_scale = (info && std::isfinite(info->scale_factor) && info->scale_factor >= 0.0f) ? static_cast<double>(info->scale_factor) : 1.0;
    auto scaled_dim = [&](int dimension) {
        if (dimension <= 0) {
            return 0;
        }
        if (base_scale <= 0.0) {
            return 0;
        }
        long long scaled = std::llround(static_cast<double>(dimension) * base_scale);
        if (scaled < 1) scaled = 1;
        if (scaled > static_cast<long long>(std::numeric_limits<int>::max())) {
            return std::numeric_limits<int>::max();
        }
        return static_cast<int>(scaled);
};

    int cw = std::max(32, scaled_dim(info->original_canvas_width));
    int ch = std::max(32, scaled_dim(info->original_canvas_height));
    if (asset) {
        int fw = asset->cached_w;
        int fh = asset->cached_h;
        SDL_Texture* ft = asset->get_final_texture();
        SDL_Texture* fr = asset->get_current_frame();
        if ((fw == 0 || fh == 0) && ft) {
            SDL_QueryTexture(ft, nullptr, nullptr, &fw, &fh);
        }
        if ((fw == 0 || fh == 0) && fr) {
            SDL_QueryTexture(fr, nullptr, nullptr, &fw, &fh);
        }
        const double asset_scale = (asset->info && std::isfinite(asset->info->scale_factor) && asset->info->scale_factor >= 0.0f) ? static_cast<double>(asset->info->scale_factor) : base_scale;
        auto scale_with_asset = [&](int dimension) {
            if (dimension <= 0) return 0;
            if (asset_scale <= 0.0) return 0;
            long long scaled = std::llround(static_cast<double>(dimension) * asset_scale);
            if (scaled < 1) scaled = 1;
            if (scaled > static_cast<long long>(std::numeric_limits<int>::max())) {
                return std::numeric_limits<int>::max();
            }
            return static_cast<int>(scaled);
};
        if (fw > 0) cw = std::max(32, scale_with_asset(fw));
        if (fh > 0) ch = std::max(32, scale_with_asset(fh));
    }

    info_ = info;
    asset_ = asset;
    room_ = nullptr;
    area_name_ = area_name;
    room_area_type_.clear();
    asset_area_type_ = canonical_area_type(area_type);
    scale_area_to_room_ = false;
    canvas_w_ = cw;
    canvas_h_ = ch;
    mask_origin_x_ = 0;
    mask_origin_y_ = 0;
    anchor_world_ = anchor_world;
    has_anchor_ = true;
    flipped_ = (asset_ ? asset_->flipped : false);

    if (mask_) {
        SDL_FreeSurface(mask_);
        mask_ = nullptr;
    }
    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }
    discard_autogen_base();
    pending_mask_generation_ = false;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    name_box_ = std::make_unique<DMTextBox>("Area Name", area_name_);
    resolution_stepper_.reset();

    Area* existing_area = nullptr;
    if (asset && info) {
        existing_area = info->find_area(area_name_);
        if (existing_area) {
            const std::string existing_type = canonical_area_type(existing_area->get_type());
            if (!existing_type.empty()) {
                asset_area_type_ = existing_type;
            }
        }
    }

    area_resolution_ = existing_area ? existing_area->resolution() : 2;
    area_resolution_ = vibble::grid::clamp_resolution(area_resolution_);
    setup_resolution_stepper();

    mask_ = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask_) return false;
    clear_mask();

    if (existing_area) {
        area_resolution_ = vibble::grid::clamp_resolution(existing_area->resolution());
        if (resolution_stepper_) {
            resolution_stepper_->set_value(area_resolution_);
        }
    }
    if (asset_area_type_.empty()) {
        std::string guess = canonical_area_type(area_name_);
        if (!is_known_area_type(asset_area_type_) && is_known_area_type(guess)) {
            asset_area_type_ = guess;
        }
    }

    init_mask_from_existing_area();
    upload_mask();

    ensure_toolbox();
    update_toolbox_title();

    crop_left_slider_ = std::make_unique<DMSlider>("Crop Left", 0, canvas_w_, 0);
    crop_right_slider_ = std::make_unique<DMSlider>("Crop Right", 0, canvas_w_, 0);
    crop_top_slider_ = std::make_unique<DMSlider>("Crop Top", 0, canvas_h_, 0);
    crop_bottom_slider_ = std::make_unique<DMSlider>("Crop Bottom", 0, canvas_h_, 0);
    reset_mask_crop_values();
    set_mode(Mode::Mask);

    active_ = true;

    saved_since_begin_ = false;
    toolbox_autoplace_done_ = false;
    persist_dirty_ = false;
    mask_dirty_ = false;

    return true;
}

bool AreaOverlayEditor::begin_for_room(Room* room, const std::string& area_name) {
    SDL_Point focus{0, 0};
    if (room && room->room_area) {
        focus = room->room_area->get_center();
    }
    return begin_for_room(room, area_name, std::string{}, focus);
}

bool AreaOverlayEditor::begin_for_room(Room* room, const std::string& area_name, const std::string& area_type) {
    SDL_Point focus{0, 0};
    if (room && room->room_area) {
        focus = room->room_area->get_center();
    }
    return begin_for_room(room, area_name, area_type, focus);
}

bool AreaOverlayEditor::begin_for_room(Room* room,
                                       const std::string& area_name,
                                       const std::string& area_type,
                                       SDL_Point focus_world) {
    if (!assets_ || !room) return false;

    info_ = nullptr;
    asset_ = nullptr;
    room_ = room;
    area_name_ = area_name;
    room_area_type_ = area_type;
    asset_area_type_.clear();
    name_box_ = std::make_unique<DMTextBox>("Area Name", area_name_);
    resolution_stepper_.reset();
    scale_area_to_room_ = false;

    SDL_Point room_center = focus_world;
    if (room_->room_area) {
        room_center = room_->room_area->get_center();
    } else {
        room_center = SDL_Point{room_->map_origin.first, room_->map_origin.second};
    }

    Area* existing_area = room_->find_area(area_name_);
    if (room_area_type_.empty() && existing_area) {
        room_area_type_ = existing_area->get_type();
    }

    auto meta_it = std::find_if(room_->areas.begin(), room_->areas.end(), [&](const Room::NamedArea& na) {
        return na.name == area_name_;
    });
    if (meta_it != room_->areas.end()) {
        scale_area_to_room_ = meta_it->scale_to_room;
    }

    area_resolution_ = existing_area ? existing_area->resolution() : 3;
    area_resolution_ = vibble::grid::clamp_resolution(area_resolution_);
    setup_resolution_stepper();
    const bool use_center_anchor = should_use_room_center_anchor(area_name_);

    std::vector<SDL_Point> reference_points;
    if (existing_area) {
        const auto& pts = existing_area->get_points();
        reference_points.insert(reference_points.end(), pts.begin(), pts.end());
    }
    if (reference_points.empty() && room_->room_area) {
        const auto& pts = room_->room_area->get_points();
        reference_points.insert(reference_points.end(), pts.begin(), pts.end());
    }
    if (reference_points.empty()) {
        const int pad = 128;
        reference_points.push_back(SDL_Point{focus_world.x - pad, focus_world.y - pad});
        reference_points.push_back(SDL_Point{focus_world.x + pad, focus_world.y - pad});
        reference_points.push_back(SDL_Point{focus_world.x + pad, focus_world.y + pad});
        reference_points.push_back(SDL_Point{focus_world.x - pad, focus_world.y + pad});
    }
    reference_points.push_back(focus_world);
    if (use_center_anchor) {
        reference_points.push_back(room_center);
    }

    int minx = reference_points.front().x;
    int maxx = reference_points.front().x;
    int miny = reference_points.front().y;
    int maxy = reference_points.front().y;
    for (const auto& p : reference_points) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    const int padding = 32;
    minx -= padding;
    miny -= padding;
    maxx += padding;
    maxy += padding;
    if (minx == maxx) maxx = minx + 32;
    if (miny == maxy) maxy = miny + 32;

    canvas_w_ = std::max(32, maxx - minx);
    canvas_h_ = std::max(32, maxy - miny);
    mask_origin_x_ = minx;
    mask_origin_y_ = miny;
    anchor_world_ = SDL_Point{ (minx + maxx) / 2, (miny + maxy) / 2 };
    if (use_center_anchor) {
        anchor_world_ = room_center;
    }
    has_anchor_ = true;
    flipped_ = false;

    if (mask_) {
        SDL_FreeSurface(mask_);
        mask_ = nullptr;
    }
    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }
    discard_autogen_base();
    pending_mask_generation_ = false;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    crop_left_slider_.reset();
    crop_right_slider_.reset();
    crop_top_slider_.reset();
    crop_bottom_slider_.reset();

    mask_ = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask_) return false;
    clear_mask();
    upload_mask();

    set_mode(Mode::Geometry);
    geometry_points_.clear();
    geometry_dirty_ = false;

    if (existing_area) {
        const auto& pts = existing_area->get_points();
        geometry_points_.insert(geometry_points_.end(), pts.begin(), pts.end());
        rebuild_mask_from_geometry();
        upload_mask();
    }

    ensure_toolbox();
    if (scale_to_room_checkbox_) {
        scale_to_room_checkbox_->set_value(scale_area_to_room_);
    }
    update_toolbox_title();
    rebuild_toolbox_rows();

    active_ = true;
    saved_since_begin_ = false;
    toolbox_autoplace_done_ = false;
    persist_dirty_ = false;
    mask_dirty_ = false;

    return true;
}

void AreaOverlayEditor::cancel() {
    active_ = false;
    pending_mask_generation_ = false;
}

void AreaOverlayEditor::mark_persist_dirty() {
    persist_dirty_ = true;
}

void AreaOverlayEditor::mark_mask_dirty() {
    mask_dirty_ = true;
    mark_persist_dirty();
}

void AreaOverlayEditor::register_tracked_checkbox(DMCheckbox* checkbox) {
    if (!checkbox) return;
    tracked_checkboxes_.push_back(TrackedCheckboxState{checkbox, checkbox->value()});
}

void AreaOverlayEditor::clear_mask() {
    if (!mask_) return;
    SDL_FillRect(mask_, nullptr, SDL_MapRGBA(mask_->format, 255, 0, 0, 0));
}

void AreaOverlayEditor::upload_mask() {
    if (!mask_) return;
    if (mask_tex_) {
        SDL_UpdateTexture(mask_tex_, nullptr, mask_->pixels, mask_->pitch);
    }
}

void AreaOverlayEditor::rebuild_mask_from_geometry() {
    if (!mask_) return;

    SDL_FillRect(mask_, nullptr, SDL_MapRGBA(mask_->format, 255, 0, 0, 0));

    if (geometry_points_.size() < 3) {
        return;
    }

    int minx = geometry_points_[0].x, maxx = geometry_points_[0].x;
    int miny = geometry_points_[0].y, maxy = geometry_points_[0].y;
    for (const auto& p : geometry_points_) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    ensure_mask_contains(minx, miny, 2);
    ensure_mask_contains(maxx, maxy, 2);

    if (SDL_LockSurface(mask_) != 0) {
        return;
    }
    Uint8* pixels = static_cast<Uint8*>(mask_->pixels);
    const int pitch = mask_->pitch;

    int x0 = std::max(0, minx - mask_origin_x_);
    int y0 = std::max(0, miny - mask_origin_y_);
    int x1 = std::min(mask_->w - 1, maxx - mask_origin_x_);
    int y1 = std::min(mask_->h - 1, maxy - mask_origin_y_);

    for (int y = y0; y <= y1; ++y) {
        Uint32* row = reinterpret_cast<Uint32*>(pixels + y * pitch);
        for (int x = x0; x <= x1; ++x) {
            int lx = x + mask_origin_x_;
            int ly = y + mask_origin_y_;
            bool inside = point_in_poly(lx, ly, geometry_points_);
            row[x] = SDL_MapRGBA(mask_->format, 255, 0, 0, inside ? 255 : 0);
        }
    }
    SDL_UnlockSurface(mask_);
}

bool AreaOverlayEditor::compute_overlay_transform(camera& cam, OverlayTransform& out) const {
    if (!mask_ || mask_->w <= 0 || mask_->h <= 0) return false;

    float scale = cam.get_scale();
    if (scale <= 0.0f) return false;
    const float inv_scale = 1.0f / scale;

    const int fw = std::max(1, canvas_w_);
    const int fh = std::max(1, canvas_h_);
    const float base_sw = static_cast<float>(fw) * inv_scale;
    const float base_sh = static_cast<float>(fh) * inv_scale;
    if (base_sw <= 0.0f || base_sh <= 0.0f) return false;

    float reference_screen_height = compute_reference_screen_height(assets_, cam);
    if (reference_screen_height <= 0.0f) reference_screen_height = 1.0f;

    const SDL_Point anchor_world = has_anchor_ ? anchor_world_ : SDL_Point{0, 0};
    const camera::RenderEffects effects = cam.compute_render_effects(anchor_world, base_sh, reference_screen_height);

    const float scaled_sw = base_sw * effects.distance_scale;
    const float scaled_sh = base_sh * effects.distance_scale;
    const float final_visible_h = scaled_sh * effects.vertical_scale;

    const int sw = std::max(1, static_cast<int>(std::lround(scaled_sw)));
    const int sh = std::max(1, static_cast<int>(std::lround(final_visible_h)));

    const float scale_per_canvas_x = static_cast<float>(sw) / static_cast<float>(fw);
    const float scale_per_canvas_y = static_cast<float>(sh) / static_cast<float>(fh);

    const float scale_per_mask_x = scale_per_canvas_x;
    const float scale_per_mask_y = scale_per_canvas_y;

    const int dst_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(mask_->w) * scale_per_mask_x)));
    const int dst_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(mask_->h) * scale_per_mask_y)));

    float pivot_local_fx = static_cast<float>(canvas_w_) * 0.5f - static_cast<float>(mask_origin_x_);
    float pivot_local_fy = static_cast<float>(canvas_h_) - static_cast<float>(mask_origin_y_);
    if (room_) {
        pivot_local_fx = static_cast<float>(anchor_world_.x) - static_cast<float>(mask_origin_x_);
        pivot_local_fy = static_cast<float>(anchor_world_.y) - static_cast<float>(mask_origin_y_);
    }

    const int pivot_screen_x = static_cast<int>(std::lround(pivot_local_fx * scale_per_mask_x));
    const int pivot_screen_y = static_cast<int>(std::lround(pivot_local_fy * scale_per_mask_y));

    const float      anchor_center_x = effects.screen_position.x + effects.parallax_offset_x;
    const SDL_Point  anchor_screen{ static_cast<int>(std::lround(anchor_center_x)), static_cast<int>(std::lround(effects.screen_position.y)) };
    out.anchor_screen = anchor_screen;
    out.dst = SDL_Rect{
        static_cast<int>(std::lround(anchor_center_x)) - pivot_screen_x,
        anchor_screen.y - pivot_screen_y,
        dst_w,
        dst_h
};
    out.pivot_in_dst = SDL_Point{ pivot_screen_x, pivot_screen_y };
    out.scale_x = scale_per_mask_x;
    out.scale_y = scale_per_mask_y;
    return true;
}

SDL_Point AreaOverlayEditor::resolve_anchor_world() const {
    if (asset_) {
        return SDL_Point{ asset_->pos.x, asset_->pos.y };
    }
    if (room_) {
        if (should_use_room_center_anchor(area_name_)) {
            if (room_->room_area) {
                return room_->room_area->get_center();
            }
            return SDL_Point{ room_->map_origin.first, room_->map_origin.second };
        }
        if (Area* existing = room_->find_area(area_name_)) {
            const auto& pts = existing->get_points();
            if (!pts.empty()) {
                SDL_Point anchor = pts.front();
                for (const auto& p : pts) {
                    anchor.x = std::min(anchor.x, p.x);
                    anchor.y = std::min(anchor.y, p.y);
                }
                return anchor;
            }
        }
    }
    return anchor_world_;
}

void AreaOverlayEditor::ensure_mask_contains(int lx, int ly, int radius) {
    if (!mask_) return;
    int sx = lx - mask_origin_x_;
    int sy = ly - mask_origin_y_;
    int min_sx = std::min(sx - radius, 0);
    int min_sy = std::min(sy - radius, 0);
    int max_sx = std::max(sx + radius + 1, mask_->w);
    int max_sy = std::max(sy + radius + 1, mask_->h);
    bool needs_expand = (min_sx < 0) || (min_sy < 0) || (max_sx > mask_->w) || (max_sy > mask_->h);
    if (!needs_expand) return;

    int new_w = max_sx - min_sx;
    int new_h = max_sy - min_sy;
    SDL_Surface* grown = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!grown) return;
    SDL_FillRect(grown, nullptr, SDL_MapRGBA(grown->format, 255, 0, 0, 0));

    int dst_x = -min_sx;
    int dst_y = -min_sy;
    SDL_Rect dst{ dst_x, dst_y, mask_->w, mask_->h };
    SDL_BlitSurface(mask_, nullptr, grown, &dst);

    SDL_FreeSurface(mask_);
    mask_ = grown;

    if (mask_autogen_base_) {
        SDL_Surface* base_grown = SDL_CreateRGBSurfaceWithFormat(0, new_w, new_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (base_grown) {
            SDL_FillRect(base_grown, nullptr, SDL_MapRGBA(base_grown->format, 255, 0, 0, 0));
            SDL_Rect base_dst{ dst_x, dst_y, mask_autogen_base_->w, mask_autogen_base_->h };
            SDL_BlitSurface(mask_autogen_base_, nullptr, base_grown, &base_dst);
            SDL_FreeSurface(mask_autogen_base_);
            mask_autogen_base_ = base_grown;
        }
    }

    mask_origin_x_ += min_sx;
    mask_origin_y_ += min_sy;
    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }
}

void AreaOverlayEditor::setup_resolution_stepper() {
    resolution_stepper_ = std::make_unique<DMNumericStepper>("Area Resolution (r)", 0, vibble::grid::kMaxResolution, area_resolution_);
    resolution_stepper_->set_on_change([this](int new_value) {
        const int clamped = vibble::grid::clamp_resolution(new_value);
        if (clamped != area_resolution_) {
            area_resolution_ = clamped;
            if (resolution_stepper_) {
                resolution_stepper_->set_value(area_resolution_);
            }
            mark_persist_dirty();
            if (persist_current_area()) {
                persist_dirty_ = false;
            }
        } else if (resolution_stepper_) {
            resolution_stepper_->set_value(clamped);
        }
    });
}

void AreaOverlayEditor::init_mask_from_existing_area() {
    if (!mask_) return;
    Area* a = nullptr;
    if (info_) {
        a = info_->find_area(area_name_);
    } else if (room_) {
        a = room_->find_area(area_name_);
    }
    if (!a) return;
    const auto& pts = a->get_points();
    if (pts.size() < 3) return;
    int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (const auto& p : pts) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    ensure_mask_contains(minx, miny, 2);
    ensure_mask_contains(maxx, maxy, 2);

    const int x0 = std::max(0, minx - mask_origin_x_);
    const int y0 = std::max(0, miny - mask_origin_y_);
    const int x1 = std::min(mask_->w - 1, maxx - mask_origin_x_);
    const int y1 = std::min(mask_->h - 1, maxy - mask_origin_y_);

    SDL_LockSurface(mask_);
    Uint8* pixels = static_cast<Uint8*>(mask_->pixels);
    const int pitch = mask_->pitch;

    for (int y = y0; y <= y1; ++y) {
        Uint32* row = reinterpret_cast<Uint32*>(pixels + y * pitch);
        for (int x = x0; x <= x1; ++x) {
            const int lx = x + mask_origin_x_;
            const int ly = y + mask_origin_y_;
            const bool inside = point_in_poly(lx, ly, pts);
            row[x] = SDL_MapRGBA(mask_->format, 255, 0, 0, inside ? 255 : 0);
        }
    }
    SDL_UnlockSurface(mask_);
}

std::vector<SDL_Point> AreaOverlayEditor::extract_edge_points(int step) const {
    std::vector<SDL_Point> out;
    if (!mask_) return out;
    SDL_LockSurface(mask_);
    const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
    const int pitch = mask_->pitch;
    auto getA = [&](int x, int y) -> Uint8 {
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint8 r, g, b, a;
        SDL_GetRGBA(*p, mask_->format, &r, &g, &b, &a);
        return a;
};
    for (int y = 1; y < mask_->h - 1; y += step) {
        for (int x = 1; x < mask_->w - 1; x += step) {
            Uint8 a = getA(x, y);
            if (a == 0) continue;
            if (getA(x - 1, y) == 0 || getA(x + 1, y) == 0 || getA(x, y - 1) == 0 || getA(x, y + 1) == 0) {
                out.push_back(SDL_Point{ x, y });
            }
        }
    }
    SDL_UnlockSurface(mask_);
    return out;
}

void AreaOverlayEditor::ensure_toolbox() {
    if (toolbox_) return;
    toolbox_ = std::make_unique<DockableCollapsible>("Area Tools", true);

    // Clicking the panel 'X' should close the editor and any embedded UI
    toolbox_->set_on_close([this]() {
        if (room_area_spawn_list_) {
            room_area_spawn_list_->close_embedded_search();
        }
        this->cancel();
    });

    toolbox_->set_expanded(true);

    toolbox_->set_floating_content_width(372);
    toolbox_->set_padding(12);
    toolbox_->set_row_gap(10);
    toolbox_->set_col_gap(12);
    btn_mask_  = std::make_unique<DMButton>("Mask",  &DMStyles::CreateButton(), 180, DMButton::height());
    btn_geom_  = std::make_unique<DMButton>("Geometry",  &DMStyles::CreateButton(), 180, DMButton::height());
    btn_delete_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 180, DMButton::height());
    if (!scale_to_room_checkbox_) {
        scale_to_room_checkbox_ = std::make_unique<DMCheckbox>("Scale area to room", scale_area_to_room_);
    }
    update_toolbox_title();
    rebuild_toolbox_rows();
}

void AreaOverlayEditor::update_toolbox_title() {
    if (!toolbox_) return;
    std::string title = "Area Tools";
    if (info_) {
        title += std::string(" — ") + info_->name;
    } else if (room_) {
        title += std::string(" — ") + room_->room_name;
    }
    if (!area_name_.empty()) {
        title += std::string(" — ") + area_name_;
    }
    toolbox_->set_title(title);
}

void AreaOverlayEditor::rebuild_toolbox_rows() {
    if (!toolbox_) return;

    owned_widgets_.clear();
    tracked_checkboxes_.clear();
    DockableCollapsible::Rows rows;

    std::vector<Widget*> first_row;
    if (btn_mask_ && asset_) {
        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_mask_.get(), [this]() {
            discard_autogen_base();
            reset_mask_crop_values();
            pending_mask_generation_ = (asset_ != nullptr);
            geometry_points_.clear();
            geometry_dirty_ = false;
            set_mode(Mode::Mask);
        }));
        first_row.push_back(owned_widgets_.back().get());
    }
    if (btn_geom_) {
        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_geom_.get(), [this]() {
            clear_mask();
            discard_autogen_base();
            geometry_points_.clear();
            geometry_dirty_ = true;
            set_mode(Mode::Geometry);
            upload_mask();
        }));
        first_row.push_back(owned_widgets_.back().get());
    }
    if (!first_row.empty()) rows.push_back(first_row);

    if (name_box_) {
        owned_widgets_.push_back(std::make_unique<TextBoxWidget>(name_box_.get(), true));
        rows.push_back({ owned_widgets_.back().get() });
    }

    if (resolution_stepper_) {
        owned_widgets_.push_back(std::make_unique<StepperWidget>(resolution_stepper_.get()));
        rows.push_back({ owned_widgets_.back().get() });
    }

    if (room_ && scale_to_room_checkbox_) {
        owned_widgets_.push_back(std::make_unique<CheckboxWidget>(scale_to_room_checkbox_.get()));
        register_tracked_checkbox(scale_to_room_checkbox_.get());
        rows.push_back({ owned_widgets_.back().get() });
    }

    if (btn_delete_) {
        std::vector<Widget*> manage_row;
        owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_delete_.get(), [this]() {
            delete_current_area();
        }));
        manage_row.push_back(owned_widgets_.back().get());
        rows.push_back(manage_row);
    }

    if (mode_ == Mode::Mask) {

        if (crop_left_slider_) {
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_left_slider_.get()));
            rows.push_back({ owned_widgets_.back().get() });
        }
        if (crop_right_slider_) {
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_right_slider_.get()));
            rows.push_back({ owned_widgets_.back().get() });
        }
        if (crop_top_slider_) {
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_top_slider_.get()));
            rows.push_back({ owned_widgets_.back().get() });
        }
        if (crop_bottom_slider_) {
            owned_widgets_.push_back(std::make_unique<SliderWidget>(crop_bottom_slider_.get()));
            rows.push_back({ owned_widgets_.back().get() });
        }
    }

    // For room-scoped area editing, append an embedded Spawn Group Config section
    if (room_ && !area_name_.empty()) {
        append_room_area_spawn_rows(rows);
    }

    toolbox_->set_rows(rows);
}

void AreaOverlayEditor::set_mode(Mode mode) {
    mode_ = mode;
    if (mode_ == Mode::Geometry) {
        clear_mask();
        discard_autogen_base();
        geometry_points_.clear();
        geometry_dirty_ = true;
        upload_mask();
    } else {
        geometry_points_.clear();
        geometry_dirty_ = false;
    }
    if (toolbox_) {
        rebuild_toolbox_rows();
    }
}

void AreaOverlayEditor::reset_mask_crop_values() {
    crop_left_px_ = 0;
    crop_right_px_ = 0;
    crop_top_px_ = 0;
    crop_bottom_px_ = 0;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    if (crop_left_slider_)   crop_left_slider_->set_value(0);
    if (crop_right_slider_)  crop_right_slider_->set_value(0);
    if (crop_top_slider_)    crop_top_slider_->set_value(0);
    if (crop_bottom_slider_) crop_bottom_slider_->set_value(0);
}

bool AreaOverlayEditor::rename_current_area(const std::string& desired_name) {
    std::string trimmed = trim_copy(desired_name);
    if (trimmed.empty()) {
        return false;
    }
    if (trimmed == area_name_) {
        if (name_box_) {
            name_box_->set_value(area_name_);
        }
        update_toolbox_title();
        return true;
    }

    auto apply_success = [&](bool persisted) {
        area_name_ = trimmed;
        if (name_box_) {
            name_box_->set_value(area_name_);
        }
        update_toolbox_title();
        if (persisted) {
            saved_since_begin_ = true;
            if (on_saved_callback_) {
                on_saved_callback_();
            }
        }
        return true;
};

    if (info_) {
        auto conflict = std::find_if(info_->areas.begin(), info_->areas.end(), [&](const AssetInfo::NamedArea& na) {
            return na.name == trimmed && na.name != area_name_;
        });
        if (conflict != info_->areas.end()) {
            return false;
        }
        bool had_existing = std::find_if(info_->areas.begin(), info_->areas.end(), [&](const AssetInfo::NamedArea& na) {
            return na.name == area_name_;
        }) != info_->areas.end();
        if (had_existing) {
            if (!info_->rename_area(area_name_, trimmed)) {
                return false;
            }
            (void)info_->commit_manifest();
        }
        return apply_success(had_existing);
    }

    if (room_) {
        auto conflict = std::find_if(room_->areas.begin(), room_->areas.end(), [&](const Room::NamedArea& na) {
            return na.name == trimmed && na.name != area_name_;
        });
        if (conflict != room_->areas.end()) {
            return false;
        }
        bool had_existing = std::find_if(room_->areas.begin(), room_->areas.end(), [&](const Room::NamedArea& na) {
            return na.name == area_name_;
        }) != room_->areas.end();
        if (had_existing) {
            if (!room_->rename_area(area_name_, trimmed)) {
                return false;
            }
            room_->save_assets_json();
        }
        return apply_success(had_existing);
    }

    return apply_success(false);
}

void AreaOverlayEditor::delete_current_area() {
    bool removed = false;
    if (info_) {
        removed = info_->remove_area(area_name_);
        if (removed) {
            (void)info_->commit_manifest();
        }
    } else if (room_) {
        removed = room_->remove_area(area_name_);
        if (removed) {
            room_->save_assets_json();
        }
    }

    if (removed) {
        saved_since_begin_ = true;
        if (on_saved_callback_) {
            on_saved_callback_();
        }
    }

    cancel();
}

void AreaOverlayEditor::discard_autogen_base() {
    if (mask_autogen_base_) {
        SDL_FreeSurface(mask_autogen_base_);
        mask_autogen_base_ = nullptr;
    }
}

bool AreaOverlayEditor::generate_mask_from_asset(SDL_Renderer* renderer) {
    if (!renderer || !asset_ || canvas_w_ <= 0 || canvas_h_ <= 0) return false;

    SDL_Texture* source = asset_->get_final_texture();
    if (!source) source = asset_->get_current_frame();
    if (!source) return false;

    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(source, nullptr, nullptr, &tex_w, &tex_h) != 0) {
        std::cerr << "[AreaOverlayEditor] SDL_QueryTexture failed: " << SDL_GetError() << "\n";
        return false;
    }
    if (tex_w <= 0 || tex_h <= 0) {
        std::cerr << "[AreaOverlayEditor] Source texture has invalid dimensions" << "\n";
        return false;
    }

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_Texture* staging = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
    if (!staging) {
        std::cerr << "[AreaOverlayEditor] Failed to create staging texture: " << SDL_GetError() << "\n";
        return false;
    }

    Uint8 prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
    SDL_GetTextureColorMod(source, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureAlphaMod(source, &prev_a);
    SDL_SetTextureColorMod(source, 255, 255, 255);
    SDL_SetTextureAlphaMod(source, 255);

    SDL_SetRenderTarget(renderer, staging);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_RenderCopyEx(renderer, source, nullptr, nullptr, 0.0, nullptr, SDL_FLIP_NONE);

    SDL_Surface* captured = SDL_CreateRGBSurfaceWithFormat(0, tex_w, tex_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!captured) {
        SDL_SetRenderTarget(renderer, prev_target);
        SDL_DestroyTexture(staging);
        SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(source, prev_a);
        std::cerr << "[AreaOverlayEditor] Failed to create capture surface: " << SDL_GetError() << "\n";
        return false;
    }
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, captured->pixels, captured->pitch) != 0) {
        SDL_FreeSurface(captured);
        SDL_SetRenderTarget(renderer, prev_target);
        SDL_DestroyTexture(staging);
        SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
        SDL_SetTextureAlphaMod(source, prev_a);
        std::cerr << "[AreaOverlayEditor] SDL_RenderReadPixels failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_SetRenderTarget(renderer, prev_target);
    SDL_DestroyTexture(staging);
    SDL_SetTextureColorMod(source, prev_r, prev_g, prev_b);
    SDL_SetTextureAlphaMod(source, prev_a);

    SDL_Surface* base = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Surface* working = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    if (!base || !working) {
        if (base) SDL_FreeSurface(base);
        if (working) SDL_FreeSurface(working);
        SDL_FreeSurface(captured);
        std::cerr << "[AreaOverlayEditor] Failed to allocate mask surfaces: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_FillRect(base, nullptr, SDL_MapRGBA(base->format, 255, 0, 0, 0));
    SDL_FillRect(working, nullptr, SDL_MapRGBA(working->format, 255, 0, 0, 0));

    if (SDL_LockSurface(captured) != 0) {
        SDL_FreeSurface(base);
        SDL_FreeSurface(working);
        SDL_FreeSurface(captured);
        std::cerr << "[AreaOverlayEditor] Failed to lock capture surface: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_LockSurface(base);
    SDL_LockSurface(working);

    const int src_w = captured->w;
    const int src_h = captured->h;
    const int dst_w = working->w;
    const int dst_h = working->h;

    for (int y = 0; y < dst_h; ++y) {
        int sy = src_h > 0 ? std::clamp((y * src_h) / std::max(1, dst_h), 0, src_h - 1) : 0;
        const Uint8* src_row_bytes = static_cast<const Uint8*>(captured->pixels) + sy * captured->pitch;
        const Uint32* src_row = reinterpret_cast<const Uint32*>(src_row_bytes);
        Uint32* base_row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(base->pixels) + y * base->pitch);
        Uint32* working_row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(working->pixels) + y * working->pitch);
        for (int x = 0; x < dst_w; ++x) {
            int sx = src_w > 0 ? std::clamp((x * src_w) / std::max(1, dst_w), 0, src_w - 1) : 0;
            Uint32 pixel = src_row[sx];
            Uint8 r, g, b, a;
            SDL_GetRGBA(pixel, captured->format, &r, &g, &b, &a);
            Uint32 value = SDL_MapRGBA(base->format, 255, 0, 0, a > 0 ? 255 : 0);
            base_row[x] = value;
            working_row[x] = value;
        }
    }

    SDL_UnlockSurface(working);
    SDL_UnlockSurface(base);
    SDL_UnlockSurface(captured);
    SDL_FreeSurface(captured);

    if (mask_) SDL_FreeSurface(mask_);
    mask_ = working;
    mask_origin_x_ = 0;
    mask_origin_y_ = 0;

    if (mask_tex_) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = nullptr;
    }

    discard_autogen_base();
    mask_autogen_base_ = base;
    applied_crop_left_ = applied_crop_right_ = applied_crop_top_ = applied_crop_bottom_ = -1;

    upload_mask();
    mark_mask_dirty();
    return true;
}

void AreaOverlayEditor::apply_mask_crop() {
    if (!mask_ || !mask_autogen_base_) return;
    if (mask_->w != mask_autogen_base_->w || mask_->h != mask_autogen_base_->h) return;

    SDL_BlitSurface(mask_autogen_base_, nullptr, mask_, nullptr);

    const int width = mask_->w;
    const int height = mask_->h;

    int left = std::clamp(crop_left_px_, 0, width);
    int right = std::clamp(crop_right_px_, 0, width);
    int top = std::clamp(crop_top_px_, 0, height);
    int bottom = std::clamp(crop_bottom_px_, 0, height);

    if (left + right >= width || top + bottom >= height) {
        SDL_FillRect(mask_, nullptr, SDL_MapRGBA(mask_->format, 255, 0, 0, 0));
        applied_crop_left_ = left;
        applied_crop_right_ = right;
        applied_crop_top_ = top;
        applied_crop_bottom_ = bottom;
        upload_mask();
        mark_mask_dirty();
        return;
    }

    Uint32 clear = SDL_MapRGBA(mask_->format, 255, 0, 0, 0);

    if (left > 0) {
        SDL_Rect rect{0, 0, left, height};
        SDL_FillRect(mask_, &rect, clear);
    }
    if (right > 0) {
        SDL_Rect rect{width - right, 0, right, height};
        SDL_FillRect(mask_, &rect, clear);
    }
    if (top > 0) {
        SDL_Rect rect{0, 0, width, top};
        SDL_FillRect(mask_, &rect, clear);
    }
    if (bottom > 0) {
        SDL_Rect rect{0, height - bottom, width, bottom};
        SDL_FillRect(mask_, &rect, clear);
    }

    applied_crop_left_ = left;
    applied_crop_right_ = right;
    applied_crop_top_ = top;
    applied_crop_bottom_ = bottom;

    upload_mask();
    mark_mask_dirty();
}

void AreaOverlayEditor::position_toolbox_near_anchor(int screen_w, int screen_h) {
    if (!toolbox_ || !assets_ || !has_anchor_) return;
    SDL_FPoint ap_f = assets_->getView().map_to_screen(anchor_world_);
    SDL_Point ap{static_cast<int>(std::lround(ap_f.x)), static_cast<int>(std::lround(ap_f.y))};
    toolbox_->set_work_area(SDL_Rect{0, 0, screen_w, screen_h});

    SDL_Rect rect = toolbox_->rect();
    constexpr int kFallbackHeight = 320;

    FloatingPanelLayoutManager::PanelInfo info;
    info.panel = toolbox_.get();
    info.force_layout = true;
    info.preferred_width = rect.w > 0 ? rect.w : DockableCollapsible::kDefaultFloatingContentWidth;
    int resolved_height = rect.h > 0 ? rect.h : toolbox_->height();
    if (resolved_height <= 0) {
        resolved_height = kFallbackHeight;
    }
    info.preferred_height = resolved_height;

    FloatingPanelLayoutManager::SlidingParentInfo parent;
    parent.bounds = SDL_Rect{ap.x, ap.y, 1, 1};
    parent.padding = 16;
    parent.anchor_left = true;
    parent.align_top = true;

    SDL_Point pos = FloatingPanelLayoutManager::instance().positionFor(info, &parent);
    toolbox_->set_position_from_layout_manager(pos.x, pos.y);
}

void AreaOverlayEditor::update(const Input& input, int screen_w, int screen_h) {
    if (!active_ || !assets_) return;

    if (has_anchor_) {
        SDL_Point resolved = resolve_anchor_world();
        if (resolved.x != anchor_world_.x || resolved.y != anchor_world_.y) {
            anchor_world_ = resolved;
        }
    }

    ensure_toolbox();
    if (!toolbox_autoplace_done_) {
        position_toolbox_near_anchor(screen_w, screen_h);
        toolbox_autoplace_done_ = true;
    } else if (toolbox_) {
        toolbox_->set_work_area(SDL_Rect{0, 0, screen_w, screen_h});
    }
    if (toolbox_) toolbox_->update(input, screen_w, screen_h);

    if (room_area_spawn_list_) {
        room_area_spawn_list_->set_screen_dimensions(screen_w, screen_h);
        // Anchor near toolbox for any floating sub-panels (e.g., search)
        SDL_Point anchor{0, 0};
        if (toolbox_) {
            SDL_Rect r = toolbox_->rect();
            anchor.x = std::max(16, r.x - 320);
            anchor.y = std::max(16, r.y + r.h / 4);
        }
        room_area_spawn_list_->set_anchor(anchor.x, anchor.y);
        room_area_spawn_list_->update(input, screen_w, screen_h);
    }

    if (mode_ == Mode::Mask && mask_autogen_base_) {
        int left = crop_left_slider_ ? crop_left_slider_->value() : 0;
        int right = crop_right_slider_ ? crop_right_slider_->value() : 0;
        int top = crop_top_slider_ ? crop_top_slider_->value() : 0;
        int bottom = crop_bottom_slider_ ? crop_bottom_slider_->value() : 0;
        if (left != applied_crop_left_ || right != applied_crop_right_ || top != applied_crop_top_ || bottom != applied_crop_bottom_) {
            crop_left_px_ = left;
            crop_right_px_ = right;
            crop_top_px_ = top;
            crop_bottom_px_ = bottom;
            apply_mask_crop();
            mark_mask_dirty();
        }
    }

    if (name_box_ && !name_box_->is_editing()) {
        std::string trimmed = trim_copy(name_box_->value());
        if (trimmed.empty()) {
            if (!area_name_.empty()) {
                mark_persist_dirty();
            }
        } else if (trimmed != area_name_) {
            mark_persist_dirty();
        }
    }

    for (auto& tracked : tracked_checkboxes_) {
        if (!tracked.checkbox) continue;
        bool value = tracked.checkbox->value();
        if (value != tracked.last_value) {
            tracked.last_value = value;
            mark_persist_dirty();
        }
    }

    if (persist_dirty_) {
        if (persist_current_area()) {
            persist_dirty_ = false;
        }
    }
}

bool AreaOverlayEditor::handle_event(const SDL_Event& e) {
    if (!active_) return false;
    if (toolbox_ && toolbox_->handle_event(e)) return true;
    if (room_area_spawn_list_ && room_area_spawn_list_->handle_event(e)) return true;

    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_ESCAPE) {
            cancel();
            return true;
        }
    }

    if (mode_ == Mode::Geometry && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (!assets_ || !mask_ || !has_anchor_) return false;

        anchor_world_ = resolve_anchor_world();

        camera& cam = assets_->getView();
        OverlayTransform tx{};
        if (!compute_overlay_transform(cam, tx)) return false;

        const int mx = e.button.x;
        const int my = e.button.y;

        const float safe_scale_x = std::max(tx.scale_x, 1e-6f);
        const float safe_scale_y = std::max(tx.scale_y, 1e-6f);

        float local_fx = (static_cast<float>(mx) - static_cast<float>(tx.dst.x)) / safe_scale_x;
        float local_fy = (static_cast<float>(my) - static_cast<float>(tx.dst.y)) / safe_scale_y;

        int sx = static_cast<int>(std::lround(local_fx));
        int sy = static_cast<int>(std::lround(local_fy));
        int lx = sx + mask_origin_x_;
        int ly = sy + mask_origin_y_;

        int hit_index = -1;
        const int threshold = 8;
        const int threshold2 = threshold * threshold;
        for (int i = 0; i < static_cast<int>(geometry_points_.size()); ++i) {
            const float local_px = static_cast<float>(geometry_points_[i].x - mask_origin_x_);
            const float local_py = static_cast<float>(geometry_points_[i].y - mask_origin_y_);
            const float screen_px = static_cast<float>(tx.dst.x) + local_px * tx.scale_x;
            const float screen_py = static_cast<float>(tx.dst.y) + local_py * tx.scale_y;
            const int dxp = static_cast<int>(std::lround(screen_px)) - mx;
            const int dyp = static_cast<int>(std::lround(screen_py)) - my;
            if (dxp * dxp + dyp * dyp <= threshold2) {
                hit_index = i;
                break;
            }
        }

        if (hit_index >= 0) {
            geometry_points_.erase(geometry_points_.begin() + hit_index);
        } else {
            ensure_mask_contains(lx, ly, 2);
            SDL_Point new_point{ lx, ly };
            auto exists = std::find_if(
                geometry_points_.begin(),
                geometry_points_.end(),
                [&](const SDL_Point& p) { return p.x == new_point.x && p.y == new_point.y; });
            if (exists == geometry_points_.end()) {
                const int insert_index = choose_insertion_index(geometry_points_, new_point);
                if (insert_index >= 0 && insert_index <= static_cast<int>(geometry_points_.size())) {
                    geometry_points_.insert(geometry_points_.begin() + insert_index, new_point);
                } else {
                    geometry_points_.push_back(new_point);
                }
            }
        }
        geometry_dirty_ = true;
        mark_persist_dirty();
        rebuild_mask_from_geometry();
        upload_mask();
        return true;
    }
    return false;
}

void AreaOverlayEditor::render(SDL_Renderer* r) {
    if (!active_) return;

    if (assets_ && mask_ && r) {
        do {
            if (pending_mask_generation_) {
                if (!generate_mask_from_asset(r)) {
                    std::cerr << "[AreaOverlayEditor] Failed to generate mask from asset for mask tool" << "";

                } else if (mode_ == Mode::Mask) {
                    apply_mask_crop();
                }
                pending_mask_generation_ = false;
            }

            camera& cam = assets_->getView();
            OverlayTransform tx{};
            if (!compute_overlay_transform(cam, tx)) break;

            SDL_Rect dst = tx.dst;

            if (!mask_tex_) {
                mask_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, mask_->w, mask_->h);
                if (!mask_tex_) break;
                upload_mask();
            }
            int tex_w = 0, tex_h = 0;
            SDL_QueryTexture(mask_tex_, nullptr, nullptr, &tex_w, &tex_h);
            if (tex_w != mask_->w || tex_h != mask_->h) {
                SDL_DestroyTexture(mask_tex_);
                mask_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, mask_->w, mask_->h);
                if (!mask_tex_) break;
                upload_mask();
            }

            SDL_SetTextureBlendMode(mask_tex_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(mask_tex_, mask_alpha_);
            SDL_Point center = tx.pivot_in_dst;
            SDL_RenderCopyEx(r, mask_tex_, nullptr, &dst, 0.0, &center, flipped_ ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

            if (mode_ == Mode::Geometry && !geometry_points_.empty()) {
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, 0, 255, 0, 220);
                const float scale_x = tx.scale_x;
                const float scale_y = tx.scale_y;
                const float dst_x = static_cast<float>(dst.x);
                const float dst_y = static_cast<float>(dst.y);

                for (size_t i = 1; i < geometry_points_.size(); ++i) {
                    const float local_px0 = static_cast<float>(geometry_points_[i-1].x - mask_origin_x_);
                    const float local_py0 = static_cast<float>(geometry_points_[i-1].y - mask_origin_y_);
                    const float local_px1 = static_cast<float>(geometry_points_[i].x - mask_origin_x_);
                    const float local_py1 = static_cast<float>(geometry_points_[i].y - mask_origin_y_);
                    const float sx0 = dst_x + local_px0 * scale_x;
                    const float sy0 = dst_y + local_py0 * scale_y;
                    const float sx1 = dst_x + local_px1 * scale_x;
                    const float sy1 = dst_y + local_py1 * scale_y;
                    SDL_RenderDrawLine(r, static_cast<int>(std::lround(sx0)), static_cast<int>(std::lround(sy0)), static_cast<int>(std::lround(sx1)), static_cast<int>(std::lround(sy1)));
                }
                for (size_t i = 0; i < geometry_points_.size(); ++i) {
                    const float local_px = static_cast<float>(geometry_points_[i].x - mask_origin_x_);
                    const float local_py = static_cast<float>(geometry_points_[i].y - mask_origin_y_);
                    const float sx = dst_x + local_px * scale_x;
                    const float sy = dst_y + local_py * scale_y;
                    const int cx = static_cast<int>(std::lround(sx));
                    const int cy = static_cast<int>(std::lround(sy));
                    SDL_Rect mr{ cx - 3, cy - 3, 6, 6 };
                    const SDL_Color fill{255, 255, 0, 240};
                    const SDL_Color outline{0, 0, 0, 255};
                    const int radius = std::min(DMStyles::CornerRadius(), std::min(mr.w, mr.h) / 2);
                    const int bevel = std::min(DMStyles::BevelDepth(), std::max(0, std::min(mr.w, mr.h) / 2));
                    dm_draw::DrawBeveledRect( r, mr, radius, bevel, fill, fill, fill, false, 0.0f, 0.0f);
                    dm_draw::DrawRoundedOutline( r, mr, radius, 1, outline);
                }
            }

        } while (false);
    }

    if (toolbox_) toolbox_->render(r);
    if (room_area_spawn_list_) room_area_spawn_list_->render(r);
}

std::vector<SDL_Point> AreaOverlayEditor::trace_polygon_from_mask() const {
    std::vector<SDL_Point> poly;
    if (!mask_) return poly;
    SDL_LockSurface(mask_);
    auto getA = [&](int x, int y) -> Uint8 {
        if (x < 0 || y < 0 || x >= mask_->w || y >= mask_->h) return 0;
        const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
        const int pitch = mask_->pitch;
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint8 r, g, b, a;
        (void)r; (void)g; (void)b;
        SDL_GetRGBA(*p, mask_->format, &r, &g, &b, &a);
        return a;
};
    int sx = -1, sy = -1;
    for (int y = 0; y < mask_->h && sy < 0; ++y) {
        for (int x = 0; x < mask_->w; ++x) {
            if (getA(x, y) == 0) continue;
            if (getA(x - 1, y) == 0 || getA(x + 1, y) == 0 || getA(x, y - 1) == 0 || getA(x, y + 1) == 0) {
                sx = x;
                sy = y;
                break;
            }
        }
    }
    if (sy < 0) {
        SDL_UnlockSurface(mask_);
        return poly;
    }

    static const int ndx[8] = { 1, 1, 0,-1,-1,-1, 0, 1 };
    static const int ndy[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
    auto neighbor_index = [&](int cx, int cy, int nx, int ny) -> int {
        for (int i = 0; i < 8; ++i) if (cx + ndx[i] == nx && cy + ndy[i] == ny) return i;
        return 0;
};
    constexpr int POLYGON_TRACE_GUARD_LIMIT = 200000;
    int px = sx - 1, py = sy;
    int cx = sx, cy = sy;
    int first_nx = -1, first_ny = -1;
    int guard = 0;
    while (guard++ < POLYGON_TRACE_GUARD_LIMIT) {
        poly.push_back(SDL_Point{ cx, cy });
        int bi = neighbor_index(cx, cy, px, py);
        int i = (bi + 1) & 7;
        bool found = false;
        for (int k = 0; k < 8; ++k) {
            int nx = cx + ndx[i];
            int ny = cy + ndy[i];
            if (getA(nx, ny) > 0) {
                px = cx;
                py = cy;
                cx = nx;
                cy = ny;
                if (first_nx < 0) {
                    first_nx = cx;
                    first_ny = cy;
                }
                found = true;
                break;
            }
            i = (i + 1) & 7;
        }
        if (!found) break;
        if ((cx == sx && cy == sy) && (poly.size() > 1 && first_nx == poly[1].x && first_ny == poly[1].y)) {
            break;
        }
    }

    std::vector<SDL_Point> out;
    out.reserve(poly.size());
    SDL_Point last{INT_MIN, INT_MIN};
    for (const auto& p : poly) {
        if (p.x != last.x || p.y != last.y) out.push_back(p);
        last = p;
    }
    SDL_UnlockSurface(mask_);
    return out;
}

bool AreaOverlayEditor::persist_current_area() {
    bool handled = false;

    if (name_box_ && !name_box_->is_editing()) {
        const std::string desired_name = name_box_->value();
        std::string trimmed = trim_copy(desired_name);
        if (trimmed.empty()) {
            if (!area_name_.empty()) {
                name_box_->set_value(area_name_);
                handled = true;
            }
        } else if (trimmed != area_name_) {
            if (!rename_current_area(desired_name)) {
                name_box_->set_value(area_name_);
            }
            handled = true;
        }
    }

    if (!info_ && !room_) {
        return handled;
    }

    int resolution_value = vibble::grid::clamp_resolution(area_resolution_);
    if (resolution_stepper_) {
        resolution_stepper_->set_value(resolution_value);
    }
    area_resolution_ = resolution_value;
    handled = true;

    auto remove_current = [this]() -> bool {
        if (info_) {
            bool removed = info_->remove_area(area_name_);
            if (removed) {
                (void)info_->commit_manifest();
            }
            return removed;
        }
        if (room_) {
            bool removed = room_->remove_area(area_name_);
            if (removed) {
                room_->save_assets_json();
            }
            return removed;
        }
        return false;
    };

    // Room area 'type' no longer used; do not assign or infer types for room areas

    auto notify_saved = [this]() {
        saved_since_begin_ = true;
        if (on_saved_callback_) {
            on_saved_callback_();
        }
    };

    auto upsert_area = [this](Area& area) {
        if (info_) {
            std::string final_type = canonical_area_type(asset_area_type_);
            if (final_type.empty()) {
                if (Area* existing = info_->find_area(area_name_)) {
                    final_type = canonical_area_type(existing->get_type());
                }
            }
            if (final_type.empty()) {
                std::string guess = canonical_area_type(area_name_);
                if (is_known_area_type(guess)) {
                    final_type = guess;
                }
            }
            if (!final_type.empty()) {
                area.set_type(final_type);
            }
            asset_area_type_ = final_type;
            AssetInfo::NamedArea::RenderFrame frame;
            frame.width = std::max(0, canvas_w_);
            frame.height = std::max(0, canvas_h_);
            frame.pivot_x = (frame.width > 0) ? frame.width / 2 : 0;
            frame.pivot_y = frame.height;
            float scale = info_->scale_factor;
            if (!(scale > 0.0f) || !std::isfinite(scale)) {
                scale = 1.0f;
            }
            frame.pixel_scale = scale;
            info_->upsert_area_from_editor(area, frame);
            (void)info_->commit_manifest();
        } else if (room_) {
            // Do not set type for room areas
            if (scale_to_room_checkbox_) {
                scale_area_to_room_ = scale_to_room_checkbox_->value();
            } else {
                scale_area_to_room_ = false;
            }
            room_->upsert_named_area(area, scale_area_to_room_, 0, 0);
            room_->save_assets_json();

            // Ensure a spawn group entry exists for this room area with Exact positioning
            try {
                nlohmann::json& root = room_->assets_data();
                auto& groups = devmode::spawn::ensure_spawn_groups_array(root);

                // Compute current room dimensions and center
                int width = 0;
                int height = 0;
                if (room_->room_area) {
                    auto b = room_->room_area->get_bounds();
                    width = std::max(1, std::get<2>(b) - std::get<0>(b));
                    height = std::max(1, std::get<3>(b) - std::get<1>(b));
                }

                // Find existing entry by linked_area name
                nlohmann::json* existing = nullptr;
                for (auto& entry : groups) {
                    if (!entry.is_object()) continue;
                    const bool linked = entry.value("link_to_area", false);
                    const std::string linked_area = entry.value("linked_area", std::string{});
                    const std::string display = entry.value("display_name", std::string{});
                    if ((linked && linked_area == area_name_) || (!linked && display == area_name_)) {
                        existing = &entry;
                        break;
                    }
                }

                const int default_resolution = room_->map_grid_settings().resolution;
                if (!existing) {
                    nlohmann::json entry = nlohmann::json::object();
                    entry["display_name"] = area_name_;
                    entry["position"] = "Exact";
                    entry["dx"] = 0;
                    entry["dy"] = 0;
                    if (width > 0) entry["origional_width"] = width;
                    if (height > 0) entry["origional_height"] = height;
                    entry["link_to_area"] = true;
                    entry["linked_area"] = area_name_;
                    devmode::spawn::ensure_spawn_group_entry_defaults(entry, area_name_, default_resolution);
                    groups.push_back(std::move(entry));
                    room_->save_assets_json();
                } else {
                    // Ensure defaults are present and method is Exact for area anchor
                    devmode::spawn::ensure_spawn_group_entry_defaults(*existing, area_name_, default_resolution);
                    if (existing->value("position", std::string{"Random"}) != std::string{"Exact"}) {
                        (*existing)["position"] = "Exact";
                    }
                    if (width > 0 && !existing->contains("origional_width")) (*existing)["origional_width"] = width;
                    if (height > 0 && !existing->contains("origional_height")) (*existing)["origional_height"] = height;
                    if (!existing->value("link_to_area", false)) {
                        (*existing)["link_to_area"] = true;
                    }
                    if (existing->value("linked_area", std::string{}) != area_name_) {
                        (*existing)["linked_area"] = area_name_;
                    }
                    room_->save_assets_json();
                }
            } catch (...) {
                // Non-fatal; ignore spawn group creation errors
            }
        }
    };

    if (mode_ == Mode::Geometry) {
        if (geometry_points_.size() < 3) {
            if (remove_current()) {
                notify_saved();
            }
            handled = true;
            geometry_dirty_ = false;
            mask_dirty_ = false;
            return handled;
        }

        std::vector<Area::Point> area_points;
        area_points.reserve(geometry_points_.size());
        for (const auto& p : geometry_points_) {
            area_points.push_back(SDL_Point{ p.x, p.y });
        }
        if (area_points.size() >= 2) {
            const auto& first = area_points.front();
            const auto& last = area_points.back();
            if (first.x == last.x && first.y == last.y) {
                area_points.pop_back();
            }
        }
        if (area_points.size() < 3) {
            if (remove_current()) {
                notify_saved();
            }
            handled = true;
            geometry_dirty_ = false;
            mask_dirty_ = false;
            return handled;
        }
        Area area(area_name_, area_points, resolution_value);
        area.set_resolution(resolution_value);
        upsert_area(area);
        notify_saved();
        geometry_dirty_ = false;
        mask_dirty_ = false;
        return true;
    }

    if (!mask_) {
        if (remove_current()) {
            notify_saved();
        }
        handled = true;
        mask_dirty_ = false;
        geometry_dirty_ = false;
        return handled;
    }

    bool has_alpha = false;
    int min_sx = mask_->w;
    int min_sy = mask_->h;
    int max_sx = -1;
    int max_sy = -1;

    if (SDL_LockSurface(mask_) == 0) {
        const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
        const int pitch = mask_->pitch;
        for (int y = 0; y < mask_->h; ++y) {
            const Uint32* row = reinterpret_cast<const Uint32*>(pixels + y * pitch);
            for (int x = 0; x < mask_->w; ++x) {
                Uint8 r, g, b, a;
                SDL_GetRGBA(row[x], mask_->format, &r, &g, &b, &a);
                if (a > 0) {
                    has_alpha = true;
                    min_sx = std::min(min_sx, x);
                    min_sy = std::min(min_sy, y);
                    max_sx = std::max(max_sx, x);
                    max_sy = std::max(max_sy, y);
                }
            }
        }
        SDL_UnlockSurface(mask_);
    }

    if (!has_alpha) {
        if (remove_current()) {
            notify_saved();
        }
        handled = true;
        mask_dirty_ = false;
        geometry_dirty_ = false;
        return handled;
    }

    auto polygon = trace_polygon_from_mask();
    if (polygon.size() < 3) {
        int rect_max_sx = max_sx;
        int rect_max_sy = max_sy;
        if (min_sx == rect_max_sx) rect_max_sx = min_sx + 1;
        if (min_sy == rect_max_sy) rect_max_sy = min_sy + 1;
        polygon.clear();
        polygon.push_back(SDL_Point{ min_sx, min_sy });
        polygon.push_back(SDL_Point{ rect_max_sx, min_sy });
        polygon.push_back(SDL_Point{ rect_max_sx, rect_max_sy });
        polygon.push_back(SDL_Point{ min_sx, rect_max_sy });
    }

    std::vector<Area::Point> area_points;
    area_points.reserve(polygon.size());
    for (const auto& p : polygon) {
        area_points.push_back(SDL_Point{ p.x + mask_origin_x_, p.y + mask_origin_y_ });
    }

    if (area_points.size() >= 2) {
        const auto& first = area_points.front();
        const auto& last = area_points.back();
        if (first.x == last.x && first.y == last.y) {
            area_points.pop_back();
        }
    }

    if (area_points.size() < 3) {
        int min_lx = min_sx + mask_origin_x_;
        int min_ly = min_sy + mask_origin_y_;
        int max_lx = max_sx + mask_origin_x_;
        int max_ly = max_sy + mask_origin_y_;
        if (min_lx == max_lx) max_lx = min_lx + 1;
        if (min_ly == max_ly) max_ly = min_ly + 1;
        area_points.clear();
        area_points.push_back(SDL_Point{ min_lx, min_ly });
        area_points.push_back(SDL_Point{ max_lx, min_ly });
        area_points.push_back(SDL_Point{ max_lx, max_ly });
        area_points.push_back(SDL_Point{ min_lx, max_ly });
    }

    if (area_points.size() < 3) {
        handled = true;
        mask_dirty_ = false;
        geometry_dirty_ = false;
        return handled;
    }

    Area area(area_name_, area_points, resolution_value);
    area.set_resolution(resolution_value);
    upsert_area(area);
    notify_saved();
    mask_dirty_ = false;
    geometry_dirty_ = false;
    return true;
}

void AreaOverlayEditor::append_room_area_spawn_rows(DockableCollapsible::Rows& rows) {
    // Build or refresh an embedded SpawnGroupConfig for the current room area
    if (!room_ || area_name_.empty()) {
        return;
    }

    if (!room_area_spawn_list_) room_area_spawn_list_ = std::make_unique<SpawnGroupConfig>(false);
    if (!room_area_spawn_list_) return;

    room_area_spawn_list_->set_embedded_mode(true);

    // Locate or create the spawn_groups array for this area within room assets JSON
    nlohmann::json* groups_ptr = nullptr;
    nlohmann::json& root = room_->assets_data();
    if (root.contains("areas") && root["areas"].is_array()) {
        for (auto& entry : root["areas"]) {
            if (!entry.is_object()) continue;
            if (entry.value("name", std::string{}) != area_name_) continue;
            if (!entry.contains("spawn_groups") || !entry["spawn_groups"].is_array()) {
                entry["spawn_groups"] = nlohmann::json::array();
            }
            groups_ptr = &entry["spawn_groups"];
            break;
        }
    }

    if (!groups_ptr) {
        return;
    }

    auto on_change = [this]() {
        if (room_) room_->save_assets_json();
    };
    auto on_entry_change = [this](const nlohmann::json&, const SpawnGroupConfig::ChangeSummary&) {
        if (room_) room_->save_assets_json();
    };

    SpawnGroupConfig::Callbacks cb{};
    cb.on_add = [this, groups_ptr]() {
        if (!groups_ptr) return;
        if (!groups_ptr->is_array()) return;
        nlohmann::json entry = nlohmann::json::object();
        entry["position"] = "Random";
        entry["min_number"] = 1;
        entry["max_number"] = 1;
        entry["candidates"] = nlohmann::json::array({ nlohmann::json::object({{"name","null"},{"chance",100}}) });
        groups_ptr->push_back(std::move(entry));
        if (room_) room_->save_assets_json();
        room_area_spawn_list_->refresh_row_configuration();
    };
    cb.on_delete = [this, groups_ptr](const std::string& id) {
        if (!groups_ptr) return;
        if (!groups_ptr->is_array()) return;
        for (auto it = groups_ptr->begin(); it != groups_ptr->end(); ++it) {
            if (it->is_object() && it->value("spawn_id", std::string{}) == id) {
                groups_ptr->erase(it);
                break;
            }
        }
        if (room_) room_->save_assets_json();
        room_area_spawn_list_->refresh_row_configuration();
    };
    cb.on_reorder = [this](const std::string&, size_t) {
        if (room_) room_->save_assets_json();
    };

    // Don’t allow linking in this context
    SpawnGroupConfig::ConfigureEntryCallback cfg = [](SpawnGroupConfig::EntryController& ctrl, const nlohmann::json&) {
        ctrl.set_linkable_room_areas_provider({});
        ctrl.set_linkable_asset_areas_provider({});
    };

    room_area_spawn_list_->set_callbacks(std::move(cb));
    room_area_spawn_list_->load(*groups_ptr, std::move(on_change), std::move(on_entry_change), std::move(cfg));
    room_area_spawn_list_->append_rows(rows);
}

