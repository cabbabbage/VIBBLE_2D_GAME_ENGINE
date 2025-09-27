#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <limits>

#include "widgets.hpp"

class TagEditorWidget : public Widget {
public:
    TagEditorWidget();
    ~TagEditorWidget() override;

    void set_tags(const std::vector<std::string>& tags,
                  const std::vector<std::string>& anti_tags);

    std::vector<std::string> tags() const;
    std::vector<std::string> anti_tags() const;

    void set_on_changed(std::function<void(const std::vector<std::string>&,
                                           const std::vector<std::string>&)> cb);

    // Widget interface
    void set_rect(const SDL_Rect& r) override;
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int w) const override;
    bool handle_event(const SDL_Event& e) override;
    void render(SDL_Renderer* r) const override;

private:
    struct Chip {
        std::string value;
        std::unique_ptr<DMButton> button;
    };

    void rebuild_buttons();
    void refresh_recommendations();
    void mark_dirty();
    void layout_if_needed() const;
    int layout(int width, int origin_x, int origin_y, bool apply);
    int layout_grid(std::vector<Chip>& chips, int width, int origin_x, int start_y, bool apply,
                    size_t visible_count = std::numeric_limits<size_t>::max());
    static int label_height();
    void draw_label(SDL_Renderer* r, const std::string& text, const SDL_Rect& rect) const;

    void handle_chip_click(const Chip& chip, const SDL_Event& e,
                           const std::function<void(const std::string&)>& on_click,
                           bool& used);

    void add_tag(const std::string& value);
    void add_anti_tag(const std::string& value);
    void remove_tag(const std::string& value);
    void remove_anti_tag(const std::string& value);
    static std::string normalize(const std::string& value);

    void notify_changed();
    void reset_toggle_state();
    void update_toggle_labels();

    SDL_Rect rect_{0,0,0,0};
    mutable bool layout_dirty_ = true;

    std::set<std::string> tags_;
    std::set<std::string> anti_tags_;
    std::vector<std::string> recommended_;

    mutable SDL_Rect tags_label_rect_{0,0,0,0};
    mutable SDL_Rect anti_label_rect_{0,0,0,0};
    mutable SDL_Rect rec_tags_label_rect_{0,0,0,0};
    mutable SDL_Rect rec_anti_label_rect_{0,0,0,0};

    std::vector<Chip> tag_chips_;
    std::vector<Chip> anti_chips_;
    std::vector<Chip> rec_tag_chips_;
    std::vector<Chip> rec_anti_chips_;

    bool show_all_tag_recs_ = false;
    bool show_all_anti_recs_ = false;
    std::unique_ptr<DMButton> show_more_tags_btn_;
    std::unique_ptr<DMButton> show_more_anti_btn_;

    std::function<void(const std::vector<std::string>&,
                       const std::vector<std::string>&)> on_changed_;
};
