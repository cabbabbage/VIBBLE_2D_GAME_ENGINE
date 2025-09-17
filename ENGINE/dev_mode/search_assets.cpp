#include "search_assets.hpp"
#include "DockableCollapsible.hpp"
#include "widgets.hpp"
#include "dm_styles.hpp"
#include "utils/input.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <cctype>

SearchAssets::SearchAssets() {
    panel_ = std::make_unique<DockableCollapsible>("Search Assets", true, 64, 64);
    panel_->set_expanded(true);
    panel_->set_visible(false);
    query_ = std::make_unique<DMTextBox>("Search", "");
    query_widget_ = std::make_unique<TextBoxWidget>(query_.get());
    panel_->set_rows({ { query_widget_.get() } });
    panel_->set_cell_width(260);
}

void SearchAssets::set_position(int x, int y) {
    if (!panel_) panel_ = std::make_unique<DockableCollapsible>("Search Assets", true, x, y);
    panel_->set_position(x, y);
}

std::string SearchAssets::to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void SearchAssets::open(Callback cb) {
    cb_ = std::move(cb);
    if (all_.empty()) load_assets();
    if (!panel_) panel_ = std::make_unique<DockableCollapsible>("Search Assets", true, 64, 64);
    panel_->set_visible(true);
    panel_->set_expanded(true);
    last_query_.clear();
    filter_assets();
}

void SearchAssets::close() {
    if (panel_) panel_->set_visible(false);
    cb_ = nullptr;
}

bool SearchAssets::visible() const {
    return panel_ && panel_->is_visible();
}

void SearchAssets::load_assets() {
    namespace fs = std::filesystem;
    all_.clear();
    fs::path src("SRC");
    if (!fs::exists(src) || !fs::is_directory(src)) return;
    for (auto& p : fs::directory_iterator(src)) {
        if (!p.is_directory()) continue;
        fs::path info = p.path() / "info.json";
        if (!fs::exists(info)) continue;
        try {
            std::ifstream f(info);
            nlohmann::json j; f >> j;
            Asset a;
            a.name = j.value("asset_name", p.path().filename().string());
            if (j.contains("tags") && j["tags"].is_array()) {
                for (auto& t : j["tags"]) if (t.is_string()) a.tags.push_back(t.get<std::string>());
            }
            all_.push_back(std::move(a));
        } catch (...) {}
    }
}

void SearchAssets::filter_assets() {
    if (!panel_ || !panel_->is_visible()) return;
    std::string q = to_lower(query_ ? query_->value() : "");
    results_.clear();
    std::set<std::string> tagset;
    for (const auto& a : all_) {
        std::string ln = to_lower(a.name);
        if (q.empty() || ln.find(q) != std::string::npos) {
            results_.push_back({a.name,false});
        }
        for (const auto& t : a.tags) {
            std::string lt = to_lower(t);
            if (lt.find(q) != std::string::npos) tagset.insert(t);
        }
    }
    for (const auto& t : tagset) {
        results_.push_back({t,true});
    }
    buttons_.clear();
    button_widgets_.clear();
    DockableCollapsible::Rows rows;
    rows.push_back({ query_widget_.get() });
    for (const auto& r : results_) {
        auto b = std::make_unique<DMButton>(r.second ? ("#"+r.first) : r.first, &DMStyles::ListButton(), 200, DMButton::height());
        auto bw = std::make_unique<ButtonWidget>(b.get(), [this, r]() {
            std::string v = r.first;
            if (r.second) v = "#" + v;
            if (cb_) cb_(v);
            close();
        });
        buttons_.push_back(std::move(b));
        button_widgets_.push_back(std::move(bw));
        rows.push_back({ button_widgets_.back().get() });
    }
    panel_->set_rows(rows);
    Input dummy; panel_->update(dummy, 1920, 1080);
}

bool SearchAssets::handle_event(const SDL_Event& e) {
    if (!panel_ || !panel_->is_visible()) return false;
    bool used = panel_->handle_event(e);
    std::string q = query_ ? query_->value() : "";
    if (q != last_query_) { last_query_ = q; filter_assets(); }
    return used;
}

void SearchAssets::update(const Input& input) {
    if (panel_ && panel_->is_visible()) panel_->update(input, 1920, 1080);
}

void SearchAssets::render(SDL_Renderer* r) const {
    if (panel_ && panel_->is_visible()) panel_->render(r);
}

bool SearchAssets::is_point_inside(int x, int y) const {
    if (!panel_ || !panel_->is_visible()) return false;
    return panel_->is_point_inside(x, y);
}
