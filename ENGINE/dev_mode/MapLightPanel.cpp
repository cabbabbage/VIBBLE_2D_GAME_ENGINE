#include "MapLightPanel.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <SDL_ttf.h>

#include "dev_mode/dm_styles.hpp"
#include "utils/input.hpp"

using nlohmann::json;

class MapLightPanel::WarningLabel : public Widget {
public:
    WarningLabel() = default;

    void set_text(std::string text) { text_ = std::move(text); }
    const std::string& text() const { return text_; }

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int w) const override {
        if (text_.empty()) {
            return 0;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return style.font_size;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, w));
        int height = surface ? surface->h : style.font_size;
        if (surface) {
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
        return height + DMSpacing::small_gap();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* r) const override {
        if (text_.empty() || !r) {
            return;
        }
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) {
            return;
        }
        SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(font, text_.c_str(), color_, std::max(10, rect_.w));
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surface);
            if (tex) {
                SDL_Rect dst{rect_.x, rect_.y, surface->w, surface->h};
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

    void set_color(SDL_Color color) { color_ = color; }

private:
    SDL_Rect rect_{0, 0, 0, 0};
    std::string text_;
    SDL_Color color_{255, 120, 120, 255};
};

class MapLightPanel::SectionLabel : public Widget {
public:
    explicit SectionLabel(std::string text)
        : text_(std::move(text)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }

    int height_for_width(int) const override {
        return DMCheckbox::height();
    }

    bool handle_event(const SDL_Event&) override { return false; }

    void render(SDL_Renderer* renderer) const override {
        if (!renderer) return;
        const DMLabelStyle& style = DMStyles::Label();
        TTF_Font* font = style.open_font();
        if (!font) return;
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text_.c_str(), style.color);
        if (surface) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
            if (tex) {
                SDL_Rect dst{ rect_.x, rect_.y, surface->w, surface->h };
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surface);
        }
        TTF_CloseFont(font);
    }

    bool wants_full_row() const override { return true; }

private:
    std::string text_;
    SDL_Rect rect_{0, 0, 0, 0};
};

int MapLightPanel::clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::clamp_float(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float MapLightPanel::wrap_angle(float a) {

    while (a < 0.0f)   a += 360.0f;
    while (a >= 360.0f) a -= 360.0f;
    return a;
}

MapLightPanel::MapLightPanel(int x, int y)
: DockableCollapsible("Map Lighting", true, x, y) {
    set_expanded(true);
    build_ui();
    update_save_status(true);
}

MapLightPanel::~MapLightPanel() = default;

void MapLightPanel::set_map_info(json* map_info, SaveCallback on_save) {
    map_info_ = map_info;
    on_save_ = std::move(on_save);
    current_key_index_ = 0;
    editing_light_ = json::object();
    if (map_info_ && map_info_->contains("map_light_data") && (*map_info_)["map_light_data"].is_object()) {
        editing_light_ = (*map_info_)["map_light_data"];
    }
    ensure_light();
    update_save_status(true);
    sync_ui_from_json();
}

void MapLightPanel::open()   {
    set_visible(true);
    set_expanded(true);
}
void MapLightPanel::close()  { set_visible(false); }
void MapLightPanel::toggle() {
    if (is_visible()) {
        close();
    } else {
        open();
    }
}
bool MapLightPanel::is_visible() const { return visible_; }

void MapLightPanel::build_ui() {

    update_top_btn_    = std::make_unique<DMButton>("Update Light", &DMStyles::AccentButton(), 160, DMButton::height());
    update_bottom_btn_ = std::make_unique<DMButton>("Update Light", &DMStyles::AccentButton(), 160, DMButton::height());
    radius_         = std::make_unique<DMSlider>("Radius",          0, 20000, 0);
    intensity_      = std::make_unique<DMSlider>("Intensity",       0,   255, 255);
    orbit_radius_   = std::make_unique<DMSlider>("Orbit Radius",    0, 20000, 0);
    update_interval_= std::make_unique<DMSlider>("Update Interval", 1,   120, 10);
    mult_x100_      = std::make_unique<DMSlider>("Mult x100",       0,   100, 0);
    falloff_        = std::make_unique<DMSlider>("Fall-off",        0,   100, 100);
    min_opacity_    = std::make_unique<DMSlider>("Min Opacity",     0,   255, 0);
    max_opacity_    = std::make_unique<DMSlider>("Max Opacity",     0,   255, 255);

    screen_r_          = std::make_unique<DMSlider>("Screen Base R",       0, 255, 255);
    screen_g_          = std::make_unique<DMSlider>("Screen Base G",       0, 255, 255);
    screen_b_          = std::make_unique<DMSlider>("Screen Base B",       0, 255, 255);
    screen_min_opacity_= std::make_unique<DMSlider>("Screen Min Opacity",  0, 255, 0);
    screen_max_opacity_= std::make_unique<DMSlider>("Screen Max Opacity",  0, 255, 255);

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

    widget_wrappers_.clear();
    widget_wrappers_.reserve(20);

    auto add_widget = [this](std::unique_ptr<Widget> w) -> Widget* {
        Widget* raw = w.get();
        widget_wrappers_.push_back(std::move(w));
        return raw;
    };

    Rows rows;

    rows.push_back({
        add_widget(std::make_unique<ButtonWidget>(update_top_btn_.get(), [this]() { apply_changes(); }))
    });

    auto warning_label = std::make_unique<WarningLabel>();
    warning_label_ = warning_label.get();
    add_widget(std::move(warning_label));
    if (warning_label_) {
        warning_label_->set_color(SDL_Color{255, 120, 120, 255});
        rows.push_back({ warning_label_ });
    }

    rows.push_back({ add_widget(std::make_unique<SectionLabel>("Orbit Settings")) });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(update_interval_.get())),
        add_widget(std::make_unique<SliderWidget>(orbit_radius_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(min_opacity_.get())),
        add_widget(std::make_unique<SliderWidget>(max_opacity_.get()))
    });

    rows.push_back({ add_widget(std::make_unique<SectionLabel>("Screen Light")) });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(screen_r_.get())),
        add_widget(std::make_unique<SliderWidget>(screen_g_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(screen_b_.get())),
        add_widget(std::make_unique<SliderWidget>(screen_min_opacity_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(screen_max_opacity_.get()))
    });

    rows.push_back({ add_widget(std::make_unique<SectionLabel>("Map Light Texture")) });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(radius_.get())),
        add_widget(std::make_unique<SliderWidget>(intensity_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(mult_x100_.get())),
        add_widget(std::make_unique<SliderWidget>(falloff_.get()))
    });

    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(base_r_.get())),
        add_widget(std::make_unique<SliderWidget>(base_g_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(base_b_.get())),
        add_widget(std::make_unique<SliderWidget>(base_a_.get()))
    });

    rows.push_back({
        add_widget(std::make_unique<ButtonWidget>(prev_key_btn_.get(), [this](){ select_prev_key(); })),
        add_widget(std::make_unique<ButtonWidget>(next_key_btn_.get(), [this](){ select_next_key(); })),
        add_widget(std::make_unique<ButtonWidget>(add_pair_btn_.get(), [this](){ add_key_pair_at_current_angle(); })),
        add_widget(std::make_unique<ButtonWidget>(delete_btn_.get(), [this](){ delete_current_key(); }))
    });

    rows.push_back({ add_widget(std::make_unique<SliderWidget>(key_angle_.get())) });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(key_r_.get())),
        add_widget(std::make_unique<SliderWidget>(key_g_.get()))
    });
    rows.push_back({
        add_widget(std::make_unique<SliderWidget>(key_b_.get())),
        add_widget(std::make_unique<SliderWidget>(key_a_.get()))
    });

    rows.push_back({
        add_widget(std::make_unique<ButtonWidget>(update_bottom_btn_.get(), [this]() { apply_changes(); }))
    });

    set_rows(rows);
}

void MapLightPanel::apply_changes() {
    if (!map_info_) {
        return;
    }

    sync_json_from_ui();
    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    ScreenLightSettings screen = sanitize_screen_settings(current_screen_settings_from_ui(), orbit);

    bool ok = commit_light_changes();
    if (ok) {
        last_applied_orbit_ = orbit;
        last_applied_screen_ = screen;
    }
}

nlohmann::json& MapLightPanel::ensure_light() {

    if (!editing_light_.is_object()) {
        editing_light_ = json::object();
    }
    json& L = editing_light_;
    if (!L.contains("radius"))         L["radius"] = 0;
    if (!L.contains("intensity"))      L["intensity"] = 255;
    if (!L.contains("orbit_radius"))   L["orbit_radius"] = 0;
    if (!L.contains("update_interval"))L["update_interval"] = 10;
    if (!L.contains("mult"))           L["mult"] = 0.0;
    if (!L.contains("fall_off"))       L["fall_off"] = 100;
    if (!L.contains("min_opacity"))    L["min_opacity"] = 0;
    if (!L.contains("max_opacity"))    L["max_opacity"] = 255;
    {
        int min_o = 0;
        int max_o = 255;
        try { min_o = L.at("min_opacity").get<int>(); } catch(...) {}
        try { max_o = L.at("max_opacity").get<int>(); } catch(...) {}
        min_o = clamp_int(min_o, 0, 255);
        max_o = clamp_int(max_o, 0, 255);
        if (min_o > max_o) std::swap(min_o, max_o);
        L["min_opacity"] = min_o;
        L["max_opacity"] = max_o;
    }

    if (!L.contains("base_color") || !L["base_color"].is_array() || L["base_color"].size() < 4) {
        L["base_color"] = {255,255,255,255};
    }

    ensure_screen_light(L);

    if (!L.contains("keys") || !L["keys"].is_array()) {

        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    }
    return L;
}

nlohmann::json& MapLightPanel::ensure_screen_light(nlohmann::json& light) {
    if (!light.contains("screen_light") || !light["screen_light"].is_object()) {
        light["screen_light"] = json::object();
    }
    json& screen = light["screen_light"];
    if (!screen.contains("color") || !screen["color"].is_array() || screen["color"].size() < 3) {
        screen["color"] = json::array({255, 255, 255});
    }
    auto clamp_component = [](const json& v) -> int {
        try {
            return clamp_int(v.get<int>(), 0, 255);
        } catch (...) {
            return 255;
        }
    };
    auto& color = screen["color"];
    if (color.is_array()) {
        for (std::size_t i = 0; i < 3; ++i) {
            if (i >= color.size()) {
                color.push_back(255);
            } else {
                color[i] = clamp_component(color[i]);
            }
        }
        while (color.size() > 3) {
            color.erase(color.size() - 1);
        }
    }

    int map_min = 0;
    int map_max = 255;
    try { map_min = light.at("min_opacity").get<int>(); } catch (...) {}
    try { map_max = light.at("max_opacity").get<int>(); } catch (...) {}
    map_min = clamp_int(map_min, 0, 255);
    map_max = clamp_int(map_max, 0, 255);
    if (map_min > map_max) std::swap(map_min, map_max);

    if (!screen.contains("min_opacity")) {
        screen["min_opacity"] = map_min;
    }
    if (!screen.contains("max_opacity")) {
        screen["max_opacity"] = map_max;
    }
    int scr_min = map_min;
    int scr_max = map_max;
    try { scr_min = clamp_int(screen.at("min_opacity").get<int>(), map_min, map_max); } catch (...) { scr_min = map_min; }
    try { scr_max = clamp_int(screen.at("max_opacity").get<int>(), map_min, map_max); } catch (...) { scr_max = map_max; }
    if (scr_min > scr_max) std::swap(scr_min, scr_max);
    screen["min_opacity"] = scr_min;
    screen["max_opacity"] = scr_max;
    return screen;
}

void MapLightPanel::sync_ui_from_json() {
    json& L = ensure_light();

    radius_       ->set_value(clamp_int(L.value("radius", 0), 0, 20000));
    intensity_    ->set_value(clamp_int(L.value("intensity", 255), 0, 255));

    {
        double m = 0.0;
        try { m = L.at("mult").get<double>(); } catch(...) {}
        m = clamp_float((float)m, 0.0f, 1.0f);
        mult_x100_->set_value((int)std::round(m * 100.0));
    }
    falloff_->set_value(clamp_int(L.value("fall_off", 100), 0, 100));

    OrbitSettings orbit{};
    orbit.update_interval = clamp_int(L.value("update_interval", 10), 1, 120);
    orbit.orbit_radius = clamp_int(L.value("orbit_radius", 0), 0, 20000);
    orbit.min_opacity = clamp_int(L.value("min_opacity", 0), 0, 255);
    orbit.max_opacity = clamp_int(L.value("max_opacity", 255), 0, 255);
    orbit = sanitize_orbit_settings(orbit);
    set_orbit_sliders(orbit);
    last_applied_orbit_ = orbit;

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

    json& screen_json = ensure_screen_light(L);
    ScreenLightSettings screen{};
    try {
        if (screen_json["color"].is_array()) {
            auto color = screen_json["color"];
            if (color.size() >= 3) {
                screen.r = clamp_int(color[0].get<int>(), 0, 255);
                screen.g = clamp_int(color[1].get<int>(), 0, 255);
                screen.b = clamp_int(color[2].get<int>(), 0, 255);
            }
        }
    } catch (...) {}
    screen.min_opacity = screen_json.value("min_opacity", orbit.min_opacity);
    screen.max_opacity = screen_json.value("max_opacity", orbit.max_opacity);
    screen = sanitize_screen_settings(screen, orbit);
    set_screen_sliders(screen);
    last_applied_screen_ = screen;

    ensure_keys_array();
    clamp_key_index();

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
    json& L = ensure_light();

    L["radius"]         = radius_->value();
    L["intensity"]      = intensity_->value();
    L["mult"]           = (double)mult_x100_->value() / 100.0;
    L["fall_off"]       = falloff_->value();

    L["base_color"]     = json::array({ base_r_->value(), base_g_->value(), base_b_->value(), base_a_->value() });

    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    write_orbit_settings_to_json(orbit);

    ScreenLightSettings screen = sanitize_screen_settings(current_screen_settings_from_ui(), orbit);
    write_screen_settings_to_json(screen);

    set_orbit_sliders(orbit);
    set_screen_sliders(screen);

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

    needs_sync_to_json_ = false;
}

void MapLightPanel::ensure_keys_array() {
    json& L = ensure_light();
    if (!L.contains("keys") || !L["keys"].is_array()) {
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
    }
}

MapLightPanel::OrbitSettings MapLightPanel::sanitize_orbit_settings(const OrbitSettings& raw) const {
    OrbitSettings out = raw;
    out.update_interval = clamp_int(out.update_interval, 1, 120);
    out.orbit_radius = clamp_int(out.orbit_radius, 0, 20000);
    out.min_opacity = clamp_int(out.min_opacity, 0, 255);
    out.max_opacity = clamp_int(out.max_opacity, 0, 255);
    if (out.min_opacity > out.max_opacity) {
        std::swap(out.min_opacity, out.max_opacity);
    }
    return out;
}

MapLightPanel::ScreenLightSettings MapLightPanel::sanitize_screen_settings(const ScreenLightSettings& raw,
                                                                          const OrbitSettings& orbit) const {
    ScreenLightSettings out = raw;
    out.r = clamp_int(out.r, 0, 255);
    out.g = clamp_int(out.g, 0, 255);
    out.b = clamp_int(out.b, 0, 255);
    int lo = orbit.min_opacity;
    int hi = orbit.max_opacity;
    if (lo > hi) std::swap(lo, hi);
    out.min_opacity = clamp_int(out.min_opacity, lo, hi);
    out.max_opacity = clamp_int(out.max_opacity, lo, hi);
    if (out.min_opacity > out.max_opacity) {
        std::swap(out.min_opacity, out.max_opacity);
    }
    return out;
}

MapLightPanel::OrbitSettings MapLightPanel::current_orbit_settings_from_ui() const {
    OrbitSettings current;
    current.update_interval = update_interval_ ? update_interval_->value() : 10;
    current.orbit_radius = orbit_radius_ ? orbit_radius_->value() : 0;
    current.min_opacity = min_opacity_ ? min_opacity_->value() : 0;
    current.max_opacity = max_opacity_ ? max_opacity_->value() : 255;
    return current;
}

MapLightPanel::ScreenLightSettings MapLightPanel::current_screen_settings_from_ui() const {
    ScreenLightSettings current;
    current.r = screen_r_ ? screen_r_->value() : 255;
    current.g = screen_g_ ? screen_g_->value() : 255;
    current.b = screen_b_ ? screen_b_->value() : 255;
    current.min_opacity = screen_min_opacity_ ? screen_min_opacity_->value() : 0;
    current.max_opacity = screen_max_opacity_ ? screen_max_opacity_->value() : 255;
    return current;
}

void MapLightPanel::set_orbit_sliders(const OrbitSettings& orbit) {
    if (update_interval_) update_interval_->set_value(orbit.update_interval);
    if (orbit_radius_)    orbit_radius_->set_value(orbit.orbit_radius);
    if (min_opacity_)     min_opacity_->set_value(orbit.min_opacity);
    if (max_opacity_)     max_opacity_->set_value(orbit.max_opacity);
}

void MapLightPanel::set_screen_sliders(const ScreenLightSettings& screen) {
    if (screen_r_)          screen_r_->set_value(screen.r);
    if (screen_g_)          screen_g_->set_value(screen.g);
    if (screen_b_)          screen_b_->set_value(screen.b);
    if (screen_min_opacity_)screen_min_opacity_->set_value(screen.min_opacity);
    if (screen_max_opacity_)screen_max_opacity_->set_value(screen.max_opacity);
}

void MapLightPanel::write_orbit_settings_to_json(const OrbitSettings& orbit) {
    json& L = ensure_light();
    L["update_interval"] = orbit.update_interval;
    L["orbit_radius"] = orbit.orbit_radius;
    L["min_opacity"] = orbit.min_opacity;
    L["max_opacity"] = orbit.max_opacity;
}

void MapLightPanel::write_screen_settings_to_json(const ScreenLightSettings& screen) {
    json& L = ensure_light();
    json& screen_json = ensure_screen_light(L);
    screen_json["color"] = json::array({ screen.r, screen.g, screen.b });
    screen_json["min_opacity"] = screen.min_opacity;
    screen_json["max_opacity"] = screen.max_opacity;
}

void MapLightPanel::apply_immediate_settings() {
    if (!map_info_) {
        return;
    }

    OrbitSettings orbit = sanitize_orbit_settings(current_orbit_settings_from_ui());
    ScreenLightSettings screen = sanitize_screen_settings(current_screen_settings_from_ui(), orbit);

    bool orbit_changed = !(orbit == last_applied_orbit_);
    bool screen_changed = !(screen == last_applied_screen_);
    if (!orbit_changed && !screen_changed) {
        return;
    }

    if (orbit_changed) {
        write_orbit_settings_to_json(orbit);
        set_orbit_sliders(orbit);
    }
    if (screen_changed) {
        write_screen_settings_to_json(screen);
        set_screen_sliders(screen);
    }

    bool ok = commit_light_changes();
    if (ok) {
        if (orbit_changed) {
            last_applied_orbit_ = orbit;
        }
        if (screen_changed) {
            last_applied_screen_ = screen;
        }
    }
}

bool MapLightPanel::commit_light_changes() {
    if (!map_info_) {
        return false;
    }
    if (!map_info_->is_object()) {
        *map_info_ = json::object();
    }
    (*map_info_)["map_light_data"] = ensure_light();

    bool ok = true;
    if (on_save_) {
        ok = on_save_();
    }
    update_save_status(ok);
    return ok;
}

void MapLightPanel::clamp_key_index() {
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) {
        L["keys"] = json::array();
        L["keys"].push_back(json::array({ 0.0, L["base_color"] }));
        n = 1;
    }
    current_key_index_ = clamp_int(current_key_index_, 0, std::max(0, n-1));

    std::ostringstream oss;
    oss << "Key " << (current_key_index_ + 1) << " / " << n;
    current_key_label_ = oss.str();
}

void MapLightPanel::select_prev_key() {
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) return;
    current_key_index_ = (current_key_index_ - 1 + n) % n;
    sync_ui_from_json();
}

void MapLightPanel::select_next_key() {
    json& L = ensure_light();
    int n = (int)L["keys"].size();
    if (n <= 0) return;
    current_key_index_ = (current_key_index_ + 1) % n;
    sync_ui_from_json();
}

void MapLightPanel::add_key_pair_at_current_angle() {
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

    std::sort(keys.begin(), keys.end(), [](const json& A, const json& B){
        double a0 = 0.0, b0 = 0.0;
        try { a0 = A[0].get<double>(); } catch(...) {}
        try { b0 = B[0].get<double>(); } catch(...) {}
        return a0 < b0;
    });

    for (int i=0;i<(int)keys.size();++i) {
        try {
            if ((int)std::round(keys[i][0].get<double>()) == ang) {
                current_key_index_ = i;
                break;
            }
        } catch(...) {}
    }

    needs_sync_to_json_ = true;
    sync_ui_from_json();
}

void MapLightPanel::delete_current_key() {
    json& L = ensure_light();
    auto& keys = L["keys"];
    if (keys.size() <= 1) return;
    if (current_key_index_ < 0 || current_key_index_ >= (int)keys.size()) return;
    keys.erase(keys.begin() + current_key_index_);
    if (current_key_index_ >= (int)keys.size()) current_key_index_ = (int)keys.size() - 1;

    needs_sync_to_json_ = true;
    sync_ui_from_json();
}

void MapLightPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_) return;

    DockableCollapsible::update(input, screen_w, screen_h);

    apply_immediate_settings();

}

bool MapLightPanel::handle_event(const SDL_Event& e) {
    if (!visible_) return false;

    bool used = DockableCollapsible::handle_event(e);

    if (used) {
        needs_sync_to_json_ = true;
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

void MapLightPanel::update_save_status(bool success) const {
    if (!warning_label_) {
        return;
    }
    const std::string failure_message = "Failed to save map lighting changes. Check logs.";
    if (success) {
        if (!persistence_warning_text_.empty()) {
            persistence_warning_text_.clear();
            warning_label_->set_text({});
            const_cast<MapLightPanel*>(this)->layout();
        }
        return;
    }
    if (persistence_warning_text_ != failure_message) {
        persistence_warning_text_ = failure_message;
        warning_label_->set_text(persistence_warning_text_);
        const_cast<MapLightPanel*>(this)->layout();
    }
}

void MapLightPanel::render_content(SDL_Renderer* r) const {

    if (!r) return;

    if (!editing_light_.is_object()) return;
    const json& L = editing_light_;
    auto keys_it = L.find("keys");
    if (keys_it == L.end() || !keys_it->is_array()) return;
    const auto& keys = *keys_it;
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

    SDL_Rect swatch = body_viewport_;
    swatch.y += std::max(0, swatch.h - 24);
    swatch.h = 16;
    swatch.w = std::min(120, swatch.w);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, r_out, g_out, b_out, a_out);
    SDL_RenderFillRect(r, &swatch);

    const SDL_Color border = DMStyles::Border();
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(r, &swatch);

}

