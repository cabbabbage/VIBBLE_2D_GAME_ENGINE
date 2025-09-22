// moved to dev_mode
#include "asset_library_ui.hpp"
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <thread>
#include <cstdlib>
#include <cstdint>
#include "utils/input.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/Asset.hpp"
#include "dm_styles.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <SDL_ttf.h>
#include "core/AssetsManager.hpp"
#include "DockableCollapsible.hpp"
#include "widgets.hpp"

#include <cstdint>   // for std::uintptr_t

namespace {
    const SDL_Color kTileBG  = dm::rgba(40,40,40,180);
    const SDL_Color kTileHL  = dm::rgba(200,200,60,100);
    const SDL_Color kTileBd  = DMStyles::Border();
    namespace fs = std::filesystem;

    TTF_Font* load_font(int size) {
        static std::unordered_map<int, TTF_Font*> cache;
        auto it = cache.find(size);
        if (it != cache.end()) return it->second;

        const DMLabelStyle& label = DMStyles::Label();
        TTF_Font* font = TTF_OpenFont(label.font_path.c_str(), size);
        if (!font) {
            std::cerr << "[AssetLibraryUI] Failed to load font '" << label.font_path
                      << "' size " << size << ": " << TTF_GetError() << "\n";
            return nullptr;
        }
        cache.emplace(size, font);
        return font;
    }

    bool create_new_asset_on_disk(const std::string& name) {
        if (name.empty()) return false;

        fs::path base("SRC");
        fs::path dir = base / name;
        try {
            if (!fs::exists(base)) {
                fs::create_directory(base);
            }
            if (fs::exists(dir)) {
                std::cerr << "[AssetLibraryUI] Asset '" << name << "' already exists\n";
                return false;
            }
            fs::create_directory(dir);

            fs::path info_path = dir / "info.json";
            std::ofstream out(info_path);
            if (!out.is_open()) {
                std::cerr << "[AssetLibraryUI] Failed to create info.json for '" << name << "'\n";
                return false;
            }
            out << "{\n"
                << "  \"asset_name\": \"" << name << "\",\n"
                << "  \"asset_type\": \"Object\",\n"
                << "  \"animations\": {},\n"
                << "  \"start\": \"\"\n"
                << "}\n";
            out.close();

            std::cout << "[AssetLibraryUI] Created new asset '" << name << "' at " << dir << "\n";

            const std::string info_arg = info_path.lexically_normal().generic_string();
            std::thread launcher([info_arg]() {
                try {
                    std::string cmd = std::string("python scripts/animation_ui.py \"") + info_arg + "\"";
                    int rc = std::system(cmd.c_str());
                    if (rc != 0) {
                        std::cerr << "[AssetLibraryUI] animation_ui.py exited with code " << rc << "\n";
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "[AssetLibraryUI] Failed to launch animation_ui.py: " << ex.what() << "\n";
                }
            });
            launcher.detach();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[AssetLibraryUI] Exception creating asset '" << name
                      << "': " << e.what() << "\n";
            return false;
        }
    }
}

// ---------------- Asset Tile ----------------
struct AssetLibraryUI::AssetTileWidget : public Widget {
    AssetLibraryUI* owner = nullptr;
    std::shared_ptr<AssetInfo> info;
    SDL_Rect rect_{0,0,0,0};
    bool hovered = false;
    bool pressed = false;
    bool started_drag = false;
    bool right_pressed = false;
    int  press_x = 0, press_y = 0;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_click;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_right_click;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_begin_drag;

    explicit AssetTileWidget(AssetLibraryUI* owner_ptr,
                             std::shared_ptr<AssetInfo> i,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> right_click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> begin_drag)
        : owner(owner_ptr),
          info(std::move(i)),
          on_click(std::move(click)),
          on_right_click(std::move(right_click)),
          on_begin_drag(std::move(begin_drag)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int /*w*/) const override { return 200; }

    bool handle_event(const SDL_Event& e) override {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.motion.x, e.motion.y };
            hovered = SDL_PointInRect(&p, &rect_);
            if (pressed && !started_drag) {
                int mdx = std::abs(e.motion.x - press_x);
                int mdy = std::abs(e.motion.y - press_y);
                if (mdx + mdy >= 6) {
                    started_drag = true;
                    if (on_begin_drag) on_begin_drag(info);
                    return true;
                }
            }
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            SDL_Point p{ e.button.x, e.button.y };
            if (!SDL_PointInRect(&p, &rect_)) return false;
            if (e.button.button == SDL_BUTTON_LEFT) {
                pressed = true;
                press_x = p.x;
                press_y = p.y;
                return true;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                right_pressed = true;
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP) {
            SDL_Point p{ e.button.x, e.button.y };
            bool inside = SDL_PointInRect(&p, &rect_);
            if (e.button.button == SDL_BUTTON_LEFT) {
                bool was = pressed;
                bool dragged = started_drag;
                pressed = false;
                started_drag = false;
                if (inside && was && !dragged) {
                    if (on_click) on_click(info);
                    return true;
                }
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                bool was = right_pressed;
                right_pressed = false;
                if (inside && was) {
                    if (on_right_click) on_right_click(info);
                    return true;
                }
            }
        }
        return false;
    }

    void render(SDL_Renderer* r) const override {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kTileBG.r, kTileBG.g, kTileBG.b, kTileBG.a);
        SDL_RenderFillRect(r, &rect_);
        const int pad = 8;
        const int label_h = 24;
        SDL_Rect label_rect{ rect_.x + pad, rect_.y + pad, rect_.w - 2 * pad, label_h };

        const AssetInfo* in = info.get();
        std::string label_text = (in && !in->name.empty()) ? in->name : "(Unnamed)";
        TTF_Font* label_font = load_font(15);
        std::string render_label = label_text;
        if (label_font) {
            int tw = 0;
            int th = 0;
            const std::string ellipsis = "...";
            if (TTF_SizeUTF8(label_font, render_label.c_str(), &tw, &th) == 0 && tw > label_rect.w) {
                std::string base = label_text;
                while (!base.empty()) {
                    base.pop_back();
                    std::string candidate = base + ellipsis;
                    if (TTF_SizeUTF8(label_font, candidate.c_str(), &tw, &th) == 0 && tw <= label_rect.w) {
                        render_label = std::move(candidate);
                        break;
                    }
                }
                if (base.empty()) {
                    render_label = ellipsis;
                }
            }
        }

        if (in) {
            SDL_Texture* tex = owner ? owner->get_default_frame_texture(*in) : nullptr;
            if (!tex) {
                auto it = in->animations.find("default");
                if (it == in->animations.end()) it = in->animations.find("start");
                if (it == in->animations.end() && !in->animations.empty()) it = in->animations.begin();
                if (it != in->animations.end() && !it->second.frames.empty()) tex = it->second.frames.front();
            }
            if (tex) {
                int tw=0, th=0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                if (tw > 0 && th > 0) {
                    SDL_Rect image_rect{ rect_.x + pad,
                                         label_rect.y + label_rect.h + pad,
                                         rect_.w - 2 * pad,
                                         rect_.h - (label_rect.h + 3 * pad) };
                    image_rect.h = std::max(image_rect.h, 0);
                    if (image_rect.w > 0 && image_rect.h > 0) {
                        float scale = std::min(image_rect.w / float(tw), image_rect.h / float(th));
                        if (scale > 0.0f) {
                            int dw = int(tw * scale);
                            int dh = int(th * scale);
                            SDL_Rect dst{ image_rect.x + (image_rect.w - dw) / 2,
                                          image_rect.y + (image_rect.h - dh) / 2,
                                          dw,
                                          dh };
                            SDL_RenderCopy(r, tex, nullptr, &dst);
                        }
                    }
                }
            }
        }
        if (hovered) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(r, kTileHL.r, kTileHL.g, kTileHL.b, kTileHL.a);
            SDL_RenderFillRect(r, &rect_);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kTileBd.r, kTileBd.g, kTileBd.b, kTileBd.a);
        SDL_RenderDrawRect(r, &rect_);
        if (label_font) {
            SDL_Color text_color = DMStyles::Label().color;
            SDL_Surface* surf = TTF_RenderUTF8_Blended(label_font, render_label.c_str(), text_color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    int dw = 0, dh = 0;
                    SDL_QueryTexture(tex, nullptr, nullptr, &dw, &dh);
                    SDL_Rect dst{ label_rect.x, label_rect.y + (label_rect.h - dh) / 2, dw, dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }
    }
};

// ---------------- AssetLibraryUI ----------------
AssetLibraryUI::AssetLibraryUI() {
    floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    floating_->set_expanded(false);

    add_button_ = std::make_unique<DMButton>("Create New Asset", &DMStyles::CreateButton(), 200, DMButton::height());
    add_button_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this](){
        showing_create_popup_ = true;
        new_asset_name_.clear();
    });
}

AssetLibraryUI::~AssetLibraryUI() = default;

void AssetLibraryUI::toggle() {
    if (!floating_) return;
    floating_->set_visible(!is_visible());
}

bool AssetLibraryUI::is_visible() const { return floating_ && floating_->is_visible(); }

void AssetLibraryUI::open() {
    if (!floating_) floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    if (floating_) {
        floating_->set_visible(true);
        floating_->set_expanded(true);
    }
}

void AssetLibraryUI::close() {
    if (floating_) floating_->set_visible(false);
}

bool AssetLibraryUI::is_input_blocking() const {
    return (floating_ && floating_->is_expanded()) || dragging_from_library_ || showing_create_popup_;
}

void AssetLibraryUI::ensure_items(AssetLibrary& lib) {
    if (items_cached_) return;
    items_.clear();
    for (const auto& kv : lib.all()) {
        if (kv.second) items_.push_back(kv.second);
    }
    std::sort(items_.begin(), items_.end(), [](const auto& a, const auto& b){
        return (a ? a->name : "") < (b ? b->name : "");
    });
    items_cached_ = true;
}

void AssetLibraryUI::rebuild_rows() {
    if (!floating_) return;
    std::vector<DockableCollapsible::Row> rows;
    if (add_button_widget_) rows.push_back({ add_button_widget_.get() });

    DockableCollapsible::Row current_row;
    current_row.reserve(2);
    for (auto& tw : tiles_) {
        current_row.push_back(tw.get());
        if (current_row.size() == 2) {
            rows.push_back(current_row);
            current_row.clear();
        }
    }
    if (!current_row.empty()) {
        rows.push_back(current_row);
    }

    floating_->set_cell_width(210);
    floating_->set_col_gap(18);
    floating_->set_rows(rows);
}

SDL_Texture* AssetLibraryUI::get_default_frame_texture(const AssetInfo& info) const {
    auto find_frame = [](const AssetInfo& inf, const std::string& key) -> SDL_Texture* {
        if (key.empty()) return nullptr;
        auto it = inf.animations.find(key);
        if (it != inf.animations.end() && !it->second.frames.empty()) {
            return it->second.frames.front();
        }
        return nullptr;
    };

    if (SDL_Texture* tex = find_frame(info, "default")) {
        return tex;
    }
    if (SDL_Texture* tex = find_frame(info, info.start_animation)) {
        return tex;
    }
    if (SDL_Texture* tex = find_frame(info, "start")) {
        return tex;
    }
    for (const auto& kv : info.animations) {
        if (!kv.second.frames.empty()) {
            return kv.second.frames.front();
        }
    }

    if (!assets_owner_) {
        return nullptr;
    }
    SDL_Renderer* renderer = assets_owner_->renderer();
    if (!renderer) {
        return nullptr;
    }


    std::string cache_key = info.name;
    if (cache_key.empty()) {
        auto addr = reinterpret_cast<std::uintptr_t>(&info);
        cache_key = "<unnamed@" + std::to_string(addr) + ">";
    }

    if (preview_attempted_.insert(cache_key).second) {
        auto& mutable_info = const_cast<AssetInfo&>(info);
        mutable_info.loadAnimations(renderer);
    }

    if (SDL_Texture* tex = find_frame(info, "default")) {
        return tex;
    }
    if (SDL_Texture* tex = find_frame(info, info.start_animation)) {
        return tex;
    }
    if (SDL_Texture* tex = find_frame(info, "start")) {
        return tex;
    }
    for (const auto& kv : info.animations) {
        if (!kv.second.frames.empty()) {
            return kv.second.frames.front();
        }
    }
    return nullptr;
}

void AssetLibraryUI::update(const Input& input,
                            int screen_w,
                            int screen_h,
                            AssetLibrary& lib,
                            Assets& assets)
{
    if (!floating_) return;
    assets_owner_ = &assets;
    ensure_items(lib);

    if (tiles_.empty()) {
        tiles_.reserve(items_.size());
        for (auto& inf : items_) {
            tiles_.push_back(std::make_unique<AssetTileWidget>(
                this,
                inf,
                [this, &assets](const std::shared_ptr<AssetInfo>& info){
                    assets.open_asset_info_editor(info);
                    close();
                },
                [this](const std::shared_ptr<AssetInfo>&){
                    close();
                },
                [this, &assets](const std::shared_ptr<AssetInfo>& info){
                    if (info) {
                        (void)get_default_frame_texture(*info);
                    }
                    drag_info_ = info;
                    int mx = 0, my = 0; SDL_GetMouseState(&mx, &my);
                    SDL_Point wp = assets.getView().screen_to_map(SDL_Point{mx,my});
                    drag_spawned_ = assets.spawn_asset(info->name, wp);
                    dragging_from_library_ = drag_spawned_ != nullptr;
                }
            ));
        }
        rebuild_rows();
    }

    floating_->set_work_area(SDL_Rect{0,0,screen_w,screen_h});
    floating_->update(input, screen_w, screen_h);

    if (floating_->is_visible() && floating_->is_expanded()) {
        SDL_Point cursor{ input.getX(), input.getY() };
        if (SDL_PointInRect(&cursor, &floating_->rect())) {
            assets.clear_editor_selection();
        }
    }

    if (dragging_from_library_ && drag_spawned_) {
        int mx = input.getX();
        int my = input.getY();
        SDL_Point wp = assets.getView().screen_to_map(SDL_Point{mx,my});
        drag_spawned_->pos.x = wp.x;
        drag_spawned_->pos.y = wp.y;
        if (!input.isDown(Input::LEFT)) {
            dragging_from_library_ = false;
            assets.finalize_asset_drag(drag_spawned_, drag_info_);
            drag_spawned_ = nullptr;
            drag_info_.reset();
        }
    }

    if (showing_create_popup_) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}


void AssetLibraryUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!floating_) return;
    floating_->render(r);

    if (showing_create_popup_) {
        SDL_Rect box{ screen_w/2 - 150, screen_h/2 - 40, 300, 80 };
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 20,20,20,220);
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 255,255,255,255);
        SDL_RenderDrawRect(r, &box);

        SDL_Rect input_rect{ box.x + 8, box.y + 8, box.w - 16, box.h - 16 };
        SDL_SetRenderDrawColor(r, 35,35,35,230);
        SDL_RenderFillRect(r, &input_rect);
        SDL_SetRenderDrawColor(r, 90,90,90,255);
        SDL_RenderDrawRect(r, &input_rect);

        const int text_padding = 12;
        TTF_Font* font = load_font(18);
        if (font) {
            std::string display = new_asset_name_.empty() ? "Enter asset name..." : new_asset_name_;
            SDL_Color color = new_asset_name_.empty()
                                ? SDL_Color{180, 180, 180, 255}
                                : SDL_Color{255, 255, 255, 255};
            int available_w = input_rect.w - 2 * text_padding;
            int tw = 0;
            int th = 0;
            std::string render_text = display;
            if (TTF_SizeUTF8(font, render_text.c_str(), &tw, &th) == 0 && tw > available_w) {
                const std::string ellipsis = "...";
                std::string base = display;
                while (!base.empty()) {
                    base.pop_back();
                    std::string candidate = base + ellipsis;
                    if (TTF_SizeUTF8(font, candidate.c_str(), &tw, &th) == 0 && tw <= available_w) {
                        render_text = std::move(candidate);
                        break;
                    }
                }
                if (base.empty()) {
                    render_text = ellipsis;
                    (void)TTF_SizeUTF8(font, render_text.c_str(), &tw, &th);
                }
            } else {
                (void)TTF_SizeUTF8(font, render_text.c_str(), &tw, &th);
            }

            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, render_text.c_str(), color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    SDL_Rect dst{ input_rect.x + text_padding,
                                  input_rect.y + (input_rect.h - th) / 2,
                                  tw,
                                  th };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }

            if (!new_asset_name_.empty()) {
                int caret_w = 0;
                int caret_h = 0;
                if (TTF_SizeUTF8(font, new_asset_name_.c_str(), &caret_w, &caret_h) != 0 || caret_w > available_w) {
                    caret_w = std::min(tw, available_w);
                    caret_h = th;
                }
                if (caret_h <= 0) caret_h = th;
                int caret_x = input_rect.x + text_padding + std::min(caret_w, available_w);
                int caret_top = input_rect.y + (input_rect.h - caret_h) / 2;
                int caret_bottom = caret_top + caret_h;
                SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
                SDL_RenderDrawLine(r, caret_x + 1, caret_top, caret_x + 1, caret_bottom);
            }
        }
    }
}


void AssetLibraryUI::handle_event(const SDL_Event& e) {
    if (!floating_) return;

    if (showing_create_popup_) {
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) {
                if (create_new_asset_on_disk(new_asset_name_)) {
                    items_cached_ = false;
                    tiles_.clear();
                }
                showing_create_popup_ = false;
            } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                showing_create_popup_ = false;
            } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                if (!new_asset_name_.empty()) new_asset_name_.pop_back();
            }
        } else if (e.type == SDL_TEXTINPUT) {
            new_asset_name_ += e.text.text;
        }
    }

    floating_->handle_event(e);
}


std::shared_ptr<AssetInfo> AssetLibraryUI::consume_selection() {
    return nullptr;
}


bool AssetLibraryUI::is_input_blocking_at(int mx, int my) const {
    if (dragging_from_library_) return true;
    if (!floating_ || !floating_->is_visible() || !floating_->is_expanded())
        return false;
    SDL_Point p{ mx, my };
    return SDL_PointInRect(&p, &floating_->rect());
}

bool AssetLibraryUI::is_dragging_asset() const {
    return dragging_from_library_;
}
