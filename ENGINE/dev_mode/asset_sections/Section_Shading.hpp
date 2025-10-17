#pragma once

#include "../DockableCollapsible.hpp"

#include <memory>
#include <string>
#include <vector>

#include "asset/asset_info.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include "dev_mode/dm_styles.hpp"
#include "dev_mode/draw_utils.hpp"
#include "dev_mode/widgets.hpp"

class AssetInfoUI;

class Section_Shading : public DockableCollapsible {
public:
    Section_Shading() : DockableCollapsible("Shading", false) {}
    void set_ui(AssetInfoUI* ui) { ui_ = ui; }
    ~Section_Shading() override = default;

    void build() override {
        widgets_.clear();
        preview_consumed_ = false;
        Rows rows;

        build_button_ = std::make_unique<DMButton>("Build Shadowmask", &DMStyles::AccentButton(), 200, DMButton::height());
        auto build_widget = std::make_unique<ButtonWidget>(build_button_.get(), [this]() { this->on_build(); });
        rows.push_back({ build_widget.get() });
        widgets_.push_back(std::move(build_widget));

        preview_button_ = std::make_unique<DMButton>("Show Preview", &DMStyles::SecondaryButton(), 200, DMButton::height());
        auto preview_widget = std::make_unique<ButtonWidget>(preview_button_.get(), [this]() { this->on_preview(); });
        rows.push_back({ preview_widget.get() });
        widgets_.push_back(std::move(preview_widget));

        save_button_ = std::make_unique<DMButton>("Save Shadowmask", &DMStyles::AccentButton(), 200, DMButton::height());
        auto save_widget = std::make_unique<ButtonWidget>(save_button_.get(), [this]() { this->on_save(); });
        rows.push_back({ save_widget.get() });
        widgets_.push_back(std::move(save_widget));

        set_rows(rows);
    }

    bool handle_event(const SDL_Event& e) override {
        return DockableCollapsible::handle_event(e);
    }

    void render_content(SDL_Renderer* r) const override {
        DockableCollapsible::render_content(r);
    }

private:
    void on_build();
    void on_preview();
    void on_save();

    std::unique_ptr<DMButton> build_button_;
    std::unique_ptr<DMButton> preview_button_;
    std::unique_ptr<DMButton> save_button_;
    std::vector<std::unique_ptr<Widget>> widgets_{};
    bool preview_consumed_ = false;
    AssetInfoUI* ui_ = nullptr;
};

inline void Section_Shading::on_build() {
    if (ui_) {
        ui_->regenerate_shadow_masks();
    }
    preview_consumed_ = false;
    if (preview_button_) {
        preview_button_->set_text("Show Preview");
    }
}

inline void Section_Shading::on_preview() {
    if (preview_consumed_) {
        return;
    }
    if (ui_) {
        ui_->regenerate_shadow_masks();
    }
    preview_consumed_ = true;
    if (preview_button_) {
        preview_button_->set_text("Preview Shown");
    }
}

inline void Section_Shading::on_save() {
    if (!info_) {
        return;
    }
    (void)info_->commit_manifest();
    if (ui_) {
        ui_->regenerate_shadow_masks();
    }
}

