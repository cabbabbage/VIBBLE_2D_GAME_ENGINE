#pragma once

#include "../DockableCollapsible.hpp"
#include "widgets.hpp"
#include "dev_mode/asset_info_sections.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>

// Collapsible section to edit tags for an asset.
class AssetInfoUI;

class Section_Tags : public DockableCollapsible {
  public:
    Section_Tags() : DockableCollapsible("Tags", false) { set_visible_height(480); }
    ~Section_Tags() override = default;

    void set_ui(AssetInfoUI* ui) { ui_ = ui; }

    void build() override {
      widgets_.clear();
      if (!info_) { set_rows({}); return; }
      std::ostringstream oss;
      for (size_t i=0;i<info_->tags.size();++i) {
        oss << info_->tags[i];
        if (i+1<info_->tags.size()) oss << ", ";
      }
      t_tags_ = std::make_unique<DMTextBox>("Tags (comma)", oss.str());
      scan_asset_tags();
      refresh_recommendations();
      rebuild_rows();
    }

    void layout() override { DockableCollapsible::layout(); }

    bool handle_event(const SDL_Event& e) override {
      bool used = DockableCollapsible::handle_event(e);
      if (!info_) return used;
      bool changed = false;
      auto tags_vec = current_tags_vec();
      if (tags_vec != info_->tags) {
        info_->set_tags(tags_vec);
        changed = true;
        refresh_recommendations();
        rebuild_rows();
      }
      if (changed) (void)info_->update_info_json();
      return used || changed;
    }

    void render_content(SDL_Renderer* /*r*/) const override {}

  private:
    // Scan existing assets for tag usage
    void scan_asset_tags() {
      using namespace std::filesystem;
      asset_tag_map_.clear();
      tag_usage_.clear();
      path src{"SRC"};
      if (!exists(src)) return;
      for (auto& dir : directory_iterator(src)) {
        if (!dir.is_directory()) continue;
        path info = dir.path() / "info.json";
        std::ifstream in(info);
        if (!in) continue;
        nlohmann::json data; try { in >> data; } catch (...) { continue; }
        std::unordered_set<std::string> tags;
        if (data.contains("tags") && data["tags"].is_array()) {
          for (auto& t : data["tags"]) {
            if (t.is_string()) {
              std::string s = t.get<std::string>();
              std::transform(s.begin(), s.end(), s.begin(), ::tolower);
              tags.insert(s);
              tag_usage_[s]++;
            }
          }
        }
        if (!tags.empty()) asset_tag_map_[dir.path().filename().string()] = std::move(tags);
      }
    }

    std::set<std::string> current_tags() const {
      return parse_csv(t_tags_ ? t_tags_->value() : std::string{});
    }
    std::vector<std::string> current_tags_vec() const {
      auto s = current_tags();
      return std::vector<std::string>(s.begin(), s.end());
    }
    static std::set<std::string> parse_csv(const std::string& s) {
      std::set<std::string> out;
      size_t pos = 0;
      while (pos != std::string::npos) {
        size_t comma = s.find(',', pos);
        std::string tok = (comma == std::string::npos) ? s.substr(pos) : s.substr(pos, comma - pos);
        size_t b = tok.find_first_not_of(" \t\n\r");
        size_t e = tok.find_last_not_of(" \t\n\r");
        if (b != std::string::npos && e != std::string::npos) {
          tok = tok.substr(b, e - b + 1);
          std::transform(tok.begin(), tok.end(), tok.begin(), ::tolower);
          if (!tok.empty()) out.insert(tok);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
      return out;
    }

    std::vector<std::string> find_recommendations(size_t limit, const std::set<std::string>& exclude) const {
      auto current = current_tags();
      std::unordered_map<std::string,int> scores;
      for (const auto& [_, tags] : asset_tag_map_) {
        int shared = 0;
        for (const auto& t : tags) if (current.count(t)) ++shared;
        if (shared) {
          for (const auto& t : tags) {
            if (!current.count(t) && !exclude.count(t)) scores[t] += shared;
          }
        }
      }
      std::vector<std::pair<std::string,int>> vec(scores.begin(), scores.end());
      std::sort(vec.begin(), vec.end(), [&](auto& a, auto& b){
        if (a.second != b.second) return a.second > b.second;
        int au = tag_usage_.count(a.first) ? tag_usage_.at(a.first) : 0;
        int bu = tag_usage_.count(b.first) ? tag_usage_.at(b.first) : 0;
        return au > bu;
      });
      std::vector<std::string> recs;
      for (auto& p : vec) recs.push_back(p.first);
      std::set<std::string> used(recs.begin(), recs.end());
      used.insert(current.begin(), current.end());
      used.insert(exclude.begin(), exclude.end());
      std::vector<std::pair<std::string,int>> usage(tag_usage_.begin(), tag_usage_.end());
      std::sort(usage.begin(), usage.end(), [](auto& a, auto& b){ return a.second > b.second; });
      for (auto& u : usage) {
        if (!used.count(u.first)) { recs.push_back(u.first); used.insert(u.first); if (recs.size()>=limit) break; }
      }
      if (recs.size()>limit) recs.resize(limit);
      return recs;
    }

    void refresh_recommendations() {
      recommended_buttons_.clear();
      auto current = current_tags();
      std::set<std::string> no_exclude;
      auto tag_recs = find_recommendations(30, no_exclude);
      for (const auto& t : tag_recs) {
        auto b = std::make_unique<DMButton>(t, &DMStyles::ListButton(), 120, DMButton::height());
        recommended_buttons_.push_back(std::move(b));
      }
    }

    void add_tag(const std::string& tag) {
      auto tags = current_tags();
      if (tags.insert(tag).second) {
        update_text_box(t_tags_.get(), tags);
        info_->set_tags(std::vector<std::string>(tags.begin(), tags.end()));
        (void)info_->update_info_json();
        refresh_recommendations();
      }
    }

    static void update_text_box(DMTextBox* tb, const std::set<std::string>& tags) {
      if (!tb) return;
      std::ostringstream oss;
      bool first = true;
      for (const auto& t : tags) {
        if (!first) oss << ", ";
        first = false;
        oss << t;
      }
      tb->set_value(oss.str());
    }

    void rebuild_rows() {
      widgets_.clear();
      DockableCollapsible::Rows rows;
      if (t_tags_) {
        auto w = std::make_unique<TextBoxWidget>(t_tags_.get());
        rows.push_back({ w.get() });
        widgets_.push_back(std::move(w));
      }
      for (auto& b : recommended_buttons_) {
        DMButton* bp = b.get();
        std::string tag = bp->text();
        auto w = std::make_unique<ButtonWidget>(bp, [this, tag]() {
          add_tag(tag);
          rebuild_rows();
        });
        rows.push_back({ w.get() });
        widgets_.push_back(std::move(w));
      }
      if (!apply_btn_) {
        apply_btn_ = std::make_unique<DMButton>("Apply Settings", &DMStyles::AccentButton(), 180, DMButton::height());
      }
      auto w_apply = std::make_unique<ButtonWidget>(apply_btn_.get(), [this]() {
        if (ui_) ui_->request_apply_section(AssetInfoSectionId::Tags);
      });
      rows.push_back({ w_apply.get() });
      widgets_.push_back(std::move(w_apply));
      set_rows(rows);
    }

    std::unique_ptr<DMTextBox> t_tags_;
    std::vector<std::unique_ptr<DMButton>> recommended_buttons_;
    std::unordered_map<std::string, std::unordered_set<std::string>> asset_tag_map_;
    std::unordered_map<std::string, int> tag_usage_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::unique_ptr<DMButton> apply_btn_;
    AssetInfoUI* ui_ = nullptr; // non-owning
};

