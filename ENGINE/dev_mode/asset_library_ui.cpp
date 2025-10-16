
#include "asset_library_ui.hpp"
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <thread>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <cctype>
#include <system_error>
#include "utils/input.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/Asset.hpp"
#include "dm_styles.hpp"
#include <iostream>
#include <filesystem>
#include <SDL_ttf.h>
#include "core/AssetsManager.hpp"
#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "draw_utils.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "dev_mode/manifest_spawn_group_utils.hpp"

#include <nlohmann/json.hpp>

namespace {
    const SDL_Color kTileBG  = dm::rgba(24, 36, 56, 210);
    const SDL_Color kTileHL  = dm::rgba(59, 130, 246, 110);
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

    std::string trim_copy(const std::string& value) {
        auto begin = value.begin();
        auto end = value.end();
        while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
            ++begin;
        }
        while (end != begin) {
            auto prev = end;
            --prev;
            if (!std::isspace(static_cast<unsigned char>(*prev))) {
                break;
            }
            end = prev;
        }
        return std::string(begin, end);
    }

    bool remove_directory_if_exists(const fs::path& path) {
        std::error_code ec;
        if (path.empty()) {
            return true;
        }
        if (!fs::exists(path, ec)) {
            return true;
        }
        fs::remove_all(path, ec);
        if (ec) {
            std::cerr << "[AssetLibraryUI] Failed to remove '" << path << "': " << ec.message() << "\n";
            return false;
        }
        return true;
    }

} // namespace

struct AssetLibraryUI::AssetTileWidget : public Widget {
    static constexpr int kPad = 8;
    static constexpr int kDeleteButtonSize = 24;
    AssetLibraryUI* owner = nullptr;
    std::shared_ptr<AssetInfo> info;
    SDL_Rect rect_{0,0,0,0};
    SDL_Rect delete_rect_{0,0,kDeleteButtonSize,kDeleteButtonSize};
    bool hovered = false;
    bool pressed = false;
    bool right_pressed = false;
    bool delete_hovered = false;
    bool delete_pressed = false;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_click;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_right_click;
    std::function<void(const std::shared_ptr<AssetInfo>&)> on_delete;

    explicit AssetTileWidget(AssetLibraryUI* owner_ptr,
                             std::shared_ptr<AssetInfo> i,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> right_click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> delete_click)
        : owner(owner_ptr),
          info(std::move(i)),
          on_click(std::move(click)),
          on_right_click(std::move(right_click)),
          on_delete(std::move(delete_click)) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        delete_rect_ = SDL_Rect{ rect_.x + kPad, rect_.y + kPad, kDeleteButtonSize, kDeleteButtonSize };
    }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int ) const override { return 200; }

    bool handle_event(const SDL_Event& e) override {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.motion.x, e.motion.y };
            hovered = SDL_PointInRect(&p, &rect_);
            delete_hovered = SDL_PointInRect(&p, &delete_rect_);
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            SDL_Point p{ e.button.x, e.button.y };
            if (!SDL_PointInRect(&p, &rect_)) {
                return false;
            }
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (SDL_PointInRect(&p, &delete_rect_)) {
                    delete_pressed = true;
                    return true;
                }
                pressed = true;
                return true;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                if (SDL_PointInRect(&p, &delete_rect_)) {
                    return true;
                }
                right_pressed = true;
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP) {
            SDL_Point p{ e.button.x, e.button.y };
            if (e.button.button == SDL_BUTTON_LEFT) {
                bool inside_delete = SDL_PointInRect(&p, &delete_rect_);
                bool inside_tile = SDL_PointInRect(&p, &rect_);
                bool was_delete = delete_pressed;
                bool was_tile = pressed;
                delete_pressed = false;
                pressed = false;
                if (inside_delete && was_delete) {
                    if (on_delete) on_delete(info);
                    return true;
                }
                if (inside_tile && was_tile) {
                    if (on_click) on_click(info);
                    return true;
                }
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                bool was = right_pressed;
                right_pressed = false;
                if (was && SDL_PointInRect(&p, &rect_)) {
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

        const int pad = kPad;
        const int label_h = 24;

        const auto& delete_style = DMStyles::DeleteButton();
        SDL_Rect button_rect = delete_rect_;
        SDL_Color delete_bg = delete_style.bg;
        if (delete_pressed) {
            delete_bg = delete_style.press_bg;
        } else if (delete_hovered) {
            delete_bg = delete_style.hover_bg;
        }
        const int corner_radius = DMStyles::CornerRadius();
        const int bevel_depth = DMStyles::BevelDepth();
        const SDL_Color& highlight = DMStyles::HighlightColor();
        const SDL_Color& shadow = DMStyles::ShadowColor();
        dm_draw::DrawBeveledRect(
            r,
            button_rect,
            corner_radius,
            bevel_depth,
            delete_bg,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline(
            r,
            button_rect,
            corner_radius,
            1,
            delete_style.border);
        SDL_SetRenderDrawColor(r, delete_style.text.r, delete_style.text.g, delete_style.text.b, delete_style.text.a);
        const int cross_inset = std::max(bevel_depth + 1, button_rect.w / 4);
        SDL_RenderDrawLine(r,
                           button_rect.x + cross_inset,
                           button_rect.y + cross_inset,
                           button_rect.x + button_rect.w - cross_inset,
                           button_rect.y + button_rect.h - cross_inset);
        SDL_RenderDrawLine(r,
                           button_rect.x + button_rect.w - cross_inset,
                           button_rect.y + cross_inset,
                           button_rect.x + cross_inset,
                           button_rect.y + button_rect.h - cross_inset);

        int label_left = button_rect.x + button_rect.w + pad;
        int label_right = rect_.x + rect_.w - pad;
        if (label_left > label_right) {
            label_left = rect_.x + pad;
        }
        SDL_Rect label_rect{ label_left, rect_.y + pad, std::max(0, label_right - label_left), label_h };

        const AssetInfo* in = info.get();
        std::string label_text = (in && !in->name.empty()) ? in->name : "(Unnamed)";
        TTF_Font* label_font = load_font(15);
        std::string render_label = label_text;
        if (label_font && label_rect.w > 0) {
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
                int tw = 0;
                int th = 0;
                SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                if (tw > 0 && th > 0) {
                    SDL_Rect image_rect{ rect_.x + pad,
                                         label_rect.y + label_rect.h + pad,
                                         rect_.w - 2 * pad,
                                         rect_.h - (label_rect.h + 3 * pad) };
                    image_rect.h = std::max(image_rect.h, 0);
                    if (image_rect.w > 0 && image_rect.h > 0) {
                        float scale = std::min(image_rect.w / float(tw), image_rect.h / float(th));
                        if (scale > 0.0f) {
                            int dw = static_cast<int>(tw * scale);
                            int dh = static_cast<int>(th * scale);
                            SDL_Rect dst{ image_rect.x + (image_rect.w - dw) / 2,
                                          image_rect.y + (image_rect.h - dh) / 2, dw, dh };
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
        const int tile_radius = std::min(DMStyles::CornerRadius(), std::min(rect_.w, rect_.h) / 2);
        dm_draw::DrawRoundedOutline(
            r,
            rect_,
            tile_radius,
            1,
            kTileBd);
        if (label_font && label_rect.w > 0) {
            SDL_Color text_color = DMStyles::Label().color;
            SDL_Surface* surf = TTF_RenderUTF8_Blended(label_font, render_label.c_str(), text_color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    int dw = 0;
                    int dh = 0;
                    SDL_QueryTexture(tex, nullptr, nullptr, &dw, &dh);
                    if (dw > label_rect.w) {
                        dw = label_rect.w;
                    }
                    SDL_Rect dst{ label_rect.x,
                                  label_rect.y + std::max(0, (label_rect.h - dh) / 2),
                                  dw,
                                  dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }
    }
};

AssetLibraryUI::AssetLibraryUI() {
    floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    floating_->set_expanded(false);

    search_box_ = std::make_unique<DMTextBox>("Search", "");
    search_widget_ = std::make_unique<TextBoxWidget>(search_box_.get(), true);

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
    return (floating_ && floating_->is_expanded()) || showing_create_popup_ || showing_delete_popup_;
}

bool AssetLibraryUI::is_locked() const {
    return floating_ && floating_->isLocked();
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
    filter_dirty_ = true;
}

void AssetLibraryUI::rebuild_rows() {
    if (!floating_) return;
    std::vector<DockableCollapsible::Row> rows;
    if (search_widget_) rows.push_back({ search_widget_.get() });
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

bool AssetLibraryUI::matches_query(const AssetInfo& info, const std::string& query) const {
    if (query.empty()) return true;

    auto to_lower_copy = [](std::string s) {
        for (auto& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
};

    std::istringstream ss(query);
    std::string token;
    std::string name_lower = to_lower_copy(info.name);

    while (ss >> token) {
        if (token.empty()) continue;

        if (token.front() == '#') {
            std::string tag = token.substr(1);
            if (tag.empty()) continue;
            std::string needle = to_lower_copy(tag);
            bool tag_match = std::any_of(info.tags.begin(), info.tags.end(), [&](const std::string& t){
                return to_lower_copy(t).find(needle) != std::string::npos;
            });
            if (!tag_match) {
                return false;
            }
        } else {
            std::string needle = to_lower_copy(token);
            if (needle.empty()) continue;
            bool in_name = name_lower.find(needle) != std::string::npos;
            if (!in_name) {
                bool in_tags = std::any_of(info.tags.begin(), info.tags.end(), [&](const std::string& t){
                    return to_lower_copy(t).find(needle) != std::string::npos;
                });
                if (!in_tags) {
                    return false;
                }
            }
        }
    }

    return true;
}

void AssetLibraryUI::refresh_tiles(Assets& assets) {
    tiles_.clear();
    tiles_.reserve(items_.size());

    Assets* assets_ptr = &assets;

    for (auto& inf : items_) {
        if (!inf) continue;
        if (!matches_query(*inf, search_query_)) continue;
        tiles_.push_back(std::make_unique<AssetTileWidget>(
            this,
            inf,
            [this](const std::shared_ptr<AssetInfo>& info){
                if (info) {
                    pending_selection_ = info;
                }
                close();
            },
            [this, assets_ptr](const std::shared_ptr<AssetInfo>& info){
                if (info && assets_ptr) {
                    assets_ptr->open_asset_info_editor(info);
                }
                close();
            },
            [this](const std::shared_ptr<AssetInfo>& info){
                request_delete(info);
            }
        ));
    }

    rebuild_rows();
}

void AssetLibraryUI::request_delete(const std::shared_ptr<AssetInfo>& info) {
    if (!info) {
        return;
    }
    PendingDeleteInfo pending;
    pending.name = info->name;
    pending.asset_dir = info->asset_dir_path();
    if (pending.asset_dir.empty() && !info->name.empty()) {
        pending.asset_dir = (std::filesystem::path("SRC") / info->name).lexically_normal().string();
    }
    pending_delete_ = std::move(pending);
    showing_delete_popup_ = true;
    showing_create_popup_ = false;
    delete_yes_hovered_ = delete_no_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = false;
}

void AssetLibraryUI::cancel_delete_request() {
    showing_delete_popup_ = false;
    clear_delete_state();
}

void AssetLibraryUI::confirm_delete_request() {
    if (!pending_delete_) {
        clear_delete_state();
        showing_delete_popup_ = false;
        return;
    }

    const PendingDeleteInfo pending = *pending_delete_;
    const std::string asset_name = pending.name;
    const std::filesystem::path asset_dir = pending.asset_dir.empty()
        ? std::filesystem::path("SRC") / asset_name
        : std::filesystem::path(pending.asset_dir);
    const std::filesystem::path cache_dir = std::filesystem::path("cache") / asset_name;

    showing_delete_popup_ = false;

    if (assets_owner_) {
        assets_owner_->clear_editor_selection();
        std::vector<Asset*> doomed;
        doomed.reserve(assets_owner_->all.size());
        for (Asset* asset : assets_owner_->all) {
            if (!asset || !asset->info) continue;
            if (asset->info->name == asset_name) {
                doomed.push_back(asset);
            }
        }
        for (Asset* asset : doomed) {
            asset->Delete();
        }
    }

    bool manifest_flush_required = false;
    if (!asset_name.empty()) {
        bool removed_from_manifest = false;
        if (manifest_store_owner_) {
            removed_from_manifest = manifest_store_owner_->remove_asset(asset_name);
            if (!removed_from_manifest) {
                std::cerr << "[AssetLibraryUI] Failed to remove '" << asset_name
                          << "' from manifest\n";
            } else {
                manifest_flush_required = true;
            }
        } else {
            std::cerr << "[AssetLibraryUI] Manifest store unavailable; manifest not updated for '"
                      << asset_name << "'\n";
        }

        if (library_owner_) {
            library_owner_->remove(asset_name);
        }
    }

    if (!asset_dir.empty()) {
        const std::string dir_name = asset_dir.filename().string();
        const bool is_src_root = (dir_name == "SRC" && asset_dir.parent_path().empty());
        if (is_src_root) {
            std::cerr << "[AssetLibraryUI] Refusing to remove root SRC directory\n";
        } else {
            remove_directory_if_exists(asset_dir);
        }
    }
    if (!asset_name.empty()) {
        remove_directory_if_exists(cache_dir);
    }

    if (!asset_name.empty()) {
        if (manifest_store_owner_) {
            const nlohmann::json& manifest = manifest_store_owner_->manifest_json();
            auto maps_it = manifest.find("maps");
            if (maps_it != manifest.end() && maps_it->is_object()) {
                for (auto it = maps_it->begin(); it != maps_it->end(); ++it) {
                    nlohmann::json map_entry = *it;
                    if (devmode::manifest_utils::remove_asset_from_spawn_groups(map_entry, asset_name)) {
                        if (!manifest_store_owner_->update_map_entry(it.key(), map_entry)) {
                            std::cerr << "[AssetLibraryUI] Failed to update manifest map entry '"
                                      << it.key() << "' while removing '" << asset_name << "'\n";
                        } else {
                            manifest_flush_required = true;
                        }
                    }
                }
            }

            auto assets_it = manifest.find("assets");
            if (assets_it != manifest.end() && assets_it->is_object()) {
                for (auto it = assets_it->begin(); it != assets_it->end(); ++it) {
                    const std::string& referenced_asset = it.key();
                    if (referenced_asset == asset_name) {
                        continue;
                    }
                    auto transaction = manifest_store_owner_->begin_asset_transaction(referenced_asset);
                    if (!transaction) {
                        continue;
                    }
                    if (devmode::manifest_utils::remove_asset_from_spawn_groups(transaction.data(), asset_name)) {
                        if (!transaction.finalize()) {
                            std::cerr << "[AssetLibraryUI] Failed to update manifest asset entry '"
                                      << referenced_asset << "' while removing '" << asset_name << "'\n";
                        } else {
                            manifest_flush_required = true;
                        }
                    }
                }
            }
        }

        if (assets_owner_) {
            devmode::manifest_utils::remove_asset_from_spawn_groups(assets_owner_->map_info_json(), asset_name);
        }
    }

    if (manifest_store_owner_ && manifest_flush_required) {
        manifest_store_owner_->flush();
    }

    preview_attempted_.erase(asset_name);
    items_cached_ = false;
    filter_dirty_ = true;
    tiles_.clear();
    rebuild_rows();
    pending_selection_.reset();
    clear_delete_state();
}

void AssetLibraryUI::clear_delete_state() {
    pending_delete_.reset();
    delete_yes_hovered_ = delete_no_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = false;
    delete_modal_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_yes_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_no_rect_ = SDL_Rect{0, 0, 0, 0};
}

void AssetLibraryUI::update_delete_modal_geometry(int screen_w, int screen_h) {
    const int modal_w = 420;
    const int modal_h = 160;
    delete_modal_rect_ = SDL_Rect{
        std::max(0, screen_w / 2 - modal_w / 2),
        std::max(0, screen_h / 2 - modal_h / 2),
        modal_w,
        modal_h
    };
    const int button_w = 140;
    const int button_h = 40;
    const int button_gap = 20;
    const int total_w = button_w * 2 + button_gap;
    const int buttons_x = delete_modal_rect_.x + (delete_modal_rect_.w - total_w) / 2;
    const int buttons_y = delete_modal_rect_.y + delete_modal_rect_.h - button_h - 20;
    delete_yes_rect_ = SDL_Rect{ buttons_x, buttons_y, button_w, button_h };
    delete_no_rect_ = SDL_Rect{ buttons_x + button_w + button_gap, buttons_y, button_w, button_h };
}

bool AssetLibraryUI::create_new_asset(const std::string& raw_name) {
    std::string name = trim_copy(raw_name);
    if (name.empty()) {
        return false;
    }

    if (!manifest_store_owner_) {
        std::cerr << "[AssetLibraryUI] Manifest store unavailable; cannot create '" << name << "'\n";
        return false;
    }

    auto session = manifest_store_owner_->begin_asset_edit(name, true);
    if (!session) {
        std::cerr << "[AssetLibraryUI] Failed to begin manifest session for '" << name << "'\n";
        return false;
    }

    if (!session.is_new_asset()) {
        std::cerr << "[AssetLibraryUI] Asset '" << name << "' already exists\n";
        session.cancel();
        return false;
    }

    fs::path base("SRC");
    fs::path dir = base / name;

    try {
        if (!fs::exists(base)) {
            fs::create_directories(base);
        }
        if (fs::exists(dir)) {
            std::cerr << "[AssetLibraryUI] Asset directory '" << dir << "' already exists\n";
            session.cancel();
            return false;
        }
        fs::create_directories(dir);

        const std::string asset_dir_str = dir.lexically_normal().generic_string();
        nlohmann::json manifest_entry = {
            {"asset_name", name},
            {"asset_type", "Object"},
            {"animations", nlohmann::json::object()},
            {"start", ""}
        };
        manifest_entry["start"] = asset_dir_str;
        manifest_entry["asset_directory"] = asset_dir_str;
        manifest_entry["tags"] = nlohmann::json::array();
        manifest_entry["anti_tags"] = nlohmann::json::array();
        manifest_entry["neighbor_search_distance"] = 500;
        manifest_entry["render_radius"] = 0;
        manifest_entry["update_radius"] = 0;
        manifest_entry["min_same_type_distance"] = 0;
        manifest_entry["min_distance_all"] = 0;
        manifest_entry["can_invert"] = false;
        manifest_entry["generate_rays"] = false;
        manifest_entry["ray_strength"] = 0;
        manifest_entry["has_shading"] = false;
        manifest_entry["lighting_info"] = nlohmann::json::array();
        manifest_entry["size_settings"] = {
            {"scale_percentage", 100.0}
        };

        session.data() = manifest_entry;
        if (!session.commit()) {
            std::cerr << "[AssetLibraryUI] Failed to commit manifest entry for '" << name << "'\n";
            std::error_code ec;
            fs::remove_all(dir, ec);
            return false;
        }

        manifest_store_owner_->flush();

        std::cout << "[AssetLibraryUI] Created new asset '" << name << "' at " << dir << "\n";

        const std::string manifest_arg = manifest::manifest_path();
        const std::string asset_arg = name;
        const std::string asset_root_arg = asset_dir_str;
        std::thread launcher([manifest_arg, asset_arg, asset_root_arg]() {
            try {
                auto quote = [](const std::string& value) {
                    std::string escaped = "\"";
                    for (char ch : value) {
                        if (ch == '\\' || ch == '\"') {
                            escaped.push_back('\\');
                        }
                        escaped.push_back(ch);
                    }
                    escaped.push_back('\"');
                    return escaped;
                };
                std::string cmd = std::string("python scripts/animation_ui.py ") +
                                   "--manifest " + quote(manifest_arg) + " " +
                                   "--asset " + quote(asset_arg);
                if (!asset_root_arg.empty()) {
                    cmd += " --asset-root " + quote(asset_root_arg);
                }
                int rc = std::system(cmd.c_str());
                if (rc != 0) {
                    std::cerr << "[AssetLibraryUI] animation_ui.py exited with code " << rc << "\n";
                }
            } catch (const std::exception& ex) {
                std::cerr << "[AssetLibraryUI] Failed to launch animation_ui.py: " << ex.what() << "\n";
            }
        });
        launcher.detach();

        if (library_owner_) {
            library_owner_->load_all_from_SRC();
        }

        preview_attempted_.erase(name);
        items_cached_ = false;
        filter_dirty_ = true;
        tiles_.clear();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[AssetLibraryUI] Exception creating asset '" << name
                  << "': " << e.what() << "\n";
        std::error_code ec;
        fs::remove_all(dir, ec);
        return false;
    }
}

bool AssetLibraryUI::handle_delete_modal_event(const SDL_Event& e) {
    if (!showing_delete_popup_) {
        return false;
    }
    if (e.type == SDL_MOUSEMOTION) {
        SDL_Point p{ e.motion.x, e.motion.y };
        delete_yes_hovered_ = SDL_PointInRect(&p, &delete_yes_rect_);
        delete_no_hovered_ = SDL_PointInRect(&p, &delete_no_rect_);
        return SDL_PointInRect(&p, &delete_modal_rect_);
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        if (SDL_PointInRect(&p, &delete_yes_rect_)) {
            delete_yes_pressed_ = true;
            return true;
        }
        if (SDL_PointInRect(&p, &delete_no_rect_)) {
            delete_no_pressed_ = true;
            return true;
        }
        if (SDL_PointInRect(&p, &delete_modal_rect_)) {
            return true;
        }
        return false;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{ e.button.x, e.button.y };
        const bool inside_yes = SDL_PointInRect(&p, &delete_yes_rect_);
        const bool inside_no = SDL_PointInRect(&p, &delete_no_rect_);
        bool consumed = SDL_PointInRect(&p, &delete_modal_rect_);
        if (inside_yes && delete_yes_pressed_) {
            delete_yes_pressed_ = false;
            delete_no_pressed_ = false;
            confirm_delete_request();
            return true;
        }
        if (inside_no && delete_no_pressed_) {
            delete_yes_pressed_ = false;
            delete_no_pressed_ = false;
            cancel_delete_request();
            return true;
        }
        delete_yes_pressed_ = false;
        delete_no_pressed_ = false;
        return consumed;
    }
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_y || e.key.keysym.sym == SDLK_SPACE) {
            confirm_delete_request();
            return true;
        }
        if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_n) {
            cancel_delete_request();
            return true;
        }
        return true;
    }
    if (e.type == SDL_TEXTINPUT) {
        return true;
    }
    return false;
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
                            Assets& assets,
                            devmode::core::ManifestStore& store)
{
    if (!floating_) return;
    assets_owner_ = &assets;
    library_owner_ = &lib;
    manifest_store_owner_ = &store;
    ensure_items(lib);

    if (search_box_) {
        std::string current = search_box_->value();
        if (current != search_query_) {
            search_query_ = std::move(current);
            filter_dirty_ = true;
        }
    }

    if (filter_dirty_) {
        filter_dirty_ = false;
        if (floating_) {
            floating_->reset_scroll();
        }
        refresh_tiles(assets);
    }

    floating_->set_work_area(SDL_Rect{0,0,screen_w,screen_h});
    floating_->update(input, screen_w, screen_h);

    if (floating_->is_visible() && floating_->is_expanded()) {
        SDL_Point cursor{ input.getX(), input.getY() };
        if (SDL_PointInRect(&cursor, &floating_->rect())) {
            assets.clear_editor_selection();
        }
    }

    if (showing_delete_popup_) {
        update_delete_modal_geometry(screen_w, screen_h);
        SDL_StopTextInput();
    } else if (showing_create_popup_) {
        SDL_StartTextInput();
    } else if (search_box_ && search_box_->is_editing()) {
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
        const SDL_Color panel_bg = DMStyles::PanelBG();
        const SDL_Color& highlight = DMStyles::HighlightColor();
        const SDL_Color& shadow = DMStyles::ShadowColor();
        const int corner_radius = DMStyles::CornerRadius();
        const int bevel_depth = DMStyles::BevelDepth();
        dm_draw::DrawBeveledRect(
            r,
            box,
            corner_radius,
            bevel_depth,
            panel_bg,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
        const SDL_Color panel_border = DMStyles::Border();
        dm_draw::DrawRoundedOutline(
            r,
            box,
            corner_radius,
            1,
            panel_border);

        SDL_Rect input_rect{ box.x + 8, box.y + 8, box.w - 16, box.h - 16 };
        const DMTextBoxStyle& textbox = DMStyles::TextBox();
        dm_draw::DrawBeveledRect(
            r,
            input_rect,
            corner_radius,
            bevel_depth,
            textbox.bg,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline(
            r,
            input_rect,
            corner_radius,
            1,
            textbox.border);

        const int text_padding = 12 + bevel_depth;
        const int interior_h = std::max(0, input_rect.h - 2 * bevel_depth);
        TTF_Font* font = load_font(18);
        if (font) {
            std::string display = new_asset_name_.empty() ? "Enter asset name..." : new_asset_name_;
            SDL_Color color = new_asset_name_.empty() ? textbox.label.color : textbox.text;
            int available_w = input_rect.w - 2 * text_padding;
            if (available_w < 0) available_w = 0;
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
                    const int text_area_h = std::max(0, interior_h - th);
                    int text_y = input_rect.y + bevel_depth + text_area_h / 2;
                    text_y = std::max(text_y, input_rect.y + bevel_depth);
                    text_y = std::min(text_y, input_rect.y + input_rect.h - bevel_depth - th);
                    SDL_Rect dst{ input_rect.x + text_padding,
                                  text_y,
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
                const int caret_area_h = std::max(0, interior_h - caret_h);
                int caret_top = input_rect.y + bevel_depth + caret_area_h / 2;
                caret_top = std::max(caret_top, input_rect.y + bevel_depth);
                caret_top = std::min(caret_top, input_rect.y + input_rect.h - bevel_depth - caret_h);
                int caret_bottom = caret_top + caret_h;
                SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
                SDL_RenderDrawLine(r, caret_x + 1, caret_top, caret_x + 1, caret_bottom);
            }
        }
    }

    if (showing_delete_popup_) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
        SDL_Rect overlay{ 0, 0, screen_w, screen_h };
        SDL_RenderFillRect(r, &overlay);

        if (delete_modal_rect_.w == 0 || delete_modal_rect_.h == 0) {
            const_cast<AssetLibraryUI*>(this)->update_delete_modal_geometry(screen_w, screen_h);
        }
        SDL_Rect box = delete_modal_rect_;
        const SDL_Color panel_bg = DMStyles::PanelBG();
        const SDL_Color& highlight = DMStyles::HighlightColor();
        const SDL_Color& shadow = DMStyles::ShadowColor();
        const int corner_radius = DMStyles::CornerRadius();
        const int bevel_depth = DMStyles::BevelDepth();
        dm_draw::DrawBeveledRect(
            r,
            box,
            corner_radius,
            bevel_depth,
            panel_bg,
            highlight,
            shadow,
            false,
            DMStyles::HighlightIntensity(),
            DMStyles::ShadowIntensity());
        const SDL_Color panel_border = DMStyles::Border();
        dm_draw::DrawRoundedOutline(
            r,
            box,
            corner_radius,
            1,
            panel_border);

        std::string asset_label = "(Unnamed)";
        if (pending_delete_ && !pending_delete_->name.empty()) {
            asset_label = pending_delete_->name;
        }
        std::string message = "Are you sure you want to permanently delete \"" + asset_label + "\"?";

        const int text_margin = 16 + bevel_depth;
        SDL_Rect text_rect{ box.x + text_margin, box.y + text_margin, box.w - 2 * text_margin, delete_yes_rect_.y - box.y - text_margin - 10 };
        text_rect.w = std::max(0, text_rect.w);
        text_rect.h = std::max(0, text_rect.h);
        TTF_Font* font = load_font(18);
        if (font && text_rect.w > 0 && text_rect.h > 0) {
            SDL_Color text_color = DMStyles::Label().color;
            SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(font, message.c_str(), text_color, text_rect.w);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    int tw = 0;
                    int th = 0;
                    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                    SDL_Rect dst{ text_rect.x,
                                  text_rect.y,
                                  std::min(tw, text_rect.w),
                                  std::min(th, text_rect.h) };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }

        auto render_button = [&](const SDL_Rect& rect, bool hovered, bool pressed, const std::string& caption, const DMButtonStyle& style) {
            SDL_Color bg = style.bg;
            if (pressed) {
                bg = style.press_bg;
            } else if (hovered) {
                bg = style.hover_bg;
            }
            dm_draw::DrawBeveledRect(
                r,
                rect,
                corner_radius,
                bevel_depth,
                bg,
                highlight,
                shadow,
                false,
                DMStyles::HighlightIntensity(),
                DMStyles::ShadowIntensity());
            dm_draw::DrawRoundedOutline(
                r,
                rect,
                corner_radius,
                1,
                style.border);

            TTF_Font* btn_font = load_font(style.label.font_size > 0 ? style.label.font_size : 16);
            if (!btn_font) {
                btn_font = load_font(16);
            }
            if (btn_font) {
                SDL_Surface* text = TTF_RenderUTF8_Blended(btn_font, caption.c_str(), style.text);
                if (text) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, text);
                    SDL_FreeSurface(text);
                    if (tex) {
                        int tw = 0;
                        int th = 0;
                        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                        const int interior_h = std::max(0, rect.h - 2 * bevel_depth);
                        int text_y = rect.y + bevel_depth + std::max(0, interior_h - th) / 2;
                        text_y = std::max(text_y, rect.y + bevel_depth);
                        text_y = std::min(text_y, rect.y + rect.h - bevel_depth - th);
                        SDL_Rect dst{
                            rect.x + (rect.w - tw) / 2,
                            text_y,
                            tw,
                            th
                        };
                        SDL_RenderCopy(r, tex, nullptr, &dst);
                        SDL_DestroyTexture(tex);
                    }
                }
            }
        };

        render_button(delete_yes_rect_, delete_yes_hovered_, delete_yes_pressed_, "Yes, delete", DMStyles::DeleteButton());
        render_button(delete_no_rect_, delete_no_hovered_, delete_no_pressed_, "Cancel", DMStyles::HeaderButton());
    }
}

bool AssetLibraryUI::handle_event(const SDL_Event& e) {
    if (!floating_) return false;

    if (showing_delete_popup_) {
        if (handle_delete_modal_event(e)) {
            return true;
        }
        switch (e.type) {
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEMOTION:
            case SDL_MOUSEWHEEL:
            case SDL_KEYDOWN:
            case SDL_TEXTINPUT:
                return true;
            default:
                break;
        }
    }

    bool handled = false;

    if (showing_create_popup_) {
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_RETURN) {
                if (create_new_asset(new_asset_name_)) {
                    new_asset_name_.clear();
                }
                showing_create_popup_ = false;
                handled = true;
            } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                showing_create_popup_ = false;
                new_asset_name_.clear();
                handled = true;
            } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                if (!new_asset_name_.empty()) new_asset_name_.pop_back();
                handled = true;
            }
        } else if (e.type == SDL_TEXTINPUT) {
            new_asset_name_ += e.text.text;
            handled = true;
        }
    }

    if (floating_->handle_event(e)) {
        handled = true;
    }

    return handled;
}

std::shared_ptr<AssetInfo> AssetLibraryUI::consume_selection() {
    auto selection = pending_selection_;
    pending_selection_.reset();
    return selection;
}

bool AssetLibraryUI::is_input_blocking_at(int mx, int my) const {
    if (showing_delete_popup_) {
        return true;
    }
    if (!floating_ || !floating_->is_visible() || !floating_->is_expanded())
        return false;
    SDL_Point p{ mx, my };
    return SDL_PointInRect(&p, &floating_->rect());
}

bool AssetLibraryUI::is_dragging_asset() const {
    return false;
}
