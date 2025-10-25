#include "AnimationInspectorPanel.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <functional>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "AnimationDocument.hpp"
#include "AudioPanel.hpp"
#include "MovementSummaryWidget.hpp"
#include "OnEndSelector.hpp"
#include "PlaybackSettingsPanel.hpp"
#include "PreviewProvider.hpp"
#include "SourceConfigPanel.hpp"
#include "string_utils.hpp"
#include "dm_styles.hpp"
#include "dev_mode/dm_icons.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"

namespace animation_editor {

namespace {

constexpr int kInspectorPadding    = 10;
constexpr int kInspectorItemGap    = 4;
constexpr int kInspectorSectionGap = 10;

constexpr int kPreviewHeight = 120;
constexpr int kHeaderButtonWidth = 160;
constexpr int kMinToggleButtonWidth = 120;

int header_toggle_width() {
    return DMButton::height();
}

void render_label(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color) {
    if (!renderer || text.empty()) {
        return;
    }

    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        return;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        TTF_CloseFont(font);
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect dst{x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
    TTF_CloseFont(font);
}

int text_width(const DMLabelStyle& style, const std::string& text) {
    TTF_Font* font = style.open_font();
    if (!font) {
        return 0;
    }
    int width = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &width, nullptr) != 0) {
        width = 0;
    }
    TTF_CloseFont(font);
    return width;
}

bool is_pointer_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEMOTION:
            return true;
        default:
            return false;
    }
}

}

AnimationInspectorPanel::AnimationInspectorPanel() {
    source_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    playback_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    movement_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    on_end_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    audio_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    collapse_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), header_toggle_width(), DMButton::height());
    update_section_toggle_labels();
    update_collapse_toggle_label();
}

void AnimationInspectorPanel::set_document(std::shared_ptr<AnimationDocument> document) {
    document_ = std::move(document);
    rebuild_widgets();
}

void AnimationInspectorPanel::set_animation_id(const std::string& animation_id) {
    animation_id_ = animation_id;
    rebuild_widgets();
}

void AnimationInspectorPanel::set_bounds(const SDL_Rect& bounds) {
    bounds_ = bounds;
    layout_dirty_ = true;
}

void AnimationInspectorPanel::set_preview_provider(std::shared_ptr<PreviewProvider> provider) {
    preview_provider_ = std::move(provider);
}

void AnimationInspectorPanel::set_source_services(std::shared_ptr<CroppingService> cropping,
                                                  std::shared_ptr<AsyncTaskQueue> tasks) {
    cropping_service_ = std::move(cropping);
    task_queue_ = std::move(tasks);
    apply_dependencies();
}

void AnimationInspectorPanel::set_source_folder_picker(PathPicker picker) {
    folder_picker_ = std::move(picker);
    apply_dependencies();
}

void AnimationInspectorPanel::set_source_animation_picker(AnimationPicker picker) {
    animation_picker_ = std::move(picker);
    apply_dependencies();
}

void AnimationInspectorPanel::set_source_gif_picker(PathPicker picker) {
    gif_picker_ = std::move(picker);
    apply_dependencies();
}

void AnimationInspectorPanel::set_source_png_sequence_picker(MultiPathPicker picker) {
    png_sequence_picker_ = std::move(picker);
    apply_dependencies();
}

void AnimationInspectorPanel::set_source_status_callback(StatusCallback callback) {
    status_callback_ = std::move(callback);
    apply_dependencies();
}

void AnimationInspectorPanel::set_frame_edit_callback(FrameEditCallback callback) {
    frame_edit_callback_ = std::move(callback);
    apply_dependencies();
}

void AnimationInspectorPanel::set_audio_importer(std::shared_ptr<AudioImporter> importer) {
    audio_importer_ = std::move(importer);
    apply_dependencies();
}

void AnimationInspectorPanel::set_audio_file_picker(AudioFilePicker picker) {
    audio_file_picker_ = std::move(picker);
    apply_dependencies();
}

int AnimationInspectorPanel::height_for_width(int width) const {
    const int padding = kInspectorPadding;
    const int section_gap = kInspectorSectionGap;
    const int item_gap = kInspectorItemGap;
    const int header_height = std::max(DMTextBox::height(), DMButton::height());
    const int content_width = std::max(0, width - padding * 2);

    int total = padding;
    total += header_height;
    total += item_gap;
    total += kPreviewHeight;

    if (collapsed_) {
        total += padding;
        return total;
    }

    int toggle_height = layout_toggle_row(0, 0, content_width, false);
    if (toggle_height > 0) {
        total += section_gap;
        total += toggle_height;
    }

    auto visible_gap = false;
    auto add_section_height = [&](int height) {
        if (height <= 0) {
            return;
        }
        if (!visible_gap) {
            total += item_gap;
            visible_gap = true;
        } else {
            total += section_gap;
        }
        total += height;
};

    if (!source_collapsed_ && source_config_) {
        add_section_height(source_config_->preferred_height(content_width));
    }
    if (!playback_collapsed_ && playback_settings_) {
        add_section_height(playback_settings_->preferred_height(content_width));
    }
    if (!movement_collapsed_ && movement_summary_) {
        add_section_height(movement_summary_->preferred_height(content_width));
    }
    if (!on_end_collapsed_ && on_end_selector_) {
        add_section_height(on_end_selector_->preferred_height(content_width));
    }
    if (!audio_collapsed_ && audio_panel_) {
        add_section_height(audio_panel_->preferred_height(content_width));
    }

    total += padding;
    return total;
}

void AnimationInspectorPanel::update() {
    layout_widgets();

    if (rename_pending_ && name_box_ && !name_box_->is_editing()) {
        commit_rename();
    }

    refresh_start_indicator();

    if (!collapsed_) {
        if (!source_collapsed_ && source_config_) source_config_->update();
        if (!playback_collapsed_ && playback_settings_) playback_settings_->update();
        if (!movement_collapsed_ && movement_summary_) movement_summary_->update();
        if (!on_end_collapsed_ && on_end_selector_) on_end_selector_->update();
        if (!audio_collapsed_ && audio_panel_) audio_panel_->update();
    }
}

void AnimationInspectorPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    layout_widgets();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    dm_draw::DrawBeveledRect( renderer, bounds_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelBG(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    dm_draw::DrawBeveledRect( renderer, header_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelHeader(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    if (collapse_toggle_button_) collapse_toggle_button_->render(renderer);
    if (name_box_) name_box_->render(renderer);
    if (start_button_) start_button_->render(renderer);
    if (delete_button_) delete_button_->render(renderer);

    if (is_start_animation_) {
        const DMLabelStyle& style = DMStyles::Label();
        SDL_Color accent = DMStyles::AccentButton().text;
        render_label(renderer, "Start Animation", header_rect_.x + kInspectorPadding, header_rect_.y + header_rect_.h - style.font_size - DMSpacing::small_gap(), accent);
    }

    dm_draw::DrawBeveledRect( renderer, preview_rect_, DMStyles::CornerRadius(), DMStyles::BevelDepth(), DMStyles::PanelHeader(), DMStyles::HighlightColor(), DMStyles::ShadowColor(), false, DMStyles::HighlightIntensity(), DMStyles::ShadowIntensity());

    SDL_Rect preview_clip = preview_rect_;
    const int preview_inset = DMStyles::BevelDepth();
    preview_clip.x += preview_inset;
    preview_clip.y += preview_inset;
    preview_clip.w = std::max(0, preview_clip.w - preview_inset * 2);
    preview_clip.h = std::max(0, preview_clip.h - preview_inset * 2);
    const bool clip_preview = preview_clip.w > 0 && preview_clip.h > 0;
    if (clip_preview) {
        SDL_RenderSetClipRect(renderer, &preview_clip);
    }

    if (preview_provider_) {
        SDL_Texture* texture = preview_provider_->get_preview_texture(renderer, animation_id_);
        if (texture) {
            int tex_w = 0;
            int tex_h = 0;
            SDL_QueryTexture(texture, nullptr, nullptr, &tex_w, &tex_h);
            const int padding = kInspectorPadding;
            int avail_w = std::max(1, preview_rect_.w - padding * 2);
            int avail_h = std::max(1, preview_rect_.h - padding * 2);
            float scale = std::min(avail_w / static_cast<float>(tex_w), avail_h / static_cast<float>(tex_h));
            int draw_w = static_cast<int>(tex_w * scale);
            int draw_h = static_cast<int>(tex_h * scale);
            SDL_Rect dst{preview_rect_.x + (preview_rect_.w - draw_w) / 2, preview_rect_.y + (preview_rect_.h - draw_h) / 2,
                         draw_w, draw_h};
            SDL_RenderCopy(renderer, texture, nullptr, &dst);
        } else {
            const DMLabelStyle& style = DMStyles::Label();
            SDL_Color color = style.color;
            render_label(renderer, "No Preview Available", preview_rect_.x + (preview_rect_.w - text_width(style, "No Preview Available")) / 2, preview_rect_.y + preview_rect_.h / 2 - style.font_size / 2, color);
        }
    }

    if (clip_preview) {
        SDL_RenderSetClipRect(renderer, nullptr);
    }

    if (!collapsed_) {
        if (source_toggle_button_) source_toggle_button_->render(renderer);
        if (playback_toggle_button_) playback_toggle_button_->render(renderer);
        if (movement_toggle_button_) movement_toggle_button_->render(renderer);
        if (on_end_toggle_button_) on_end_toggle_button_->render(renderer);
        if (audio_toggle_button_) audio_toggle_button_->render(renderer);

        if (!source_collapsed_ && source_config_) source_config_->render(renderer);
        if (!playback_collapsed_ && playback_settings_) playback_settings_->render(renderer);
        if (!movement_collapsed_ && movement_summary_) movement_summary_->render(renderer);
        if (!on_end_collapsed_ && on_end_selector_) on_end_selector_->render(renderer);
        if (!audio_collapsed_ && audio_panel_) audio_panel_->render(renderer);
    }
}

bool AnimationInspectorPanel::handle_event(const SDL_Event& e) {
    layout_widgets();

    if (is_pointer_event(e)) {
        SDL_Point p;
        if (e.type == SDL_MOUSEMOTION) {
            p.x = e.motion.x;
            p.y = e.motion.y;
        } else {
            p.x = e.button.x;
            p.y = e.button.y;
        }
        if (!SDL_PointInRect(&p, &bounds_)) {
            return false;
        }
    }

    bool handled = false;
    bool was_editing = name_box_ && name_box_->is_editing();
    auto handle_name_event = [&](const SDL_Event& ev) {
        if (!name_box_) {
            return;
        }
        if (name_box_->handle_event(ev)) {
            rename_pending_ = true;
            handled = true;
        }
};

    if (e.type == SDL_KEYDOWN) {
        if (name_box_ && name_box_->is_editing()) {
            handle_name_event(e);
        }

        if (e.key.keysym.sym == SDLK_TAB) {
            auto order = focus_order();
            if (!order.empty()) {
                int direction = (e.key.keysym.mod & KMOD_SHIFT) ? -1 : 1;
                int count = static_cast<int>(order.size());
                int next = focus_index_;
                if (next < 0 || next >= count) {
                    next = (direction > 0) ? -1 : 0;
                }
                next += direction;
                if (next < 0) {
                    next = count - 1;
                } else if (next >= count) {
                    next = 0;
                }
                set_focus(order[next]);
                handled = true;
            }
        } else {
            auto order = focus_order();
            if (focus_index_ >= 0 && focus_index_ < static_cast<int>(order.size())) {
                FocusTarget target = order[focus_index_];
                if ((e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER ||
                     e.key.keysym.sym == SDLK_SPACE) &&
                    !(target == FocusTarget::kName && name_box_ && name_box_->is_editing())) {
                    activate_focus_target(target);
                    handled = true;
                }
            }
        }
    } else if (e.type == SDL_TEXTINPUT) {
        if (name_box_ && name_box_->is_editing()) {
            handle_name_event(e);
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        SDL_Point p{e.button.x, e.button.y};
        FocusTarget clicked = FocusTarget::kNone;
        if (collapse_toggle_button_ && SDL_PointInRect(&p, &collapse_toggle_rect_)) {
            clicked = FocusTarget::kCollapse;
        } else if (name_box_ && SDL_PointInRect(&p, &name_box_->rect())) {
            clicked = FocusTarget::kName;
        } else if (start_button_ && SDL_PointInRect(&p, &start_button_->rect())) {
            clicked = FocusTarget::kStart;
        } else if (delete_button_ && SDL_PointInRect(&p, &delete_button_->rect())) {
            clicked = FocusTarget::kDelete;
        } else if (!collapsed_) {
            if (source_toggle_button_ && SDL_PointInRect(&p, &source_toggle_button_->rect())) {
                clicked = FocusTarget::kToggleSources;
            } else if (playback_toggle_button_ && SDL_PointInRect(&p, &playback_toggle_button_->rect())) {
                clicked = FocusTarget::kTogglePlayback;
            } else if (movement_toggle_button_ && SDL_PointInRect(&p, &movement_toggle_button_->rect())) {
                clicked = FocusTarget::kToggleMovement;
            } else if (on_end_toggle_button_ && SDL_PointInRect(&p, &on_end_toggle_button_->rect())) {
                clicked = FocusTarget::kToggleOnEnd;
            } else if (audio_toggle_button_ && SDL_PointInRect(&p, &audio_toggle_button_->rect())) {
                clicked = FocusTarget::kToggleAudio;
            }
        }
        set_focus(clicked);
    }

    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        handle_name_event(e);
    }

    if (collapse_toggle_button_ && collapse_toggle_button_->handle_event(e)) {
        handled = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            activate_focus_target(FocusTarget::kCollapse);
        }
    }

    if (start_button_ && start_button_->handle_event(e)) {
        handled = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            activate_focus_target(FocusTarget::kStart);
        }
    }

    if (delete_button_ && delete_button_->handle_event(e)) {
        handled = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            activate_focus_target(FocusTarget::kDelete);
        }
    }

    if (!collapsed_) {
        auto handle_toggle = [&](DMButton* button, FocusTarget target) {
            if (!button) {
                return;
            }
            if (button->handle_event(e)) {
                handled = true;
                if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                    activate_focus_target(target);
                }
            }
};

        handle_toggle(source_toggle_button_.get(), FocusTarget::kToggleSources);
        handle_toggle(playback_toggle_button_.get(), FocusTarget::kTogglePlayback);
        handle_toggle(movement_toggle_button_.get(), FocusTarget::kToggleMovement);
        handle_toggle(on_end_toggle_button_.get(), FocusTarget::kToggleOnEnd);
        handle_toggle(audio_toggle_button_.get(), FocusTarget::kToggleAudio);

        if (!source_collapsed_ && source_config_ && source_config_->handle_event(e)) handled = true;
        if (!playback_collapsed_ && playback_settings_ && playback_settings_->handle_event(e)) handled = true;
        if (!movement_collapsed_ && movement_summary_ && movement_summary_->handle_event(e)) handled = true;
        if (!on_end_collapsed_ && on_end_selector_ && on_end_selector_->handle_event(e)) handled = true;
        if (!audio_collapsed_ && audio_panel_ && audio_panel_->handle_event(e)) handled = true;
    }

    if (was_editing && name_box_ && !name_box_->is_editing()) {
        rename_pending_ = true;
    }

    return handled;
}

void AnimationInspectorPanel::rebuild_widgets() {
    if (!document_ || animation_id_.empty()) {
        return;
    }

    if (!name_box_) {
        name_box_ = std::make_unique<DMTextBox>("Animation ID", animation_id_);
    } else {
        name_box_->set_value(animation_id_);
    }

    if (!start_button_) {
        start_button_ = std::make_unique<DMButton>("Set as Start", &DMStyles::AccentButton(), kHeaderButtonWidth, DMButton::height());
    }

    if (!delete_button_) {
        delete_button_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), kHeaderButtonWidth, DMButton::height());
    }

    if (!source_toggle_button_) {
        source_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    } else {
        source_toggle_button_->set_style(&DMStyles::FooterToggleButton());
    }
    if (!playback_toggle_button_) {
        playback_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    } else {
        playback_toggle_button_->set_style(&DMStyles::FooterToggleButton());
    }
    if (!movement_toggle_button_) {
        movement_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    } else {
        movement_toggle_button_->set_style(&DMStyles::FooterToggleButton());
    }
    if (!on_end_toggle_button_) {
        on_end_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    } else {
        on_end_toggle_button_->set_style(&DMStyles::FooterToggleButton());
    }
    if (!audio_toggle_button_) {
        audio_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::FooterToggleButton(), 200, DMButton::height());
    } else {
        audio_toggle_button_->set_style(&DMStyles::FooterToggleButton());
    }
    if (!collapse_toggle_button_) {
        collapse_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), header_toggle_width(), DMButton::height());
    }

    if (!source_config_) {
        source_config_ = std::make_unique<SourceConfigPanel>();
    }
    source_config_->set_document(document_);
    source_config_->set_animation_id(animation_id_);

    if (!playback_settings_) {
        playback_settings_ = std::make_unique<PlaybackSettingsPanel>();
    }
    playback_settings_->set_document(document_);
    playback_settings_->set_animation_id(animation_id_);

    if (!movement_summary_) {
        movement_summary_ = std::make_unique<MovementSummaryWidget>();
    }
    movement_summary_->set_document(document_);
    movement_summary_->set_animation_id(animation_id_);

    if (!on_end_selector_) {
        on_end_selector_ = std::make_unique<OnEndSelector>();
    }
    on_end_selector_->set_document(document_);
    on_end_selector_->set_animation_id(animation_id_);

    if (!audio_panel_) {
        audio_panel_ = std::make_unique<AudioPanel>();
    }
    audio_panel_->set_document(document_);
    audio_panel_->set_animation_id(animation_id_);

    rename_pending_ = false;
    refresh_start_indicator();
    layout_dirty_ = true;
    apply_dependencies();
    update_section_toggle_labels();
    update_collapse_toggle_label();
}

void AnimationInspectorPanel::refresh_totals() {
    if (movement_summary_) {
        movement_summary_->set_document(document_);
        movement_summary_->set_animation_id(animation_id_);
    }
}

void AnimationInspectorPanel::apply_dependencies() {
    if (source_config_) {
        source_config_->set_services(cropping_service_, task_queue_);
        source_config_->set_folder_picker(folder_picker_);
        source_config_->set_animation_picker(animation_picker_);
        source_config_->set_gif_picker(gif_picker_);
        source_config_->set_png_sequence_picker(png_sequence_picker_);
        source_config_->set_status_callback(status_callback_);
    }

    if (movement_summary_) {
        movement_summary_->set_edit_callback(frame_edit_callback_);
    }

    if (audio_panel_) {
        audio_panel_->set_importer(audio_importer_);
        audio_panel_->set_file_picker(audio_file_picker_);
    }
}

void AnimationInspectorPanel::update_collapse_toggle_label() {
    if (!collapse_toggle_button_) {
        return;
    }

    const std::string label = collapsed_ ? std::string(DMIcons::CollapseCollapsed()) : std::string(DMIcons::CollapseExpanded());
    collapse_toggle_button_->set_text(label);
}

void AnimationInspectorPanel::update_section_toggle_labels() {
    auto make_label = [](bool collapsed, std::string_view name) {
        std::string label = collapsed ? std::string(DMIcons::CollapseCollapsed()) : std::string(DMIcons::CollapseExpanded());
        label.push_back(' ');
        label.append(name.begin(), name.end());
        return label;
};

    if (source_toggle_button_) {
        source_toggle_button_->set_text(make_label(source_collapsed_, "Frame Sources"));
    }
    if (playback_toggle_button_) {
        playback_toggle_button_->set_text(make_label(playback_collapsed_, "Playback"));
    }
    if (movement_toggle_button_) {
        movement_toggle_button_->set_text(make_label(movement_collapsed_, "Movement"));
    }
    if (on_end_toggle_button_) {
        on_end_toggle_button_->set_text(make_label(on_end_collapsed_, "On End"));
    }
    if (audio_toggle_button_) {
        audio_toggle_button_->set_text(make_label(audio_collapsed_, "Audio"));
    }
}

void AnimationInspectorPanel::notify_section_visibility(const std::string& section_name, bool visible) {
    if (!status_callback_) {
        return;
    }
    if (visible) {
        status_callback_(section_name + " section expanded.");
    } else {
        status_callback_(section_name + " section hidden. Use the section toggles to reveal it.");
    }
}

std::vector<AnimationInspectorPanel::FocusTarget> AnimationInspectorPanel::focus_order() const {
    std::vector<FocusTarget> order;
    if (collapse_toggle_button_) order.push_back(FocusTarget::kCollapse);
    if (name_box_) order.push_back(FocusTarget::kName);
    if (start_button_) order.push_back(FocusTarget::kStart);
    if (delete_button_) order.push_back(FocusTarget::kDelete);
    if (!collapsed_) {
        if (source_toggle_button_) order.push_back(FocusTarget::kToggleSources);
        if (playback_toggle_button_) order.push_back(FocusTarget::kTogglePlayback);
        if (movement_toggle_button_) order.push_back(FocusTarget::kToggleMovement);
        if (on_end_toggle_button_) order.push_back(FocusTarget::kToggleOnEnd);
        if (audio_toggle_button_) order.push_back(FocusTarget::kToggleAudio);
    }
    return order;
}

void AnimationInspectorPanel::set_focus(FocusTarget target) {
    current_focus_target_ = target;
    if (target == FocusTarget::kNone) {
        focus_index_ = -1;
        return;
    }
    auto order = focus_order();
    focus_index_ = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == target) {
            focus_index_ = static_cast<int>(i);
            break;
        }
    }
    if (focus_index_ >= 0) {
        announce_focus(target);
    } else {
        current_focus_target_ = FocusTarget::kNone;
    }
}

void AnimationInspectorPanel::announce_focus(FocusTarget target) const {
    if (!status_callback_) {
        return;
    }

    switch (target) {
        case FocusTarget::kCollapse:
            status_callback_("Focus: Collapse inspector toggle. Press Enter or Space to toggle.");
            break;
        case FocusTarget::kName:
            status_callback_("Focus: Animation name. Press Enter to begin editing.");
            break;
        case FocusTarget::kStart:
            status_callback_("Focus: Mark animation as start. Press Enter or Space to apply.");
            break;
        case FocusTarget::kDelete:
            status_callback_("Focus: Delete animation. Press Enter or Space to remove.");
            break;
        case FocusTarget::kToggleSources:
            status_callback_("Focus: Frame Sources toggle. Press Enter or Space to expand or collapse.");
            break;
        case FocusTarget::kTogglePlayback:
            status_callback_("Focus: Playback settings toggle. Press Enter or Space to expand or collapse.");
            break;
        case FocusTarget::kToggleMovement:
            status_callback_("Focus: Movement summary toggle. Press Enter or Space to expand or collapse.");
            break;
        case FocusTarget::kToggleOnEnd:
            status_callback_("Focus: On End behavior toggle. Press Enter or Space to expand or collapse.");
            break;
        case FocusTarget::kToggleAudio:
            status_callback_("Focus: Audio settings toggle. Press Enter or Space to expand or collapse.");
            break;
        case FocusTarget::kNone:
        default:
            break;
    }
}

void AnimationInspectorPanel::activate_focus_target(FocusTarget target) {
    switch (target) {
        case FocusTarget::kCollapse:
            collapsed_ = !collapsed_;
            update_collapse_toggle_label();
            layout_dirty_ = true;
            if (status_callback_) {
                status_callback_(collapsed_ ? "Inspector collapsed." : "Inspector expanded.");
            }
            break;
        case FocusTarget::kName:
            if (status_callback_) {
                status_callback_("Press Enter inside the name field to begin editing.");
            }
            break;
        case FocusTarget::kStart:
            if (document_) {
                document_->set_start_animation(animation_id_);
            }
            refresh_start_indicator();
            if (status_callback_) {
                status_callback_("Animation marked as start animation.");
            }
            break;
        case FocusTarget::kDelete:
            if (document_) {
                document_->delete_animation(animation_id_);
            }
            if (status_callback_) {
                status_callback_("Animation deleted from the document.");
            }
            break;
        case FocusTarget::kToggleSources:
            source_collapsed_ = !source_collapsed_;
            update_section_toggle_labels();
            layout_dirty_ = true;
            notify_section_visibility("Frame Sources", !source_collapsed_);
            break;
        case FocusTarget::kTogglePlayback:
            playback_collapsed_ = !playback_collapsed_;
            update_section_toggle_labels();
            layout_dirty_ = true;
            notify_section_visibility("Playback", !playback_collapsed_);
            break;
        case FocusTarget::kToggleMovement:
            movement_collapsed_ = !movement_collapsed_;
            update_section_toggle_labels();
            layout_dirty_ = true;
            notify_section_visibility("Movement", !movement_collapsed_);
            break;
        case FocusTarget::kToggleOnEnd:
            on_end_collapsed_ = !on_end_collapsed_;
            update_section_toggle_labels();
            layout_dirty_ = true;
            notify_section_visibility("On End", !on_end_collapsed_);
            break;
        case FocusTarget::kToggleAudio:
            audio_collapsed_ = !audio_collapsed_;
            update_section_toggle_labels();
            layout_dirty_ = true;
            notify_section_visibility("Audio", !audio_collapsed_);
            break;
        case FocusTarget::kNone:
        default:
            break;
    }
    refresh_focus_index();
}

void AnimationInspectorPanel::layout_widgets() const {
    if (!layout_dirty_) {
        return;
    }

    auto* self = const_cast<AnimationInspectorPanel*>(this);
    self->layout_dirty_ = false;

    const int padding = kInspectorPadding;
    const int section_gap = kInspectorSectionGap;
    const int item_gap = kInspectorItemGap;
    const int toolbar_gap = DMSpacing::small_gap();

    const int width = std::max(0, bounds_.w - padding * 2);
    const int content_width = width;
    const int x = bounds_.x + padding;
    int y = bounds_.y + padding;

    const int collapse_width = collapse_toggle_button_ ? header_toggle_width() : 0;
    const int button_height = DMButton::height();

    int toolbar_right = x + width;
    int action_buttons = 0;
    if (start_button_) ++action_buttons;
    if (delete_button_) ++action_buttons;
    int action_width = action_buttons * kHeaderButtonWidth + std::max(0, action_buttons - 1) * toolbar_gap;
    int action_left = toolbar_right - action_width;

    if (start_button_) {
        SDL_Rect rect{action_left, y, kHeaderButtonWidth, button_height};
        start_button_->set_rect(rect);
        action_left += kHeaderButtonWidth + toolbar_gap;
    }
    if (delete_button_) {
        SDL_Rect rect{action_left, y, kHeaderButtonWidth, button_height};
        delete_button_->set_rect(rect);
    }

    int name_left = x + collapse_width;
    if (collapse_width > 0) {
        name_left += toolbar_gap;
    }
    int name_right = toolbar_right - action_width;
    if (action_width > 0) {
        name_right -= toolbar_gap;
    }
    int name_width = std::max(0, name_right - name_left);

    if (collapse_toggle_button_) {
        collapse_toggle_rect_ = SDL_Rect{x, y, collapse_width, button_height};
        collapse_toggle_button_->set_rect(collapse_toggle_rect_);
    } else {
        collapse_toggle_rect_ = SDL_Rect{x, y, 0, 0};
    }

    int name_height = DMTextBox::height();
    if (name_box_) {
        name_height = name_box_->height_for_width(name_width);
        SDL_Rect rect{name_left, y, name_width, name_height};
        name_box_->set_rect(rect);
    }

    int header_height = std::max(name_height, button_height);
    y += header_height + item_gap;
    self->header_rect_ = SDL_Rect{bounds_.x, bounds_.y, bounds_.w, y - bounds_.y};

    self->preview_rect_ = SDL_Rect{x, y, width, kPreviewHeight};
    y += kPreviewHeight;

    if (collapsed_) {
        toggles_rect_ = SDL_Rect{x, y, width, 0};
        if (source_toggle_button_) source_toggle_button_->set_rect(toggles_rect_);
        if (playback_toggle_button_) playback_toggle_button_->set_rect(toggles_rect_);
        if (movement_toggle_button_) movement_toggle_button_->set_rect(toggles_rect_);
        if (on_end_toggle_button_) on_end_toggle_button_->set_rect(toggles_rect_);
        if (audio_toggle_button_) audio_toggle_button_->set_rect(toggles_rect_);

        source_rect_ = SDL_Rect{x, y, width, 0};
        if (source_config_) source_config_->set_bounds(source_rect_);
        playback_rect_ = SDL_Rect{x, y, width, 0};
        if (playback_settings_) playback_settings_->set_bounds(playback_rect_);
        movement_rect_ = SDL_Rect{x, y, width, 0};
        if (movement_summary_) movement_summary_->set_bounds(movement_rect_);
        on_end_rect_ = SDL_Rect{x, y, width, 0};
        if (on_end_selector_) on_end_selector_->set_bounds(on_end_rect_);
        audio_rect_ = SDL_Rect{x, y, width, 0};
        if (audio_panel_) audio_panel_->set_bounds(audio_rect_);
        refresh_focus_index();
        return;
    }

    y += section_gap;
    int toggle_height = layout_toggle_row(x, y, width, true);
    y += toggle_height;

    bool placed_section = false;

    auto assign_section = [&](bool collapsed, auto* widget, SDL_Rect& rect) {
        if (!widget) {
            rect = SDL_Rect{x, y, width, 0};
            return;
        }
        int section_height = widget->preferred_height(content_width);
        if (collapsed || section_height <= 0) {
            rect = SDL_Rect{x, y, width, 0};
            widget->set_bounds(rect);
            return;
        }
        if (!placed_section) {
            y += item_gap;
            placed_section = true;
        } else {
            y += section_gap;
        }
        rect = SDL_Rect{x, y, width, section_height};
        widget->set_bounds(rect);
        y += section_height;
};

    assign_section(source_collapsed_ || !source_config_, source_config_.get(), source_rect_);
    assign_section(playback_collapsed_ || !playback_settings_, playback_settings_.get(), playback_rect_);
    assign_section(movement_collapsed_ || !movement_summary_, movement_summary_.get(), movement_rect_);
    assign_section(on_end_collapsed_ || !on_end_selector_, on_end_selector_.get(), on_end_rect_);
    assign_section(audio_collapsed_ || !audio_panel_, audio_panel_.get(), audio_rect_);

    refresh_focus_index();
}

void AnimationInspectorPanel::refresh_focus_index() const {
    auto* self = const_cast<AnimationInspectorPanel*>(this);
    auto order = focus_order();
    self->focus_index_ = -1;
    if (self->current_focus_target_ == FocusTarget::kNone) {
        return;
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == self->current_focus_target_) {
            self->focus_index_ = static_cast<int>(i);
            return;
        }
    }
    self->current_focus_target_ = FocusTarget::kNone;
}

void AnimationInspectorPanel::commit_rename() {
    if (!rename_pending_ || !document_ || !name_box_) {
        rename_pending_ = false;
        if (name_box_) {
            name_box_->set_value(animation_id_);
        }
        return;
    }

    std::string desired = strings::trim_copy(name_box_->value());
    if (desired.empty() || desired == animation_id_) {
        name_box_->set_value(animation_id_);
        rename_pending_ = false;
        return;
    }

    const std::string old_id = animation_id_;
    auto before = document_->animation_ids();
    document_->rename_animation(animation_id_, desired);
    auto after = document_->animation_ids();

    std::string new_id = desired;
    if (std::find(after.begin(), after.end(), desired) == after.end()) {
        for (const auto& id : after) {
            if (std::find(before.begin(), before.end(), id) == before.end()) {
                new_id = id;
                break;
            }
        }
    }

    animation_id_ = new_id;
    name_box_->set_value(animation_id_);
    rename_pending_ = false;

    if (preview_provider_) {
        preview_provider_->invalidate(old_id);
        preview_provider_->invalidate(animation_id_);
    }

    refresh_totals();
    refresh_start_indicator();
    layout_dirty_ = true;
}

void AnimationInspectorPanel::refresh_start_indicator() {
    bool new_state = false;
    if (document_) {
        auto start = document_->start_animation();
        new_state = start.has_value() && *start == animation_id_;
    }

    is_start_animation_ = new_state;

    if (start_button_) {
        if (is_start_animation_) {
            start_button_->set_text("Start Animation");
            start_button_->set_style(&DMStyles::HeaderButton());
        } else {
            start_button_->set_text("Set as Start");
            start_button_->set_style(&DMStyles::AccentButton());
        }
    }
}

int AnimationInspectorPanel::layout_toggle_row(int origin_x, int origin_y, int width, bool apply) const {
    auto* self = const_cast<AnimationInspectorPanel*>(this);
    if (width <= 0) {
        if (apply) {
            self->toggles_rect_ = SDL_Rect{origin_x, origin_y, width, 0};
            if (source_toggle_button_) source_toggle_button_->set_rect(self->toggles_rect_);
            if (playback_toggle_button_) playback_toggle_button_->set_rect(self->toggles_rect_);
            if (movement_toggle_button_) movement_toggle_button_->set_rect(self->toggles_rect_);
            if (on_end_toggle_button_) on_end_toggle_button_->set_rect(self->toggles_rect_);
            if (audio_toggle_button_) audio_toggle_button_->set_rect(self->toggles_rect_);
        }
        return 0;
    }

    const int button_height = DMButton::height();
    const int gap = DMSpacing::small_gap();

    int line_x = origin_x;
    int line_y = origin_y;
    int bottom = origin_y;

    auto place_button = [&](DMButton* button) {
        if (!button) {
            return;
        }
        int button_width = button->preferred_width();
        button_width = std::max(kMinToggleButtonWidth, std::min(button_width, width));
        if (line_x > origin_x && line_x + button_width > origin_x + width) {
            line_x = origin_x;
            line_y += button_height + gap;
        }
        if (apply) {
            SDL_Rect rect{line_x, line_y, button_width, button_height};
            button->set_rect(rect);
        }
        line_x += button_width + gap;
        bottom = std::max(bottom, line_y + button_height);
};

    place_button(source_toggle_button_.get());
    place_button(playback_toggle_button_.get());
    place_button(movement_toggle_button_.get());
    place_button(on_end_toggle_button_.get());
    place_button(audio_toggle_button_.get());

    int height = std::max(0, bottom - origin_y);
    if (apply) {
        self->toggles_rect_ = SDL_Rect{origin_x, origin_y, width, height};
    }
    return height;
}

}
