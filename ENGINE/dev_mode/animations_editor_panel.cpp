#include "animations_editor_panel.hpp"

#include "DockableCollapsible.hpp"
#include "dm_styles.hpp"
#include "widgets.hpp"

#include <fstream>
#include "utils/input.hpp"
#include "animation_utils.hpp"
#include <SDL_image.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace fs = std::filesystem;

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }

class ThumbWidget : public Widget {
public:
    using PathFn = std::function<std::string()>;
    explicit ThumbWidget(PathFn fn, int preferred_h = 120)
        : fn_(std::move(fn)), pref_h_(preferred_h) {}
    ~ThumbWidget() override { if (tex_) SDL_DestroyTexture(tex_); }
    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int ) const override { return pref_h_; }
    bool handle_event(const SDL_Event& ) override { return false; }
    void render(SDL_Renderer* r) const override {
        if (!r) return;

        const std::string path = fn_ ? fn_() : std::string{};
        if (path.empty()) return;

        if (!tex_ || path != last_path_) {
            if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }
            SDL_Texture* t = IMG_LoadTexture(r, path.c_str());
            if (t) { tex_ = t; last_path_ = path; }
        }
        if (!tex_) return;
        int tw=0, th=0; SDL_QueryTexture(tex_, nullptr, nullptr, &tw, &th);
        if (tw <= 0 || th <= 0) return;

        float sx = rect_.w / float(std::max(1, tw));
        float sy = rect_.h / float(std::max(1, th));
        float s = std::min(sx, sy);
        int dw = int(tw * s), dh = int(th * s);
        SDL_Rect dst{ rect_.x + (rect_.w - dw)/2, rect_.y + (rect_.h - dh)/2, dw, dh };
        SDL_RenderCopy(r, tex_, nullptr, &dst);

        const SDL_Color border = DMStyles::Border();
        SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
        SDL_RenderDrawRect(r, &rect_);
    }
    void invalidate() { last_path_.clear(); }
private:
    mutable SDL_Rect rect_{0,0,120,120};
    PathFn fn_{};
    int pref_h_ = 120;
    mutable SDL_Texture* tex_ = nullptr;
    mutable std::string last_path_;
};

AnimationsEditorPanel::AnimationsEditorPanel() {
    box_ = std::make_unique<DockableCollapsible>("Animations", true, 32, 64);
    box_->set_expanded(true);

    box_->set_visible(false);
}

AnimationsEditorPanel::~AnimationsEditorPanel() = default;

void AnimationsEditorPanel::set_asset_paths(const std::string& asset_dir_path,
                                            const std::string& info_json_path) {
    asset_dir_path_ = asset_dir_path;
    info_json_path_ = info_json_path;
    load_info_json();
    if (is_open()) {
        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
    }
}

void AnimationsEditorPanel::open()  {
    if (box_) box_->set_visible(true);

    request_rebuild();
}
void AnimationsEditorPanel::close() { if (box_) box_->set_visible(false); }
bool AnimationsEditorPanel::is_open() const { return box_ && box_->is_visible(); }

void AnimationsEditorPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_open()) return;
    if (rebuild_requested_) {
        rebuild_requested_ = false;
        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
    }
    if (box_) box_->update(input, screen_w, screen_h);

    bool now_open = movement_modal_.is_open();
    if (movement_was_open_ && !now_open && !movement_anim_name_.empty()) {

        nlohmann::json payload = animation_payload(movement_anim_name_);
        if (!payload.is_object()) payload = nlohmann::json::object();
        nlohmann::json mv = nlohmann::json::array();
        const auto& pos = movement_modal_.positions();
        for (const auto& p : pos) mv.push_back(nlohmann::json::array({ p.first, p.second }));
        payload["movement"] = mv;
        upsert_animation(movement_anim_name_, payload);
        (void)save_info_json();

        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
        movement_anim_name_.clear();
    }
    movement_was_open_ = now_open;
}

bool AnimationsEditorPanel::handle_event(const SDL_Event& e) {
    if (!is_open()) return false;
    if (movement_modal_.is_open() && movement_modal_.handle_event(e)) return true;
    if (!box_) return false;
    bool used = box_->handle_event(e);

    bool changed_any = false;
    for (auto& it : items_) {
        if (!it) continue;
        if (it->id_box) {
            std::string new_name = it->id_box->value();
            if (!new_name.empty() && new_name != it->name) {
                if (rename_animation(it->name, new_name)) {
                    it->name = new_name; changed_any = true;
                } else {
                    it->id_box->set_value(it->name);
                }
            }
        }

        nlohmann::json payload = it->last_payload.is_object() ? it->last_payload : nlohmann::json::object();
        if (!payload.is_object()) payload = nlohmann::json::object();
        nlohmann::json src = payload.contains("source") && payload["source"].is_object() ? payload["source"] : nlohmann::json::object();

        std::string kind = "folder";
        if (it->kind_dd) kind = (it->kind_dd->selected() == 1) ? "animation" : "folder";
        src["kind"] = kind;
        if (kind == "folder") {
            std::string p = it->path_box ? it->path_box->value() : std::string{};
            src["path"] = p; src["name"] = nullptr;
        } else {
            auto names = current_names_sorted();
            std::string ref = (!names.empty() && it->ref_dd) ? names[clampi(it->ref_dd->selected(), 0, (int)names.size()-1)] : std::string{};

            if (ref == it->name || creates_cycle(it->name, ref)) ref.clear();
            src["name"] = ref; src["path"] = "";
        }
        payload["source"] = src;
        payload["flipped_source"] = it->flipped_cb ? it->flipped_cb->value() : false;
        payload["reverse_source"] = it->reversed_cb ? it->reversed_cb->value() : false;
        payload["locked"] = it->locked_cb ? it->locked_cb->value() : false;
        payload["loop"] = it->loop_cb ? it->loop_cb->value() : false;
        payload["rnd_start"] = it->rnd_start_cb ? it->rnd_start_cb->value() : false;

        int spd = it->speed_sl ? it->speed_sl->value() : 1; if (spd == 0) spd = 1; payload["speed_factor"] = spd;

        int nframes = compute_frames_from_source(payload["source"]); if (nframes <= 0) nframes = 1; payload["number_of_frames"] = nframes;
        try {
            nlohmann::json mv = payload.value("movement", nlohmann::json::array());
            if (!mv.is_array()) mv = nlohmann::json::array();
            if ((int)mv.size() < nframes) { while ((int)mv.size() < nframes) mv.push_back({0,0}); }
            else if ((int)mv.size() > nframes) { nlohmann::json trimmed = nlohmann::json::array(); for (int i=0;i<nframes;++i) trimmed.push_back(mv[i]); mv.swap(trimmed); }
            if (nframes >= 1 && mv.size() >= 1) { try { mv[0] = {0,0}; } catch(...){} }
            payload["movement"] = mv;
        } catch(...) {}

        if (it->on_end_dd) {
            auto all = current_names_sorted();
            std::vector<std::string> opts; opts.push_back("default"); opts.insert(opts.end(), all.begin(), all.end());
            int idx = clampi(it->on_end_dd->selected(), 0, (int)opts.size()-1);
            payload["on_end"] = opts[idx];
        }

        if (payload.dump() != it->last_payload.dump()) {
            if (upsert_animation(it->name, payload)) {
                (void)save_info_json();
                it->last_payload = payload; changed_any = true;
                if (it->frames_label) {
                    std::ostringstream oss; oss << "Frames: " << nframes; it->frames_label->set_value(oss.str());
                }
            }
        }
    }

    if (start_dd_) {
        auto names = current_names_sorted();
        int idx = clampi(start_dd_->selected(), 0, (int)names.size()-1);
        std::string cur = get_start_animation_name();
        if (!names.empty() && idx < (int)names.size() && names[idx] != cur) {
            set_start_animation_name(names[idx]); (void)save_info_json(); changed_any = true;
        }
    }

    if (changed_any) {

        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
    }

    if (rebuild_requested_) {
        rebuild_requested_ = false;
        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
        used = true;
    }

    return used || changed_any;
}

void AnimationsEditorPanel::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (!is_open()) return;
    if (box_) box_->render(r);
    if (movement_modal_.is_open()) const_cast<animation::MovementModal&>(movement_modal_).render(r);
}

std::vector<std::string> AnimationsEditorPanel::current_names_sorted() const {
    std::vector<std::string> names = animation_names(); std::sort(names.begin(), names.end()); return names;
}

int AnimationsEditorPanel::compute_frames_from_source(const nlohmann::json& source) const {
    try {
        if (!source.is_object()) return 1;
        std::string kind = source.value("kind", std::string{"folder"});
        if (kind == "animation") {
            std::string ref = source.value("name", std::string{}); if (ref.empty()) return 1;
            nlohmann::json other = animation_payload(ref); if (!other.is_object()) return 1;
            return compute_frames_from_source(other.value("source", nlohmann::json::object()));
        }
        std::string rel = source.value("path", std::string{});
        fs::path dir = fs::path(asset_dir_path_) / rel; if (!fs::exists(dir) || !fs::is_directory(dir)) return 1;
        int count = 0; for (auto& e : fs::directory_iterator(dir)) { if (!e.is_regular_file()) continue; auto ext = e.path().extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") ++count; }
        return std::max(1, count);
    } catch(...) { return 1; }
}

nlohmann::json AnimationsEditorPanel::default_payload(const std::string& name) {
    nlohmann::json p; p["source"] = nlohmann::json::object({{"kind","folder"},{"path",name},{"name",nullptr}});
    p["flipped_source"]=false; p["reverse_source"]=false; p["locked"]=false; p["rnd_start"]=false; p["loop"]=false; p["speed_factor"]=1; p["number_of_frames"]=1; p["movement"]=nlohmann::json::array({nlohmann::json::array({0,0})}); p["on_end"] = "default"; return p;
}

bool AnimationsEditorPanel::load_info_json() {
    info_json_ = nlohmann::json::object();
    try {
        std::ifstream in(info_json_path_);
        if (in) {
            in >> info_json_;
        }
    } catch (...) {
        info_json_ = nlohmann::json::object();
    }

    if (!info_json_.contains("animations") || !info_json_["animations"].is_object()) {
        info_json_["animations"] = nlohmann::json::object();
    }
    if (!info_json_.contains("start") || !info_json_["start"].is_string()) {
        info_json_["start"] = std::string{};
    }
    return true;
}

bool AnimationsEditorPanel::save_info_json() const {
    try {
        std::ofstream out(info_json_path_);
        if (!out) return false;
        out << info_json_.dump(4);
        return true;
    } catch (...) { return false; }
}

std::vector<std::string> AnimationsEditorPanel::animation_names() const {
    std::vector<std::string> names;
    try {
        if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
            for (auto it = info_json_["animations"].begin(); it != info_json_["animations"].end(); ++it) {
                names.push_back(it.key());
            }
        }
    } catch (...) {}
    return names;
}

nlohmann::json AnimationsEditorPanel::animation_payload(const std::string& name) const {
    try {
        if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
            auto it = info_json_["animations"].find(name);
            if (it != info_json_["animations"].end()) return *it;
        }
    } catch (...) {}
    return nlohmann::json::object();
}

bool AnimationsEditorPanel::upsert_animation(const std::string& name, const nlohmann::json& payload) {
    if (name.empty()) return false;
    try {
        if (!info_json_.contains("animations") || !info_json_["animations"].is_object()) info_json_["animations"] = nlohmann::json::object();
        info_json_["animations"][name] = payload;
        return true;
    } catch (...) { return false; }
}

bool AnimationsEditorPanel::remove_animation(const std::string& name) {
    bool removed = false;
    try {
        if (info_json_.contains("animations") && info_json_["animations"].is_object()) {
            removed = info_json_["animations"].erase(name) > 0;
        }
        if (get_start_animation_name() == name) set_start_animation_name("");
    } catch (...) { removed = false; }
    return removed;
}

bool AnimationsEditorPanel::rename_animation(const std::string& old_name, const std::string& new_name) {
    if (old_name.empty() || new_name.empty() || old_name == new_name) return false;
    try {
        nlohmann::json payload = animation_payload(old_name);
        if (!payload.is_object()) return false;
        upsert_animation(new_name, payload);
        remove_animation(old_name);
        if (get_start_animation_name() == old_name) set_start_animation_name(new_name);
        return true;
    } catch (...) { return false; }
}

std::string AnimationsEditorPanel::get_start_animation_name() const {
    try { return info_json_.value("start", std::string{}); } catch (...) { return {}; }
}

void AnimationsEditorPanel::set_start_animation_name(const std::string& name) {
    try { info_json_["start"] = name; } catch (...) {}
}

std::string AnimationsEditorPanel::first_frame_path(const nlohmann::json& source) const {
    try {
        if (!source.is_object()) return {};
        std::string kind = source.value("kind", std::string{"folder"});
        if (kind == "animation") {
            std::string ref = source.value("name", std::string{});
            if (ref.empty()) return {};
            nlohmann::json other = animation_payload(ref);
            if (!other.is_object()) return {};
            return first_frame_path(other.value("source", nlohmann::json::object()));
        }
        std::string rel = source.value("path", std::string{});
        if (rel.empty()) return {};
        fs::path dir = fs::path(asset_dir_path_) / rel;
        auto images = animation::get_image_paths(dir);
        if (images.empty()) return {};
        return images.front().string();
    } catch (...) { return {}; }
}

bool AnimationsEditorPanel::creates_cycle(const std::string& current, const std::string& ref) const {
    if (current.empty() || ref.empty()) return false;
    if (current == ref) return true;
    std::unordered_map<std::string, std::string> next;
    try {
        auto names = animation_names();
        for (const auto& nm : names) {
            nlohmann::json p = animation_payload(nm);
            if (!p.is_object()) continue;
            auto src = p.value("source", nlohmann::json::object());
            if (src.value("kind", std::string{}) == std::string{"animation"}) {
                std::string rn = src.value("name", std::string{});
                if (!rn.empty()) next[nm] = rn;
            }
        }
    } catch (...) {}
    next[current] = ref;
    std::unordered_set<std::string> seen;
    std::string x = current;
    for (int steps = 0; steps < 1000; ++steps) {
        if (seen.count(x)) return true;
        seen.insert(x);
        auto it = next.find(x);
        if (it == next.end()) return false;
        x = it->second;
    }
    return true;
}

void AnimationsEditorPanel::rebuild_all_rows() {
    rows_.clear(); header_widgets_.clear(); items_.clear();
    if (!box_) return;
    rebuild_header_row();
    rebuild_animation_rows();
    box_->set_rows(rows_);
}

void AnimationsEditorPanel::rebuild_header_row() {
    header_widgets_.clear();
    std::vector<Widget*> row;
    auto names = current_names_sorted();
    int sel = 0; for (size_t i=0;i<names.size();++i) if (names[i] == get_start_animation_name()) { sel = (int)i; break; }
    start_dd_ = std::make_unique<DMDropdown>("Start", names, sel);
    header_widgets_.push_back(std::make_unique<DropdownWidget>(start_dd_.get()));
    row.push_back(header_widgets_.back().get());

    new_btn_ = std::make_unique<DMButton>("New Animation", &DMStyles::CreateButton(), 160, DMButton::height());
    header_widgets_.push_back(std::make_unique<ButtonWidget>(new_btn_.get(), [this]() {
        std::string base = "new_anim"; std::string nm = base; int i = 1; auto names = animation_names();
        auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
        while (exists(nm)) nm = base + std::string("_") + std::to_string(i++);
        auto p = default_payload(nm); p["number_of_frames"] = compute_frames_from_source(p["source"]);
        upsert_animation(nm, p); save_info_json(); request_rebuild();
    }));
    row.push_back(header_widgets_.back().get());

    new_folder_btn_ = std::make_unique<DMButton>("New From Folder...", &DMStyles::ListButton(), 180, DMButton::height());
    auto new_folder_btn_ptr = new_folder_btn_.get();
    header_widgets_.push_back(std::make_unique<ButtonWidget>(new_folder_btn_ptr, [this]() {

        std::string base = "new_anim"; std::string nm = base; int i=1; auto names = animation_names();
        auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
        while (exists(nm)) nm = base + std::string{"_"} + std::to_string(i++);
        std::string rel = nm;
        try { fs::create_directories(fs::path(asset_dir_path_) / rel); } catch(...) {}
        auto p = default_payload(nm); p["source"]["path"] = rel; p["source"]["kind"] = "folder"; p["source"]["name"] = nullptr;
        p["number_of_frames"] = compute_frames_from_source(p["source"]);
        upsert_animation(nm, p); save_info_json(); request_rebuild();
    }));
    row.push_back(header_widgets_.back().get());

    if (rows_.empty()) rows_.push_back(row); else rows_[0] = row;

    if (names.empty()) {

        auto lbl = std::make_unique<DMTextBox>("", "No animations found. Create one to get started.");
        header_widgets_.push_back(std::make_unique<TextBoxWidget>(lbl.get()));

        auto create_btn = std::make_unique<DMButton>("Create First Animation", &DMStyles::CreateButton(), 220, DMButton::height());
        header_widgets_.push_back(std::make_unique<ButtonWidget>(create_btn.get(), [this]() {
            std::string base = "new_anim"; std::string nm = base; int i=1; auto names = animation_names();
            auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
            while (exists(nm)) nm = base + std::string{"_"} + std::to_string(i++);
            auto p = default_payload(nm);
            upsert_animation(nm, p); (void)save_info_json(); request_rebuild();
        }));

        rows_.push_back(std::vector<Widget*>{ header_widgets_[1].get(), header_widgets_[2].get() });

        (void)lbl.release(); (void)create_btn.release();
    }
}

void AnimationsEditorPanel::rebuild_animation_rows() {
    auto names = animation_names(); std::sort(names.begin(), names.end());
    for (const auto& nm : names) {
        auto ui = std::make_unique<AnimUI>(); ui->name = nm; ui->last_payload = animation_payload(nm); if (!ui->last_payload.is_object()) ui->last_payload = nlohmann::json::object();
        nlohmann::json src = ui->last_payload.value("source", nlohmann::json::object());
        std::string kind = src.value("kind", std::string{"folder"}); std::string path = src.value("path", std::string{}); std::string ref = src.value("name", std::string{});

        ui->id_box = std::make_unique<DMTextBox>("ID", nm);
        ui->del_btn = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 100, DMButton::height());
        std::vector<std::string> kind_opts{"folder","animation"}; int kind_idx = (kind == "animation") ? 1 : 0; ui->kind_dd = std::make_unique<DMDropdown>("Kind", kind_opts, kind_idx);
        ui->path_box = std::make_unique<DMTextBox>("Folder", path);
        auto all_names = current_names_sorted(); int ref_idx = 0; for (size_t i=0;i<all_names.size();++i) if (all_names[i] == ref) { ref_idx = (int)i; break; }
        ui->ref_dd = std::make_unique<DMDropdown>("Animation", all_names, ref_idx);
        ui->flipped_cb = std::make_unique<DMCheckbox>("flipped", ui->last_payload.value("flipped_source", false));
        ui->reversed_cb = std::make_unique<DMCheckbox>("reverse", ui->last_payload.value("reverse_source", false));
        ui->locked_cb = std::make_unique<DMCheckbox>("locked", ui->last_payload.value("locked", false));
        ui->rnd_start_cb = std::make_unique<DMCheckbox>("rnd start", ui->last_payload.value("rnd_start", false));
        ui->loop_cb = std::make_unique<DMCheckbox>("loop", ui->last_payload.value("loop", false));
        int spd = 1; try { spd = (int)std::lround(ui->last_payload.value("speed_factor", 1.0f)); } catch(...) { spd = 1; } if (spd == 0) spd = 1; spd = clampi(spd, -20, 20);
        ui->speed_sl = std::make_unique<DMSlider>("speed", -20, 20, spd);
        ui->movement_btn = std::make_unique<DMButton>("Edit Movement...", &DMStyles::HeaderButton(), 180, DMButton::height());

        int nframes = compute_frames_from_source(src); std::ostringstream oss; oss << "Frames: " << nframes; ui->frames_label = std::make_unique<DMTextBox>("", oss.str());

        auto w_id = std::make_unique<TextBoxWidget>(ui->id_box.get());
        Widget* w_id_ptr = w_id.get(); ui->row_widgets.push_back(std::move(w_id));
        auto w_del = std::make_unique<ButtonWidget>(ui->del_btn.get(), [this, nm]() {
            remove_animation(nm); save_info_json(); request_rebuild();
        });
        Widget* w_del_ptr = w_del.get(); ui->row_widgets.push_back(std::move(w_del));
        rows_.push_back(std::vector<Widget*>{ w_id_ptr, w_del_ptr });

        auto w_kind = std::make_unique<DropdownWidget>(ui->kind_dd.get());
        Widget* w_kind_ptr = w_kind.get(); ui->row_widgets.push_back(std::move(w_kind));
        if (kind_idx == 0) {
            auto w_path = std::make_unique<TextBoxWidget>(ui->path_box.get());
            Widget* w_path_ptr = w_path.get(); ui->row_widgets.push_back(std::move(w_path));
            rows_.push_back(std::vector<Widget*>{ w_kind_ptr, w_path_ptr });
        } else {
            auto w_ref = std::make_unique<DropdownWidget>(ui->ref_dd.get());
            Widget* w_ref_ptr = w_ref.get(); ui->row_widgets.push_back(std::move(w_ref));
            rows_.push_back(std::vector<Widget*>{ w_kind_ptr, w_ref_ptr });
        }

        if (kind_idx == 0) {
            ui->create_folder_btn = std::make_unique<DMButton>("Create Folder", &DMStyles::ListButton(), 160, DMButton::height());
            auto w_cf = std::make_unique<ButtonWidget>(ui->create_folder_btn.get(), [this, nm]() {

                for (auto& it2 : items_) {
                    if (!it2 || it2->name != nm) continue;
                    std::string rel = it2->path_box ? it2->path_box->value() : std::string{};
                    if (rel.empty()) { rel = nm; if (it2->path_box) it2->path_box->set_value(rel); }
                    try {
                        fs::path dir = fs::path(asset_dir_path_) / rel;
                        fs::create_directories(dir);
                    } catch(...) {}

                    nlohmann::json payload = animation_payload(nm);
                    if (!payload.is_object()) payload = nlohmann::json::object();
                    nlohmann::json src = payload.value("source", nlohmann::json::object());
                    src["kind"] = "folder"; src["path"] = rel; src["name"] = nullptr; payload["source"] = src;
                    upsert_animation(nm, payload); (void)save_info_json();
                    request_rebuild();
                    break;
                }
            });
            Widget* w_cf_ptr = w_cf.get(); ui->row_widgets.push_back(std::move(w_cf));

            auto thumb = std::make_unique<ThumbWidget>([this, nm, ui_ptr = ui.get()](){
                nlohmann::json s = ui_ptr->last_payload.value("source", nlohmann::json::object());
                s["kind"] = "folder"; s["path"] = ui_ptr->path_box ? ui_ptr->path_box->value() : std::string{}; s["name"] = nullptr;
                return first_frame_path(s);
            }, 96);
            Widget* w_thumbp = thumb.get(); ui->row_widgets.push_back(std::move(thumb));
            rows_.push_back(std::vector<Widget*>{ w_cf_ptr, w_thumbp });
        }
        else {

            auto thumb = std::make_unique<ThumbWidget>([this, ui_ptr = ui.get()](){
                nlohmann::json s = ui_ptr->last_payload.value("source", nlohmann::json::object());
                s["kind"] = "animation";
                return first_frame_path(s);
            }, 96);
            Widget* w_thumbp = thumb.get(); ui->row_widgets.push_back(std::move(thumb));
            rows_.push_back(std::vector<Widget*>{ w_thumbp });
        }

        auto w_cb1 = std::make_unique<CheckboxWidget>(ui->flipped_cb.get()); Widget* w_cb1p = w_cb1.get(); ui->row_widgets.push_back(std::move(w_cb1));
        auto w_cb2 = std::make_unique<CheckboxWidget>(ui->reversed_cb.get()); Widget* w_cb2p = w_cb2.get(); ui->row_widgets.push_back(std::move(w_cb2));
        auto w_cb3 = std::make_unique<CheckboxWidget>(ui->locked_cb.get());   Widget* w_cb3p = w_cb3.get(); ui->row_widgets.push_back(std::move(w_cb3));
        auto w_cb4 = std::make_unique<CheckboxWidget>(ui->rnd_start_cb.get());Widget* w_cb4p = w_cb4.get(); ui->row_widgets.push_back(std::move(w_cb4));
        auto w_cb5 = std::make_unique<CheckboxWidget>(ui->loop_cb.get());     Widget* w_cb5p = w_cb5.get(); ui->row_widgets.push_back(std::move(w_cb5));
        rows_.push_back(std::vector<Widget*>{ w_cb1p, w_cb2p, w_cb3p, w_cb4p, w_cb5p });

        auto w_spd = std::make_unique<SliderWidget>(ui->speed_sl.get()); Widget* w_spdp = w_spd.get(); ui->row_widgets.push_back(std::move(w_spd));
        auto w_mov = std::make_unique<ButtonWidget>(ui->movement_btn.get(), [this, nm]() {
            nlohmann::json payload = animation_payload(nm);
            std::vector<animation::MovementModal::Position> pos; try { auto mv = payload.at("movement"); if (mv.is_array()) { for (auto& p : mv) { if (p.is_array() && p.size() >= 2) pos.emplace_back(p[0].get<int>(), p[1].get<int>()); } } } catch(...) {}
            movement_anim_name_ = nm;
            movement_modal_.open(pos);
        });
        Widget* w_movp = w_mov.get(); ui->row_widgets.push_back(std::move(w_mov));
        auto w_frames = std::make_unique<TextBoxWidget>(ui->frames_label.get()); Widget* w_framesp = w_frames.get(); ui->row_widgets.push_back(std::move(w_frames));
        rows_.push_back(std::vector<Widget*>{ w_spdp, w_movp, w_framesp });

        std::string on_end_val = ui->last_payload.value("on_end", std::string{"default"});
        auto all2 = current_names_sorted();
        std::vector<std::string> end_opts; end_opts.push_back("default"); end_opts.insert(end_opts.end(), all2.begin(), all2.end());
        int end_idx = 0; for (size_t i=0;i<end_opts.size();++i) if (end_opts[i] == on_end_val) { end_idx = (int)i; break; }
        ui->on_end_dd = std::make_unique<DMDropdown>("on_end", end_opts, end_idx);
        auto w_end = std::make_unique<DropdownWidget>(ui->on_end_dd.get()); Widget* w_endp = w_end.get(); ui->row_widgets.push_back(std::move(w_end));

        ui->dup_btn = std::make_unique<DMButton>("Duplicate", &DMStyles::ListButton(), 120, DMButton::height());
        auto w_dup = std::make_unique<ButtonWidget>(ui->dup_btn.get(), [this, nm]() {

            std::string base = nm + std::string{"_copy"};
            std::string new_nm = base; int i=1; auto names = animation_names();
            auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
            while (exists(new_nm)) new_nm = base + std::string{"_"} + std::to_string(i++);
            nlohmann::json payload = animation_payload(nm);
            upsert_animation(new_nm, payload); (void)save_info_json(); request_rebuild();
        });
        Widget* w_dupp = w_dup.get(); ui->row_widgets.push_back(std::move(w_dup));
        rows_.push_back(std::vector<Widget*>{ w_endp, w_dupp });

        if (kind_idx == 0) {

            ui->alpha_sl = std::make_unique<DMSlider>("alpha", 0, 255, 0);
            auto w_alpha = std::make_unique<SliderWidget>(ui->alpha_sl.get()); Widget* w_alphap = w_alpha.get(); ui->row_widgets.push_back(std::move(w_alpha));

            ui->compute_btn = std::make_unique<DMButton>("Compute Bounds", &DMStyles::ListButton(), 160, DMButton::height());
            auto w_comp = std::make_unique<ButtonWidget>(ui->compute_btn.get(), [this, nm]() {
                nlohmann::json payload = animation_payload(nm);
                auto src = payload.value("source", nlohmann::json::object());
                std::string rel = src.value("path", std::string{});
                if (rel.empty()) return;
                fs::path dir = fs::path(asset_dir_path_) / rel;
                auto images = animation::get_image_paths(dir);

                for (auto& it2 : items_) {
                    if (it2 && it2->name == nm) {
                        int thr = it2->alpha_sl ? it2->alpha_sl->value() : 0;
                        animation::Bounds b = animation::compute_union_bounds(images, thr);
                        it2->last_bounds = b; it2->has_bounds = (b.base_w > 0 && b.base_h > 0 && (b.top||b.bottom||b.left||b.right));
                        std::ostringstream oss; oss << "Crop T:" << b.top << " B:" << b.bottom << " L:" << b.left << " R:" << b.right;
                        if (!it2->crop_summary) it2->crop_summary = std::make_unique<DMTextBox>("Bounds", "");
                        it2->crop_summary->set_value(oss.str());

                        break;
                    }
                }
            });
            Widget* w_compp = w_comp.get(); ui->row_widgets.push_back(std::move(w_comp));

            if (!ui->crop_summary) ui->crop_summary = std::make_unique<DMTextBox>("Bounds", "");
            auto w_csum = std::make_unique<TextBoxWidget>(ui->crop_summary.get()); Widget* w_csump = w_csum.get(); ui->row_widgets.push_back(std::move(w_csum));
            rows_.push_back(std::vector<Widget*>{ w_alphap, w_compp, w_csump });

            ui->crop_btn = std::make_unique<DMButton>("Apply Crop", &DMStyles::DeleteButton(), 140, DMButton::height());
            auto w_crop = std::make_unique<ButtonWidget>(ui->crop_btn.get(), [this, nm]() {

                for (auto& it2 : items_) {
                    if (it2 && it2->name == nm) {
                        nlohmann::json payload = animation_payload(nm);
                        auto src = payload.value("source", nlohmann::json::object());
                        std::string rel = src.value("path", std::string{});
                        if (rel.empty()) return;
                        fs::path dir = fs::path(asset_dir_path_) / rel;
                        auto images = animation::get_image_paths(dir);
                        int thr = it2->alpha_sl ? it2->alpha_sl->value() : 0;

                        animation::Bounds b = it2->has_bounds ? it2->last_bounds : animation::compute_union_bounds(images, thr);
                        if (b.base_w == 0) return;
                        (void)animation::crop_images_with_bounds(images, b.top, b.bottom, b.left, b.right);

                        request_rebuild();
                        break;
                    }
                }
            });
            Widget* w_cropp = w_crop.get(); ui->row_widgets.push_back(std::move(w_crop));
            rows_.push_back(std::vector<Widget*>{ w_cropp });
        }

        items_.push_back(std::move(ui));
    }
}
