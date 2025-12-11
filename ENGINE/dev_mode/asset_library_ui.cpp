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
#include "utils/string_utils.hpp"
#include "asset/asset_library.hpp"
#include "asset/asset_info.hpp"
#include "asset/animation.hpp"
#include "asset/Asset.hpp"
#include "dm_styles.hpp"
#include <iostream>
#include <filesystem>
#include <SDL_ttf.h>
#include "core/AssetsManager.hpp"
#include "map_generation/room.hpp"
#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "draw_utils.hpp"
#include "dev_mode/core/manifest_store.hpp"
#include "core/manifest/manifest_loader.hpp"
#include "dev_mode/manifest_spawn_group_utils.hpp"
#include "dev_mode/manifest_asset_utils.hpp"
#include "dev_mode_utils.hpp"
#include "tag_library.hpp"
#include "tag_utils.hpp"
#include "dev_mode/asset_paths.hpp"

#include <nlohmann/json.hpp>
#include <unordered_set>
#include <string_view>

namespace asset_paths = devmode::asset_paths;

namespace {
    const SDL_Color kTileBG  = dm::rgba(24, 36, 56, 210);
    const SDL_Color kTileHL  = dm::rgba(59, 130, 246, 110);
    const SDL_Color kTileBd  = DMStyles::Border();
    const SDL_Color kSearchErrorColor = dm::rgba(220, 38, 38);
    namespace fs = std::filesystem;

    using vibble::strings::to_lower_copy;

    std::string normalize_tag_value(std::string_view raw_value) {
        std::string normalized = tag_utils::normalize(raw_value);
        if (!normalized.empty() && normalized.front() == '#') {
            normalized.erase(normalized.begin());
        }
        return normalized;
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

    bool remove_tag_from_json_array(nlohmann::json& object,
                                    const char* key,
                                    const std::string& normalized,
                                    const std::string& hashed) {
        auto it = object.find(key);
        if (it == object.end() || !it->is_array()) {
            return false;
        }
        auto& arr = *it;
        auto original_size = arr.size();
        arr.erase(
            std::remove_if(arr.begin(), arr.end(), [&](const nlohmann::json& entry) {
                if (!entry.is_string()) {
                    return false;
                }
                const std::string value = entry.get<std::string>();
                const std::string normalized_value = normalize_tag_value(value);
                return normalized_value == normalized || value == hashed;
            }),
            arr.end());
        return original_size != arr.size();
    }

    bool manifest_contains_asset_reference(const nlohmann::json& node, const std::string& asset_name) {
        if (node.is_string()) {
            return node.get<std::string>() == asset_name;
        }
        if (node.is_array()) {
            for (const auto& entry : node) {
                if (manifest_contains_asset_reference(entry, asset_name)) {
                    return true;
                }
            }
            return false;
        }
        if (node.is_object()) {
            for (const auto& it : node.items()) {
                if (manifest_contains_asset_reference(it.value(), asset_name)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool remove_asset_from_required_children(nlohmann::json& map_entry, const std::string& asset_name) {
        bool modified = false;
        auto layers_it = map_entry.find("map_layers");
        if (layers_it == map_entry.end() || !layers_it->is_array()) {
            return false;
        }
        for (auto& layer_entry : *layers_it) {
            if (!layer_entry.is_object()) {
                continue;
            }
            auto rooms_it = layer_entry.find("rooms");
            if (rooms_it == layer_entry.end() || !rooms_it->is_array()) {
                continue;
            }
            for (auto& room_entry : *rooms_it) {
                if (!room_entry.is_object()) {
                    continue;
                }
                auto req_it = room_entry.find("required_children");
                if (req_it == room_entry.end() || !req_it->is_array()) {
                    continue;
                }
                auto& arr = *req_it;
                const auto original_size = arr.size();
                arr.erase(
                    std::remove_if(arr.begin(), arr.end(), [&](const nlohmann::json& element) {
                        return element.is_string() && element.get<std::string>() == asset_name;
                    }),
                    arr.end());
                if (arr.size() != original_size) {
                    modified = true;
                    if (arr.empty()) {
                        room_entry.erase(req_it);
                    }
                }
            }
        }
        return modified;
    }

    bool is_child_entries_array(const nlohmann::json& candidate) {
        if (!candidate.is_array()) {
            return false;
        }
        if (candidate.empty()) {
            return true;
        }
        for (const auto& entry : candidate) {
            if (entry.is_array()) {
                if (entry.empty() || !entry[0].is_number_integer()) {
                    return false;
                }
            } else if (entry.is_object()) {
                auto idx_it = entry.find("child_index");
                if (idx_it == entry.end() || !idx_it->is_number_integer()) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return true;
    }

    bool adjust_child_entries(nlohmann::json& child_entries, const std::vector<int>& removed_indices) {
        if (!child_entries.is_array() || removed_indices.empty()) {
            return false;
        }
        bool changed = false;
        nlohmann::json updated = nlohmann::json::array();
        for (const auto& child_entry : child_entries) {
            int child_index = -1;
            if (child_entry.is_array() && !child_entry.empty() && child_entry[0].is_number_integer()) {
                child_index = child_entry[0].get<int>();
            } else if (child_entry.is_object()) {
                auto idx_it = child_entry.find("child_index");
                if (idx_it != child_entry.end() && idx_it->is_number_integer()) {
                    child_index = idx_it->get<int>();
                }
            }
            if (child_index >= 0 && std::binary_search(removed_indices.begin(), removed_indices.end(), child_index)) {
                changed = true;
                continue;
            }
            if (child_index >= 0) {
                int new_index = child_index;
                for (int removed : removed_indices) {
                    if (removed < child_index) {
                        --new_index;
                    } else {
                        break;
                    }
                }
                if (new_index != child_index) {
                    nlohmann::json adjusted = child_entry;
                    if (adjusted.is_array()) {
                        adjusted[0] = new_index;
                    } else {
                        adjusted["child_index"] = new_index;
                    }
                    updated.push_back(std::move(adjusted));
                    changed = true;
                    continue;
                }
            }
            updated.push_back(child_entry);
        }
        if (changed) {
            child_entries = std::move(updated);
        }
        return changed;
    }

    bool adjust_movement_entries(nlohmann::json& movement, const std::vector<int>& removed_indices) {
        if (!movement.is_array() || removed_indices.empty()) {
            return false;
        }
        bool changed = false;
        for (auto& entry : movement) {
            if (entry.is_array()) {
                for (auto& element : entry) {
                    if (is_child_entries_array(element)) {
                        if (adjust_child_entries(element, removed_indices)) {
                            changed = true;
                        }
                        break;
                    }
                }
            } else if (entry.is_object()) {
                auto children_it = entry.find("children");
                if (children_it != entry.end() && children_it->is_array()) {
                    if (adjust_child_entries(*children_it, removed_indices)) {
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }

    bool remove_asset_from_animation_children(nlohmann::json& asset_json, const std::string& asset_name) {
        if (!asset_json.is_object()) {
            return false;
        }

        auto children_it = asset_json.find("animation_children");
        if (children_it == asset_json.end() || !children_it->is_array()) {
            return false;
        }
        std::vector<int> removed_indices;
        auto& children = *children_it;
        for (int idx = 0; idx < static_cast<int>(children.size()); ++idx) {
            if (children[idx].is_string() && children[idx].get<std::string>() == asset_name) {
                removed_indices.push_back(idx);
            }
        }
        if (removed_indices.empty()) {
            return false;
        }
        std::sort(removed_indices.begin(), removed_indices.end());
        removed_indices.erase(std::unique(removed_indices.begin(), removed_indices.end()), removed_indices.end());
        for (auto it = removed_indices.rbegin(); it != removed_indices.rend(); ++it) {
            children.erase(children.begin() + *it);
        }
        if (children.empty()) {
            asset_json.erase(children_it);
        }

        auto animations_it = asset_json.find("animations");
        if (animations_it != asset_json.end() && animations_it->is_object()) {
            for (auto anim_it = animations_it->begin(); anim_it != animations_it->end(); ++anim_it) {
                if (!anim_it.value().is_object()) continue;
                auto& anim_json = anim_it.value();
                auto anim_children_it = anim_json.find("children");
                if (anim_children_it != anim_json.end() && anim_children_it->is_array()) {
                    auto& child_arr = *anim_children_it;
                    for (auto it = removed_indices.rbegin(); it != removed_indices.rend(); ++it) {
                        if (*it >= 0 && *it < static_cast<int>(child_arr.size())) {
                            child_arr.erase(child_arr.begin() + *it);
                        }
                    }
                    if (child_arr.empty()) {
                        anim_json.erase(anim_children_it);
                    }
                }
                auto movement_it = anim_json.find("movement");
                if (movement_it != anim_json.end()) {
                    adjust_movement_entries(*movement_it, removed_indices);
                }
                auto paths_it = anim_json.find("movement_paths");
                if (paths_it != anim_json.end() && paths_it->is_array()) {
                    for (auto& path : *paths_it) {
                        adjust_movement_entries(path, removed_indices);
                    }
                }
            }
        }
        return true;
    }

}

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
    std::function<void(const std::shared_ptr<AssetInfo>&, bool)> on_multi_select_toggle;
    bool multi_select_enabled = false;
    bool multi_select_selected = false;
    bool multi_select_pressed = false;

    explicit AssetTileWidget(AssetLibraryUI* owner_ptr,
                             std::shared_ptr<AssetInfo> i,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> right_click,
                             std::function<void(const std::shared_ptr<AssetInfo>&)> delete_click,
                             std::function<void(const std::shared_ptr<AssetInfo>&, bool)> multi_select_click,
                             bool enable_multi_select,
                             bool initially_selected)
        : owner(owner_ptr),
          info(std::move(i)),
          on_click(std::move(click)),
          on_right_click(std::move(right_click)),
          on_delete(std::move(delete_click)),
          on_multi_select_toggle(std::move(multi_select_click)),
          multi_select_enabled(enable_multi_select),
          multi_select_selected(initially_selected) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        delete_rect_ = SDL_Rect{ rect_.x + kPad, rect_.y + kPad, kDeleteButtonSize, kDeleteButtonSize };
    }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int ) const override { return 200; }

    bool handle_event(const SDL_Event& e) override {
        if (multi_select_enabled) {
            if (e.type == SDL_MOUSEMOTION) {
                SDL_Point p{ e.motion.x, e.motion.y };
                hovered = SDL_PointInRect(&p, &rect_);
                delete_hovered = SDL_PointInRect(&p, &delete_rect_);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point p{ e.button.x, e.button.y };
                if (SDL_PointInRect(&p, &rect_)) {
                    multi_select_pressed = true;
                    return true;
                }
                return false;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point p{ e.button.x, e.button.y };
                bool inside = SDL_PointInRect(&p, &rect_);
                bool was_pressed = multi_select_pressed;
                multi_select_pressed = false;
                if (inside && was_pressed) {
                    multi_select_selected = !multi_select_selected;
                    if (on_multi_select_toggle) {
                        on_multi_select_toggle(info, multi_select_selected);
                    }
                    return true;
                }
            }
            return false;
        }

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

        SDL_Rect button_rect = delete_rect_;
        const int corner_radius = DMStyles::CornerRadius();
        const int bevel_depth = DMStyles::BevelDepth();
        const SDL_Color& highlight = DMStyles::HighlightColor();
        const SDL_Color& shadow = DMStyles::ShadowColor();

        if (multi_select_enabled) {
            SDL_Color checkbox_bg = multi_select_selected ? DMStyles::CheckboxHoverFill() : DMStyles::CheckboxBaseFill();
            if (delete_hovered) {
                checkbox_bg = DMStyles::CheckboxHoverFill();
            }
            dm_draw::DrawBeveledRect( r, button_rect, corner_radius, bevel_depth, checkbox_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
            SDL_Color border = multi_select_selected ? DMStyles::CheckboxActiveOutline() : DMStyles::CheckboxOutlineColor();
            if (delete_hovered) {
                border = DMStyles::CheckboxHoverOutline();
            }
            dm_draw::DrawRoundedOutline( r, button_rect, std::min(corner_radius, button_rect.w / 2), 1, border);
            if (multi_select_selected) {
                SDL_Color check = DMStyles::CheckboxCheckColor();
                SDL_SetRenderDrawColor(r, check.r, check.g, check.b, check.a);
                const int inset = std::max(3, button_rect.w / 5);
                SDL_RenderDrawLine(r, button_rect.x + inset, button_rect.y + button_rect.h / 2, button_rect.x + button_rect.w / 2, button_rect.y + button_rect.h - inset + 1);
                SDL_RenderDrawLine(r, button_rect.x + button_rect.w / 2, button_rect.y + button_rect.h - inset + 1, button_rect.x + button_rect.w - inset, button_rect.y + inset);
            }
        } else {
            const auto& delete_style = DMStyles::DeleteButton();
            SDL_Color delete_bg = delete_style.bg;
            if (delete_pressed) {
                delete_bg = delete_style.press_bg;
            } else if (delete_hovered) {
                delete_bg = delete_style.hover_bg;
            }
            dm_draw::DrawBeveledRect( r, button_rect, corner_radius, bevel_depth, delete_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
            dm_draw::DrawRoundedOutline( r, button_rect, corner_radius, 1, delete_style.border);
            SDL_SetRenderDrawColor(r, delete_style.text.r, delete_style.text.g, delete_style.text.b, delete_style.text.a);
            const int cross_inset = std::max(bevel_depth + 1, button_rect.w / 4);
            SDL_RenderDrawLine(r, button_rect.x + cross_inset, button_rect.y + cross_inset, button_rect.x + button_rect.w - cross_inset, button_rect.y + button_rect.h - cross_inset);
            SDL_RenderDrawLine(r, button_rect.x + button_rect.w - cross_inset, button_rect.y + cross_inset, button_rect.x + cross_inset, button_rect.y + button_rect.h - cross_inset);
        }

        int label_left = button_rect.x + button_rect.w + pad;
        int label_right = rect_.x + rect_.w - pad;
        if (label_left > label_right) {
            label_left = rect_.x + pad;
        }
        SDL_Rect label_rect{ label_left, rect_.y + pad, std::max(0, label_right - label_left), label_h };

        const AssetInfo* in = info.get();
        std::string label_text = (in && !in->name.empty()) ? in->name : "(Unnamed)";
        TTF_Font* label_font = devmode::utils::load_font(15);
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
                if (it != in->animations.end() && !it->second.frames.empty() && !it->second.frames.front()->variants.empty()) tex = it->second.frames.front()->variants[0].base_texture;
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
        dm_draw::DrawRoundedOutline( r, rect_, tile_radius, 1, kTileBd);
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
                                  label_rect.y + std::max(0, (label_rect.h - dh) / 2), dw, dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }
    }
};

struct AssetLibraryUI::HashtagTileWidget : public Widget {
    static constexpr int kPad = 8;
    static constexpr int kDeleteButtonSize = 24;
    AssetLibraryUI* owner = nullptr;
    std::string tag;
    int asset_count = 0;
    SDL_Rect rect_{0,0,0,0};
    bool hovered = false;
    bool pressed = false;
    std::function<void(const std::string&)> on_click;
    std::function<void(const std::string&)> on_delete;
    bool resolvable = false;
    SDL_Rect delete_rect_{0,0,kDeleteButtonSize,kDeleteButtonSize};
    bool delete_hovered = false;
    bool delete_pressed = false;

    HashtagTileWidget(AssetLibraryUI* owner_ptr,
                      std::string tag_value,
                      int count,
                      std::function<void(const std::string&)> click,
                      std::function<void(const std::string&)> delete_click)
        : owner(owner_ptr),
          tag(std::move(tag_value)),
          asset_count(count),
          on_click(std::move(click)),
          on_delete(std::move(delete_click)),
          resolvable(count > 0) {}

    void set_rect(const SDL_Rect& r) override {
        rect_ = r;
        delete_rect_ = SDL_Rect{ rect_.x + kPad, rect_.y + kPad, kDeleteButtonSize, kDeleteButtonSize };
    }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return 180; }

    bool handle_event(const SDL_Event& e) override {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.motion.x, e.motion.y };
            hovered = SDL_PointInRect(&p, &rect_);
            delete_hovered = SDL_PointInRect(&p, &delete_rect_);
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            if (SDL_PointInRect(&p, &delete_rect_)) {
                delete_pressed = true;
                return true;
            }
            if (SDL_PointInRect(&p, &rect_)) {
                pressed = true;
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            SDL_Point p{ e.button.x, e.button.y };
            bool inside_delete = SDL_PointInRect(&p, &delete_rect_);
            bool was_delete = delete_pressed;
            delete_pressed = false;
            bool inside = SDL_PointInRect(&p, &rect_);
            bool was_pressed = pressed;
            pressed = false;
            if (inside_delete && was_delete) {
                if (on_delete) {
                    on_delete(tag);
                }
                return true;
            }
            if (inside && was_pressed) {
                if (on_click && resolvable) {
                    on_click(tag);
                }
                return true;
            }
        }
        return false;
    }

    void render(SDL_Renderer* r) const override {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, kTileBG.r, kTileBG.g, kTileBG.b, kTileBG.a);
        SDL_RenderFillRect(r, &rect_);

        const int pad = kPad;
        const int label_h = 26;
        const int footer_h = 24;

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
        dm_draw::DrawBeveledRect( r, button_rect, corner_radius, bevel_depth, delete_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        dm_draw::DrawRoundedOutline( r, button_rect, corner_radius, 1, delete_style.border);
        SDL_SetRenderDrawColor(r, delete_style.text.r, delete_style.text.g, delete_style.text.b, delete_style.text.a);
        const int cross_inset = std::max(bevel_depth + 1, button_rect.w / 4);
        SDL_RenderDrawLine(r, button_rect.x + cross_inset, button_rect.y + cross_inset, button_rect.x + button_rect.w - cross_inset, button_rect.y + button_rect.h - cross_inset);
        SDL_RenderDrawLine(r, button_rect.x + button_rect.w - cross_inset, button_rect.y + cross_inset, button_rect.x + cross_inset, button_rect.y + button_rect.h - cross_inset);
        SDL_RenderDrawLine(r, button_rect.x + cross_inset, button_rect.y + button_rect.h - cross_inset, button_rect.x + button_rect.w - cross_inset, button_rect.y + cross_inset);

        int label_left = button_rect.x + button_rect.w + pad;
        int label_right = rect_.x + rect_.w - pad;
        if (label_left > label_right) {
            label_left = rect_.x + pad;
        }

        SDL_Rect label_rect{ label_left, rect_.y + pad, std::max(0, label_right - label_left), label_h };
        SDL_Rect footer_rect{ rect_.x + pad,
                              rect_.y + rect_.h - pad - footer_h,
                              std::max(0, rect_.w - 2 * pad), footer_h };
        int preview_top = label_rect.y + label_rect.h + pad;
        int preview_bottom = footer_rect.y - pad;
        if (preview_bottom < preview_top) preview_bottom = preview_top;
        SDL_Rect preview_rect{ rect_.x + pad,
                               preview_top,
                               std::max(0, rect_.w - 2 * pad), std::max(0, preview_bottom - preview_top) };

        TTF_Font* label_font = devmode::utils::load_font(17);
        std::string caption = "#" + tag;
        if (label_font && label_rect.w > 0) {
            std::string render_label = caption;
            int tw = 0;
            int th = 0;
            if (TTF_SizeUTF8(label_font, render_label.c_str(), &tw, &th) == 0 && tw > label_rect.w) {
                const std::string ellipsis = "...";
                std::string base = caption;
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
            SDL_Color text_color = DMStyles::Label().color;
            SDL_Surface* surf = TTF_RenderUTF8_Blended(label_font, render_label.c_str(), text_color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    int dw = 0;
                    int dh = 0;
                    SDL_QueryTexture(tex, nullptr, nullptr, &dw, &dh);
                    SDL_Rect dst{ label_rect.x,
                                  label_rect.y + std::max(0, (label_rect.h - dh) / 2), std::min(dw, label_rect.w), dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }

        if (preview_rect.w > 0 && preview_rect.h > 0) {
            const std::string icon_text = "#";
            const SDL_Color icon_color{255, 255, 255, 255};
            TTF_Font* icon_font = nullptr;
            int icon_w = 0;
            int icon_h = 0;
            const int font_sizes[] = {112, 104, 96, 88, 80, 72, 64, 56, 48, 40, 32, 24};
            for (int size : font_sizes) {
                TTF_Font* candidate = devmode::utils::load_font(size);
                if (!candidate) continue;
                int tw = 0;
                int th = 0;
                if (TTF_SizeUTF8(candidate, icon_text.c_str(), &tw, &th) != 0) continue;
                icon_font = candidate;
                icon_w = tw;
                icon_h = th;
                if (tw <= preview_rect.w && th <= preview_rect.h) {
                    break;
                }
            }

            if (icon_font && icon_w > 0 && icon_h > 0) {
                SDL_Surface* surf = TTF_RenderUTF8_Blended(icon_font, icon_text.c_str(), icon_color);
                if (surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                    SDL_FreeSurface(surf);
                    if (tex) {
                        int dw = std::min(icon_w, preview_rect.w);
                        int dh = std::min(icon_h, preview_rect.h);
                        SDL_Rect dst{preview_rect.x + (preview_rect.w - dw) / 2,
                                     preview_rect.y + (preview_rect.h - dh) / 2, dw, dh};
                        SDL_RenderCopy(r, tex, nullptr, &dst);
                        SDL_DestroyTexture(tex);
                    }
                }
            }
        }

        std::string footer_text;
        if (asset_count <= 0) {
            footer_text = "No matching assets";
        } else if (asset_count == 1) {
            footer_text = "1 matching asset";
        } else {
            footer_text = std::to_string(asset_count) + " matching assets";
        }

        TTF_Font* footer_font = devmode::utils::load_font(14);
        if (footer_font && footer_rect.w > 0) {
            SDL_Color footer_color = DMStyles::Label().color;
            if (!resolvable) {
                footer_color = SDL_Color{160, 160, 160, footer_color.a};
            }
            SDL_Surface* surf = TTF_RenderUTF8_Blended(footer_font, footer_text.c_str(), footer_color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    int dw = 0;
                    int dh = 0;
                    SDL_QueryTexture(tex, nullptr, nullptr, &dw, &dh);
                    SDL_Rect dst{ footer_rect.x,
                                  footer_rect.y + std::max(0, (footer_rect.h - dh) / 2), std::min(dw, footer_rect.w), dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
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
        dm_draw::DrawRoundedOutline(r, rect_, tile_radius, 1, kTileBd);
    }
};

struct AssetLibraryUI::RoomAreaTileWidget : public Widget {
    static constexpr int kPad = 8;
    AssetLibraryUI* owner = nullptr;
    std::string room_name;
    std::string area_name;
    SDL_Rect rect_{0,0,0,0};
    bool hovered = false;
    bool pressed = false;
    std::function<void(const AreaRef&)> on_click;

    explicit RoomAreaTileWidget(AssetLibraryUI* owner_ptr,
                                std::string room,
                                std::string area,
                                std::function<void(const AreaRef&)> click)
        : owner(owner_ptr), room_name(std::move(room)), area_name(std::move(area)), on_click(std::move(click)) {}

    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int) const override { return 112; }

    bool handle_event(const SDL_Event& e) override {
        if (e.type == SDL_MOUSEMOTION) {
            SDL_Point p{ e.motion.x, e.motion.y };
            hovered = SDL_PointInRect(&p, &rect_);
        } else if (e.type == SDL_MOUSEBUTTONDOWN) {
            SDL_Point p{ e.button.x, e.button.y };
            if (e.button.button == SDL_BUTTON_LEFT && SDL_PointInRect(&p, &rect_)) {
                pressed = true;
                return true;
            }
        } else if (e.type == SDL_MOUSEBUTTONUP) {
            SDL_Point p{ e.button.x, e.button.y };
            if (e.button.button == SDL_BUTTON_LEFT) {
                bool was = pressed;
                pressed = false;
                if (was && SDL_PointInRect(&p, &rect_)) {
                    if (on_click) on_click(AreaRef{room_name, area_name});
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
        const SDL_Color border = kTileBd;
        const int tile_radius = std::min(DMStyles::CornerRadius(), std::min(rect_.w, rect_.h) / 2);
        dm_draw::DrawRoundedOutline(r, rect_, tile_radius, 1, border);

        std::string label = std::string("Area ") + area_name + " — Room " + room_name;
        TTF_Font* font = devmode::utils::load_font(15);
        SDL_Rect label_rect{ rect_.x + pad, rect_.y + pad, std::max(0, rect_.w - 2*pad), 24 };
        if (font && label_rect.w > 0) {
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, label.c_str(), DMStyles::Label().color);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
                SDL_FreeSurface(surf);
                if (tex) {
                    int dw=0, dh=0;
                    SDL_QueryTexture(tex, nullptr, nullptr, &dw, &dh);
                    if (dw > label_rect.w) dw = label_rect.w;
                    SDL_Rect dst{ label_rect.x, label_rect.y + std::max(0, (label_rect.h - dh)/2), dw, dh };
                    SDL_RenderCopy(r, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
            }
        }

        if (hovered) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(r, kTileHL.r, kTileHL.g, kTileHL.b, kTileHL.a);
            SDL_RenderFillRect(r, &rect_);
        }
    }
};

AssetLibraryUI::AssetLibraryUI() {
    floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    floating_->set_expanded(false);

    search_box_ = std::make_unique<DMTextBox>("Search", "");
    search_widget_ = std::make_unique<TextBoxWidget>(search_box_.get(), true);

    multi_select_button_ = std::make_unique<DMButton>("Select Multiple", &DMStyles::HeaderButton(), 200, DMButton::height());
    multi_select_button_widget_ = std::make_unique<ButtonWidget>(multi_select_button_.get(), [this](){
        toggle_multi_select_mode();
    });

    delete_all_button_ = std::make_unique<DMButton>("Delete All", &DMStyles::DeleteButton(), 200, DMButton::height());
    delete_all_button_widget_ = std::make_unique<ButtonWidget>(delete_all_button_.get(), [this](){
        handle_delete_all_request();
    });

    add_button_ = std::make_unique<DMButton>("Create New Asset", &DMStyles::CreateButton(), 200, DMButton::height());
    add_button_widget_ = std::make_unique<ButtonWidget>(add_button_.get(), [this](){
        handle_create_button_pressed();
    });
}

AssetLibraryUI::~AssetLibraryUI() = default;

void AssetLibraryUI::toggle() {
    if (!floating_) return;
    const bool should_show = !is_visible();
    floating_->set_visible(should_show);
    if (should_show) {
        floating_->set_expanded(true);

        rebuild_rows();
        if (search_box_) search_box_->start_editing();
    } else if (search_box_) {
        search_box_->stop_editing();
    }
}

bool AssetLibraryUI::is_visible() const { return floating_ && floating_->is_visible(); }

void AssetLibraryUI::open() {
    if (!floating_) floating_ = std::make_unique<DockableCollapsible>("Asset Library", true, 10, 10);
    if (floating_) {
        floating_->set_visible(true);
        floating_->set_expanded(true);

        rebuild_rows();
        if (search_box_) search_box_->start_editing();
    }
}

void AssetLibraryUI::close() {
    if (floating_) floating_->set_visible(false);
    if (search_box_) search_box_->stop_editing();
}

bool AssetLibraryUI::is_input_blocking() const {
    return (floating_ && floating_->is_expanded()) || showing_delete_popup_;
}

bool AssetLibraryUI::is_locked() const {
    return floating_ && floating_->isLocked();
}

void AssetLibraryUI::ensure_items(AssetLibrary& lib) {
    bool assets_changed = false;
    if (!items_cached_) {
        items_.clear();
        for (const auto& kv : lib.all()) {
            if (kv.second) items_.push_back(kv.second);
        }
        std::sort(items_.begin(), items_.end(), [](const auto& a, const auto& b){
            return (a ? a->name : "") < (b ? b->name : "");
        });
        items_cached_ = true;
        assets_changed = true;
        filter_dirty_ = true;
    }

    std::uint64_t current_tag_version = tag_utils::tag_version();
    if (!tag_items_initialized_ || current_tag_version != tag_version_token_) {
        tag_version_token_ = current_tag_version;
        tag_items_initialized_ = true;
        tag_assets_dirty_ = true;
    }

    if (assets_changed) {
        tag_assets_dirty_ = true;
    }

    if (tag_assets_dirty_) {
        bool tags_changed = refresh_tag_items();
        rebuild_tag_asset_lookup();
        tag_assets_dirty_ = false;
        if (tags_changed) {
            filter_dirty_ = true;
        }
    }
}

void AssetLibraryUI::rebuild_rows() {
    if (!floating_) return;
    std::vector<DockableCollapsible::Row> rows;
    if (search_widget_) rows.push_back({ search_widget_.get() });
    if (multi_select_button_widget_) rows.push_back({ multi_select_button_widget_.get() });
    if (delete_all_button_widget_ && multi_select_mode_ && !multi_select_selection_.empty()) {
        rows.push_back({ delete_all_button_widget_.get() });
    }
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

void AssetLibraryUI::toggle_multi_select_mode() {
    multi_select_mode_ = !multi_select_mode_;
    if (!multi_select_mode_) {
        multi_select_selection_.clear();
    }
    update_multi_select_controls();
    if (assets_owner_) {
        refresh_tiles(*assets_owner_);
    } else {
        filter_dirty_ = true;
    }
}

void AssetLibraryUI::update_multi_select_controls() {
    if (multi_select_button_) {
        multi_select_button_->set_text(multi_select_mode_ ? "Cancel Multi-Select" : "Select Multiple");
    }
    rebuild_rows();
}

void AssetLibraryUI::handle_multi_select_selection(const std::shared_ptr<AssetInfo>& info, bool selected) {
    if (!multi_select_mode_ || !info || info->name.empty()) {
        return;
    }
    if (selected) {
        multi_select_selection_.insert(info->name);
    } else {
        multi_select_selection_.erase(info->name);
    }
    update_multi_select_controls();
}

void AssetLibraryUI::handle_delete_all_request() {
    if (multi_select_selection_.empty()) {
        return;
    }
    if (multi_select_selection_.size() == 1) {
        const std::string single_name = *multi_select_selection_.begin();
        std::shared_ptr<AssetInfo> info;
        if (library_owner_) {
            info = library_owner_->get(single_name);
        }
        if (!info) {
            for (const auto& candidate : items_) {
                if (candidate && candidate->name == single_name) {
                    info = candidate;
                    break;
                }
            }
        }
        if (info) {
            request_delete(info);
            return;
        }
    }

    std::vector<PendingDeleteInfo> requests;
    requests.reserve(multi_select_selection_.size());
    for (const auto& name : multi_select_selection_) {
        PendingDeleteInfo pending;
        pending.name = name;
        std::shared_ptr<AssetInfo> info;
        if (library_owner_) {
            info = library_owner_->get(name);
        }
        if (!info) {
            for (const auto& candidate : items_) {
                if (candidate && candidate->name == name) {
                    info = candidate;
                    break;
                }
            }
        }
        if (info) {
            pending.asset_dir = info->asset_dir_path();
            if (pending.asset_dir.empty() && !info->name.empty()) {
                pending.asset_dir = asset_paths::asset_folder_path(info->name).generic_string();
            }
        } else if (!name.empty()) {
            pending.asset_dir = asset_paths::asset_folder_path(name).generic_string();
        }
        requests.push_back(std::move(pending));
    }
    begin_bulk_delete(std::move(requests));
}

void AssetLibraryUI::begin_bulk_delete(std::vector<PendingDeleteInfo> requests) {
    requests.erase(
        std::remove_if(requests.begin(), requests.end(), [](const PendingDeleteInfo& pending) {
            return pending.name.empty();
        }),
        requests.end());
    if (requests.empty()) {
        bulk_delete_queue_.clear();
        bulk_delete_mode_ = false;
        return;
    }
    bulk_delete_queue_ = std::move(requests);
    pending_delete_.reset();
    delete_yes_hovered_ = delete_no_hovered_ = delete_skip_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = delete_skip_pressed_ = false;
    bulk_delete_mode_ = true;
    if (skip_delete_confirmation_in_session_) {
        execute_bulk_delete_queue();
        return;
    }
    showing_delete_popup_ = true;
}

void AssetLibraryUI::execute_bulk_delete_queue() {
    if (bulk_delete_queue_.empty()) {
        bulk_delete_mode_ = false;
        showing_delete_popup_ = false;
        clear_delete_state();
        return;
    }
    showing_delete_popup_ = false;
    auto requests = bulk_delete_queue_;
    bulk_delete_queue_.clear();
    for (const auto& pending : requests) {
        perform_delete(pending, true);
    }
    clear_delete_state();
    multi_select_selection_.clear();
    update_multi_select_controls();
}

bool AssetLibraryUI::matches_query(const AssetInfo& info, const std::string& query) const {
    if (query.empty()) return true;

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

bool AssetLibraryUI::matches_tag_query(const std::string& tag, const std::string& query) const {
    if (query.empty()) return true;

    std::istringstream ss(query);
    std::string token;
    std::string tag_lower = to_lower_copy(tag);

    while (ss >> token) {
        if (token.empty()) continue;

        if (!token.empty() && token.front() == '#') {
            token.erase(token.begin());
        }

        std::string needle = to_lower_copy(token);
        if (needle.empty()) continue;

        if (tag_lower.find(needle) == std::string::npos) {
            return false;
        }
    }

    return true;
}

bool AssetLibraryUI::refresh_tag_items() {
    std::unordered_set<std::string> combined;
    const auto& library_tags = TagLibrary::instance().tags();
    combined.reserve(library_tags.size() + items_.size() * 2);

    for (const auto& tag : library_tags) {
        std::string normalized = normalize_tag_value(tag);
        if (!normalized.empty()) {
            combined.insert(std::move(normalized));
        }
    }

    for (const auto& info : items_) {
        if (!info) continue;
        for (const auto& raw_tag : info->tags) {
            std::string normalized = normalize_tag_value(raw_tag);
            if (!normalized.empty()) {
                combined.insert(std::move(normalized));
            }
        }
    }

    std::vector<std::string> merged;
    merged.reserve(combined.size());
    for (const auto& entry : combined) {
        if (!entry.empty()) {
            merged.push_back(entry);
        }
    }
    std::sort(merged.begin(), merged.end());

    if (merged != tag_items_) {
        tag_items_ = std::move(merged);
        return true;
    }
    return false;
}

void AssetLibraryUI::rebuild_tag_asset_lookup() {
    tag_asset_lookup_.clear();
    for (const auto& tag : tag_items_) {
        tag_asset_lookup_.emplace(tag, std::vector<std::shared_ptr<AssetInfo>>{});
    }

    for (const auto& info : items_) {
        if (!info) continue;
        for (const auto& raw_tag : info->tags) {
            std::string normalized = normalize_tag_value(raw_tag);
            if (normalized.empty()) continue;
            auto& bucket = tag_asset_lookup_[normalized];
            bucket.push_back(info);
        }
    }

    for (auto& [tag, bucket] : tag_asset_lookup_) {
        bucket.erase(std::remove(bucket.begin(), bucket.end(), nullptr), bucket.end());
        std::sort(bucket.begin(), bucket.end(), [](const auto& a, const auto& b) {
            const std::string& an = a ? a->name : std::string{};
            const std::string& bn = b ? b->name : std::string{};
            return an < bn;
        });
        bucket.erase(std::unique(bucket.begin(), bucket.end()), bucket.end());
    }
}

std::shared_ptr<AssetInfo> AssetLibraryUI::resolve_tag_to_asset(const std::string& tag) const {
    std::string normalized = normalize_tag_value(tag);
    if (normalized.empty()) {
        return nullptr;
    }

    auto pick_first = [](const auto& vec) -> std::shared_ptr<AssetInfo> {
        return vec.empty() ? nullptr : vec.front();
};

    auto it = tag_asset_lookup_.find(normalized);
    if (it != tag_asset_lookup_.end()) {
        if (auto found = pick_first(it->second)) {
            return found;
        }
    }

    if (auto raw_it = tag_asset_lookup_.find(tag); raw_it != tag_asset_lookup_.end()) {
        if (auto found = pick_first(raw_it->second)) {
            return found;
        }
    }

    if (!library_owner_) {
        return nullptr;
    }

    std::shared_ptr<AssetInfo> fallback;
    const auto& all_assets = library_owner_->all();
    for (const auto& [name, info] : all_assets) {
        if (!info) continue;
        for (const auto& raw_tag : info->tags) {
            if (normalize_tag_value(raw_tag) == normalized) {
                if (!fallback || info->name < fallback->name) {
                    fallback = info;
                }
                break;
            }
        }
    }
    return fallback;
}

int AssetLibraryUI::count_assets_for_tag(const std::string& tag) const {
    std::string normalized = normalize_tag_value(tag);
    auto it = tag_asset_lookup_.find(normalized.empty() ? tag : normalized);
    if (it != tag_asset_lookup_.end()) {
        return static_cast<int>(it->second.size());
    }
    return 0;
}

bool AssetLibraryUI::remove_tag_from_manifest_assets(const std::string& tag) {
    if (!manifest_store_owner_ || tag.empty()) {
        return false;
    }

    const std::string hashed = "#" + tag;
    bool manifest_changed = false;

    auto asset_views = manifest_store_owner_->assets();
    for (const auto& view : asset_views) {
        if (!view) {
            continue;
        }

        auto txn = manifest_store_owner_->begin_asset_transaction(view.name, false);
        if (!txn) {
            continue;
        }

        nlohmann::json& data = txn.data();
        bool modified = false;
        modified |= remove_tag_from_json_array(data, "tags", tag, hashed);
        modified |= remove_tag_from_json_array(data, "anti_tags", tag, hashed);

        if (modified) {
            if (!txn.finalize()) {
                std::cerr << "[AssetLibraryUI] Failed to persist tag removal for asset '" << view.name << "'\n";
            } else {
                manifest_changed = true;
            }
        } else {
            txn.cancel();
        }
    }

    return manifest_changed;
}

bool AssetLibraryUI::remove_tag_from_manifest_maps(const std::string& tag) {
    if (!manifest_store_owner_ || tag.empty()) {
        return false;
    }

    const std::string hashed = "#" + tag;
    bool manifest_changed = false;
    const nlohmann::json& manifest = manifest_store_owner_->manifest_json();
    auto maps_it = manifest.find("maps");
    if (maps_it == manifest.end() || !maps_it->is_object()) {
        return false;
    }

    for (auto it = maps_it->begin(); it != maps_it->end(); ++it) {
        nlohmann::json map_entry = *it;
        bool updated = false;
        if (!hashed.empty()) {
            updated |= devmode::manifest_utils::remove_asset_from_spawn_groups(map_entry, hashed);
        }
        if (!tag.empty()) {
            updated |= devmode::manifest_utils::remove_asset_from_spawn_groups(map_entry, tag);
        }
        if (updated) {
            if (!manifest_store_owner_->update_map_entry(it.key(), map_entry)) {
                std::cerr << "[AssetLibraryUI] Failed to update spawn groups for map '" << it.key() << "'\n";
            } else {
                manifest_changed = true;
            }
        }
    }

    return manifest_changed;
}

void AssetLibraryUI::delete_hashtag(const std::string& tag) {
    std::string normalized = normalize_tag_value(tag);
    if (normalized.empty()) {
        return;
    }

    const std::string hashed = "#" + normalized;

    bool manifest_changed = remove_tag_from_manifest_assets(normalized);
    manifest_changed |= remove_tag_from_manifest_maps(normalized);

    if (manifest_changed && manifest_store_owner_) {
        manifest_store_owner_->flush();
    }

    if (assets_owner_) {
        devmode::manifest_utils::remove_asset_from_spawn_groups(assets_owner_->map_info_json(), hashed);
        devmode::manifest_utils::remove_asset_from_spawn_groups(assets_owner_->map_info_json(), normalized);
    }

    bool tag_removed = TagLibrary::instance().remove_tag(normalized);
    if (!tag_removed) {
        std::cerr << "[AssetLibraryUI] Tag '" << normalized << "' not found in tag library\n";
    }

    for (auto& info : items_) {
        if (!info) {
            continue;
        }
        info->remove_tag(normalized);
        info->remove_tag(hashed);
        info->remove_anti_tag(normalized);
        info->remove_anti_tag(hashed);
    }

    tag_items_.erase(
        std::remove_if(tag_items_.begin(), tag_items_.end(), [&](const std::string& value) {
            return normalize_tag_value(value) == normalized;
        }),
        tag_items_.end());

    tag_asset_lookup_.erase(normalized);
    tag_asset_lookup_.erase(hashed);

    tag_items_initialized_ = false;
    tag_assets_dirty_ = true;
    filter_dirty_ = true;

    if (manifest_changed || tag_removed) {
        tag_utils::notify_tags_changed();
    }
}

void AssetLibraryUI::refresh_tiles(Assets& assets) {
    tiles_.clear();
    tiles_.reserve(items_.size() + tag_items_.size());

    Assets* assets_ptr = &assets;

    for (auto& inf : items_) {
        if (!inf) continue;
        if (!matches_query(*inf, search_query_)) continue;
        bool is_selected = false;
        if (!inf->name.empty()) {
            is_selected = multi_select_selection_.find(inf->name) != multi_select_selection_.end();
        }
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
            },
            [this](const std::shared_ptr<AssetInfo>& info, bool selected){
                handle_multi_select_selection(info, selected);
            },
            multi_select_mode_,
            is_selected
        ));
    }

    for (const auto& tag : tag_items_) {
        if (!matches_tag_query(tag, search_query_)) continue;
        int count = count_assets_for_tag(tag);
        tiles_.push_back(std::make_unique<HashtagTileWidget>(
            this,
            tag,
            count,
            [this](const std::string& tag_value){
                auto resolved = resolve_tag_to_asset(tag_value);
                if (resolved) {
                    pending_selection_ = resolved;
                    close();
                } else {
                    std::cerr << "[AssetLibraryUI] No assets found for tag '" << tag_value << "'\n";
                }
            },
            [this](const std::string& tag_value){
                delete_hashtag(tag_value);
            }
        ));
    }

    if (assets_owner_) {
        std::vector<std::pair<std::string, std::string>> area_refs;
        for (Room* room : assets_owner_->rooms()) {
            if (!room) continue;
            for (const auto& na : room->areas) {
                if (na.name.empty() || !na.area) continue;
                const std::string label = room->room_name + "/" + na.name;
                if (!search_query_.empty()) {
                    auto q = devmode::utils::trim_whitespace_copy(search_query_);
                    std::string lowered = vibble::strings::to_lower_copy(label);
                    std::string lowered_q = vibble::strings::to_lower_copy(q);
                    if (lowered.find(lowered_q) == std::string::npos) continue;
                }
                area_refs.emplace_back(room->room_name, na.name);
            }
        }
        std::sort(area_refs.begin(), area_refs.end());
        for (const auto& ref : area_refs) {
            tiles_.push_back(std::make_unique<RoomAreaTileWidget>(
                this,
                ref.first,
                ref.second,
                [this](const AreaRef& ref){ pending_area_selection_ = ref; close(); }
            ));
        }
    }

    rebuild_rows();
}

void AssetLibraryUI::perform_delete(const PendingDeleteInfo& pending, bool defer_multi_select_refresh) {
    const std::string asset_name = pending.name;
    const std::filesystem::path asset_dir = pending.asset_dir.empty() && !asset_name.empty() ? asset_paths::asset_folder_path(asset_name) : std::filesystem::path(pending.asset_dir);
    const std::filesystem::path cache_dir = std::filesystem::path("cache") / asset_name;

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
    bool manifest_entry_removed = false;
    if (!asset_name.empty()) {
        if (manifest_store_owner_) {
            const auto remove_result = devmode::manifest_utils::remove_asset_entry(manifest_store_owner_, asset_name, &std::cerr);
            manifest_entry_removed = remove_result.removed;
            if (!manifest_entry_removed) {
                std::cerr << "[AssetLibraryUI] Failed to remove '" << asset_name
                          << "' from manifest\n";
            }
            manifest_flush_required = manifest_flush_required || remove_result.used_store;
        } else {
            std::cerr << "[AssetLibraryUI] Manifest store unavailable; manifest not updated for '"
                      << asset_name << "'\n";
            manifest_entry_removed = devmode::manifest_utils::remove_manifest_asset_entry(asset_name, &std::cerr);
            if (!manifest_entry_removed) {
                std::cerr << "[AssetLibraryUI] Failed to remove '" << asset_name
                          << "' from manifest assets list\n";
            }
        }

        if (manifest_store_owner_ && manifest_entry_removed) {
            manifest_flush_required = manifest_flush_required || manifest_store_owner_->dirty();
            const nlohmann::json& manifest = manifest_store_owner_->manifest_json();
            const bool references_remaining = manifest_contains_asset_reference(manifest, asset_name);
            if (references_remaining) {
                auto maps_it = manifest.find("maps");
                if (maps_it != manifest.end() && maps_it->is_object()) {
                    for (auto it = maps_it->begin(); it != maps_it->end(); ++it) {
                        nlohmann::json map_entry = *it;
                        bool updated = false;
                        updated |= devmode::manifest_utils::remove_asset_from_spawn_groups(map_entry, asset_name);
                        updated |= remove_asset_from_required_children(map_entry, asset_name);
                        if (updated) {
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
                        auto transaction = manifest_store_owner_->begin_asset_transaction(referenced_asset);
                        if (!transaction) {
                            continue;
                        }
                        bool updated = false;
                        updated |= devmode::manifest_utils::remove_asset_from_spawn_groups(transaction.data(), asset_name);
                        updated |= remove_asset_from_animation_children(transaction.data(), asset_name);
                        if (updated) {
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
        }
    }

    if (!asset_dir.empty()) {
        const auto normalized_dir = asset_dir.lexically_normal();
        if (asset_paths::is_protected_asset_root(normalized_dir)) {
            std::cerr << "[AssetLibraryUI] Refusing to remove protected asset root '" << normalized_dir.generic_string() << "'\n";
        } else {
            remove_directory_if_exists(normalized_dir);
        }
    }
    if (!asset_name.empty()) {
        remove_directory_if_exists(cache_dir);
    }

    if (!asset_name.empty()) {
        if (assets_owner_) {
            devmode::manifest_utils::remove_asset_from_spawn_groups(assets_owner_->map_info_json(), asset_name);
        }
    }

    if (manifest_store_owner_ && manifest_flush_required) {
        manifest_store_owner_->flush();
    }

    if (library_owner_ && !asset_name.empty()) {
        library_owner_->remove(asset_name);
    }

    preview_attempted_.erase(asset_name);
    multi_select_selection_.erase(asset_name);
    items_cached_ = false;
    filter_dirty_ = true;
    tiles_.clear();
    pending_selection_.reset();
    if (!defer_multi_select_refresh) {
        update_multi_select_controls();
    }
}

void AssetLibraryUI::request_delete(const std::shared_ptr<AssetInfo>& info) {
    if (!info) {
        return;
    }
    PendingDeleteInfo pending;
    pending.name = info->name;
    pending.asset_dir = info->asset_dir_path();
    if (pending.asset_dir.empty() && !info->name.empty()) {
        pending.asset_dir = asset_paths::asset_folder_path(info->name).generic_string();
    }
    pending_delete_ = std::move(pending);
    delete_yes_hovered_ = delete_no_hovered_ = delete_skip_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = delete_skip_pressed_ = false;
    if (skip_delete_confirmation_in_session_) {
        confirm_delete_request();
        return;
    }
    showing_delete_popup_ = true;
}

void AssetLibraryUI::cancel_delete_request() {
    showing_delete_popup_ = false;
    clear_delete_state();
}

void AssetLibraryUI::confirm_delete_request() {
    if (bulk_delete_mode_) {
        execute_bulk_delete_queue();
        return;
    }
    if (!pending_delete_) {
        clear_delete_state();
        showing_delete_popup_ = false;
        return;
    }
    const PendingDeleteInfo pending = *pending_delete_;
    showing_delete_popup_ = false;
    perform_delete(pending);
    clear_delete_state();
}

void AssetLibraryUI::clear_delete_state() {
    pending_delete_.reset();
    delete_yes_hovered_ = delete_no_hovered_ = false;
    delete_yes_pressed_ = delete_no_pressed_ = false;
    delete_skip_hovered_ = delete_skip_pressed_ = false;
    delete_modal_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_yes_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_no_rect_ = SDL_Rect{0, 0, 0, 0};
    delete_skip_rect_ = SDL_Rect{0, 0, 0, 0};
    bulk_delete_queue_.clear();
    bulk_delete_mode_ = false;
}

void AssetLibraryUI::update_delete_modal_geometry(int screen_w, int screen_h) {
    const int modal_w = 420;
    const int modal_h = 210;
    delete_modal_rect_ = SDL_Rect{
        std::max(0, screen_w / 2 - modal_w / 2), std::max(0, screen_h / 2 - modal_h / 2), modal_w, modal_h };
    const int button_w = 140;
    const int button_h = 40;
    const int button_gap = 20;
    const int total_w = button_w * 2 + button_gap;
    const int buttons_x = delete_modal_rect_.x + (delete_modal_rect_.w - total_w) / 2;
    const int buttons_y = delete_modal_rect_.y + delete_modal_rect_.h - button_h - 20;
    delete_yes_rect_ = SDL_Rect{ buttons_x, buttons_y, button_w, button_h };
    delete_no_rect_ = SDL_Rect{ buttons_x + button_w + button_gap, buttons_y, button_w, button_h };
    const int skip_button_h = 32;
    const int skip_button_w = delete_modal_rect_.w - 72;
    const int skip_button_x = delete_modal_rect_.x + (delete_modal_rect_.w - skip_button_w) / 2;
    const int skip_button_y = buttons_y - skip_button_h - 16;
    delete_skip_rect_ = SDL_Rect{ skip_button_x, skip_button_y, skip_button_w, skip_button_h };
}

void AssetLibraryUI::handle_create_button_pressed() {
    if (!search_box_) {
        return;
    }
    std::string raw_value = search_box_->value();
    std::string trimmed = devmode::utils::trim_whitespace_copy(raw_value);
    if (trimmed.empty()) {
        show_search_error("Enter a name before creating.");
        return;
    }
    if (library_owner_ && library_owner_->get(trimmed)) {
        show_search_error("'" + trimmed + "' already exists.");
        return;
    }
    const CreateAssetResult result = create_new_asset(trimmed);
    switch (result) {
        case CreateAssetResult::Success:
            clear_search_error();
            break;
        case CreateAssetResult::AlreadyExists:
            show_search_error("'" + trimmed + "' already exists.");
            break;
        case CreateAssetResult::Failed:
        default:
            show_search_error("Failed to create asset.");
            break;
    }
}

void AssetLibraryUI::show_search_error(const std::string& message) {
    if (!search_box_) {
        return;
    }
    search_error_active_ = true;
    const std::string label_text = "Search - " + message;
    search_box_->set_label_text(label_text);
    search_box_->set_label_color_override(kSearchErrorColor);
}

void AssetLibraryUI::clear_search_error() {
    if (!search_error_active_) {
        return;
    }
    search_error_active_ = false;
    if (search_box_) {
        search_box_->reset_label_text();
        search_box_->clear_label_color_override();
    }
}

AssetLibraryUI::CreateAssetResult AssetLibraryUI::create_new_asset(const std::string& raw_name) {
    std::string name = devmode::utils::trim_whitespace_copy(raw_name);
    if (name.empty()) {
        return CreateAssetResult::Failed;
    }

    if (!manifest_store_owner_) {
        std::cerr << "[AssetLibraryUI] Manifest store unavailable; cannot create '" << name << "'\n";
        return CreateAssetResult::Failed;
    }

    auto session = manifest_store_owner_->begin_asset_edit(name, true);
    if (!session) {
        std::cerr << "[AssetLibraryUI] Failed to begin manifest session for '" << name << "'\n";
        return CreateAssetResult::Failed;
    }

    if (!session.is_new_asset()) {
        std::cerr << "[AssetLibraryUI] Asset '" << name << "' already exists\n";
        session.cancel();
        return CreateAssetResult::AlreadyExists;
    }

    fs::path base = asset_paths::assets_root_path();
    fs::path dir = base / name;

    try {
        if (!fs::exists(base)) {
            fs::create_directories(base);
        }
        if (fs::exists(dir)) {
            std::cerr << "[AssetLibraryUI] Asset directory '" << dir << "' already exists\n";
            session.cancel();
            return CreateAssetResult::AlreadyExists;
        }
        fs::create_directories(dir);
        fs::create_directories(dir / "default");

        const std::string asset_dir_str = dir.lexically_normal().generic_string();

        nlohmann::json default_anim = {
            {"loop", true},
            {"locked", false},
            {"reverse_source", false},
            {"flipped_source", false},
            {"rnd_start", false},
            {"source", nlohmann::json{
                {"kind", "folder"},
                {"path", "default"},
                {"name", ""}
            }}
};

        nlohmann::json manifest_entry = {
            {"asset_name", name},
            {"asset_type", "Object"},
            {"animations", nlohmann::json{{"default", default_anim}}},
            {"start", "default"},
            {"asset_directory", asset_dir_str}
};
        manifest_entry["tags"] = nlohmann::json::array();
        manifest_entry["anti_tags"] = nlohmann::json::array();
        manifest_entry["neighbor_search_distance"] = 500;
        manifest_entry["render_radius"] = 0;
        manifest_entry["update_radius"] = 0;
        manifest_entry["min_same_type_distance"] = 0;
        manifest_entry["min_distance_all"] = 0;
        manifest_entry["can_invert"] = false;
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
            return CreateAssetResult::Failed;
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
            library_owner_->add_asset(name, manifest_entry);
            if (assets_owner_) {
                if (SDL_Renderer* renderer = assets_owner_->renderer()) {
                    library_owner_->ensureAllAnimationsLoaded(renderer);
                }

                auto new_info = library_owner_->get(name);
                if (new_info) {
                    assets_owner_->open_asset_info_editor(new_info);
                    assets_owner_->open_animation_editor_for_asset(new_info);
                }
            }
        }

        preview_attempted_.erase(name);
        items_cached_ = false;
        filter_dirty_ = true;
        tiles_.clear();
        return CreateAssetResult::Success;
    } catch (const std::exception& e) {
        std::cerr << "[AssetLibraryUI] Exception creating asset '" << name
                  << "': " << e.what() << "\n";
        std::error_code ec;
        fs::remove_all(dir, ec);
        return CreateAssetResult::Failed;
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
        delete_skip_hovered_ = SDL_PointInRect(&p, &delete_skip_rect_);
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
        if (SDL_PointInRect(&p, &delete_skip_rect_)) {
            delete_skip_pressed_ = true;
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
        const bool inside_skip = SDL_PointInRect(&p, &delete_skip_rect_);
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
        if (inside_skip && delete_skip_pressed_) {
            delete_yes_pressed_ = false;
            delete_no_pressed_ = false;

            skip_delete_confirmation_in_session_ = true;
            delete_skip_pressed_ = false;
            confirm_delete_request();
            return true;
        }
        delete_yes_pressed_ = false;
        delete_no_pressed_ = false;
        delete_skip_pressed_ = false;
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
            return (!it->second.frames.empty() && !it->second.frames.front()->variants.empty()) ? it->second.frames.front()->variants[0].base_texture : nullptr;
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
        if (!kv.second.frames.empty() && !kv.second.frames.front()->variants.empty()) {
            return kv.second.frames.front()->variants[0].base_texture;
        }
    }
    return nullptr;
}

std::optional<AssetLibraryUI::AreaRef> AssetLibraryUI::consume_area_selection() {
    if (pending_area_selection_) {
        auto out = pending_area_selection_;
        pending_area_selection_.reset();
        return out;
    }
    return std::nullopt;
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
            if (search_error_active_) {
                clear_search_error();
            }
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
    }
}

void AssetLibraryUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!floating_) return;
    floating_->render(r);

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
        dm_draw::DrawBeveledRect( r, box, corner_radius, bevel_depth, panel_bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
        const SDL_Color panel_border = DMStyles::Border();
        dm_draw::DrawRoundedOutline( r, box, corner_radius, 1, panel_border);

        std::string message;
        if (bulk_delete_mode_) {
            const size_t count = bulk_delete_queue_.size();
            message = "Are you sure you want to permanently delete " + std::to_string(count) + " assets?";
            if (count > 0) {
                const size_t preview_count = std::min<size_t>(count, 3);
                std::string preview;
                for (size_t idx = 0; idx < preview_count; ++idx) {
                    const std::string& label = bulk_delete_queue_[idx].name.empty() ? "(Unnamed)" : bulk_delete_queue_[idx].name;
                    if (!preview.empty()) {
                        preview.append(", ");
                    }
                    preview.append(label);
                }
                if (count > preview_count) {
                    preview.append(", ...");
                }
                message += " This includes: " + preview + ".";
            }
        } else {
            std::string asset_label = "(Unnamed)";
            if (pending_delete_ && !pending_delete_->name.empty()) {
                asset_label = pending_delete_->name;
            }
            message = "Are you sure you want to permanently delete \"" + asset_label + "\"?";
        }

        const int text_margin = 16 + bevel_depth;
        const int text_rect_bottom = delete_skip_rect_.y - 12;
        const int text_height = std::max(0, text_rect_bottom - (box.y + text_margin));
        SDL_Rect text_rect{ box.x + text_margin, box.y + text_margin, box.w - 2 * text_margin, text_height };
        text_rect.w = std::max(0, text_rect.w);
        text_rect.h = std::max(0, text_rect.h);
        TTF_Font* font = devmode::utils::load_font(18);
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
                                  std::min(tw, text_rect.w), std::min(th, text_rect.h) };
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
            dm_draw::DrawBeveledRect( r, rect, corner_radius, bevel_depth, bg, highlight, shadow, false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());
            dm_draw::DrawRoundedOutline( r, rect, corner_radius, 1, style.border);

            TTF_Font* btn_font = devmode::utils::load_font(style.label.font_size > 0 ? style.label.font_size : 16);
            if (!btn_font) {
                btn_font = devmode::utils::load_font(16);
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
                            rect.x + (rect.w - tw) / 2, text_y, tw, th };
                        SDL_RenderCopy(r, tex, nullptr, &dst);
                        SDL_DestroyTexture(tex);
                    }
                }
            }
};

        render_button(delete_yes_rect_, delete_yes_hovered_, delete_yes_pressed_, "Yes, delete", DMStyles::DeleteButton());
        render_button(delete_no_rect_, delete_no_hovered_, delete_no_pressed_, "Cancel", DMStyles::HeaderButton());
        render_button(delete_skip_rect_, delete_skip_hovered_, delete_skip_pressed_, "Yes, don't show me this again", DMStyles::ListButton());
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

    if (floating_->handle_event(e)) {
        handled = true;
    }

    if (!handled && search_widget_ && search_box_ && e.type == SDL_TEXTINPUT) {
        if (!search_box_->is_editing()) {
            search_box_->start_editing();
        }
        if (search_widget_->handle_event(e)) {
            handled = true;
        }
    }

    return handled;
}

std::shared_ptr<AssetInfo> AssetLibraryUI::consume_selection() {
    auto selection = pending_selection_;
    pending_selection_.reset();
    return selection;
}

bool AssetLibraryUI::is_input_blocking_at(int mx, int my) const {
    if (!floating_ || !floating_->is_visible() || !floating_->is_expanded())
        return false;
    SDL_Point p{ mx, my };
    if (showing_delete_popup_) {
        if (delete_modal_rect_.w > 0 && delete_modal_rect_.h > 0) {
            if (SDL_PointInRect(&p, &delete_modal_rect_) == SDL_TRUE) {
                return true;
            }
        }

        return SDL_PointInRect(&p, &floating_->rect()) == SDL_TRUE;
    }
    return SDL_PointInRect(&p, &floating_->rect()) == SDL_TRUE;
}

bool AssetLibraryUI::is_dragging_asset() const {
    return false;
}

void AssetLibraryUI::set_position(int x, int y) {
    if (floating_) {
        floating_->set_position(x, y);
    }
}

void AssetLibraryUI::set_expanded(bool e) {
    if (floating_) {
        floating_->set_expanded(e);

        rebuild_rows();
    }
}

bool AssetLibraryUI::is_expanded() const {
    return floating_ && floating_->is_expanded();
}
