 #include "map_assets_modals.hpp"

 #include <algorithm>
 #include "spawn_group_config_ui.hpp"
 #include "FloatingDockableManager.hpp"
 #include "utils/input.hpp"

 using nlohmann::json;

 SingleSpawnGroupModal::SingleSpawnGroupModal() = default;
 SingleSpawnGroupModal::~SingleSpawnGroupModal() = default;

 void SingleSpawnGroupModal::ensure_single_group(json& section,
                                                 const std::string& default_display_name) {
     if (!section.is_object()) {
         section = json::object();
     }
     if (!section.contains("spawn_groups") || !section["spawn_groups"].is_array()) {
         section["spawn_groups"] = json::array();
     }
     auto& groups = section["spawn_groups"];
     if (groups.empty()) {
         json entry = json::object();
         entry["display_name"] = default_display_name;
         entry["position"] = "Random";
         entry["candidates"] = json::array();
         // Ensure a placeholder null candidate
         json null_cand = json::object();
         null_cand["name"] = "null";
         null_cand["chance"] = 0;
         entry["candidates"].push_back(std::move(null_cand));
         groups.push_back(std::move(entry));
     } else if (groups.size() > 1) {
         // Restrict to first entry only
         json first = groups[0];
         groups = json::array();
         groups.push_back(std::move(first));
     }
 }

 void SingleSpawnGroupModal::open(json& map_info,
                                  const std::string& section_key,
                                  const std::string& default_display_name,
                                  const std::string& ownership_label,
                                  SDL_Color ownership_color,
                                  SaveCallback on_save) {
     map_info_ = &map_info;
     on_save_ = std::move(on_save);
     section_ = &(*map_info_)[section_key];
     ensure_single_group(*section_, default_display_name);

     auto& groups = (*section_)["spawn_groups"];
     json entry = groups[0];
     if (!cfg_) cfg_ = std::make_unique<SpawnGroupConfigUI>();
     cfg_->load(entry);
     cfg_->set_ownership_label(ownership_label, ownership_color);
     cfg_->lock_method_to("Random");
     cfg_->set_quantity_hidden(true);
     cfg_->set_on_close([this]() {
         if (!this->map_info_ || !this->section_) return;
         // Persist back into the section JSON
         json updated = cfg_->to_json();
         auto& groups = (*section_)["spawn_groups"];
         if (groups.is_array()) {
             if (groups.empty()) {
                 groups = json::array();
                 groups.push_back(updated);
             } else {
                 groups[0] = updated;
                 // Ensure only one group remains
                 if (groups.size() > 1) {
                     json first = groups[0];
                     groups = json::array();
                     groups.push_back(std::move(first));
                 }
             }
         }
         if (on_save_) on_save_();
     });
     cfg_->open_panel();
 }

 void SingleSpawnGroupModal::close() {
     if (cfg_) cfg_->close();
 }

 bool SingleSpawnGroupModal::visible() const {
     return cfg_ && cfg_->visible();
 }

 void SingleSpawnGroupModal::update(const Input& input) {
     if (cfg_) cfg_->update(input);
 }

 bool SingleSpawnGroupModal::handle_event(const SDL_Event& e) {
     if (!cfg_) return false;
     return cfg_->handle_event(e);
 }

 void SingleSpawnGroupModal::render(SDL_Renderer* r) const {
     if (cfg_) cfg_->render(r);
 }

 bool SingleSpawnGroupModal::is_point_inside(int x, int y) const {
     if (!cfg_) return false;
     return cfg_->is_point_inside(x, y);
 }

