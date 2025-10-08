#include "AnimationInspectorPanel.hpp"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <functional>
#include <cctype>
#include <string>
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
#include "dev_mode/widgets.hpp"

namespace animation_editor {

namespace {

constexpr int kInspectorPadding    = 10;
constexpr int kInspectorItemGap    = 4;
constexpr int kInspectorSectionGap = 10;

constexpr int kPreviewHeight = 120;
constexpr int kHeaderButtonWidth = 160;

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

}  // namespace

AnimationInspectorPanel::AnimationInspectorPanel() {
    source_toggle_button_ = std::make_unique<DMButton>("[+] Frame Sources", &DMStyles::HeaderButton(), 200,
                                                      DMButton::height());
    collapse_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), header_toggle_width(),
                                                         DMButton::height());
    update_source_toggle_label();
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

void AnimationInspectorPanel::set_movement_edit_callback(MovementEditCallback callback) {
    movement_edit_callback_ = std::move(callback);
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
    const int gap = kInspectorSectionGap;
    const int header_height = std::max(DMTextBox::height(), DMButton::height());
    const int toggle_height = DMButton::height();

    const int content_width = std::max(0, width - padding * 2);

    const bool collapsed = collapsed_;

    int source_height = (!collapsed && !source_collapsed_ && source_config_)
                            ? source_config_->preferred_height(content_width)
                            : 0;
    int playback_height = !collapsed && playback_settings_ ? playback_settings_->preferred_height(content_width) : 0;
    int movement_height = !collapsed && movement_summary_ ? movement_summary_->preferred_height(content_width) : 0;
    int on_end_height = !collapsed && on_end_selector_ ? on_end_selector_->preferred_height(content_width) : 0;
    int audio_height = !collapsed && audio_panel_ ? audio_panel_->preferred_height(content_width) : 0;

    auto has_following = [&](int index) {
        switch (index) {
            case 0:
                return playback_height > 0 || movement_height > 0 || on_end_height > 0 || audio_height > 0;
            case 1:
                return movement_height > 0 || on_end_height > 0 || audio_height > 0;
            case 2:
                return on_end_height > 0 || audio_height > 0;
            case 3:
                return audio_height > 0;
            default:
                return false;
        }
    };

    int total = padding;  // top padding
    total += header_height;
    total += kInspectorItemGap;
    total += kPreviewHeight;

    if (collapsed) {
        total += padding;
        return total;
    }

    const bool has_sections = (source_height > 0 || playback_height > 0 || movement_height > 0 || on_end_height > 0 ||
                               audio_height > 0);
    if (has_sections) {
        total += gap;
    }

    total += toggle_height;
    if (source_height <= 0 && (playback_height > 0 || movement_height > 0 || on_end_height > 0 || audio_height > 0)) {
        total += gap;
    }
    if (source_height > 0) {
        total += kInspectorItemGap;
        total += source_height;
        if (has_following(0)) {
            total += gap;
        }
    }
    if (playback_height > 0) {
        total += playback_height;
        if (has_following(1)) {
            total += gap;
        }
    }
    if (movement_height > 0) {
        total += movement_height;
        if (has_following(2)) {
            total += gap;
        }
    }
    if (on_end_height > 0) {
        total += on_end_height;
        if (has_following(3)) {
            total += gap;
        }
    }
    if (audio_height > 0) {
        total += audio_height;
    }
    total += padding;  // bottom padding

    return total;
}

void AnimationInspectorPanel::update() {
    layout_widgets();

    if (rename_pending_ && name_box_ && !name_box_->is_editing()) {
        commit_rename();
    }

    refresh_start_indicator();

    if (!collapsed_) {
        if (source_config_) source_config_->update();
        if (playback_settings_) playback_settings_->update();
        if (movement_summary_) movement_summary_->update();
        if (on_end_selector_) on_end_selector_->update();
        if (audio_panel_) audio_panel_->update();
    }
}

void AnimationInspectorPanel::render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    layout_widgets();

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const SDL_Color& bg = DMStyles::PanelBG();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &bounds_);

    const SDL_Color& header_bg = DMStyles::PanelHeader();
    SDL_SetRenderDrawColor(renderer, header_bg.r, header_bg.g, header_bg.b, header_bg.a);
    SDL_RenderFillRect(renderer, &header_rect_);

    const SDL_Color& border = DMStyles::Border();
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &bounds_);

    if (collapse_toggle_button_) collapse_toggle_button_->render(renderer);
    if (name_box_) name_box_->render(renderer);
    if (start_button_) start_button_->render(renderer);
    if (delete_button_) delete_button_->render(renderer);

    // Start indicator label
    if (is_start_animation_) {
        const DMLabelStyle& style = DMStyles::Label();
        SDL_Color accent = DMStyles::AccentButton().text;
        render_label(renderer, "Start Animation", header_rect_.x + kInspectorPadding,
                     header_rect_.y + header_rect_.h - style.font_size - DMSpacing::small_gap(), accent);
    }

    // Preview panel background
    SDL_SetRenderDrawColor(renderer, 28, 37, 48, 230);
    SDL_RenderFillRect(renderer, &preview_rect_);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &preview_rect_);

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
            render_label(renderer, "No Preview Available",
                         preview_rect_.x + (preview_rect_.w - text_width(style, "No Preview Available")) / 2,
                         preview_rect_.y + preview_rect_.h / 2 - style.font_size / 2, color);
        }
    }

    if (!collapsed_) {
        if (source_toggle_button_) source_toggle_button_->render(renderer);
        if (!source_collapsed_ && source_config_) source_config_->render(renderer);
        if (playback_settings_) playback_settings_->render(renderer);
        if (movement_summary_) movement_summary_->render(renderer);
        if (on_end_selector_) on_end_selector_->render(renderer);
        if (audio_panel_) audio_panel_->render(renderer);
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
    if (collapse_toggle_button_ && collapse_toggle_button_->handle_event(e)) {
        handled = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            collapsed_ = !collapsed_;
            update_collapse_toggle_label();
            layout_dirty_ = true;
        }
    }

    if (name_box_ && name_box_->handle_event(e)) {
        rename_pending_ = true;
        handled = true;
    }

    if (start_button_ && start_button_->handle_event(e)) {
        if (document_) {
            document_->set_start_animation(animation_id_);
        }
        refresh_start_indicator();
        handled = true;
    }

    if (delete_button_ && delete_button_->handle_event(e)) {
        if (document_) {
            document_->delete_animation(animation_id_);
        }
        handled = true;
    }

    if (!collapsed_ && source_toggle_button_) {
        bool consumed = source_toggle_button_->handle_event(e);
        if (consumed) {
            handled = true;
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                source_collapsed_ = !source_collapsed_;
                update_source_toggle_label();
                layout_dirty_ = true;
            }
        }
    }

    if (!collapsed_) {
        if (!source_collapsed_ && source_config_ && source_config_->handle_event(e)) handled = true;
        if (playback_settings_ && playback_settings_->handle_event(e)) handled = true;
        if (movement_summary_ && movement_summary_->handle_event(e)) handled = true;
        if (on_end_selector_ && on_end_selector_->handle_event(e)) handled = true;
        if (audio_panel_ && audio_panel_->handle_event(e)) handled = true;
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
        start_button_ = std::make_unique<DMButton>("Set as Start", &DMStyles::AccentButton(), kHeaderButtonWidth,
                                                   DMButton::height());
    }

    if (!delete_button_) {
        delete_button_ = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), kHeaderButtonWidth,
                                                    DMButton::height());
    }

    if (!source_toggle_button_) {
        source_toggle_button_ = std::make_unique<DMButton>("[+] Frame Sources", &DMStyles::HeaderButton(), 200,
                                                           DMButton::height());
    }
    if (!collapse_toggle_button_) {
        collapse_toggle_button_ = std::make_unique<DMButton>("", &DMStyles::HeaderButton(), header_toggle_width(),
                                                             DMButton::height());
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
    update_source_toggle_label();
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
        movement_summary_->set_edit_callback(movement_edit_callback_);
    }

    if (audio_panel_) {
        audio_panel_->set_importer(audio_importer_);
        audio_panel_->set_file_picker(audio_file_picker_);
    }
}

void AnimationInspectorPanel::update_source_toggle_label() {
    if (!source_toggle_button_) {
        return;
    }

    const char* label = source_collapsed_ ? "[+] Frame Sources" : "[-] Frame Sources";
    source_toggle_button_->set_text(label);
}

void AnimationInspectorPanel::update_collapse_toggle_label() {
    if (!collapse_toggle_button_) {
        return;
    }

    collapse_toggle_button_->set_text(collapsed_ ? ">" : "v");
}

void AnimationInspectorPanel::layout_widgets() const {
    if (!layout_dirty_) {
        return;
    }

    auto* self = const_cast<AnimationInspectorPanel*>(this);
    self->layout_dirty_ = false;

    const int padding = kInspectorPadding;
    const int gap = kInspectorSectionGap;
    const int item_gap = kInspectorItemGap;
    const int width = std::max(0, bounds_.w - padding * 2);
    const int content_width = width;
    int x = bounds_.x + padding;
    int y = bounds_.y + padding;

    int toggle_width = collapse_toggle_button_ ? header_toggle_width() : 0;
    int name_left = x + toggle_width;
    if (toggle_width > 0) {
        name_left += item_gap;
    }

    int right_edge = x + width;
    int delete_x = std::max(name_left, right_edge - kHeaderButtonWidth);
    int start_x = std::max(name_left, delete_x - item_gap - kHeaderButtonWidth);

    int name_width = std::max(0, start_x - name_left - item_gap);
    if (name_width <= 0) {
        name_width = std::max(0, right_edge - name_left - item_gap);
    }

    if (self->start_button_) {
        SDL_Rect start_rect{start_x, y, kHeaderButtonWidth, DMButton::height()};
        self->start_button_->set_rect(start_rect);
    }

    if (self->delete_button_) {
        SDL_Rect delete_rect{delete_x, y, kHeaderButtonWidth, DMButton::height()};
        self->delete_button_->set_rect(delete_rect);
    }

    if (self->collapse_toggle_button_) {
        self->collapse_toggle_rect_ = SDL_Rect{x, y, toggle_width, DMButton::height()};
        self->collapse_toggle_button_->set_rect(self->collapse_toggle_rect_);
    } else {
        self->collapse_toggle_rect_ = SDL_Rect{x, y, 0, 0};
    }

    int name_height = DMTextBox::height();
    if (self->name_box_) {
        name_height = self->name_box_->height_for_width(name_width);
        SDL_Rect name_rect{name_left, y, name_width, name_height};
        self->name_box_->set_rect(name_rect);
    }

    int header_height = std::max(name_height, DMButton::height());
    y += header_height + item_gap;
    self->header_rect_ = SDL_Rect{bounds_.x, bounds_.y, bounds_.w, y - bounds_.y};

    self->preview_rect_ = SDL_Rect{x, y, width, kPreviewHeight};
    y += kPreviewHeight;

    if (collapsed_) {
        self->source_toggle_rect_ = SDL_Rect{x, y, width, 0};
        if (self->source_toggle_button_) self->source_toggle_button_->set_rect(self->source_toggle_rect_);
        self->source_rect_ = SDL_Rect{x, y, width, 0};
        if (self->source_config_) self->source_config_->set_bounds(self->source_rect_);
        self->playback_rect_ = SDL_Rect{x, y, width, 0};
        if (self->playback_settings_) self->playback_settings_->set_bounds(self->playback_rect_);
        self->movement_rect_ = SDL_Rect{x, y, width, 0};
        if (self->movement_summary_) self->movement_summary_->set_bounds(self->movement_rect_);
        self->on_end_rect_ = SDL_Rect{x, y, width, 0};
        if (self->on_end_selector_) self->on_end_selector_->set_bounds(self->on_end_rect_);
        self->audio_rect_ = SDL_Rect{x, y, width, 0};
        if (self->audio_panel_) self->audio_panel_->set_bounds(self->audio_rect_);
        return;
    }

    int expanded_source_height = (!source_collapsed_ && source_config_) ? source_config_->preferred_height(content_width) : 0;

    if (expanded_source_height > 0 || playback_settings_ || movement_summary_ || on_end_selector_ || audio_panel_) {
        y += gap;
    }

    self->source_toggle_rect_ = SDL_Rect{x, y, width, DMButton::height()};
    if (self->source_toggle_button_) self->source_toggle_button_->set_rect(self->source_toggle_rect_);
    y += DMButton::height();

    if (!source_collapsed_) {
        int source_height = expanded_source_height;
        if (source_height > 0) {
            y += item_gap;
            self->source_rect_ = SDL_Rect{x, y, width, source_height};
            if (self->source_config_) self->source_config_->set_bounds(self->source_rect_);
            y += source_height;
            y += gap;
        } else {
            self->source_rect_ = SDL_Rect{x, y, width, 0};
            if (self->source_config_) self->source_config_->set_bounds(self->source_rect_);
        }
    } else {
        self->source_rect_ = SDL_Rect{x, y, width, 0};
        if (self->source_config_) self->source_config_->set_bounds(self->source_rect_);
    }

    int playback_height = playback_settings_ ? playback_settings_->preferred_height(content_width) : 0;
    int movement_height = movement_summary_ ? movement_summary_->preferred_height(content_width) : 0;
    int on_end_height = on_end_selector_ ? on_end_selector_->preferred_height(content_width) : 0;
    int audio_height = audio_panel_ ? audio_panel_->preferred_height(content_width) : 0;

    if ((playback_height > 0 || movement_height > 0 || on_end_height > 0 || audio_height > 0) && expanded_source_height <= 0) {
        y += gap;
    }

    struct SectionInfo {
        int height;
        SDL_Rect* rect;
        std::function<void(const SDL_Rect&)> apply_bounds;
    };

    std::array<SectionInfo, 4> sections{{
        {playback_height, &self->playback_rect_, [self](const SDL_Rect& r) {
             if (self->playback_settings_) self->playback_settings_->set_bounds(r);
         }},
        {movement_height, &self->movement_rect_, [self](const SDL_Rect& r) {
             if (self->movement_summary_) self->movement_summary_->set_bounds(r);
         }},
        {on_end_height, &self->on_end_rect_, [self](const SDL_Rect& r) {
             if (self->on_end_selector_) self->on_end_selector_->set_bounds(r);
         }},
        {audio_height, &self->audio_rect_, [self](const SDL_Rect& r) {
             if (self->audio_panel_) self->audio_panel_->set_bounds(r);
         }},
    }};

    auto has_following_section = [&](size_t index) {
        for (size_t i = index + 1; i < sections.size(); ++i) {
            if (sections[i].height > 0) {
                return true;
            }
        }
        return false;
    };

    for (size_t i = 0; i < sections.size(); ++i) {
        const auto& sec = sections[i];
        SDL_Rect rect{x, y, width, std::max(0, sec.height)};
        *sec.rect = rect;
        if (sec.apply_bounds) {
            sec.apply_bounds(rect);
        }
        if (sec.height > 0) {
            y += sec.height;
            if (has_following_section(i)) {
                y += gap;
            }
        }
    }
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

}  // namespace animation_editor

