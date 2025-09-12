#include "animations_floating_panel.hpp"

#include "FloatingCollapsible.hpp"
#include "widgets.hpp"
#include "dm_styles.hpp"

#include "asset/asset_info.hpp"
#include "utils/input.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }

AnimationsFloatingPanel::AnimationsFloatingPanel() {
    box_ = std::make_unique<FloatingCollapsible>("Animations", 32, 64);
    box_->set_expanded(true);
}

AnimationsFloatingPanel::~AnimationsFloatingPanel() = default;

void AnimationsFloatingPanel::set_info(const std::shared_ptr<AssetInfo>& info) {
    info_ = info;
    rebuild_rows();
}

void AnimationsFloatingPanel::open() { if (box_) box_->set_visible(true); }
void AnimationsFloatingPanel::close() { if (box_) box_->set_visible(false); }
bool AnimationsFloatingPanel::is_open() const { return box_ && box_->is_visible(); }

void AnimationsFloatingPanel::update(const Input& input, int screen_w, int screen_h) {
    if (box_) box_->update(input, screen_w, screen_h);
}

bool AnimationsFloatingPanel::handle_event(const SDL_Event& e) {
    if (!box_ || !info_) return false;
    bool used = box_->handle_event(e);

    // Header: new button
    if (new_btn_) {
        // ButtonWidget already toggles click; here we act on value state by inspecting handle_event result is not trivial.
        // Instead, we rebuild rows on each frame and wire ButtonWidget with on_click callback in rebuild_rows().
    }

    // Detect changes per item and write JSON
    bool changed_any = false;
    for (auto& it : items_) {
        if (!it) continue;
        // rename
        if (it->id_box) {
            std::string new_name = it->id_box->value();
            if (!new_name.empty() && new_name != it->name) {
                if (info_->rename_animation(it->name, new_name)) {
                    it->name = new_name;
                    changed_any = true;
                } else {
                    // revert textbox to old if rename failed
                    it->id_box->set_value(it->name);
                }
            }
        }
        // build payload from controls
        nlohmann::json payload = it->last_payload.is_object() ? it->last_payload : nlohmann::json::object();
        if (!payload.is_object()) payload = nlohmann::json::object();
        nlohmann::json src = payload.contains("source") && payload["source"].is_object() ? payload["source"] : nlohmann::json::object();
        // kind
        std::string kind = "folder";
        if (it->kind_dd_) { /* silence - placeholder */ }
        // DMDropdown has no direct API to set label; we track selection only via index.
        // We reconstruct from UI by reading the displayed options index via render-only approach is not feasible here.
        // As a workaround, we encode kind selection in the textbox path/ref visibility: if ref_dd exists and has options, and path_box value empty -> animation.
        if (it->kind_dd) {
            // We created kind_dd with ["folder","animation"] in that order
            kind = (it->kind_dd->selected() == 1) ? "animation" : "folder";
        }
        src["kind"] = kind;
        if (kind == "folder") {
            std::string p = it->path_box ? it->path_box->value() : std::string{};
            src["path"] = p;
            src["name"] = nullptr;
        } else {
            // animation ref
            std::vector<std::string> names = current_names_sorted();
            std::string ref = (!names.empty() && it->ref_dd) ? names[clampi(it->ref_dd->selected(), 0, (int)names.size()-1)] : std::string{};
            src["name"] = ref;
            src["path"] = "";
        }
        payload["source"] = src;
        // flags
        payload["flipped_source"] = it->flipped_cb ? it->flipped_cb->value() : false;
        payload["reverse_source"] = it->reversed_cb ? it->reversed_cb->value() : false;
        payload["locked"] = it->locked_cb ? it->locked_cb->value() : false;
        payload["loop"] = it->loop_cb ? it->loop_cb->value() : false;
        payload["rnd_start"] = it->rnd_start_cb ? it->rnd_start_cb->value() : false;
        // speed
        int spd = it->speed_sl ? it->speed_sl->value() : 1;
        if (spd == 0) spd = 1;
        payload["speed_factor"] = spd;
        // adjust movement length to number_of_frames
        int nframes = compute_frames_from_source(*info_, payload["source"]);
        if (nframes <= 0) nframes = 1;
        payload["number_of_frames"] = nframes;
        // movement array coercion: preserve existing if reasonable
        try {
            nlohmann::json mv = payload.value("movement", nlohmann::json::array());
            if (!mv.is_array()) mv = nlohmann::json::array();
            if ((int)mv.size() < nframes) {
                while ((int)mv.size() < nframes) mv.push_back({0,0});
            } else if ((int)mv.size() > nframes) {
                nlohmann::json trimmed = nlohmann::json::array();
                for (int i=0;i<nframes;++i) trimmed.push_back(mv[i]);
                mv.swap(trimmed);
            }
            if (nframes >= 1 && mv.size() >= 1) {
                try { mv[0] = {0,0}; } catch(...) {}
            }
            payload["movement"] = mv;
        } catch (...) {}

        // detect diff
        if (payload.dump() != it->last_payload.dump()) {
            if (info_->upsert_animation(it->name, payload)) {
                (void)info_->update_info_json();
                it->last_payload = payload;
                changed_any = true;
                // update frames label
                if (it->frames_label) {
                    std::ostringstream oss; oss << "Frames: " << nframes;
                    it->frames_label->set_value(oss.str());
                }
            }
        }
    }

    // Start dropdown
    if (start_dd_) {
        auto names = current_names_sorted();
        int idx = clampi(start_dd_->selected(), 0, (int)names.size()-1);
        std::string cur_start = info_->start_animation;
        if (!names.empty() && idx < (int)names.size()) {
            if (names[idx] != cur_start) {
                info_->set_start_animation_name(names[idx]);
                (void)info_->update_info_json();
                changed_any = true;
            }
        }
    }

    if (changed_any) {
        // Update header start options in case names changed
        rebuild_header_row();
    }

    return used || changed_any;
}

void AnimationsFloatingPanel::render(SDL_Renderer* r, int screen_w, int screen_h) const {
    if (box_) box_->render(r);
}

std::vector<std::string> AnimationsFloatingPanel::current_names_sorted() const {
    std::vector<std::string> names;
    if (info_) names = info_->animation_names();
    std::sort(names.begin(), names.end());
    return names;
}

int AnimationsFloatingPanel::compute_frames_from_source(const AssetInfo& info, const nlohmann::json& source) {
    try {
        if (!source.is_object()) return 1;
        std::string kind = source.value("kind", std::string{"folder"});
        if (kind == "animation") {
            // follow one level
            std::string ref = source.value("name", std::string{});
            if (ref.empty()) return 1;
            nlohmann::json other = info.animation_payload(ref);
            if (!other.is_object()) return 1;
            return compute_frames_from_source(info, other.value("source", nlohmann::json::object()));
        }
        // folder
        std::string rel = source.value("path", std::string{});
        fs::path dir = fs::path(info.asset_dir_path()) / rel;
        if (!fs::exists(dir) || !fs::is_directory(dir)) return 1;
        int count = 0;
        for (auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp") {
                ++count;
            }
        }
        return std::max(1, count);
    } catch (...) { return 1; }
}

nlohmann::json AnimationsFloatingPanel::default_payload(const std::string& name) {
    nlohmann::json p;
    p["source"] = nlohmann::json::object({ {"kind","folder"}, {"path", name}, {"name", nullptr} });
    p["flipped_source"] = false;
    p["reverse_source"] = false;
    p["locked"] = false;
    p["rnd_start"] = false;
    p["loop"] = false;
    p["speed_factor"] = 1;
    p["number_of_frames"] = 1;
    p["movement"] = nlohmann::json::array({ nlohmann::json::array({0,0}) });
    p["on_end"] = "default";
    return p;
}

void AnimationsFloatingPanel::rebuild_rows() {
    rows_.clear();
    header_widgets_.clear();
    items_.clear();
    if (!box_) return;
    rebuild_header_row();
    rebuild_animation_rows();
    box_->set_rows(rows_);
}

void AnimationsFloatingPanel::rebuild_header_row() {
    if (!info_) return;
    header_widgets_.clear();
    std::vector<Widget*> row;
    // Start dropdown
    auto names = current_names_sorted();
    int sel = 0;
    for (size_t i=0;i<names.size();++i) if (names[i] == info_->start_animation) { sel = (int)i; break; }
    start_dd_ = std::make_unique<DMDropdown>("Start", names, sel);
    header_widgets_.push_back(std::make_unique<DropdownWidget>(start_dd_.get()));
    row.push_back(header_widgets_.back().get());
    // New animation button
    new_btn_ = std::make_unique<DMButton>("New Animation", &DMStyles::CreateButton(), 160, DMButton::height());
    header_widgets_.push_back(std::make_unique<ButtonWidget>(new_btn_.get(), [this]() {
        if (!info_) return;
        std::string base = "new_anim";
        std::string nm = base;
        int i = 1;
        auto names = info_->animation_names();
        auto exists = [&](const std::string& s){ return std::find(names.begin(), names.end(), s) != names.end(); };
        while (exists(nm)) { nm = base + std::string("_") + std::to_string(i++); }
        auto p = default_payload(nm);
        p["number_of_frames"] = compute_frames_from_source(*info_, p["source"]);
        info_->upsert_animation(nm, p);
        info_->update_info_json();
        rebuild_rows();
    }));
    row.push_back(header_widgets_.back().get());
    rows_.push_back(row);
}

void AnimationsFloatingPanel::rebuild_animation_rows() {
    if (!info_) return;
    auto names = info_->animation_names();
    std::sort(names.begin(), names.end());
    for (const auto& nm : names) {
        auto ui = std::make_unique<AnimUI>();
        ui->name = nm;
        ui->last_payload = info_->animation_payload(nm);
        if (!ui->last_payload.is_object()) ui->last_payload = nlohmann::json::object();
        nlohmann::json src = ui->last_payload.value("source", nlohmann::json::object());
        std::string kind = src.value("kind", std::string{"folder"});
        std::string path = src.value("path", std::string{});
        std::string ref  = src.value("name", std::string{});

        // Controls
        ui->id_box = std::make_unique<DMTextBox>("ID", nm);
        ui->del_btn = std::make_unique<DMButton>("Delete", &DMStyles::DeleteButton(), 100, DMButton::height());

        // Kind dropdown
        std::vector<std::string> kind_opts{ "folder", "animation" };
        int kind_idx = (kind == "animation") ? 1 : 0;
        ui->kind_dd = std::make_unique<DMDropdown>("Kind", kind_opts, kind_idx);

        ui->path_box = std::make_unique<DMTextBox>("Folder", path);
        auto all_names = current_names_sorted();
        int ref_idx = 0; for (size_t i=0;i<all_names.size();++i) if (all_names[i] == ref) { ref_idx = (int)i; break; }
        ui->ref_dd = std::make_unique<DMDropdown>("Animation", all_names, ref_idx);

        ui->flipped_cb  = std::make_unique<DMCheckbox>("flipped",  ui->last_payload.value("flipped_source", false));
        ui->reversed_cb = std::make_unique<DMCheckbox>("reverse",  ui->last_payload.value("reverse_source", false));
        ui->locked_cb   = std::make_unique<DMCheckbox>("locked",   ui->last_payload.value("locked", false));
        ui->rnd_start_cb= std::make_unique<DMCheckbox>("rnd start",ui->last_payload.value("rnd_start", false));
        ui->loop_cb     = std::make_unique<DMCheckbox>("loop",     ui->last_payload.value("loop", false));

        int spd = 1;
        try { spd = (int)std::lround(ui->last_payload.value("speed_factor", 1.0f)); } catch (...) { spd = 1; }
        if (spd == 0) spd = 1; spd = clampi(spd, -20, 20);
        ui->speed_sl = std::make_unique<DMSlider>("speed", -20, 20, spd);

        ui->movement_btn = std::make_unique<DMButton>("Edit Movement...", &DMStyles::HeaderButton(), 180, DMButton::height());

        // Frames label
        int nframes = compute_frames_from_source(*info_, src);
        std::ostringstream oss; oss << "Frames: " << nframes;
        ui->frames_label = std::make_unique<DMTextBox>("", oss.str());

        // Wire wrappers into rows
        // Row A: ID + Delete
        ui->row_widgets.push_back(std::make_unique<TextBoxWidget>(ui->id_box.get()));
        ui->row_widgets.push_back(std::make_unique<ButtonWidget>(ui->del_btn.get(), [this, nm]() {
            if (!info_) return; info_->remove_animation(nm); info_->update_info_json(); rebuild_rows();
        }));
        rows_.push_back(std::vector<Widget*>{ ui->row_widgets[0].get(), ui->row_widgets[1].get() });

        // Row B: Kind + Path/Ref
        ui->row_widgets.push_back(std::make_unique<DropdownWidget>(ui->kind_dd.get()));
        if (kind_idx == 0) {
            ui->row_widgets.push_back(std::make_unique<TextBoxWidget>(ui->path_box.get()));
        } else {
            ui->row_widgets.push_back(std::make_unique<DropdownWidget>(ui->ref_dd.get()));
        }
        rows_.push_back(std::vector<Widget*>{ ui->row_widgets[2].get(), ui->row_widgets[3].get() });

        // Row C: flags
        ui->row_widgets.push_back(std::make_unique<CheckboxWidget>(ui->flipped_cb.get()));
        ui->row_widgets.push_back(std::make_unique<CheckboxWidget>(ui->reversed_cb.get()));
        ui->row_widgets.push_back(std::make_unique<CheckboxWidget>(ui->locked_cb.get()));
        ui->row_widgets.push_back(std::make_unique<CheckboxWidget>(ui->rnd_start_cb.get()));
        ui->row_widgets.push_back(std::make_unique<CheckboxWidget>(ui->loop_cb.get()));
        rows_.push_back(std::vector<Widget*>{ ui->row_widgets[4].get(), ui->row_widgets[5].get(), ui->row_widgets[6].get(), ui->row_widgets[7].get(), ui->row_widgets[8].get() });

        // Row D: speed + movement + frames label
        ui->row_widgets.push_back(std::make_unique<SliderWidget>(ui->speed_sl.get()));
        ui->row_widgets.push_back(std::make_unique<ButtonWidget>(ui->movement_btn.get(), [this, nm]() {
            // TODO: hook movement modal overlay in C++
            (void)nm;
        }));
        ui->row_widgets.push_back(std::make_unique<TextBoxWidget>(ui->frames_label.get()));
        rows_.push_back(std::vector<Widget*>{ ui->row_widgets[9].get(), ui->row_widgets[10].get(), ui->row_widgets[11].get() });

        items_.push_back(std::move(ui));
    }
}

