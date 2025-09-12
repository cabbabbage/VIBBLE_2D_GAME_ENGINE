#include "animations_editor_panel.hpp"

#include "FloatingCollapsible.hpp"
#include "dm_styles.hpp"
#include "widgets.hpp"

#include "asset/asset_info.hpp"
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

// Simple thumbnail widget that loads and draws an image path on demand.
class ThumbWidget : public Widget {
public:
    using PathFn = std::function<std::string()>;
    explicit ThumbWidget(PathFn fn, int preferred_h = 120)
        : fn_(std::move(fn)), pref_h_(preferred_h) {}
    ~ThumbWidget() override { if (tex_) SDL_DestroyTexture(tex_); }
    void set_rect(const SDL_Rect& r) override { rect_ = r; }
    const SDL_Rect& rect() const override { return rect_; }
    int height_for_width(int /*w*/) const override { return pref_h_; }
    bool handle_event(const SDL_Event& /*e*/) override { return false; }
    void render(SDL_Renderer* r) const override {
        if (!r) return;
        // Resolve path
        const std::string path = fn_ ? fn_() : std::string{};
        if (path.empty()) return;
        // Reload if path changed or missing texture
        if (!tex_ || path != last_path_) {
            if (tex_) { SDL_DestroyTexture(tex_); tex_ = nullptr; }
            SDL_Texture* t = IMG_LoadTexture(r, path.c_str());
            if (t) { tex_ = t; last_path_ = path; }
        }
        if (!tex_) return;
        int tw=0, th=0; SDL_QueryTexture(tex_, nullptr, nullptr, &tw, &th);
        if (tw <= 0 || th <= 0) return;
        // Aspect fit into rect_
        float sx = rect_.w / float(std::max(1, tw));
        float sy = rect_.h / float(std::max(1, th));
        float s = std::min(sx, sy);
        int dw = int(tw * s), dh = int(th * s);
        SDL_Rect dst{ rect_.x + (rect_.w - dw)/2, rect_.y + (rect_.h - dh)/2, dw, dh };
        SDL_RenderCopy(r, tex_, nullptr, &dst);
        // Border
        SDL_SetRenderDrawColor(r, 90, 90, 90, 255);
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

// Resolve first frame image path for preview: follows one ref hop.
static std::string first_frame_path(const AssetInfo& info, const nlohmann::json& source) {
    try {
        if (!source.is_object()) return {};
        std::string kind = source.value("kind", std::string{"folder"});
        if (kind == "animation") {
            std::string ref = source.value("name", std::string{});
            if (ref.empty()) return {};
            nlohmann::json other = info.animation_payload(ref);
            if (!other.is_object()) return {};
            return first_frame_path(info, other.value("source", nlohmann::json::object()));
        }
        std::string rel = source.value("path", std::string{});
        if (rel.empty()) return {};
        fs::path dir = fs::path(info.asset_dir_path()) / rel;
        auto images = animation::get_image_paths(dir);
        if (images.empty()) return {};
        return images.front().string();
    } catch(...) { return {}; }
}

// Detect whether assigning current->ref would create a loop via source.kind=="animation" edges.
static bool creates_cycle(const AssetInfo& info, const std::string& current, const std::string& ref) {
    if (current.empty() || ref.empty()) return false;
    if (current == ref) return true;
    // Build a simple next map (one hop per anim)
    std::unordered_map<std::string,std::string> next;
    try {
        auto names = info.animation_names();
        for (const auto& nm : names) {
            nlohmann::json p = info.animation_payload(nm);
            if (!p.is_object()) continue;
            auto src = p.value("source", nlohmann::json::object());
            if (src.value("kind", std::string{}) == std::string{"animation"}) {
                std::string rn = src.value("name", std::string{});
                if (!rn.empty()) next[nm] = rn;
            }
        }
    } catch(...) {}
    // Temporarily add current->ref
    next[current] = ref;
    // Follow from current, detect loop
    std::unordered_set<std::string> seen;
    std::string x = current;
    for (int steps=0; steps < 1000; ++steps) {
        if (seen.count(x)) return true;
        seen.insert(x);
        auto it = next.find(x);
        if (it == next.end()) return false;
        x = it->second;
    }
    return true; // safety
}

AnimationsEditorPanel::AnimationsEditorPanel() {
    box_ = std::make_unique<FloatingCollapsible>("Animations", 32, 64);
    box_->set_expanded(true);
    // Start hidden; only show when user clicks "Configure Animations"
    box_->set_visible(false);
}

AnimationsEditorPanel::~AnimationsEditorPanel() = default;

void AnimationsEditorPanel::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    if (is_open()) {
        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
    }
}

void AnimationsEditorPanel::open()  {
    if (box_) box_->set_visible(true);
    // Defer heavy rebuild to next update tick to avoid rebuilding during event dispatch
    request_rebuild();
}
void AnimationsEditorPanel::close() { if (box_) box_->set_visible(false); }
bool AnimationsEditorPanel::is_open() const { return box_ && box_->is_visible(); }

void AnimationsEditorPanel::update(const Input& input, int screen_w, int screen_h) {
    if (!is_open()) return;
    if (rebuild_requested_) {
        rebuild_requested_ = false;
        if (info_) {
            rebuild_all_rows();
            if (box_) box_->set_rows(rows_);
        }
    }
    if (box_) box_->update(input, screen_w, screen_h);
    // Detect movement modal close and persist positions
    bool now_open = movement_modal_.is_open();
    if (movement_was_open_ && !now_open && info_ && !movement_anim_name_.empty()) {
        // Write positions back to JSON for the animation we edited
        nlohmann::json payload = info_->animation_payload(movement_anim_name_);
        if (!payload.is_object()) payload = nlohmann::json::object();
        nlohmann::json mv = nlohmann::json::array();
        const auto& pos = movement_modal_.positions();
        for (const auto& p : pos) mv.push_back(nlohmann::json::array({ p.first, p.second }));
        payload["movement"] = mv;
        info_->upsert_animation(movement_anim_name_, payload);
        (void)info_->update_info_json();
        // Rebuild rows to reflect any size changes
        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
        movement_anim_name_.clear();
    }
    movement_was_open_ = now_open;
}

bool AnimationsEditorPanel::handle_event(const SDL_Event& e) {
    if (!is_open()) return false;
    if (movement_modal_.is_open() && movement_modal_.handle_event(e)) return true;
    if (!box_ || !info_) return false;
    bool used = box_->handle_event(e);

    bool changed_any = false;
    for (auto& it : items_) {
        if (!it) continue;
        if (it->id_box) {
            std::string new_name = it->id_box->value();
            if (!new_name.empty() && new_name != it->name) {
                if (info_->rename_animation(it->name, new_name)) {
                    it->name = new_name; changed_any = true;
                } else {
                    it->id_box->set_value(it->name);
                }
            }
        }
        // build payload from controls
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
            // Guard against self or cycles
            if (ref == it->name || creates_cycle(*info_, it->name, ref)) ref.clear();
            src["name"] = ref; src["path"] = "";
        }
        payload["source"] = src;
        payload["flipped_source"] = it->flipped_cb ? it->flipped_cb->value() : false;
        payload["reverse_source"] = it->reversed_cb ? it->reversed_cb->value() : false;
        payload["locked"] = it->locked_cb ? it->locked_cb->value() : false;
        payload["loop"] = it->loop_cb ? it->loop_cb->value() : false;
        payload["rnd_start"] = it->rnd_start_cb ? it->rnd_start_cb->value() : false;

        int spd = it->speed_sl ? it->speed_sl->value() : 1; if (spd == 0) spd = 1; payload["speed_factor"] = spd;

        int nframes = compute_frames_from_source(*info_, payload["source"]); if (nframes <= 0) nframes = 1; payload["number_of_frames"] = nframes;
        try {
            nlohmann::json mv = payload.value("movement", nlohmann::json::array());
            if (!mv.is_array()) mv = nlohmann::json::array();
            if ((int)mv.size() < nframes) { while ((int)mv.size() < nframes) mv.push_back({0,0}); }
            else if ((int)mv.size() > nframes) { nlohmann::json trimmed = nlohmann::json::array(); for (int i=0;i<nframes;++i) trimmed.push_back(mv[i]); mv.swap(trimmed); }
            if (nframes >= 1 && mv.size() >= 1) { try { mv[0] = {0,0}; } catch(...){} }
            payload["movement"] = mv;
        } catch(...) {}

        // on_end selection
        if (it->on_end_dd) {
            auto all = current_names_sorted();
            std::vector<std::string> opts; opts.push_back("default"); opts.insert(opts.end(), all.begin(), all.end());
            int idx = clampi(it->on_end_dd->selected(), 0, (int)opts.size()-1);
            payload["on_end"] = opts[idx];
        }

        if (payload.dump() != it->last_payload.dump()) {
            if (info_->upsert_animation(it->name, payload)) {
                (void)info_->update_info_json();
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
        std::string cur = info_->start_animation;
        if (!names.empty() && idx < (int)names.size() && names[idx] != cur) {
            info_->set_start_animation_name(names[idx]); (void)info_->update_info_json(); changed_any = true;
        }
    }

    if (changed_any) {
        // Rebuild everything to refresh dynamic widgets (e.g., kind path/ref row)
        rebuild_all_rows();
        if (box_) box_->set_rows(rows_);
    }

    // Apply any rebuild request emitted from button callbacks safely after box_ handled the event
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
    std::vector<std::string> names; if (info_) names = info_->animation_names(); std::sort(names.begin(), names.end()); return names;
}

int AnimationsEditorPanel::compute_frames_from_source(const AssetInfo& info, const nlohmann::json& source) {
    try {
        if (!source.is_object()) return 1;
        std::string kind = source.value("kind", std::string{"folder"});
        if (kind == "animation") {
            std::string ref = source.value("name", std::string{}); if (ref.empty()) return 1;
            nlohmann::json other = info.animation_payload(ref); if (!other.is_object()) return 1;
            return compute_frames_from_source(info, other.value("source", nlohmann::json::object()));
        }
        std::string rel = source.value("path", std::string{});
        fs::path dir = fs::path(info.asset_dir_path()) / rel; if (!fs::exists(dir) || !fs::is_directory(dir)) return 1;
        int count = 0; for (auto& e : fs::directory_iterator(dir)) { if (!e.is_regular_file()) continue; auto ext = e.path().extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") ++count; }
        return std::max(1, count);
    } catch(...) { return 1; }
}

nlohmann::json AnimationsEditorPanel::default_payload(const std::string& name) {
    nlohmann::json p; p["source"] = nlohmann::json::object({{"kind","folder"},{"path",name},{"name",nullptr}});
    p["flipped_source"]=false; p["reverse_source"]=false; p["locked"]=false; p["rnd_start"]=false; p["loop"]=false; p["speed_factor"]=1; p["number_of_frames"]=1; p["movement"]=nlohmann::json::array({nlohmann::json::array({0,0})}); p["on_end"] = "default"; return p;
}

void AnimationsEditorPanel::rebuild_all_rows() {
    rows_.clear(); header_widgets_.clear(); items_.clear();
    if (!box_) return;
    rebuild_header_row();
    rebuild_animation_rows();
    box_->set_rows(rows_);
}

void AnimationsEditorPanel::rebuild_header_row() {
    if (!info_) return;
    header_widgets_.clear();
    std::vector<Widget*> row;
    auto names = current_names_sorted();
    int sel = 0; for (size_t i=0;i<names.size();++i) if (names[i] == info_->start_animation) { sel = (int)i; break; }
    start_dd_ = std::make_unique<DMDropdown>("Start", names, sel);
    header_widgets_.push_back(std::make_unique<DropdownWidget>(start_dd_.get()));
    row.push_back(header_widgets_.back().get());

    new_btn_ = std::make_unique<DMButton>("New Animation", &DMStyles::CreateButton(), 160, DMButton::height());
    header_widgets_.push_back(std::make_unique<ButtonWidget>(new_btn_.get(), [this]() {
        if (!info_) return;
        std::string base = "new_anim"; std::string nm = base; int i = 1; auto names = info_->animation_names();
        auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
        while (exists(nm)) nm = base + std::string("_") + std::to_string(i++);
        auto p = default_payload(nm); p["number_of_frames"] = compute_frames_from_source(*info_, p["source"]);
        info_->upsert_animation(nm, p); info_->update_info_json(); request_rebuild();
    }));
    row.push_back(header_widgets_.back().get());

    // New From Folder... convenience button
    auto new_folder_btn = std::make_unique<DMButton>("New From Folder...", &DMStyles::ListButton(), 180, DMButton::height());
    auto new_folder_btn_ptr = new_folder_btn.get();
    header_widgets_.push_back(std::make_unique<ButtonWidget>(new_folder_btn_ptr, [this]() {
        if (!info_) return;
        // Create a unique name and default folder path = name
        std::string base = "new_anim"; std::string nm = base; int i=1; auto names = info_->animation_names();
        auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
        while (exists(nm)) nm = base + std::string{"_"} + std::to_string(i++);
        std::string rel = nm; // default folder under asset dir
        try { fs::create_directories(fs::path(info_->asset_dir_path()) / rel); } catch(...) {}
        auto p = default_payload(nm); p["source"]["path"] = rel; p["source"]["kind"] = "folder"; p["source"]["name"] = nullptr;
        p["number_of_frames"] = compute_frames_from_source(*info_, p["source"]);
        info_->upsert_animation(nm, p); info_->update_info_json(); request_rebuild();
    }));
    row.push_back(header_widgets_.back().get());

    if (rows_.empty()) rows_.push_back(row); else rows_[0] = row;

    // Empty state helper row when no animations exist
    if (names.empty()) {
        // Message
        auto lbl = std::make_unique<DMTextBox>("", "No animations found. Create one to get started.");
        header_widgets_.push_back(std::make_unique<TextBoxWidget>(lbl.get()));
        // Button
        auto create_btn = std::make_unique<DMButton>("Create First Animation", &DMStyles::CreateButton(), 220, DMButton::height());
        header_widgets_.push_back(std::make_unique<ButtonWidget>(create_btn.get(), [this]() {
            if (!info_) return;
            std::string base = "new_anim"; std::string nm = base; int i=1; auto names = info_->animation_names();
            auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
            while (exists(nm)) nm = base + std::string{"_"} + std::to_string(i++);
            auto p = default_payload(nm);
            info_->upsert_animation(nm, p); (void)info_->update_info_json(); request_rebuild();
        }));
        // Build row
        rows_.push_back(std::vector<Widget*>{ header_widgets_[1].get(), header_widgets_[2].get() });
        // Keep DMTextBox and DMButton alive via local unique_ptrs ownership transfer
        (void)lbl.release(); (void)create_btn.release();
    }
}

void AnimationsEditorPanel::rebuild_animation_rows() {
    if (!info_) return;
    auto names = info_->animation_names(); std::sort(names.begin(), names.end());
    for (const auto& nm : names) {
        auto ui = std::make_unique<AnimUI>(); ui->name = nm; ui->last_payload = info_->animation_payload(nm); if (!ui->last_payload.is_object()) ui->last_payload = nlohmann::json::object();
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

        int nframes = compute_frames_from_source(*info_, src); std::ostringstream oss; oss << "Frames: " << nframes; ui->frames_label = std::make_unique<DMTextBox>("", oss.str());

        // Row A
        auto w_id = std::make_unique<TextBoxWidget>(ui->id_box.get());
        Widget* w_id_ptr = w_id.get(); ui->row_widgets.push_back(std::move(w_id));
        auto w_del = std::make_unique<ButtonWidget>(ui->del_btn.get(), [this, nm]() {
            if (!info_) return; info_->remove_animation(nm); info_->update_info_json(); request_rebuild();
        });
        Widget* w_del_ptr = w_del.get(); ui->row_widgets.push_back(std::move(w_del));
        rows_.push_back(std::vector<Widget*>{ w_id_ptr, w_del_ptr });
        // Row B
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
        // Row B2: Folder scaffolding + Thumbnail preview
        if (kind_idx == 0) {
            ui->create_folder_btn = std::make_unique<DMButton>("Create Folder", &DMStyles::ListButton(), 160, DMButton::height());
            auto w_cf = std::make_unique<ButtonWidget>(ui->create_folder_btn.get(), [this, nm]() {
                if (!info_) return;
                // Find UI again to get path value
                for (auto& it2 : items_) {
                    if (!it2 || it2->name != nm) continue;
                    std::string rel = it2->path_box ? it2->path_box->value() : std::string{};
                    if (rel.empty()) { rel = nm; if (it2->path_box) it2->path_box->set_value(rel); }
                    try {
                        fs::path dir = fs::path(info_->asset_dir_path()) / rel;
                        fs::create_directories(dir);
                    } catch(...) {}
                    // Persist path in JSON and request rebuild
                    nlohmann::json payload = info_->animation_payload(nm);
                    if (!payload.is_object()) payload = nlohmann::json::object();
                    nlohmann::json src = payload.value("source", nlohmann::json::object());
                    src["kind"] = "folder"; src["path"] = rel; src["name"] = nullptr; payload["source"] = src;
                    info_->upsert_animation(nm, payload); (void)info_->update_info_json();
                    request_rebuild();
                    break;
                }
            });
            Widget* w_cf_ptr = w_cf.get(); ui->row_widgets.push_back(std::move(w_cf));
            // Thumbnail preview for folder
            auto thumb = std::make_unique<ThumbWidget>([this, nm, ui_ptr = ui.get()](){
                if (!info_) return std::string{};
                nlohmann::json s = ui_ptr->last_payload.value("source", nlohmann::json::object());
                s["kind"] = "folder"; s["path"] = ui_ptr->path_box ? ui_ptr->path_box->value() : std::string{}; s["name"] = nullptr;
                return first_frame_path(*info_, s);
            }, 96);
            Widget* w_thumbp = thumb.get(); ui->row_widgets.push_back(std::move(thumb));
            rows_.push_back(std::vector<Widget*>{ w_cf_ptr, w_thumbp });
        }
        else {
            // Thumbnail for ref kind
            auto thumb = std::make_unique<ThumbWidget>([this, ui_ptr = ui.get()](){
                if (!info_) return std::string{};
                nlohmann::json s = ui_ptr->last_payload.value("source", nlohmann::json::object());
                s["kind"] = "animation"; // ensure
                return first_frame_path(*info_, s);
            }, 96);
            Widget* w_thumbp = thumb.get(); ui->row_widgets.push_back(std::move(thumb));
            rows_.push_back(std::vector<Widget*>{ w_thumbp });
        }
        // Row C flags
        auto w_cb1 = std::make_unique<CheckboxWidget>(ui->flipped_cb.get()); Widget* w_cb1p = w_cb1.get(); ui->row_widgets.push_back(std::move(w_cb1));
        auto w_cb2 = std::make_unique<CheckboxWidget>(ui->reversed_cb.get()); Widget* w_cb2p = w_cb2.get(); ui->row_widgets.push_back(std::move(w_cb2));
        auto w_cb3 = std::make_unique<CheckboxWidget>(ui->locked_cb.get());   Widget* w_cb3p = w_cb3.get(); ui->row_widgets.push_back(std::move(w_cb3));
        auto w_cb4 = std::make_unique<CheckboxWidget>(ui->rnd_start_cb.get());Widget* w_cb4p = w_cb4.get(); ui->row_widgets.push_back(std::move(w_cb4));
        auto w_cb5 = std::make_unique<CheckboxWidget>(ui->loop_cb.get());     Widget* w_cb5p = w_cb5.get(); ui->row_widgets.push_back(std::move(w_cb5));
        rows_.push_back(std::vector<Widget*>{ w_cb1p, w_cb2p, w_cb3p, w_cb4p, w_cb5p });
        // Row D speed + movement + frames
        auto w_spd = std::make_unique<SliderWidget>(ui->speed_sl.get()); Widget* w_spdp = w_spd.get(); ui->row_widgets.push_back(std::move(w_spd));
        auto w_mov = std::make_unique<ButtonWidget>(ui->movement_btn.get(), [this, nm]() {
            if (!info_) return; nlohmann::json payload = info_->animation_payload(nm);
            std::vector<animation::MovementModal::Position> pos; try { auto mv = payload.at("movement"); if (mv.is_array()) { for (auto& p : mv) { if (p.is_array() && p.size() >= 2) pos.emplace_back(p[0].get<int>(), p[1].get<int>()); } } } catch(...) {}
            movement_anim_name_ = nm;
            movement_modal_.open(pos);
        });
        Widget* w_movp = w_mov.get(); ui->row_widgets.push_back(std::move(w_mov));
        auto w_frames = std::make_unique<TextBoxWidget>(ui->frames_label.get()); Widget* w_framesp = w_frames.get(); ui->row_widgets.push_back(std::move(w_frames));
        rows_.push_back(std::vector<Widget*>{ w_spdp, w_movp, w_framesp });

        // Row E: on_end + duplicate
        // on_end dropdown: include "default" sentinel followed by animation names
        std::string on_end_val = ui->last_payload.value("on_end", std::string{"default"});
        auto all2 = current_names_sorted();
        std::vector<std::string> end_opts; end_opts.push_back("default"); end_opts.insert(end_opts.end(), all2.begin(), all2.end());
        int end_idx = 0; for (size_t i=0;i<end_opts.size();++i) if (end_opts[i] == on_end_val) { end_idx = (int)i; break; }
        ui->on_end_dd = std::make_unique<DMDropdown>("on_end", end_opts, end_idx);
        auto w_end = std::make_unique<DropdownWidget>(ui->on_end_dd.get()); Widget* w_endp = w_end.get(); ui->row_widgets.push_back(std::move(w_end));

        ui->dup_btn = std::make_unique<DMButton>("Duplicate", &DMStyles::ListButton(), 120, DMButton::height());
        auto w_dup = std::make_unique<ButtonWidget>(ui->dup_btn.get(), [this, nm]() {
            if (!info_) return;
            // Generate unique name based on nm
            std::string base = nm + std::string{"_copy"};
            std::string new_nm = base; int i=1; auto names = info_->animation_names();
            auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
            while (exists(new_nm)) new_nm = base + std::string{"_"} + std::to_string(i++);
            nlohmann::json payload = info_->animation_payload(nm);
            info_->upsert_animation(new_nm, payload); (void)info_->update_info_json(); request_rebuild();
        });
        Widget* w_dupp = w_dup.get(); ui->row_widgets.push_back(std::move(w_dup));
        rows_.push_back(std::vector<Widget*>{ w_endp, w_dupp });

        // Row F: Crop helpers for folder sources
        if (kind_idx == 0) {
            // Alpha threshold slider
            ui->alpha_sl = std::make_unique<DMSlider>("alpha", 0, 255, 0);
            auto w_alpha = std::make_unique<SliderWidget>(ui->alpha_sl.get()); Widget* w_alphap = w_alpha.get(); ui->row_widgets.push_back(std::move(w_alpha));
            // Compute button
            ui->compute_btn = std::make_unique<DMButton>("Compute Bounds", &DMStyles::ListButton(), 160, DMButton::height());
            auto w_comp = std::make_unique<ButtonWidget>(ui->compute_btn.get(), [this, nm]() {
                if (!info_) return;
                nlohmann::json payload = info_->animation_payload(nm);
                auto src = payload.value("source", nlohmann::json::object());
                std::string rel = src.value("path", std::string{});
                if (rel.empty()) return;
                fs::path dir = fs::path(info_->asset_dir_path()) / rel;
                auto images = animation::get_image_paths(dir);
                // Find this UI entry to read alpha slider and write results
                for (auto& it2 : items_) {
                    if (it2 && it2->name == nm) {
                        int thr = it2->alpha_sl ? it2->alpha_sl->value() : 0;
                        animation::Bounds b = animation::compute_union_bounds(images, thr);
                        it2->last_bounds = b; it2->has_bounds = (b.base_w > 0 && b.base_h > 0 && (b.top||b.bottom||b.left||b.right));
                        std::ostringstream oss; oss << "Crop T:" << b.top << " B:" << b.bottom << " L:" << b.left << " R:" << b.right;
                        if (!it2->crop_summary) it2->crop_summary = std::make_unique<DMTextBox>("Bounds", "");
                        it2->crop_summary->set_value(oss.str());
                        // No full rebuild here; we updated the visible summary textbox.
                        break;
                    }
                }
            });
            Widget* w_compp = w_comp.get(); ui->row_widgets.push_back(std::move(w_comp));
            // Crop summary textbox (shows after compute)
            if (!ui->crop_summary) ui->crop_summary = std::make_unique<DMTextBox>("Bounds", "");
            auto w_csum = std::make_unique<TextBoxWidget>(ui->crop_summary.get()); Widget* w_csump = w_csum.get(); ui->row_widgets.push_back(std::move(w_csum));
            rows_.push_back(std::vector<Widget*>{ w_alphap, w_compp, w_csump });

            // Row G: Apply crop
            ui->crop_btn = std::make_unique<DMButton>("Apply Crop", &DMStyles::DeleteButton(), 140, DMButton::height());
            auto w_crop = std::make_unique<ButtonWidget>(ui->crop_btn.get(), [this, nm]() {
                if (!info_) return;
                // Find this UI entry
                for (auto& it2 : items_) {
                    if (it2 && it2->name == nm) {
                        nlohmann::json payload = info_->animation_payload(nm);
                        auto src = payload.value("source", nlohmann::json::object());
                        std::string rel = src.value("path", std::string{});
                        if (rel.empty()) return;
                        fs::path dir = fs::path(info_->asset_dir_path()) / rel;
                        auto images = animation::get_image_paths(dir);
                        int thr = it2->alpha_sl ? it2->alpha_sl->value() : 0;
                        // Compute bounds if missing
                        animation::Bounds b = it2->has_bounds ? it2->last_bounds : animation::compute_union_bounds(images, thr);
                        if (b.base_w == 0) return;
                        (void)animation::crop_images_with_bounds(images, b.top, b.bottom, b.left, b.right);
                        // Recompute frames label and refresh UI
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
