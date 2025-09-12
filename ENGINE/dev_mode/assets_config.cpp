#include "assets_config.hpp"
#include "FloatingCollapsible.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include <algorithm>

AssetsConfig::AssetsConfig() {
    spawn_methods_ = {"Random","Center","Perimeter","Exact","Distributed"};
    panel_ = std::make_unique<FloatingCollapsible>("Assets", 32, 32);
    panel_->set_expanded(true);
    panel_->set_visible(false);
    b_add_ = std::make_unique<DMButton>("Add Asset", &DMStyles::CreateButton(), 120, DMButton::height());
    b_add_w_ = std::make_unique<ButtonWidget>(b_add_.get(), [this]() {
        search_.set_position(panel_->rect().x + 40, panel_->rect().y + 40);
        search_.open([this](const std::string& name){
            Entry e; e.name = name; e.method = 0; e.min = 0; e.max = 0;
            entries_.push_back(std::move(e));
            rebuild_entry_widgets();
            rebuild_rows();
        });
    });
    b_done_ = std::make_unique<DMButton>("Done", &DMStyles::ListButton(), 80, DMButton::height());
    b_done_w_ = std::make_unique<ButtonWidget>(b_done_.get(), [this]() {
        if (on_close_) on_close_(build_json());
        close();
    });
    rebuild_rows();
}

void AssetsConfig::set_position(int x, int y) {
    if (!panel_) panel_ = std::make_unique<FloatingCollapsible>("Assets", x, y);
    panel_->set_position(x, y);
}

void AssetsConfig::open(const nlohmann::json& assets, std::function<void(const nlohmann::json&)> on_close) {
    search_.close();
    entries_.clear();
    if (assets.is_array()) {
        for (auto& it : assets) {
            Entry e;
            if (it.contains("name") && it["name"].is_string()) e.name = it["name"].get<std::string>();
            else if (it.contains("tag") && it["tag"].is_string()) e.name = "#" + it["tag"].get<std::string>();
            e.min = it.value("min_number", 0);
            e.max = it.value("max_number", 0);
            std::string m = it.value("position", "Random");
            e.method = 0;
            for (size_t i = 0; i < spawn_methods_.size(); ++i) if (spawn_methods_[i] == m) e.method = (int)i;
            entries_.push_back(std::move(e));
        }
    }
    on_close_ = std::move(on_close);
    rebuild_entry_widgets();
    rebuild_rows();
    if (panel_) {
        panel_->set_visible(true);
        Input dummy; panel_->update(dummy, 1920, 1080);
    }
}

void AssetsConfig::close() {
    if (panel_) panel_->set_visible(false);
    on_close_ = nullptr;
    search_.close();
}

void AssetsConfig::rebuild_entry_widgets() {
    for (auto& e : entries_) {
        e.label = std::make_unique<DMButton>(e.name, &DMStyles::HeaderButton(), 100, DMButton::height());
        e.label_w = std::make_unique<ButtonWidget>(e.label.get());
        e.dd_method = std::make_unique<DMDropdown>("Method", spawn_methods_, e.method);
        e.dd_method_w = std::make_unique<DropdownWidget>(e.dd_method.get());
        e.s_range = std::make_unique<DMRangeSlider>(0, 100, e.min, e.max);
        e.s_range_w = std::make_unique<RangeSliderWidget>(e.s_range.get());
        e.b_delete = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 80, DMButton::height());
        e.b_delete_w = std::make_unique<ButtonWidget>(e.b_delete.get(), [this, &e]() {
            auto it = std::find_if(entries_.begin(), entries_.end(), [&e](const Entry& other){ return &other == &e; });
            if (it != entries_.end()) {
                entries_.erase(it);
                rebuild_entry_widgets();
                rebuild_rows();
            }
        });
    }
}

void AssetsConfig::rebuild_rows() {
    if (!panel_) return;
    FloatingCollapsible::Rows rows;
    for (auto& e : entries_) {
        rows.push_back({ e.label_w.get(), e.dd_method_w.get(), e.b_delete_w.get() });
        rows.push_back({ e.s_range_w.get() });
    }
    rows.push_back({ b_add_w_.get(), b_done_w_.get() });
    panel_->set_cell_width(120);
    panel_->set_rows(rows);
    Input dummy; panel_->update(dummy, 1920, 1080);
}

nlohmann::json AssetsConfig::build_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries_) {
        nlohmann::json j;
        if (!e.name.empty() && e.name[0] == '#') j["tag"] = e.name.substr(1);
        else j["name"] = e.name;
        j["position"] = spawn_methods_[e.method];
        j["min_number"] = e.min;
        j["max_number"] = e.max;
        arr.push_back(j);
    }
    return arr;
}

bool AssetsConfig::visible() const { return panel_ && panel_->is_visible(); }

void AssetsConfig::update(const Input& input) {
    if (search_.visible()) search_.update(input);
    if (panel_ && panel_->is_visible()) panel_->update(input, 1920, 1080);
}

bool AssetsConfig::handle_event(const SDL_Event& e) {
    if (search_.visible()) return search_.handle_event(e);
    if (!panel_ || !panel_->is_visible()) return false;
    bool used = panel_->handle_event(e);
    for (auto& en : entries_) {
        if (en.dd_method) en.method = en.dd_method->selected();
        if (en.s_range)  { en.min = en.s_range->min_value(); en.max = en.s_range->max_value(); }
    }
    return used;
}

void AssetsConfig::render(SDL_Renderer* r) const {
    if (search_.visible()) { search_.render(r); return; }
    if (panel_ && panel_->is_visible()) {
        panel_->render(r);
    }
}
