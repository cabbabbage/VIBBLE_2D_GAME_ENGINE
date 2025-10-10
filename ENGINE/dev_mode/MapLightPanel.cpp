#include "MapLightPanel.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>
#include <optional>
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

    update_btn_ = std::make_unique<DMButton>("Update Light", &DMStyles::AccentButton(), 160, DMButton::height());
    orbit_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    screen_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    texture_section_btn_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), 220, DMButton::height());
    update_section_header_labels();

    radius_         = std::make_unique<DMSlider>("Radius",          0, 20000, 0);
    intensity_      = std::make_unique<DMSlider>("Intensity",       0,   255, 255);
    orbit_x_        = std::make_unique<DMSlider>("Orbit X Radius",  0, 20000, 0);
    orbit_y_        = std::make_unique<DMSlider>("Orbit Y Radius",  0, 20000, 0);
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

    if (update_interval_) update_interval_->set_defer_commit_until_unfocus(true);
    if (orbit_x_)        orbit_x_->set_defer_commit_until_unfocus(true);
    if (orbit_y_)        orbit_y_->set_defer_commit_until_unfocus(true);
    if (min_opacity_)    min_opacity_->set_defer_commit_until_unfocus(true);
    if (max_opacity_)    max_opacity_->set_defer_commit_until_unfocus(true);
    if (screen_r_)       screen_r_->set_defer_commit_until_unfocus(true);
    if (screen_g_)       screen_g_->set_defer_commit_until_unfocus(true);
    if (screen_b_)       screen_b_->set_defer_commit_until_unfocus(true);
    if (screen_min_opacity_) screen_min_opacity_->set_defer_commit_until_unfocus(true);
    if (screen_max_opacity_) screen_max_opacity_->set_defer_commit_until_unfocus(true);

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

    rebuild_rows();
}

void MapLightPanel::update_section_header_labels() {
    auto label_for = [](const std::string& title, bool collapsed) {
        return std::string(collapsed ? "[+] " : "[-] ") + title;
};
    if (orbit_section_btn_) {
        orbit_section_btn_->set_text(label_for("Orbit Settings", orbit_section_collapsed_));
    }
    if (screen_section_btn_) {
        screen_section_btn_->set_text(label_for("Screen Light", screen_section_collapsed_));
    }
    if (texture_section_btn_) {
        texture_section_btn_->set_text(label_for("Map Light Texture", texture_section_collapsed_));
    }
}

void MapLightPanel::rebuild_rows() {
    update_section_header_labels();

    widget_wrappers_.clear();
    widget_wrappers_.reserve(48);

    auto add_widget = [this](std::unique_ptr<Widget> w) -> Widget* {
        Widget* raw = w.get();
        widget_wrappers_.push_back(std::move(w));
        return raw;
};

    Rows rows;

    auto warning_label = std::make_unique<WarningLabel>();
    warning_label_ = warning_label.get();
    warning_label_->set_color(SDL_Color{255, 120, 120, 255});
    if (!persistence_warning_text_.empty()) {
        warning_label_->set_text(persistence_warning_text_);
    }
    rows.push_back({ add_widget(std::move(warning_label)) });

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(orbit_section_btn_.get(), [this]() { toggle_orbit_section(); })) });
    if (!orbit_section_collapsed_) {
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(update_interval_.get())),
            add_widget(std::make_unique<SliderWidget>(orbit_x_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(orbit_y_.get()))
        });
        rows.push_back({
            add_widget(std::make_unique<SliderWidget>(min_opacity_.get())),
            add_widget(std::make_unique<SliderWidget>(max_opacity_.get()))
        });
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(screen_section_btn_.get(), [this]() { toggle_screen_section(); })) });
    if (!screen_section_collapsed_) {
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
    }

    rows.push_back({ add_widget(std::make_unique<ButtonWidget>(texture_section_btn_.get(), [this]() { toggle_texture_section(); })) });
    if (!texture_section_collapsed_) {
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
            add_widget(std::make_unique<ButtonWidget>(prev_key_btn_.get(), [this]() { select_prev_key(); })),
            add_widget(std::make_unique<ButtonWidget>(next_key_btn_.get(), [this]() { select_next_key(); })),
            add_widget(std::make_unique<ButtonWidget>(add_pair_btn_.get(), [this]() { add_key_pair_at_current_angle(); })),
            add_widget(std::make_unique<ButtonWidget>(delete_btn_.get(), [this]() { delete_current_key(); }))
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
            add_widget(std::make_unique<ButtonWidget>(update_btn_.get(), [this]() { apply_changes(); }))
        });
    }

    set_rows(rows);
}

void MapLightPanel::toggle_orbit_section() {
    orbit_section_collapsed_ = !orbit_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::toggle_screen_section() {
    screen_section_collapsed_ = !screen_section_collapsed_;
    rebuild_rows();
}

void MapLightPanel::toggle_texture_section() {
    texture_section_collapsed_ = !texture_section_collapsed_;
    rebuild_rows();
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

    auto parse_int = [](const json& value, int fallback) -> std::optional<int> {
        try {
            if (value.is_number_integer()) {
                return value.get<int>();
            }
            if (value.is_number_float()) {
                return static_cast<int>(std::lround(value.get<double>()));
            }
            if (value.is_string()) {
                const std::string text = value.get<std::string>();
                size_t idx = 0;
                int parsed = std::stoi(text, &idx);
                if (idx == text.size()) {
                    return parsed;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
};

    auto read_int = [&](const char* key, int fallback, int lo, int hi) {
        int value = fallback;
        auto it = L.find(key);
        if (it != L.end()) {
            if (auto parsed = parse_int(*it, fallback)) {
                value = *parsed;
            }
        }
        return clamp_int(value, lo, hi);
};

    auto parse_double = [](const json& value, double fallback) -> std::optional<double> {
        try {
            if (value.is_number_float()) {
                return value.get<double>();
            }
            if (value.is_number_integer()) {
                return static_cast<double>(value.get<int>());
            }
            if (value.is_string()) {
                const std::string text = value.get<std::string>();
                size_t idx = 0;
                double parsed = std::stod(text, &idx);
                if (idx == text.size()) {
                    return parsed;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
};

    auto read_double = [&](const char* key, double fallback, double lo, double hi) {
        double value = fallback;
        auto it = L.find(key);
        if (it != L.end()) {
            if (auto parsed = parse_double(*it, fallback)) {
                value = *parsed;
            }
        }
        return std::clamp(value, lo, hi);
};

    L["radius"] = read_int("radius", 0, 0, 20000);
    L["intensity"] = read_int("intensity", 255, 0, 255);
    L["fall_off"] = read_int("fall_off", 100, 0, 100);
    L["update_interval"] = read_int("update_interval", 10, 1, 120);

    double mult = read_double("mult", 0.0, 0.0, 1.0);
    L["mult"] = mult;

    int min_opacity = read_int("min_opacity", 0, 0, 255);
    int max_opacity = read_int("max_opacity", 255, 0, 255);
    if (min_opacity > max_opacity) {
        std::swap(min_opacity, max_opacity);
    }
    L["min_opacity"] = min_opacity;
    L["max_opacity"] = max_opacity;

    auto read_radius = [&](const char* key) -> std::optional<int> {
        auto it = L.find(key);
        if (it == L.end()) {
            return std::nullopt;
        }
        if (auto parsed = parse_int(*it, 0)) {
            return clamp_int(*parsed, 0, 20000);
        }
        return std::nullopt;
};

    const int fallback_orbit = read_radius("orbit_radius").value_or(0);
    int orbit_x = read_radius("orbit_x").value_or(fallback_orbit);
    int orbit_y = read_radius("orbit_y").value_or(orbit_x);
    orbit_x = clamp_int(orbit_x, 0, 20000);
    orbit_y = clamp_int(orbit_y, 0, 20000);
    L["orbit_x"] = orbit_x;
    L["orbit_y"] = orbit_y;
    L["orbit_radius"] = std::max(orbit_x, orbit_y);

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
    const int fallback_orbit = clamp_int(L.value("orbit_radius", 0), 0, 20000);
    orbit.orbit_x = clamp_int(L.value("orbit_x", fallback_orbit), 0, 20000);
    orbit.orbit_y = clamp_int(L.value("orbit_y", orbit.orbit_x), 0, 20000);
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
    out.orbit_x = clamp_int(out.orbit_x, 0, 20000);
    out.orbit_y = clamp_int(out.orbit_y, 0, 20000);
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
    current.orbit_x = orbit_x_ ? orbit_x_->value() : 0;
    current.orbit_y = orbit_y_ ? orbit_y_->value() : current.orbit_x;
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
    if (orbit_x_)         orbit_x_->set_value(orbit.orbit_x);
    if (orbit_y_)         orbit_y_->set_value(orbit.orbit_y);
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
    L["orbit_x"] = orbit.orbit_x;
    L["orbit_y"] = orbit.orbit_y;
    L["orbit_radius"] = std::max(orbit.orbit_x, orbit.orbit_y);
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

