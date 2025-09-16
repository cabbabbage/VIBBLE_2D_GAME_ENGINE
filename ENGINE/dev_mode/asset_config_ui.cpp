#include "asset_config_ui.hpp"
#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"

AssetConfigUI::AssetConfigUI() {
    // Include "Exact Position" to match the runtime spawn options used by the
    // engine.  Without this, assets with that method would default to "Random"
    // when opened in the UI, leading to inconsistent behaviour and potential
    // crashes when editing.
    spawn_methods_ = {"Random","Center","Perimeter","Exact","Exact Position","Percent","Distributed"};
    panel_ = std::make_unique<DockableCollapsible>("Asset", true, 0, 0);
    panel_->set_expanded(true);
    panel_->set_visible(false);
    b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 80, DMButton::height());
    b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() { close(); });
}

void AssetConfigUI::set_position(int x, int y) {
    if (panel_) panel_->set_position(x, y);
}

void AssetConfigUI::load(const nlohmann::json& j) {
    name_.clear();
    spawn_id_ = j.value("spawn_id", std::string{});
    if (j.contains("name") && j["name"].is_string()) name_ = j["name"].get<std::string>();
    else if (j.contains("tag") && j["tag"].is_string()) name_ = "#" + j["tag"].get<std::string>();
    method_ = 0;
    std::string m = j.value("position", spawn_methods_.front());
    for (size_t i=0;i<spawn_methods_.size();++i) if (spawn_methods_[i]==m) method_=int(i);
    min_ = j.value("min_number",0);
    max_ = j.value("max_number",0);
    grid_spacing_ = j.value("grid_spacing_min", j.value("grid_spacing_max", grid_spacing_));
    jitter_ = j.value("jitter_min", j.value("jitter_max", jitter_));
    empty_ = j.value("empty_grid_spaces_min", j.value("empty_grid_spaces_max", empty_));
    border_ = j.value("border_shift_min", j.value("border_shift_max", border_));
    sector_center_ = j.value("sector_center_min", j.value("sector_center_max", sector_center_));
    sector_range_ = j.value("sector_range_min", j.value("sector_range_max", sector_range_));
    percent_x_min_ = j.value("percent_x_min", j.value("percent_x_max", 0));
    percent_x_max_ = j.value("percent_x_max", percent_x_min_);
    percent_y_min_ = j.value("percent_y_min", j.value("percent_y_max", 0));
    percent_y_max_ = j.value("percent_y_max", percent_y_min_);
    if (panel_) panel_->set_title(name_);
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
    s_range_ = std::make_unique<DMRangeSlider>(0, 100, min_, max_);
    s_range_w_ = std::make_unique<RangeSliderWidget>(s_range_.get());
    s_grid_spacing_.reset(); s_grid_spacing_w_.reset();
    s_jitter_.reset(); s_jitter_w_.reset();
    s_empty_.reset(); s_empty_w_.reset();
    s_border_.reset(); s_border_w_.reset();
    s_sector_center_.reset(); s_sector_center_w_.reset();
    s_sector_range_.reset(); s_sector_range_w_.reset();
    s_percent_x_.reset(); s_percent_x_w_.reset();
    s_percent_y_.reset(); s_percent_y_w_.reset();
    const std::string& m = spawn_methods_[method_];
    if (m == "Distributed") {
        s_grid_spacing_ = std::make_unique<DMSlider>("Grid", 0, 400, grid_spacing_);
        s_grid_spacing_w_ = std::make_unique<SliderWidget>(s_grid_spacing_.get());
        s_jitter_ = std::make_unique<DMSlider>("Jitter", 0, 100, jitter_);
        s_jitter_w_ = std::make_unique<SliderWidget>(s_jitter_.get());
        s_empty_ = std::make_unique<DMSlider>("Empty%", 0, 100, empty_);
        s_empty_w_ = std::make_unique<SliderWidget>(s_empty_.get());
    } else if (m == "Perimeter") {
        s_border_ = std::make_unique<DMSlider>("Border%", 0, 100, border_);
        s_border_w_ = std::make_unique<SliderWidget>(s_border_.get());
        s_sector_center_ = std::make_unique<DMSlider>("SectorC", 0, 359, sector_center_);
        s_sector_center_w_ = std::make_unique<SliderWidget>(s_sector_center_.get());
        s_sector_range_ = std::make_unique<DMSlider>("SectorR", 0, 360, sector_range_);
        s_sector_range_w_ = std::make_unique<SliderWidget>(s_sector_range_.get());
    } else if (m == "Percent") {
        s_percent_x_ = std::make_unique<DMRangeSlider>(-100, 100, percent_x_min_, percent_x_max_);
        s_percent_x_w_ = std::make_unique<RangeSliderWidget>(s_percent_x_.get());
        s_percent_y_ = std::make_unique<DMRangeSlider>(-100, 100, percent_y_min_, percent_y_max_);
        s_percent_y_w_ = std::make_unique<RangeSliderWidget>(s_percent_y_.get());
    }
}

void AssetConfigUI::rebuild_rows() {
    if (!panel_) return;
    DockableCollapsible::Rows rows;
    rows.push_back({ dd_method_w_.get(), b_done_w_.get() });
    rows.push_back({ s_range_w_.get() });
    const std::string& m = spawn_methods_[method_];
    if (m == "Distributed") {
        rows.push_back({ s_grid_spacing_w_.get(), s_jitter_w_.get(), s_empty_w_.get() });
    } else if (m == "Perimeter") {
        rows.push_back({ s_border_w_.get(), s_sector_center_w_.get(), s_sector_range_w_.get() });
    } else if (m == "Percent") {
        rows.push_back({ s_percent_x_w_.get() });
        rows.push_back({ s_percent_y_w_.get() });
    }
    panel_->set_cell_width(120);
    panel_->set_rows(rows);
    Input dummy; panel_->update(dummy, 1920, 1080);
}

void AssetConfigUI::update(const Input& input) {
    if (panel_ && panel_->is_visible()) panel_->update(input, 1920, 1080);
}

bool AssetConfigUI::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    bool used = panel_->handle_event(e);
    int prev_method = method_;
    if (dd_method_) method_ = dd_method_->selected();
    if (s_range_) { min_ = s_range_->min_value(); max_ = s_range_->max_value(); }
    if (s_grid_spacing_) grid_spacing_ = s_grid_spacing_->value();
    if (s_jitter_) jitter_ = s_jitter_->value();
    if (s_empty_) empty_ = s_empty_->value();
    if (s_border_) border_ = s_border_->value();
    if (s_sector_center_) sector_center_ = s_sector_center_->value();
    if (s_sector_range_) sector_range_ = s_sector_range_->value();
    if (s_percent_x_) { percent_x_min_ = s_percent_x_->min_value(); percent_x_max_ = s_percent_x_->max_value(); }
    if (s_percent_y_) { percent_y_min_ = s_percent_y_->min_value(); percent_y_max_ = s_percent_y_->max_value(); }
    if (prev_method != method_) { rebuild_widgets(); rebuild_rows(); }
    return used;
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
    if (m == "Distributed") {
        j["grid_spacing_min"] = j["grid_spacing_max"] = grid_spacing_;
        j["jitter_min"] = j["jitter_max"] = jitter_;
        j["empty_grid_spaces_min"] = j["empty_grid_spaces_max"] = empty_;
    } else if (m == "Perimeter") {
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
