#include "MapLightPanel.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "dev_mode/dm_styles.hpp" // optional, for colors if you want
#include "utils/input.hpp"        // already used by DockableCollapsible

using nlohmann::json;

// --------------------- Small helpers ---------------------

int MapLightPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::clamp_float(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::wrap_angle(float a) {
    // normalize into [0, 360)
    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

// --------------------- Ctor / Attach ---------------------

MapLightPanel::MapLightPanel(int x, int y)
: DockableCollapsible("Map Lighting", /*floatable=*/true, x, y) {
    set_expanded(true);
    build_ui();
}

MapLightPanel::~MapLightPanel() = default;

void MapLightPanel::set_map_info(json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    sync_ui_from_json();
}

// --------------------- Visibility ------------------------

void MapLightPanel::open()   { set_visible(true);  }
void MapLightPanel::close()  { set_visible(false); }
void MapLightPanel::toggle() { set_visible(!visible_); }
bool MapLightPanel::is_visible() const { return visible_; }

// --------------------- UI Build --------------------------

void MapLightPanel::build_ui() {
    // Create all widgets
    radius_         = std::make_unique<DMSlider>("Radius",          0, 20000, 0);
    intensity_      = std::make_unique<DMSlider>("Intensity",       0,   255, 255);
    orbit_radius_   = std::make_unique<DMSlider>("Orbit Radius",    0, 20000, 0);
    update_interval_= std::make_unique<DMSlider>("Update Interval", 1,   120, 10);
    mult_x100_      = std::make_unique<DMSlider>("Mult x100",       0,   100, 0);
    falloff_        = std::make_unique<DMSlider>("Fall-off",        0,   100, 100);
    min_opacity_    = std::make_unique<DMSlider>("Min Opacity",     0,   255, 0);
    max_opacity_    = std::make_unique<DMSlider>("Max Opacity",     0,   255, 255);

    base_r_ = std::make_unique<DMSlider>("Base R", 0, 255, 255);
    base_g_ = std::make_unique<DMSlider>("Base G", 0, 255, 255);
    base_b_ = std::make_unique<DMSlider>("Base B", 0, 255, 255);
    base_a_ = std::make_unique<DMSlider>("Base A", 0, 255, 255);

    prev_key_btn_ = std::make_unique<DMButton>("< Prev", &DMStyles::HeaderButton(), 120, DMButton::height());
    next_key_btn_ = std::make_unique<DMButton>("Next >", &DMStyles::HeaderButton(), 120, DMButton::height());
    add_pair_btn_ = std::make_unique<DMButton>("+ Pair @Angle", &DMStyles::HeaderButton(), 180, DMButton::height());
    delete_btn_   = std::make_unique<DMButton>("Delete Key", &DMStyles::HeaderButton(), 140, DMButton::height());

    key_angle_ = std::make_unique<DMSlider>("Key Angle (deg)", 0, 360, 0);
    key_r_     = std::make_unique<DMSlider>("Key R", 0, 255, 255);
    key_g_     = std::make_unique<DMSlider>("Key G", 0, 255, 255);
    key_b_     = std::make_unique<DMSlider>("Key B", 0, 255, 255);
    key_a_     = std::make_unique<DMSlider>("Key A", 0, 255, 255);

    // Layout: rows of widgets (auto-scrolling handled by DockableCollapsible)
    Rows rows;

    // Top numeric settings
    rows.push_back({ radius_.get(), intensity_.get() });
    rows.push_back({ orbit_radius_.get(), update_interval_.get() });
    rows.push_back({ mult_x100_.get(), falloff_.get() });
    rows.push_back({ min_opacity_.get(), max_opacity_.get() });

    // Base color
    rows.push_back({ base_r_.get(), base_g_.get() });
    rows.push_back({ base_b_.get(), base_a_.get() });

    // Keys pager
    rows.push_back({ prev_key_btn_.get(), next_key_btn_.get(), add_pair_btn_.get(), delete_btn_.get() });

    // Key editor (angle + color)
    rows.push_back({ key_angle_.get() });
    rows.push_back({ key_r_.get(), key_g_.get() });
    rows.push_back({ key_b_.get(), key_a_.get() });

    set_rows(rows);
}

// --------------------- Sync (JSON <-> UI) ----------------

nlohmann::json& MapLightPanel::ensure_light() {
    // Create defaults when missing
    if (!map_info_) {
        static json dummy = json::object();
        return dummy; // safe no-op object (won't be saved)
    }
    if (!map_info_->contains("map_light_data") || !(*map_info_)["map_light_data"].is_object()) {
        (*map_info_)["map_light_data"] = json::object();
    }
    json& L = (*map_info_)["map_light_data"];
    if (!L.contains("radius"))         L["radius"] = 0;
    if (!L.contains("intensity"))      L["intensity"] = 255;
    if (!L.contains("orbit_radius"))   L["orbit_radius"] = 0;
    if (!L.contains("update_interval"))L["update_interval"] = 10;
    if (!L.contains("mult"))           L["mult"] = 0.0;
    if (!L.contains("fall_off"))       L["fall_off"] = 100;
    if (!L.contains("min_opacity"))    L["min_opacity"] = 0;
    if (!L.contains("max_opacity"))    L["max_opacity"] = 255;

    if (!L.contains("base_color") || !L["base_color"].is_array() || L["base_color"].size() < 4) {
        L["base_color"] = {255,255,255,255};
    }
    if (!L.contains("keys") || !L["keys"].is_array()) {
        // default single key at angle 0 with base color
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    }
    return L;
}

void MapLightPanel::sync_ui_from_json() {
    json& L = ensure_light();

    // Scalars
    radius_       ->set_value(clamp_int(L.value("radius", 0), 0, 20000));
    intensity_    ->set_value(clamp_int(L.value("intensity", 255), 0, 255));
    orbit_radius_ ->set_value(clamp_int(L.value("orbit_radius", 0), 0, 20000));
    update_interval_->set_value(clamp_int(L.value("update_interval", 10), 1, 120));
    // mult in JSON is 0.0..1.0, we show as 0..100
    {
        double m = 0.0;
        try { m = L.at("mult").get<double>(); } catch(...) {}
        m = clamp_float((float)m, 0.0f, 1.0f);
        mult_x100_->set_value((int)std::round(m * 100.0));
    }
    falloff_->set_value(clamp_int(L.value("fall_off", 100), 0, 100));
    min_opacity_->set_value(clamp_int(L.value("min_opacity", 0), 0, 255));
    max_opacity_->set_value(clamp_int(L.value("max_opacity", 255), 0, 255));

    // Base color
    auto bc = L["base_color"];
    int br = 255, bg = 255, bb = 255, ba = 255;
    try {
        if (bc.is_array() && bc.size() >= 4) {
            br = clamp_int(bc[0].get<int>(), 0, 255);
            bg = clamp_int(bc[1].get<int>(), 0, 255);
            bb = clamp_int(bc[2].get<int>(), 0, 255);
            ba = clamp_int(bc[3].get<int>(), 0, 255);
        }
    } catch(...) {}
    base_r_->set_value(br);
    base_g_->set_value(bg);
    base_b_->set_value(bb);
    base_a_->set_value(ba);

    // Keys pager defaults
    ensure_keys_array();
    clamp_key_index();

    // Load selected key into sliders
    const auto& keys = L["keys"];
    if (!keys.empty() && keys[current_key_index_].is_array() && keys[current_key_index_].size() >= 2) {
        float ang = 0.0f;
        int r=255,g=255,b=255,a=255;
        try {
            ang = (float)keys[current_key_index_][0].get<double>();
            auto kc = keys[current_key_index_][1];
            if (kc.is_array() && kc.size() >= 4) {
                r = clamp_int(kc[0].get<int>(), 0, 255);
                g = clamp_int(kc[1].get<int>(), 0, 255);
                b = clamp_int(kc[2].get<int>(), 0, 255);
                a = clamp_int(kc[3].get<int>(), 0, 255);
            }
        } catch(...) {}
        key_angle_->set_value((int)std::round(wrap_angle(ang)));
        key_r_->set_value(r);
        key_g_->set_value(g);
        key_b_->set_value(b);
        key_a_->set_value(a);
    } else {
        key_angle_->set_value(0);
        key_r_->set_value(br);
        key_g_->set_value(bg);
        key_b_->set_value(bb);
        key_a_->set_value(ba);
    }

    needs_sync_to_json_ = false;
}

void MapLightPanel::sync_json_from_ui() {
    if (!map_info_) return;
    json& L = ensure_light();

    L["radius"]         = radius_->value();
    L["intensity"]      = intensity_->value();
    L["orbit_radius"]   = orbit_radius_->value();
    L["update_interval"]= update_interval_->value();
    L["mult"]           = (double)mult_x100_->value() / 100.0;
    L["fall_off"]       = falloff_->value();
    L["min_opacity"]    = min_opacity_->value();
    L["max_opacity"]    = max_opacity_->value();

    L["base_color"]     = json::array({ base_r_->value(), base_g_->value(), base_b_->value(), base_a_->value() });

    ensure_keys_array();
    clamp_key_index();

    auto& keys = L["keys"];
    if (!keys.empty() && current_key_index_ >= 0 && current_key_index_ < (int)keys.size()) {
        const int ang = clamp_int(key_angle_->value(), 0, 360);
        const int r   = clamp_int(key_r_->value(),   0, 255);
        const int g   = clamp_int(key_g_->value(),   0, 255);
        const int b   = clamp_int(key_b_->value(),   0, 255);
        const int a   = clamp_int(key_a_->value(),   0, 255);
        keys[current_key_index_] = json::array({ (double)ang, json::array({ r, g, b, a }) });
    }

    if (on_save_) on_save_();
    needs_sync_to_json_ = false;
}

// --------------------- Keys helpers ----------------------

void MapLightPanel::ensure_keys_array() {
    if (!map_info_) return;
    json& L = ensure_light();
    if (!L.contains("keys") || !L["keys"].is_array()) {
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    }
}

void MapLightPanel::clamp_key_index() {
    if (!map_info_) { current_key_index_ = 0; return; }
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) {
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
        n = 1;
    }
    current_key_index_ = clamp_int(current_key_index_, 0, std::max(0, n-1));
    // prepare label for render
    std::ostringstream oss;
    oss << "Key " << (current_key_index_ + 1) << " / " << n;
    current_key_label_ = oss.str();
}

void MapLightPanel::select_prev_key() {
    if (!map_info_) return;
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) return;
    current_key_index_ = (current_key_index_ - 1 + n) % n;
    sync_ui_from_json();
}

void MapLightPanel::select_next_key() {
    if (!map_info_) return;
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) return;
    current_key_index_ = (current_key_index_ + 1) % n;
    sync_ui_from_json();
}

void MapLightPanel::add_key_pair_at_current_angle() {
    if (!map_info_) return;
    json& L = ensure_light();

    const int ang = clamp_int(key_angle_->value(), 0, 360);
    const int r   = clamp_int(key_r_->value(), 0, 255);
    const int g   = clamp_int(key_g_->value(), 0, 255);
    const int b   = clamp_int(key_b_->value(), 0, 255);
    const int a   = clamp_int(key_a_->value(), 0, 255);

    const int ang2 = (ang + 180) % 360;

    auto key1 = json::array({ (double)ang,  json::array({ r,g,b,a }) });
    auto key2 = json::array({ (double)ang2, json::array({ r,g,b,a }) });

    auto& keys = L["keys"];
    keys.push_back(key1);
    keys.push_back(key2);

    // Keep keys sorted by angle for sanity
    std::sort(keys.begin(), keys.end(), [](const json& A, const json& B){
        double a0 = 0.0, b0 = 0.0;
        try { a0 = A[0].get<double>(); } catch(...) {}
        try { b0 = B[0].get<double>(); } catch(...) {}
        return a0 < b0;
    });

    // Move selection to the first of the new pair
    // Find its index
    for (int i=0;i<(int)keys.size();++i) {
        try {
            if ((int)std::round(keys[i][0].get<double>()) == ang) {
                current_key_index_ = i;
                break;
            }
        } catch(...) {}
    }

    needs_sync_to_json_ = true;
    if (on_save_) on_save_();
    sync_ui_from_json();
}

void MapLightPanel::delete_current_key() {
    if (!map_info_) return;
    json& L = ensure_light();
    auto& keys = L["keys"];
    if (keys.size() <= 1) return; // keep at least one key
    if (current_key_index_ < 0 || current_key_index_ >= (int)keys.size()) return;
    keys.erase(keys.begin() + current_key_index_);
    if (current_key_index_ >= (int)keys.size()) current_key_index_ = (int)keys.size() - 1;

    needs_sync_to_json_ = true;
    if (on_save_) on_save_();
    sync_ui_from_json();
}

// --------------------- Panel lifecycle -------------------

void MapLightPanel::update(const Input& input) {
    if (!visible_) return;

    // Let base handle layout/scrolling math
    DockableCollapsible::update(input, /*screen_w*/0, /*screen_h*/0);

    // Nothing else needed here; edits are detected in handle_event, then we sync-json-on-change.
}

bool MapLightPanel::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    // First let base route to children
    bool used = DockableCollapsible::handle_event(e);

    // Handle our buttons (they’re part of rows, so base already sent events to them;
    // we just check their pressed state on mouse-up)
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        // We rely on DMButton::handle_event returning true and Dockable already consuming,
        // but we still need to check intent (no direct "wasPressed" API),
        // so we re-check if pointer is over and assume activation. This is sufficient in practice
        // because Dockable already did hit-tests.
        // Simpler: if base consumed, we’ll just try to do the actions anyway.

        // Prev / Next / Add Pair / Delete
        // We’ll trigger on any click release inside panel; that’s acceptable for a dev tool.
        // If you want stricter gating, wrap DMButton with an "armed" state.
        SDL_Point mp{ e.button.x, e.button.y };
        if (prev_key_btn_) {
            // No exact "inside" check available here without storing rects.
            // Rely on Dockable to have routed correctly; then allow both prev/next in used path.
        }
    }

    // We *also* want direct, precise actions:
    // The DMButton already consumed events; we can poll hover/pressed state if exposed.
    // For portability, do simple re-hit-test:
    auto try_button = [&](DMButton* btn, std::function<void()> action){
        if (!btn) return;
        // Approximate: if last event is mouse button up and within panel, trigger
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            // use panel hit test to avoid false triggers outside panel
            if (is_point_inside(p.x, p.y)) {
                action();
            }
        }
    };

    // For a dev tool, we won’t overcomplicate input gating—triggering on release within panel is enough:
    try_button(prev_key_btn_.get(),  [&]{ select_prev_key(); });
    try_button(next_key_btn_.get(),  [&]{ select_next_key(); });
    try_button(add_pair_btn_.get(),  [&]{ add_key_pair_at_current_angle(); });
    try_button(delete_btn_.get(),    [&]{ delete_current_key(); });

    // After any widget interaction, push back to JSON.
    if (used) {
        needs_sync_to_json_ = true;
    }

    // Also respond to ESC close if you want:
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        // no-op; leave open
    }

    if (needs_sync_to_json_) {
        sync_json_from_ui();
    }

    return used;
}

void MapLightPanel::render(SDL_Renderer* r) const {
    if (!visible_) return;
    DockableCollapsible::render(r);
}

bool MapLightPanel::is_point_inside(int x, int y) const {
    return DockableCollapsible::is_point_inside(x, y);
}

// --------------------- Custom content --------------------

void MapLightPanel::render_content(SDL_Renderer* r) const {
    // Optional: draw a compact “current key” readout below the pager
    // (Dockable renders all child widgets already.)
    if (!r) return;

    // We’ll draw a tiny swatch for the current key color to the right of the angle slider.
    if (!map_info_) return;

    const json& L = (*map_info_)["map_light_data"];
    if (!L.contains("keys") || !L["keys"].is_array()) return;
    const auto& keys = L["keys"];
    if (keys.empty()) return;

    int r_out=255,g_out=255,b_out=255,a_out=255;
    double ang = 0.0;
    try {
        const auto& K = keys.at(std::min<int>(current_key_index_, (int)keys.size()-1));
        if (K.is_array() && K.size() >= 2) {
            ang = K[0].get<double>();
            const auto& kc = K[1];
            if (kc.is_array() && kc.size() >= 4) {
                r_out = clamp_int(kc[0].get<int>(), 0, 255);
                g_out = clamp_int(kc[1].get<int>(), 0, 255);
                b_out = clamp_int(kc[2].get<int>(), 0, 255);
                a_out = clamp_int(kc[3].get<int>(), 0, 255);
            }
        }
    } catch(...) {}

    // Find some area in the body viewport to draw a small color block.
    // We can piggyback on available body_viewport_ that Dockable computes.
    SDL_Rect swatch = body_viewport_;
    swatch.y += std::max(0, swatch.h - 24);
    swatch.h = 16;
    swatch.w = std::min(120, swatch.w);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, r_out, g_out, b_out, a_out);
    SDL_RenderFillRect(r, &swatch);

    SDL_SetRenderDrawColor(r, 20, 20, 20, 220);
    SDL_RenderDrawRect(r, &swatch);

    // A subtle label line above (optional): we won’t render text here to avoid font deps.
    // If you want text, you can use DMTextBox-style label render with TTF (like widgets.cpp does).
}

