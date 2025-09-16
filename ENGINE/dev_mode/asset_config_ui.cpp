#include "asset_config_ui.hpp"
#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include "search_assets.hpp"

AssetConfigUI::AssetConfigUI() {
    // Allowed spawn methods
    spawn_methods_ = {"Random","Perimeter","Exact","Percent"};

    // Include "Exact Position" to match the runtime spawn options used by the
    // engine.  Without this, assets with that method would default to "Random"
    // when opened in the UI, leading to inconsistent behaviour and potential
    // crashes when editing.
    spawn_methods_ = {"Random","Center","Perimeter","Exact","Exact Position","Percent"};
    panel_ = std::make_unique<DockableCollapsible>("Asset", true, 0, 0);
    panel_->set_expanded(true);
    panel_->set_visible(false);

    b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 80, DMButton::height());
    b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() { close(); });

    add_button_ = std::make_unique<DMButton>("Add Candidate...", &DMStyles::CreateButton(), 180, DMButton::height());
    add_button_w_ = std::make_unique<ButtonWidget>(add_button_.get(), [this]() {
        // Hook into SearchAssets; for now just stub
        add_candidate("NewAsset", 100);
    });
}

bool AssetConfigUI::method_forces_single_quantity(const std::string& method) const {
    return method == "Exact" || method == "Percent";
}

void AssetConfigUI::set_position(int x, int y) {
    if (panel_) panel_->set_position(x, y);
}

void AssetConfigUI::load(const nlohmann::json& j) {
    spawn_id_ = j.value("spawn_id", std::string{});
    entry_ = j;

    // Ensure candidates
    if (!entry_.contains("candidates") || !entry_["candidates"].is_array()) {
        entry_["candidates"] = nlohmann::json::array();
    }
    bool has_null = false;
    for (auto& c : entry_["candidates"]) {
        if (c["name"].is_null()) { has_null = true; break; }
    }
    if (!has_null) {
        entry_["candidates"].push_back({{"name", nullptr}, {"chance", 0}});
    }

    // Spawn method
    method_ = 0;
    std::string m = j.value("position", spawn_methods_.front());
    for (size_t i=0; i<spawn_methods_.size(); ++i)
        if (spawn_methods_[i]==m) method_ = int(i);

    min_number_ = j.value("min_number", 1);
    max_number_ = j.value("max_number", 1);
    inherited_  = j.value("inherited", false);
    overlap_    = j.value("check_overlap", false);
    spacing_    = j.value("check_min_spacing", false);
    tag_        = j.value("tag", false);
    ep_x_min_   = j.value("ep_x_min", 50);
    ep_x_max_   = j.value("ep_x_max", 50);
    ep_y_min_   = j.value("ep_y_min", 50);
    ep_y_max_   = j.value("ep_y_max", 50);

    if (panel_) panel_->set_title(spawn_id_);

    rebuild_widgets();
    rebuild_rows();
}

void AssetConfigUI::open_panel() {
    if (panel_) panel_->set_visible(true);
}

void AssetConfigUI::close() {
    if (panel_) panel_->set_visible(false);
}

bool AssetConfigUI::visible() const { return panel_ && panel_->is_visible(); }

void AssetConfigUI::rebuild_widgets() {
    dd_method_ = std::make_unique<DMDropdown>("Method", spawn_methods_, method_);
    dd_method_w_ = std::make_unique<DropdownWidget>(dd_method_.get());

    s_minmax_.reset();
    s_minmax_w_.reset();
    s_border_.reset(); s_border_w_.reset();
    s_sector_center_.reset(); s_sector_center_w_.reset();
    s_sector_range_.reset(); s_sector_range_w_.reset();
    s_percent_x_.reset(); s_percent_x_w_.reset();
    s_percent_y_.reset(); s_percent_y_w_.reset();

    const std::string& m = spawn_methods_[method_];
    if (!method_forces_single_quantity(m)) {
        s_minmax_ = std::make_unique<DMRangeSlider>(0, 50, min_number_, max_number_);
        s_minmax_w_ = std::make_unique<RangeSliderWidget>(s_minmax_.get());
    }
    if (m == "Perimeter") {
        s_border_ = std::make_unique<DMSlider>("Border%", 0, 100, entry_.value("border_shift", 0));
        s_range_ = std::make_unique<DMRangeSlider>(0, 100, min_, max_);
        s_range_w_ = std::make_unique<RangeSliderWidget>(s_range_.get());
    }
    if (m == "Perimeter") {
        s_border_ = std::make_unique<DMSlider>("Border%", 0, 100, border_);
        s_border_w_ = std::make_unique<SliderWidget>(s_border_.get());
        s_sector_center_ = std::make_unique<DMSlider>("SectorC", 0, 359, entry_.value("sector_center", 0));
        s_sector_center_w_ = std::make_unique<SliderWidget>(s_sector_center_.get());
        s_sector_range_ = std::make_unique<DMSlider>("SectorR", 0, 360, entry_.value("sector_range", 360));
        s_sector_range_w_ = std::make_unique<SliderWidget>(s_sector_range_.get());
    } else if (m == "Percent") {
        s_percent_x_ = std::make_unique<DMRangeSlider>(-100, 100, ep_x_min_, ep_x_max_);
        s_percent_x_w_ = std::make_unique<RangeSliderWidget>(s_percent_x_.get());
        s_percent_y_ = std::make_unique<DMRangeSlider>(-100, 100, ep_y_min_, ep_y_max_);
        s_percent_y_w_ = std::make_unique<RangeSliderWidget>(s_percent_y_.get());
    }
}

void AssetConfigUI::rebuild_rows() {
    if (!panel_) return;
    DockableCollapsible::Rows rows;

    rows.push_back({ dd_method_w_.get(), b_done_w_.get() });

    // Candidate section
    rows.push_back({ add_button_w_.get() });
    candidates_.clear();
    for (auto& c : entry_["candidates"]) {
        CandidateRow row;
        row.name = c["name"].is_null() ? "null" : c["name"].get<std::string>();
        row.chance = c.value("chance", 0);

        row.name_box = std::make_unique<DMTextBox>("Asset", row.name);
        row.chance_slider = std::make_unique<DMSlider>("Chance", 0, 100, row.chance);
        row.name_w = std::make_unique<TextBoxWidget>(row.name_box.get());
        row.chance_w = std::make_unique<SliderWidget>(row.chance_slider.get());

        if (!c["name"].is_null()) {
            row.del_button = std::make_unique<DMButton>("X", &DMStyles::DeleteButton(), 40, DMButton::height());
            row.del_w = std::make_unique<ButtonWidget>(row.del_button.get(), [this, nm=row.name]() {
                remove_candidate(nm);
            });
            rows.push_back({ row.name_w.get(), row.chance_w.get(), row.del_w.get() });
        } else {
            rows.push_back({ row.name_w.get(), row.chance_w.get() });
        }
        candidates_.push_back(std::move(row));
    }

    // Other settings
    if (s_minmax_w_) rows.push_back({ s_minmax_w_.get() });
    rows.push_back({ cb_inherited_w_.get(), cb_overlap_w_.get(), cb_spacing_w_.get(), cb_tag_w_.get() });
    if (s_border_w_) rows.push_back({ s_border_w_.get(), s_sector_center_w_.get(), s_sector_range_w_.get() });
    if (s_percent_x_w_) rows.push_back({ s_percent_x_w_.get() });
    if (s_percent_y_w_) rows.push_back({ s_percent_y_w_.get() });

    if (s_range_w_) rows.push_back({ s_range_w_.get() });
    const std::string& m = spawn_methods_[method_];
    if (m == "Perimeter") {
        rows.push_back({ s_border_w_.get(), s_sector_center_w_.get(), s_sector_range_w_.get() });
    } else if (m == "Percent") {
        rows.push_back({ s_percent_x_w_.get() });
        rows.push_back({ s_percent_y_w_.get() });
    }
    panel_->set_cell_width(120);
    panel_->set_rows(rows);
}

void AssetConfigUI::add_candidate(const std::string& name, int chance) {
    entry_["candidates"].push_back({{"name", name}, {"chance", chance}});
    rebuild_rows();
    sync_json();
}

void AssetConfigUI::remove_candidate(const std::string& name) {
    auto& arr = entry_["candidates"];
    arr.erase(std::remove_if(arr.begin(), arr.end(),
        [&](nlohmann::json& c) {
            return !c["name"].is_null() && c["name"] == name;
        }), arr.end());
    rebuild_rows();
    sync_json();
}

void AssetConfigUI::sync_json() {
    for (size_t i=0; i<candidates_.size(); ++i) {
        auto& c = entry_["candidates"][i];
        c["name"] = candidates_[i].name_box->value();
        c["chance"] = candidates_[i].chance_slider->value();
    }
    entry_["position"] = spawn_methods_[method_];
    entry_["min_number"] = s_minmax_ ? s_minmax_->min_value() : 1;
    entry_["max_number"] = s_minmax_ ? s_minmax_->max_value() : 1;
    entry_["inherited"] = cb_inherited_ ? cb_inherited_->value() : false;
    entry_["check_overlap"] = cb_overlap_ ? cb_overlap_->value() : false;
    entry_["check_min_spacing"] = cb_spacing_ ? cb_spacing_->value() : false;
    entry_["tag"] = cb_tag_ ? cb_tag_->value() : false;
    if (s_border_) entry_["border_shift"] = s_border_->value();
    if (s_sector_center_) entry_["sector_center"] = s_sector_center_->value();
    if (s_sector_range_) entry_["sector_range"] = s_sector_range_->value();
    if (s_percent_x_) { entry_["ep_x_min"] = s_percent_x_->min_value(); entry_["ep_x_max"] = s_percent_x_->max_value(); }
    if (s_percent_y_) { entry_["ep_y_min"] = s_percent_y_->min_value(); entry_["ep_y_max"] = s_percent_y_->max_value(); }
}

void AssetConfigUI::update(const Input& input) {
    if (panel_ && panel_->is_visible()) {
        panel_->update(input, 1920, 1080);
        method_ = dd_method_ ? dd_method_->selected() : 0;
        sync_json();
    }
}

bool AssetConfigUI::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    return panel_->handle_event(e);
}

void AssetConfigUI::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
}

nlohmann::json AssetConfigUI::to_json() const {
    nlohmann::json j;
    if (!spawn_id_.empty()) j["spawn_id"] = spawn_id_;
    if (!name_.empty() && name_[0] == '#') j["tag"] = name_.substr(1);
    else j["name"] = name_;
    j["position"] = spawn_methods_[method_];
    j["min_number"] = min_;
    j["max_number"] = max_;
    const std::string& m = spawn_methods_[method_];
    if (m == "Perimeter") {
        j["border_shift_min"] = j["border_shift_max"] = border_;
        j["sector_center_min"] = j["sector_center_max"] = sector_center_;
        j["sector_range_min"] = j["sector_range_max"] = sector_range_;
    } else if (m == "Percent") {
        j["percent_x_min"] = percent_x_min_;
        j["percent_x_max"] = percent_x_max_;
        j["percent_y_min"] = percent_y_min_;
        j["percent_y_max"] = percent_y_max_;
    }
    return j;
}

bool AssetConfigUI::is_point_inside(int x, int y) const {
    if (panel_ && panel_->is_visible()) {
        SDL_Point p{ x, y };
        return SDL_PointInRect(&p, &panel_->rect());
    }
    return false;
}

nlohmann::json AssetConfigUI::to_json() const {
    return entry_;
}
