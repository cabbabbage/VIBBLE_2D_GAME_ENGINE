#include "asset_info_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <SDL_log.h>
#include <SDL_ttf.h>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>
#include <functional>

#include "asset/asset_info.hpp"
#include "utils/input.hpp"
#include "utils/area.hpp"

#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "asset_sections/Section_BasicInfo.hpp"
#include "asset_sections/Section_Tags.hpp"
#include "asset_sections/Section_Lighting.hpp"
#include "asset_sections/Section_Areas.hpp"
#include "asset_sections/Section_Spacing.hpp"
#include "asset_sections/Section_ChildAssets.hpp"
#include "widgets.hpp"
#include "core/AssetsManager.hpp"
#include "animations_editor_panel.hpp"
#include "asset/Asset.hpp"
#include "render/camera.hpp"
#include "utils/light_source.hpp"

namespace {

void render_label_text(SDL_Renderer* renderer, const std::string& text, int x, int y) {
    if (!renderer || text.empty()) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (!surf) {
        TTF_CloseFont(font);
        return;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        SDL_Rect dst{x, y, surf->w, surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    TTF_CloseFont(font);
}

class ApplySettingsModal {
public:
    using ApplyCallback = std::function<bool(const std::vector<std::string>&)>;

    void open(AssetInfoSectionId, std::string heading, ApplyCallback cb) {
        heading_ = std::move(heading);
        callback_ = std::move(cb);
        visible_ = true;
        scroll_offset_ = 0;
        entries_.clear();
        load_entries();
        if (!apply_btn_) {
            apply_btn_ = std::make_unique<DMButton>("Apply", &DMStyles::AccentButton(), 120, DMButton::height());
        }
        if (!cancel_btn_) {
            cancel_btn_ = std::make_unique<DMButton>("Cancel", &DMStyles::ListButton(), 120, DMButton::height());
        }
    }

    void close() {
        visible_ = false;
        callback_ = nullptr;
    }

    bool is_open() const { return visible_; }

    void update(const Input&, int screen_w, int screen_h) {
        if (!visible_) return;
        screen_w_ = screen_w;
        screen_h_ = screen_h;
        layout();
    }

    bool handle_event(const SDL_Event& e) {
        if (!visible_) return false;
        layout();

        const bool pointer_event =
            (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
        const bool wheel_event = (e.type == SDL_MOUSEWHEEL);
        SDL_Point pointer{0, 0};
        if (pointer_event) {
            pointer.x = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
            pointer.y = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
        } else if (wheel_event) {
            SDL_GetMouseState(&pointer.x, &pointer.y);
        }

        const bool inside_panel = SDL_PointInRect(&pointer, &panel_rect_);
        const bool inside_list = SDL_PointInRect(&pointer, &list_rect_);

        if (pointer_event && !inside_panel && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            close();
            return true;
        }

        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            close();
            return true;
        }

        if (apply_btn_ && apply_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (callback_) {
                    if (callback_(selected_assets())) {
                        close();
                    }
                } else {
                    close();
                }
            }
            return true;
        }

        if (cancel_btn_ && cancel_btn_->handle_event(e)) {
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                close();
            }
            return true;
        }

        if (wheel_event && inside_list) {
            scroll_by(-e.wheel.y * 40);
            return true;
        }

        bool used = false;
        if (inside_panel || (!pointer_event && !wheel_event)) {
            for (auto& entry : entries_) {
                if (entry.checkbox && entry.checkbox->handle_event(e)) {
                    used = true;
                    break;
                }
            }
        }

        if (used) return true;
        if (pointer_event && inside_panel) return true;
        if (wheel_event && inside_panel) return true;
        return used;
    }

    void render(SDL_Renderer* renderer) const {
        if (!visible_ || !renderer) return;
        layout();

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        SDL_RenderFillRect(renderer, nullptr);

        const SDL_Color bg = DMStyles::PanelBG();
        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(renderer, &panel_rect_);
        const SDL_Color border = DMStyles::Border();
        SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(renderer, &panel_rect_);

        render_label_text(renderer, heading_, heading_rect_.x, heading_rect_.y);

        if (apply_btn_) apply_btn_->render(renderer);
        if (cancel_btn_) cancel_btn_->render(renderer);

        SDL_Rect prev_clip;
        SDL_RenderGetClipRect(renderer, &prev_clip);
        SDL_RenderSetClipRect(renderer, &list_rect_);
        for (const auto& entry : entries_) {
            if (entry.checkbox) entry.checkbox->render(renderer);
        }
        SDL_RenderSetClipRect(renderer, &prev_clip);
    }

private:
    struct Entry {
        std::string name;
        std::unique_ptr<DMCheckbox> checkbox;
    };

    void load_entries() {
        namespace fs = std::filesystem;
        fs::path src_root{"SRC"};
        try {
            if (fs::exists(src_root)) {
                for (const auto& dir : fs::directory_iterator(src_root)) {
                    if (!dir.is_directory()) continue;
                    fs::path info_path = dir.path() / "info.json";
                    if (!fs::exists(info_path)) continue;
                    Entry entry;
                    entry.name = dir.path().filename().string();
                    entry.checkbox = std::make_unique<DMCheckbox>(entry.name, false);
                    entries_.push_back(std::move(entry));
                }
            }
        } catch (...) {
            entries_.clear();
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return a.name < b.name;
        });
    }

    void layout() const {
        if (!visible_ || screen_w_ <= 0 || screen_h_ <= 0) return;
        const int padding = DMSpacing::panel_padding();
        const int max_panel_w = std::max(260, std::min(screen_w_ - 2 * padding, 520));
        const int max_panel_h = std::max(240, std::min(screen_h_ - 2 * padding, 640));
        panel_rect_.w = std::clamp(max_panel_w, 260, std::max(260, screen_w_ - 2 * padding));
        panel_rect_.h = std::clamp(max_panel_h, 240, std::max(240, screen_h_ - 2 * padding));
        panel_rect_.x = (screen_w_ - panel_rect_.w) / 2;
        panel_rect_.y = (screen_h_ - panel_rect_.h) / 2;

        heading_rect_ = SDL_Rect{ panel_rect_.x + padding,
                                  panel_rect_.y + padding,
                                  panel_rect_.w - 2 * padding,
                                  DMStyles::Label().font_size + 4 };
        int y = heading_rect_.y + heading_rect_.h + DMSpacing::item_gap();

        if (apply_btn_) {
            apply_btn_->set_rect(SDL_Rect{ panel_rect_.x + padding, y, 120, DMButton::height() });
        }
        if (cancel_btn_) {
            int cancel_x = panel_rect_.x + padding;
            if (apply_btn_) cancel_x = apply_btn_->rect().x + apply_btn_->rect().w + DMSpacing::item_gap();
            cancel_btn_->set_rect(SDL_Rect{ cancel_x, y, 120, DMButton::height() });
        }
        y += DMButton::height() + DMSpacing::item_gap();

        list_rect_ = SDL_Rect{ panel_rect_.x + padding,
                               y,
                               panel_rect_.w - 2 * padding,
                               panel_rect_.h - (y - panel_rect_.y) - padding };
        if (list_rect_.h < 0) list_rect_.h = 0;

        int entry_y = list_rect_.y - scroll_offset_;
        for (auto& entry : entries_) {
            if (!entry.checkbox) continue;
            entry.checkbox->set_rect(SDL_Rect{ list_rect_.x, entry_y, list_rect_.w, DMCheckbox::height() });
            entry_y += DMCheckbox::height() + DMSpacing::item_gap();
        }
        int total_height = entry_y - (list_rect_.y - scroll_offset_);
        max_scroll_ = std::max(0, total_height - list_rect_.h);
        if (scroll_offset_ > max_scroll_) {
            scroll_offset_ = max_scroll_;
            entry_y = list_rect_.y - scroll_offset_;
            for (auto& entry : entries_) {
                if (!entry.checkbox) continue;
                entry.checkbox->set_rect(SDL_Rect{ list_rect_.x, entry_y, list_rect_.w, DMCheckbox::height() });
                entry_y += DMCheckbox::height() + DMSpacing::item_gap();
            }
        }
    }

    void scroll_by(int delta) {
        if (delta == 0) return;
        scroll_offset_ = std::clamp(scroll_offset_ + delta, 0, max_scroll_);
        layout();
    }

    std::vector<std::string> selected_assets() const {
        std::vector<std::string> names;
        for (const auto& entry : entries_) {
            if (entry.checkbox && entry.checkbox->value()) {
                names.push_back(entry.name);
            }
        }
        return names;
    }

private:
    std::string heading_;
    ApplyCallback callback_{};
    std::vector<Entry> entries_;
    std::unique_ptr<DMButton> apply_btn_;
    std::unique_ptr<DMButton> cancel_btn_;
    mutable SDL_Rect panel_rect_{0,0,0,0};
    mutable SDL_Rect heading_rect_{0,0,0,0};
    mutable SDL_Rect list_rect_{0,0,0,0};
    mutable int scroll_offset_ = 0;
    mutable int max_scroll_ = 0;
    int screen_w_ = 0;
    int screen_h_ = 0;
    bool visible_ = false;
};

bool load_json_file(const std::filesystem::path& path, nlohmann::json& out) {
    std::ifstream in(path);
    if (!in) {
        SDL_Log("Failed to open %s", path.string().c_str());
        return false;
    }
    try {
        in >> out;
        if (!out.is_object()) {
            out = nlohmann::json::object();
        }
        return true;
    } catch (const std::exception& ex) {
        SDL_Log("Failed to parse %s: %s", path.string().c_str(), ex.what());
        return false;
    }
}

bool write_json_file(const std::filesystem::path& path, const nlohmann::json& data) {
    std::ofstream out(path);
    if (!out) {
        SDL_Log("Failed to write %s", path.string().c_str());
        return false;
    }
    try {
        out << data.dump(4);
        return true;
    } catch (const std::exception& ex) {
        SDL_Log("Failed to serialize %s: %s", path.string().c_str(), ex.what());
        return false;
    }
}

bool copy_section_from_source(AssetInfoSectionId section_id, const nlohmann::json& source, nlohmann::json& target) {
    if (!target.is_object()) return false;
    bool changed = false;
    auto copy_key = [&](const char* key) {
        auto it = source.find(key);
        if (it != source.end()) {
            if (!target.contains(key) || target[key] != *it) {
                target[key] = *it;
                return true;
            }
        } else if (target.contains(key)) {
            target.erase(key);
            return true;
        }
        return false;
    };

    switch (section_id) {
        case AssetInfoSectionId::BasicInfo: {
            changed |= copy_key("asset_type");
            if (source.contains("size_settings") && source["size_settings"].is_object()) {
                if (!target.contains("size_settings") || target["size_settings"] != source["size_settings"]) {
                    target["size_settings"] = source["size_settings"];
                    changed = true;
                }
            } else if (target.contains("size_settings")) {
                target.erase("size_settings");
                changed = true;
            }
            changed |= copy_key("z_threshold");
            changed |= copy_key("can_invert");
            break;
        }
        case AssetInfoSectionId::Tags:
            changed |= copy_key("tags");
            changed |= copy_key("anti_tags");
            break;
        case AssetInfoSectionId::Lighting:
            changed |= copy_key("has_shading");
            changed |= copy_key("lighting_info");
            break;
        case AssetInfoSectionId::Spacing:
            changed |= copy_key("min_same_type_distance");
            changed |= copy_key("min_distance_all");
            break;
        case AssetInfoSectionId::Areas:
            changed |= copy_key("areas");
            break;
        case AssetInfoSectionId::ChildAssets:
            changed |= copy_key("child_assets");
            break;
    }
    return changed;
}

} // namespace

AssetInfoUI::AssetInfoUI() {
    auto basic = std::make_unique<Section_BasicInfo>();
    basic_info_section_ = basic.get();
    basic_info_section_->set_ui(this);
    sections_.push_back(std::move(basic));
    auto tags = std::make_unique<Section_Tags>();
    tags->set_ui(this);
    sections_.push_back(std::move(tags));
    auto lighting = std::make_unique<Section_Lighting>();
    lighting->set_ui(this);
    lighting_section_ = lighting.get();
    sections_.push_back(std::move(lighting));
    auto spacing = std::make_unique<Section_Spacing>();
    spacing->set_ui(this);
    sections_.push_back(std::move(spacing));
    auto areas = std::make_unique<Section_Areas>();
    areas_section_ = areas.get();
    areas_section_->set_open_editor_callback([this](const std::string& nm){ open_area_editor(nm); });
    areas_section_->set_delete_callback([this](const std::string& nm){
        if (!info_) return;
        if (info_->remove_area(nm)) {
            (void)info_->update_info_json();
        }
    });
    areas_section_->set_ui(this);
    sections_.push_back(std::move(areas));
    auto children = std::make_unique<Section_ChildAssets>();
    children->set_open_area_editor_callback([this](const std::string& nm){ open_area_editor(nm); });
    children->set_ui(this);
    sections_.push_back(std::move(children));
    // Configure Animations footer button
    configure_btn_ = std::make_unique<DMButton>("Configure Animations", &DMStyles::CreateButton(), 220, DMButton::height());
    animations_panel_ = std::make_unique<AnimationsEditorPanel>();
}

AssetInfoUI::~AssetInfoUI() {
    apply_camera_override(false);
}

void AssetInfoUI::set_assets(Assets* a) {
    if (assets_ == a) return;
    if (camera_override_active_) {
        apply_camera_override(false);
    }
    assets_ = a;
    if (visible_) {
        apply_camera_override(true);
    }
}

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    scroll_ = 0;
    if (apply_modal_) apply_modal_->close();
    for (auto& s : sections_) {
        s->set_info(info_);
        s->reset_scroll();
        s->build();
    }
}

void AssetInfoUI::clear_info() {
    info_.reset();
    scroll_ = 0;
    if (apply_modal_) apply_modal_->close();
    for (auto& s : sections_) {
        s->set_info(nullptr);
        s->reset_scroll();
        s->build();
    }
    target_asset_ = nullptr;
}

void AssetInfoUI::open()  {
    visible_ = true;
    apply_camera_override(true);
    for (auto& s : sections_) s->set_expanded(false);
}
void AssetInfoUI::close() {
    if (!visible_) return;
    apply_camera_override(false);
    visible_ = false;
    if (apply_modal_) apply_modal_->close();
}
void AssetInfoUI::toggle(){
    if (visible_) {
        close();
    } else {
        open();
    }
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
    int panel_x = (screen_w * 2) / 3;
    int panel_w = screen_w - panel_x;
    panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };
    const int padding = DMSpacing::panel_padding();
    const int gap = DMSpacing::section_gap();
    const int content_x = panel_.x + padding;
    const int content_w = panel_.w - 2 * padding;
    const int content_top = panel_.y + padding;

    const int label_height = DMButton::height();
    const int label_gap = DMSpacing::item_gap();
    name_label_rect_ = SDL_Rect{ content_x, content_top, content_w, label_height };
    int scroll_start = content_top + label_height + label_gap;

    auto layout_with_scroll = [&](int scroll_value) {
        int y = scroll_start;
        for (auto& s : sections_) {
            s->set_rect(SDL_Rect{ content_x, y - scroll_value, content_w, 0 });
            y += s->height() + gap;
        }
        if (configure_btn_) {
            configure_btn_->set_rect(SDL_Rect{ content_x, y - scroll_value, content_w, DMButton::height() });
            y += DMButton::height() + gap;
        }
        return y;
    };

    int end_y = layout_with_scroll(scroll_);
    int content_height = end_y - scroll_start;
    int visible_height = panel_.h - padding - label_height - label_gap;
    max_scroll_ = std::max(0, content_height - std::max(0, visible_height));
    int clamped = std::max(0, std::min(max_scroll_, scroll_));
    if (clamped != scroll_) {
        scroll_ = clamped;
        end_y = layout_with_scroll(scroll_);
        content_height = end_y - scroll_start;
        max_scroll_ = std::max(0, content_height - std::max(0, visible_height));
    }

    scroll_region_ = SDL_Rect{
        panel_.x,
        name_label_rect_.y + name_label_rect_.h,
        panel_.w,
        std::max(0, panel_.h - (name_label_rect_.y + name_label_rect_.h))
    };
}



void AssetInfoUI::handle_event(const SDL_Event& e) {
    if (apply_modal_ && apply_modal_->is_open()) {
        if (apply_modal_->handle_event(e)) return;
        if (apply_modal_->is_open()) return;
    }

    if (!visible_ || !info_) return;

    if (animations_panel_ && animations_panel_->is_open() && animations_panel_->handle_event(e))
        return;

    bool pointer_inside = false;
    const bool pointer_event =
        (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION);
    if (pointer_event) {
        SDL_Point p{
            e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x,
            e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y
        };
        pointer_inside = SDL_PointInRect(&p, &panel_);
        if (!pointer_inside) {
            return;
        }
    } else if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0;
        int my = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_Point p{mx, my};
        pointer_inside = SDL_PointInRect(&p, &scroll_region_);
        if (!pointer_inside) {
            return;
        }
    }

    if (e.type == SDL_MOUSEWHEEL) {
        scroll_ -= e.wheel.y * 40;
        scroll_ = std::max(0, std::min(max_scroll_, scroll_));
        return;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        close();
        return;
    }

    for (auto& s : sections_) {
        if (s->handle_event(e)) return;
    }

    if (configure_btn_ && configure_btn_->handle_event(e)) {
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (info_) {
                try {
                    std::string cmd = std::string("python scripts/animation_ui.py \"") + info_->info_json_path() + "\"";
                    int rc = std::system(cmd.c_str());
                    if (rc != 0) {
                        SDL_Log("animation_ui.py exited with code %d", rc);
                    }
                } catch (const std::exception& ex) {
                    SDL_Log("Failed to launch animation_ui.py: %s", ex.what());
                }
            }
        }
        return;
    }

    if (pointer_inside) {
        return;
    }
}



void AssetInfoUI::update(const Input& input, int screen_w, int screen_h) {
    if (!visible_ || !info_) return;
    layout_widgets(screen_w, screen_h);

    if (apply_modal_ && apply_modal_->is_open()) {
        apply_modal_->update(input, screen_w, screen_h);
        return;
    }

    int mx = input.getX();
    int my = input.getY();
    if (mx >= scroll_region_.x && mx < scroll_region_.x + scroll_region_.w &&
        my >= scroll_region_.y && my < scroll_region_.y + scroll_region_.h) {
        int dy = input.getScrollY();
        if (dy != 0) {
            scroll_ -= dy * 40;
            scroll_ = std::max(0, std::min(max_scroll_, scroll_));
        }
    }

    for (auto& s : sections_) s->update(input, screen_w, screen_h);

    // Accordion behavior: only one open at a time
    for (size_t i = 0; i < sections_.size(); ++i) {
        if (sections_[i]->is_expanded()) {
            for (size_t j = 0; j < sections_.size(); ++j) {
                if (i != j) sections_[j]->set_expanded(false);
            }
            break;
        }
    }

    if (pulse_frames_ > 0) {
        --pulse_frames_;
    }

    layout_widgets(screen_w, screen_h);

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->update(input, screen_w, screen_h);
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!visible_ || !info_) return;

    layout_widgets(screen_w, screen_h);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Color bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &panel_);

    if (info_) {
        render_label_text(r, info_->name, name_label_rect_.x, name_label_rect_.y);
    }

    if (pulse_frames_ > 0) {
        Uint8 alpha = static_cast<Uint8>(std::clamp(pulse_frames_ * 12, 0, 180));
        SDL_Rect header_rect{panel_.x, panel_.y, panel_.w, DMButton::height()};
        SDL_SetRenderDrawColor(r, 255, 220, 64, alpha);
        SDL_RenderFillRect(r, &header_rect);
    }

    SDL_Rect prev_clip;
    SDL_RenderGetClipRect(r, &prev_clip);
#if SDL_VERSION_ATLEAST(2,0,4)
    const SDL_bool was_clipping = SDL_RenderIsClipEnabled(r);
#else
    const SDL_bool was_clipping = (prev_clip.w != 0 || prev_clip.h != 0) ? SDL_TRUE : SDL_FALSE;
#endif
    SDL_RenderSetClipRect(r, &panel_);

    // Render sections (already offset by scroll_)
    for (auto& s : sections_) s->render(r);

    // Footer button
    if (configure_btn_) configure_btn_->render(r);

    if (was_clipping == SDL_TRUE) {
        SDL_RenderSetClipRect(r, &prev_clip);
    } else {
        SDL_RenderSetClipRect(r, nullptr);
    }

    if (animations_panel_ && animations_panel_->is_open())
        animations_panel_->render(r, screen_w, screen_h);

    if (apply_modal_ && apply_modal_->is_open())
        apply_modal_->render(r);

    last_renderer_ = r;
}

void AssetInfoUI::pulse_header() {
    pulse_frames_ = 20;
}

void AssetInfoUI::apply_camera_override(bool enable) {
    if (!assets_) return;
    camera& cam = assets_->getView();
    if (enable) {
        if (camera_override_active_) return;
        prev_camera_realism_enabled_ = cam.realism_enabled();
        prev_camera_parallax_enabled_ = cam.parallax_enabled();
        cam.set_realism_enabled(false);
        cam.set_parallax_enabled(false);
        camera_override_active_ = true;
    } else {
        if (!camera_override_active_) return;
        cam.set_realism_enabled(prev_camera_realism_enabled_);
        cam.set_parallax_enabled(prev_camera_parallax_enabled_);
        camera_override_active_ = false;
    }
}

float AssetInfoUI::compute_player_screen_height(const camera& cam) const {
    if (!assets_ || !assets_->player) return 1.0f;
    Asset* player_asset = assets_->player;
    if (!player_asset) return 1.0f;

    SDL_Texture* player_final = player_asset->get_final_texture();
    SDL_Texture* player_frame = player_asset->get_current_frame();
    int pw = player_asset->cached_w;
    int ph = player_asset->cached_h;
    if ((pw == 0 || ph == 0) && player_final) {
        SDL_QueryTexture(player_final, nullptr, nullptr, &pw, &ph);
    }
    if ((pw == 0 || ph == 0) && player_frame) {
        SDL_QueryTexture(player_frame, nullptr, nullptr, &pw, &ph);
    }
    if (pw != 0) player_asset->cached_w = pw;
    if (ph != 0) player_asset->cached_h = ph;

    float scale = cam.get_scale();
    float inv_scale = (scale > 0.0f) ? (1.0f / scale) : 1.0f;
    if (ph > 0) {
        float screen_h = static_cast<float>(ph) * inv_scale;
        return screen_h > 0.0f ? screen_h : 1.0f;
    }
    return 1.0f;
}





void AssetInfoUI::render_world_overlay(SDL_Renderer* r, const camera& cam) const {
    if (!visible_ || !info_) return;

    float reference_screen_height = compute_player_screen_height(cam);

    if (basic_info_section_ && basic_info_section_->is_expanded()) {
        basic_info_section_->render_world_overlay(r, cam, target_asset_, reference_screen_height);
    }

    if (!lighting_section_ || !lighting_section_->is_expanded() || !lighting_section_->shading_enabled() || !target_asset_) return;
    const LightSource& light = lighting_section_->shading_light();
    if (light.x_radius <= 0 && light.y_radius <= 0) return;
    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    const bool flipped = target_asset_->flipped;
    const int base_offset_x = flipped ? -light.offset_x : light.offset_x;
    for (int deg = 0; deg < 360; ++deg) {
        double rad = deg * M_PI / 180.0;
        double cx = std::cos(rad) * static_cast<double>(light.x_radius);
        double cy = std::sin(rad) * static_cast<double>(light.y_radius);
        if (flipped) cx = -cx;
        int wx = target_asset_->pos.x + base_offset_x + static_cast<int>(std::llround(cx));
        int wy = target_asset_->pos.y + light.offset_y - static_cast<int>(std::llround(cy));
        SDL_Point p = cam.compute_render_effects(SDL_Point{wx, wy}, 0.0f, 0.0f).screen_position;
        SDL_RenderDrawPoint(r, p.x, p.y);
    }
}

void AssetInfoUI::refresh_target_asset_scale() {
    if (!info_ || !target_asset_) return;
    SDL_Renderer* renderer = last_renderer_;
    if (!renderer) return;
    info_->loadAnimations(renderer);
    target_asset_->finalize_setup();
    target_asset_->set_final_texture(nullptr);
    target_asset_->cached_w = 0;
    target_asset_->cached_h = 0;
}

void AssetInfoUI::sync_target_z_threshold() {
    if (!target_asset_) return;
    target_asset_->set_z_index();
}

void AssetInfoUI::request_apply_section(AssetInfoSectionId section_id) {
    if (!info_) return;
    if (!apply_modal_) apply_modal_ = std::make_unique<ApplySettingsModal>();
    std::string heading = "Apply ";
    heading += section_display_name(section_id);
    heading += " Settings";
    apply_modal_->open(section_id, heading, [this, section_id](const std::vector<std::string>& assets) {
        return apply_section_to_assets(section_id, assets);
    });
}

bool AssetInfoUI::apply_section_to_assets(AssetInfoSectionId section_id, const std::vector<std::string>& asset_names) {
    if (!info_) return false;
    if (asset_names.empty()) return true;

    (void)info_->update_info_json();
    nlohmann::json source;
    if (!load_json_file(info_->info_json_path(), source)) {
        return false;
    }

    bool all_success = true;
    for (const auto& name : asset_names) {
        std::filesystem::path path = std::filesystem::path("SRC") / name / "info.json";
        nlohmann::json target;
        if (!load_json_file(path, target)) {
            all_success = false;
            continue;
        }
        if (!copy_section_from_source(section_id, source, target)) {
            continue;
        }
        if (!write_json_file(path, target)) {
            all_success = false;
        }
    }

    if (all_success) {
        pulse_header();
    } else {
        SDL_Log("Some assets failed to receive applied settings.");
    }
    return all_success;
}

const char* AssetInfoUI::section_display_name(AssetInfoSectionId section_id) {
    switch (section_id) {
        case AssetInfoSectionId::BasicInfo:   return "Basic Info";
        case AssetInfoSectionId::Tags:        return "Tags";
        case AssetInfoSectionId::Lighting:    return "Lighting";
        case AssetInfoSectionId::Spacing:     return "Spacing";
        case AssetInfoSectionId::Areas:       return "Areas";
        case AssetInfoSectionId::ChildAssets: return "Child Assets";
    }
    return "Settings";
}

bool AssetInfoUI::is_point_inside(int x, int y) const {
    SDL_Point p{ x, y };
    return visible_ && SDL_PointInRect(&p, &panel_);
}

void AssetInfoUI::save_now() const {
    if (info_) (void)info_->update_info_json();
}

void AssetInfoUI::open_area_editor(const std::string& name) {
    if (!info_ || !assets_) return;
    assets_->begin_area_edit_for_selected_asset(name);
}
