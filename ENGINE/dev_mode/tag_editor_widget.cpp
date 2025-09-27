#include "tag_editor_widget.hpp"

#include "dm_styles.hpp"
#include "tag_library.hpp"
#include "tag_utils.hpp"

#include <algorithm>

namespace {
constexpr int kChipWidth = 132;
constexpr int kRecommendChipWidth = 148;

std::unique_ptr<DMButton> make_button(const std::string& text, const DMButtonStyle& style, int width) {
    return std::make_unique<DMButton>(text, &style, width, DMButton::height());
}

}

TagEditorWidget::TagEditorWidget() = default;
TagEditorWidget::~TagEditorWidget() = default;

void TagEditorWidget::set_tags(const std::vector<std::string>& tags,
                               const std::vector<std::string>& anti_tags) {
    tags_.clear();
    anti_tags_.clear();
    for (const auto& t : tags) {
        auto norm = normalize(t);
        if (!norm.empty()) tags_.insert(std::move(norm));
    }
    for (const auto& t : anti_tags) {
        auto norm = normalize(t);
        if (norm.empty()) continue;
        if (tags_.count(norm)) continue;
        anti_tags_.insert(std::move(norm));
    }
    refresh_recommendations();
    rebuild_buttons();
    mark_dirty();
}

std::vector<std::string> TagEditorWidget::tags() const {
    return std::vector<std::string>(tags_.begin(), tags_.end());
}

std::vector<std::string> TagEditorWidget::anti_tags() const {
    return std::vector<std::string>(anti_tags_.begin(), anti_tags_.end());
}

void TagEditorWidget::set_on_changed(std::function<void(const std::vector<std::string>&,
                                                        const std::vector<std::string>&)> cb) {
    on_changed_ = std::move(cb);
}

void TagEditorWidget::set_rect(const SDL_Rect& r) {
    rect_ = r;
    mark_dirty();
}

int TagEditorWidget::height_for_width(int w) const {
    auto self = const_cast<TagEditorWidget*>(this);
    int width = std::max(40, w);
    return self->layout(width, 0, 0, false);
}

bool TagEditorWidget::handle_event(const SDL_Event& e) {
    layout_if_needed();
    bool used = false;
    for (const auto& chip : tag_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { remove_tag(value); }, used);
    }
    for (const auto& chip : anti_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { remove_anti_tag(value); }, used);
    }
    for (const auto& chip : rec_tag_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { add_tag(value); }, used);
    }
    for (const auto& chip : rec_anti_chips_) {
        handle_chip_click(chip, e, [this](const std::string& value) { add_anti_tag(value); }, used);
    }
    return used;
}

void TagEditorWidget::render(SDL_Renderer* r) const {
    if (!r) return;
    const_cast<TagEditorWidget*>(this)->layout_if_needed();

    draw_label(r, "Tags", tags_label_rect_);
    draw_label(r, "Anti Tags", anti_label_rect_);
    if (rec_tags_label_rect_.w > 0) draw_label(r, "Tag Recommendations", rec_tags_label_rect_);
    if (rec_anti_label_rect_.w > 0) draw_label(r, "Anti Tag Recommendations", rec_anti_label_rect_);

    for (const auto& chip : tag_chips_) {
        if (chip.button) chip.button->render(r);
    }
    for (const auto& chip : anti_chips_) {
        if (chip.button) chip.button->render(r);
    }
    for (const auto& chip : rec_tag_chips_) {
        if (chip.button) chip.button->render(r);
    }
    for (const auto& chip : rec_anti_chips_) {
        if (chip.button) chip.button->render(r);
    }
}

void TagEditorWidget::rebuild_buttons() {
    tag_chips_.clear();
    anti_chips_.clear();
    rec_tag_chips_.clear();
    rec_anti_chips_.clear();

    const auto& tag_style = DMStyles::CreateButton();
    const auto& anti_style = DMStyles::DeleteButton();
    const auto& rec_style = DMStyles::ListButton();

    for (const auto& value : tags_) {
        Chip chip;
        chip.value = value;
        chip.button = make_button(value, tag_style, kChipWidth);
        tag_chips_.push_back(std::move(chip));
    }

    for (const auto& value : anti_tags_) {
        Chip chip;
        chip.value = value;
        chip.button = make_button(value, anti_style, kChipWidth);
        anti_chips_.push_back(std::move(chip));
    }

    for (const auto& value : recommended_) {
        Chip add_chip;
        add_chip.value = value;
        add_chip.button = make_button("+ " + value, rec_style, kRecommendChipWidth);
        rec_tag_chips_.push_back(std::move(add_chip));

        Chip anti_chip;
        anti_chip.value = value;
        anti_chip.button = make_button("- " + value, rec_style, kRecommendChipWidth);
        rec_anti_chips_.push_back(std::move(anti_chip));
    }
}

void TagEditorWidget::refresh_recommendations() {
    std::set<std::string> pool(TagLibrary::instance().tags().begin(), TagLibrary::instance().tags().end());
    pool.insert(tags_.begin(), tags_.end());
    pool.insert(anti_tags_.begin(), anti_tags_.end());

    recommended_.clear();
    for (const auto& value : pool) {
        if (!tags_.count(value) && !anti_tags_.count(value)) {
            recommended_.push_back(value);
        }
    }
}

void TagEditorWidget::mark_dirty() {
    layout_dirty_ = true;
}

void TagEditorWidget::layout_if_needed() const {
    if (!layout_dirty_) return;
    auto self = const_cast<TagEditorWidget*>(this);
    int width = std::max(40, rect_.w);
    self->layout(width, rect_.x, rect_.y, true);
    self->layout_dirty_ = false;
}

int TagEditorWidget::layout(int width, int origin_x, int origin_y, bool apply) {
    const int pad = DMSpacing::small_gap();
    const int label_gap = DMSpacing::label_gap();
    const int section_gap = DMSpacing::item_gap();
    int y = origin_y + pad;
    int label_h = label_height();

    if (apply) {
        tags_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
    }
    y += label_h + label_gap;
    y = layout_grid(tag_chips_, width, origin_x, y, apply);
    y += section_gap;

    if (apply) {
        anti_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
    }
    y += label_h + label_gap;
    y = layout_grid(anti_chips_, width, origin_x, y, apply);
    y += section_gap;

    if (!recommended_.empty()) {
        if (apply) {
            rec_tags_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
        }
        y += label_h + label_gap;
        y = layout_grid(rec_tag_chips_, width, origin_x, y, apply);
        y += section_gap;

        if (apply) {
            rec_anti_label_rect_ = SDL_Rect{ origin_x, y, width, label_h };
        }
        y += label_h + label_gap;
        y = layout_grid(rec_anti_chips_, width, origin_x, y, apply);
        y += section_gap;
    } else if (apply) {
        rec_tags_label_rect_ = SDL_Rect{0,0,0,0};
        rec_anti_label_rect_ = SDL_Rect{0,0,0,0};
    }

    y += pad;
    return y - origin_y;
}

int TagEditorWidget::layout_grid(std::vector<Chip>& chips, int width, int origin_x, int start_y, bool apply) {
    if (chips.empty()) return start_y;
    const int gap = DMSpacing::small_gap();
    int chip_width = &chips == &rec_tag_chips_ || &chips == &rec_anti_chips_ ? kRecommendChipWidth : kChipWidth;
    chip_width = std::min(chip_width, width);
    chip_width = std::max(chip_width, 80);
    int columns = std::max(1, (width + gap) / (chip_width + gap));
    int chip_height = DMButton::height();

    for (size_t i = 0; i < chips.size(); ++i) {
        int row = static_cast<int>(i / columns);
        int col = static_cast<int>(i % columns);
        int x = origin_x + col * (chip_width + gap);
        int y = start_y + row * (chip_height + gap);
        if (apply && chips[i].button) {
            chips[i].button->set_rect(SDL_Rect{ x, y, chip_width, chip_height });
        }
    }

    int rows = static_cast<int>((chips.size() + columns - 1) / columns);
    if (rows <= 0) return start_y;
    int total_height = rows * chip_height + (rows - 1) * gap;
    return start_y + total_height;
}

int TagEditorWidget::label_height() {
    static int cached = 0;
    if (cached > 0) return cached;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) {
        cached = style.font_size;
        return cached;
    }
    int w = 0;
    int h = 0;
    TTF_SizeUTF8(font, "Tags", &w, &h);
    TTF_CloseFont(font);
    cached = h;
    return cached;
}

void TagEditorWidget::draw_label(SDL_Renderer* r, const std::string& text, const SDL_Rect& rect) const {
    if (rect.w <= 0 && rect.h <= 0) return;
    const DMLabelStyle& style = DMStyles::Label();
    TTF_Font* font = style.open_font();
    if (!font) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), style.color);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        if (tex) {
            SDL_Rect dst{ rect.x, rect.y, surf->w, surf->h };
            SDL_RenderCopy(r, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        SDL_FreeSurface(surf);
    }
    TTF_CloseFont(font);
}

void TagEditorWidget::handle_chip_click(const Chip& chip, const SDL_Event& e,
                                        const std::function<void(const std::string&)>& on_click,
                                        bool& used) {
    if (!chip.button) return;
    if (chip.button->handle_event(e)) {
        used = true;
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            on_click(chip.value);
        }
    }
}

void TagEditorWidget::add_tag(const std::string& value) {
    auto norm = normalize(value);
    if (norm.empty()) return;
    bool changed = false;
    if (anti_tags_.erase(norm) > 0) changed = true;
    if (tags_.insert(norm).second) changed = true;
    if (changed) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

void TagEditorWidget::add_anti_tag(const std::string& value) {
    auto norm = normalize(value);
    if (norm.empty()) return;
    bool changed = false;
    if (tags_.erase(norm) > 0) changed = true;
    if (anti_tags_.insert(norm).second) changed = true;
    if (changed) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

void TagEditorWidget::remove_tag(const std::string& value) {
    auto norm = normalize(value);
    if (tags_.erase(norm) > 0) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

void TagEditorWidget::remove_anti_tag(const std::string& value) {
    auto norm = normalize(value);
    if (anti_tags_.erase(norm) > 0) {
        refresh_recommendations();
        rebuild_buttons();
        mark_dirty();
        notify_changed();
    }
}

std::string TagEditorWidget::normalize(const std::string& value) {
    return tag_utils::normalize(value);
}

void TagEditorWidget::notify_changed() {
    if (!on_changed_) return;
    on_changed_(tags(), anti_tags());
}
