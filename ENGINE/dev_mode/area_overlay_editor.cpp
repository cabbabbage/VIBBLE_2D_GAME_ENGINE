#include "area_overlay_editor.hpp"

#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "draw_utils.hpp"
#include "dm_styles.hpp"

#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "render/camera.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <climits>

namespace {
    // Point in polygon (even-odd) for integer coords
    static bool point_in_poly(int px, int py, const std::vector<SDL_Point>& poly) {
        bool inside = false;
        size_t n = poly.size();
        if (n < 3) return false;
        for (size_t i=0, j=n-1; i<n; j=i++) {
            const int xi = poly[i].x, yi = poly[i].y;
            const int xj = poly[j].x, yj = poly[j].y;
            bool intersect = false;
            if ((yi > py) != (yj > py)) {
                if (yj != yi) {
                    double x_intersect = (xj - xi) * (py - yi) / (double)(yj - yi) + xi;
                    if (px < x_intersect) {
                        intersect = true;
                    }
                }
            }
            if (intersect) inside = !inside;
        }
        return inside;
    }
}

// Default mask alpha for overlay visualization; can be adjusted as needed.
constexpr Uint8 kDefaultMaskAlpha = 128;

AreaOverlayEditor::AreaOverlayEditor()
    : mask_alpha_(kDefaultMaskAlpha) {}

AreaOverlayEditor::~AreaOverlayEditor() {
    if (mask_) SDL_FreeSurface(mask_);
    if (mask_tex_) SDL_DestroyTexture(mask_tex_);
}

bool AreaOverlayEditor::begin(AssetInfo* info, Asset* asset, const std::string& area_name) {
    if (!assets_ || !info || !asset) return false;
    // Determine canvas from info scale and original dims
    int cw = std::max(32, (int)std::lround(info->original_canvas_width * info->scale_factor));
    int ch = std::max(32, (int)std::lround(info->original_canvas_height * info->scale_factor));
    info_ = info;
    asset_ = asset;
    area_name_ = area_name;
    canvas_w_ = cw;
    canvas_h_ = ch;
    mask_origin_x_ = 0;
    mask_origin_y_ = 0;

    if (mask_) { SDL_FreeSurface(mask_); mask_ = nullptr; }
    if (mask_tex_) { SDL_DestroyTexture(mask_tex_); mask_tex_ = nullptr; }
    mask_ = SDL_CreateRGBSurfaceWithFormat(0, canvas_w_, canvas_h_, 32, SDL_PIXELFORMAT_RGBA32);
    if (!mask_) return false;
    clear_mask();

    // Preload from existing area (unflipped local coords)
    init_mask_from_existing_area();

    // Defer mask texture creation to render() when renderer is available
    upload_mask();

    ensure_toolbox();
    active_ = true;
    drawing_ = false;
    mode_ = Mode::Draw;
    saved_since_begin_ = false;
    toolbox_autoplace_done_ = false;
    return true;
}

void AreaOverlayEditor::cancel() {
    active_ = false;
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

void AreaOverlayEditor::stamp(int cx, int cy, int radius, bool erase) {
    if (!mask_) return;
    Uint32 col = SDL_MapRGBA(mask_->format, 255, 0, 0, erase ? 0 : 255);
    dm_draw::stamp_circle(mask_, cx, cy, radius, col);
    upload_mask();
}

void AreaOverlayEditor::ensure_mask_contains(int lx, int ly, int radius) {
    if (!mask_) return;
    // Convert local coords to current surface coords
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

    // Blit old onto new at offset
    int dst_x = -min_sx;
    int dst_y = -min_sy;
    SDL_Rect dst{ dst_x, dst_y, mask_->w, mask_->h };
    SDL_BlitSurface(mask_, nullptr, grown, &dst);

    SDL_FreeSurface(mask_);
    mask_ = grown;
    // Update origin and texture; will recreate texture in render if needed
    mask_origin_x_ += min_sx;
    mask_origin_y_ += min_sy;
    if (mask_tex_) { SDL_DestroyTexture(mask_tex_); mask_tex_ = nullptr; }
}

void AreaOverlayEditor::init_mask_from_existing_area() {
    if (!mask_ || !info_) return;
    // Find the area by name
    Area* a = info_->find_area(area_name_);
    if (!a) return;
    // a->get_points() are in local canvas coordinates already (unflipped)
    const auto& pts = a->get_points();
    if (pts.size() < 3) return;
    // Ensure mask covers polygon bounds (with padding)
    int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (const auto& p : pts) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    ensure_mask_contains(minx, miny, 2);
    ensure_mask_contains(maxx, maxy, 2);
    // Slow, simple raster fill; initialize alpha to 255 where inside polygon
    SDL_LockSurface(mask_);
    Uint8* pixels = static_cast<Uint8*>(mask_->pixels);
    const int pitch = mask_->pitch;
    for (int y = 0; y < mask_->h; ++y) {
        for (int x = 0; x < mask_->w; ++x) {
            int lx = x + mask_origin_x_;
            int ly = y + mask_origin_y_;
            bool inside = point_in_poly(lx, ly, pts);
            Uint32* p = reinterpret_cast<Uint32*>(pixels + y * pitch) + x;
            *p = SDL_MapRGBA(mask_->format, 255, 0, 0, inside ? 255 : 0);
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

void AreaOverlayEditor::save_area() {
    if (!info_) return;
    // Trace polygon from mask and save as new area (pivot: center-bottom)
    std::vector<SDL_Point> pts = trace_polygon_from_mask();
    if (pts.empty()) { cancel(); return; }
    // Convert from surface -> local coords
    for (auto& p : pts) { p.x += mask_origin_x_; p.y += mask_origin_y_; }
    // Build area in local canvas coords
    Area edited(area_name_, pts);
    // Set pivot at canvas center-bottom (to match loader/writer conventions)
    edited.pos.x = canvas_w_ / 2;
    edited.pos.y = canvas_h_;
    edited.update_geometry_data();
    info_->upsert_area_from_editor(edited);
    (void)info_->update_info_json();
    saved_since_begin_ = true;
    cancel();
}

void AreaOverlayEditor::ensure_toolbox() {
    if (toolbox_) return;
    toolbox_ = std::make_unique<DockableCollapsible>("Area Tools", true);
    btn_draw_  = std::make_unique<DMButton>("Draw",  &DMStyles::CreateButton(), 180, DMButton::height());
    btn_erase_ = std::make_unique<DMButton>("Erase", &DMStyles::CreateButton(), 180, DMButton::height());
    btn_save_  = std::make_unique<DMButton>("Save",  &DMStyles::CreateButton(), 180, DMButton::height());
    rebuild_toolbox_rows();
}

void AreaOverlayEditor::rebuild_toolbox_rows() {
    if (!toolbox_) return;
    owned_widgets_.clear();
    std::vector<Widget*> row1, row2, row3;
    owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_draw_.get(),  [this]{ mode_ = Mode::Draw; }));
    owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_erase_.get(), [this]{ mode_ = Mode::Erase; }));
    owned_widgets_.push_back(std::make_unique<ButtonWidget>(btn_save_.get(),  [this]{ save_area(); }));
    row1.push_back(owned_widgets_[0].get());
    row2.push_back(owned_widgets_[1].get());
    row3.push_back(owned_widgets_[2].get());
    DockableCollapsible::Rows rows;
    rows.push_back(row1);
    rows.push_back(row2);
    rows.push_back(row3);
    toolbox_->set_rows(rows);
}

void AreaOverlayEditor::position_toolbox_left_of_asset(int screen_w, int screen_h) {
    if (!toolbox_ || !assets_ || !asset_) return;
    SDL_Point ap = assets_->getView().map_to_screen(SDL_Point{asset_->pos.x, asset_->pos.y});
    // Estimate toolbox width; layout will set exact
    int tb_w = toolbox_->rect().w;
    int x = ap.x - tb_w - 16;
    int y = ap.y - 200; // roughly align around asset center
    x = std::max(8, x);
    y = std::max(8, y);
    toolbox_->set_position(x, y);
    toolbox_->set_work_area(SDL_Rect{0,0,screen_w,screen_h});
}

void AreaOverlayEditor::update(const Input& input, int screen_w, int screen_h) {
    if (!active_ || !assets_ || !asset_) return;

    // Ensure toolbox exists; auto-place once, then leave movable by the user
    ensure_toolbox();
    if (!toolbox_autoplace_done_) {
        position_toolbox_left_of_asset(screen_w, screen_h);
        toolbox_autoplace_done_ = true;
    } else if (toolbox_) {
        toolbox_->set_work_area(SDL_Rect{0,0,screen_w,screen_h});
    }
    if (toolbox_) toolbox_->update(input, screen_w, screen_h);

    // Drawing (disabled when interacting with toolbox UI)
    const int mx = input.getX();
    const int my = input.getY();
    SDL_Point mouse_point{mx, my};
    const bool over_toolbox = (toolbox_ && SDL_PointInRect(&mouse_point, &toolbox_->rect()));
    if (input.isDown(Input::LEFT) && !over_toolbox) {
        drawing_ = true;
    } else if (!input.isDown(Input::LEFT) || over_toolbox) {
        drawing_ = false;
    }

    if (drawing_) {
        // Map screen -> world -> local (unflipped) canvas coords
        SDL_Point world = assets_->getView().screen_to_map(SDL_Point{mx, my});
        int dxp = world.x - asset_->pos.x;
        int dyp = world.y - asset_->pos.y;
        if (asset_->flipped) dxp = -dxp;
        int lx = (canvas_w_ / 2) + dxp;
        int ly = (canvas_h_) + dyp;
        // Allow painting outside initial canvas by growing the mask as needed
        ensure_mask_contains(lx, ly, brush_radius_);
        int sx = lx - mask_origin_x_;
        int sy = ly - mask_origin_y_;
        if (sx >= 0 && sx < mask_->w && sy >= 0 && sy < mask_->h) {
            stamp(sx, sy, brush_radius_, mode_ == Mode::Erase);
        }
    }
}

bool AreaOverlayEditor::handle_event(const SDL_Event& e) {
    if (!active_) return false;
    // Forward to toolbox first
    if (toolbox_ && toolbox_->handle_event(e)) return true;

    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_ESCAPE) { cancel(); return true; }
    }
    return false;
}

void AreaOverlayEditor::render(SDL_Renderer* r) {
    if (!active_ || !assets_ || !asset_) return;

    float scale = assets_->getView().get_scale();
    if (scale <= 0.00001f) scale = 0.00001f;
    int pivot_x = canvas_w_ / 2;
    int pivot_y = canvas_h_;
    SDL_Point top_left_world{ asset_->pos.x + (mask_origin_x_ - pivot_x), asset_->pos.y + (mask_origin_y_ - pivot_y) };
    SDL_Point tl = assets_->getView().map_to_screen(top_left_world);
    SDL_Rect dst{ tl.x, tl.y, (int)std::lround(mask_->w / scale), (int)std::lround(mask_->h / scale) };

    if (!mask_tex_) {
        mask_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, mask_->w, mask_->h);
        if (!mask_tex_) return;
        upload_mask();
    }
    // Recreate texture if size changed due to growth
    int tex_w=0, tex_h=0;
    SDL_QueryTexture(mask_tex_, nullptr, nullptr, &tex_w, &tex_h);
    if (tex_w != mask_->w || tex_h != mask_->h) {
        SDL_DestroyTexture(mask_tex_);
        mask_tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, mask_->w, mask_->h);
        if (!mask_tex_) return;
        upload_mask();
    }
    SDL_SetTextureBlendMode(mask_tex_, SDL_BLENDMODE_BLEND);
    // Use configurable mask alpha for overlay visualization
    SDL_SetTextureAlphaMod(mask_tex_, mask_alpha_);
    if (toolbox_) toolbox_->render(r);
}

// Trace outer boundary polygon using Moore neighborhood. Returns points in surface coords.
std::vector<SDL_Point> AreaOverlayEditor::trace_polygon_from_mask() const {
    std::vector<SDL_Point> poly;
    if (!mask_) return poly;
    SDL_LockSurface(mask_);
    auto getA = [&](int x, int y) -> Uint8 {
        if (x < 0 || y < 0 || x >= mask_->w || y >= mask_->h) return 0;
        const Uint8* pixels = static_cast<const Uint8*>(mask_->pixels);
        const int pitch = mask_->pitch;
        const Uint32* p = reinterpret_cast<const Uint32*>(pixels + y * pitch) + x;
        Uint8 r, g, b, a; (void)r; (void)g; (void)b; SDL_GetRGBA(*p, mask_->format, &r, &g, &b, &a);
        return a;
    };
    // Find starting boundary pixel
    int sx = -1, sy = -1;
    for (int y = 0; y < mask_->h && sy < 0; ++y) {
        for (int x = 0; x < mask_->w; ++x) {
            if (getA(x,y) == 0) continue;
            if (getA(x-1,y) == 0 || getA(x+1,y) == 0 || getA(x,y-1) == 0 || getA(x,y+1) == 0) { sx = x; sy = y; break; }
        }
    }
    if (sy < 0) { SDL_UnlockSurface(mask_); return poly; }

    static const int ndx[8] = { 1, 1, 0,-1,-1,-1, 0, 1 };
    static const int ndy[8] = { 0, 1, 1, 1, 0,-1,-1,-1 };
    auto neighbor_index = [&](int cx, int cy, int nx, int ny) -> int {
        for (int i=0;i<8;++i) if (cx + ndx[i] == nx && cy + ndy[i] == ny) return i; return 0;
    };
    // Guard limit to prevent infinite loops in polygon tracing; adjust as needed for complex shapes.
    constexpr int POLYGON_TRACE_GUARD_LIMIT = 200000;
    int px = sx - 1, py = sy;
    int cx = sx, cy = sy;
    int first_nx = -1, first_ny = -1;
    int guard = 0;
    while (guard++ < POLYGON_TRACE_GUARD_LIMIT) {
        poly.push_back(SDL_Point{ cx, cy });
        int bi = neighbor_index(cx, cy, px, py);
        int i = (bi + 1) & 7; // start clockwise from backtrack neighbor
        bool found = false;
        for (int k = 0; k < 8; ++k) {
            int nx = cx + ndx[i];
            int ny = cy + ndy[i];
            if (getA(nx, ny) > 0) {
                px = cx; py = cy;
                cx = nx; cy = ny;
                if (first_nx < 0) { first_nx = cx; first_ny = cy; }
                found = true;
                break;
            }
            i = (i + 1) & 7;
        }
        if (!found) break; // isolated pixel
        if ((cx == sx && cy == sy) && (poly.size() > 1 && first_nx == poly[1].x && first_ny == poly[1].y)) {
            break; // closed loop
        }
    }
    // Deduplicate consecutive points
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
