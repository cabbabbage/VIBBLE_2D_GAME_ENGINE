#include "asset_info_ui.hpp"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <vector>
#include <cstdlib>
#include "utils/input.hpp"
#include "asset/asset_info.hpp"
#include "ui/slider.hpp"
#include "ui/checkbox.hpp"
#include "ui/text_box.hpp"
#include "utils/text_style.hpp"
#include "ui/styles.hpp"
#include "ui/dev_styles.hpp"
#include "utils/area.hpp"
#include "custom_controllers/Bomb_controller.hpp"
#include "custom_controllers/Davey_controller.hpp"
#include "custom_controllers/default_controller.hpp"
#include "custom_controllers/Frog_controller.hpp"
#include "custom_controllers/Vibble_controller.hpp"
namespace {
	std::string trim(const std::string& s) {
		size_t b = s.find_first_not_of(" \t\n\r");
		if (b == std::string::npos) return "";
		size_t e = s.find_last_not_of(" \t\n\r");
		return s.substr(b, e-b+1);
	}
}

AssetInfoUI::AssetInfoUI() {}
AssetInfoUI::~AssetInfoUI() = default;

void AssetInfoUI::set_info(const std::shared_ptr<AssetInfo>& info) {
	info_ = info;
	build_widgets();
}

void AssetInfoUI::clear_info() {
	info_.reset();
	s_z_threshold_.reset();
	s_min_same_type_.reset();
	s_min_all_.reset();
	s_scale_pct_.reset();
	c_passable_.reset();
	c_flipable_.reset();
	t_type_.reset();
	t_tags_.reset();
	b_config_anim_.reset();
}

void AssetInfoUI::open()  { visible_ = true; }
void AssetInfoUI::close() { visible_ = false; }
void AssetInfoUI::toggle(){ visible_ = !visible_; }

void AssetInfoUI::build_widgets() {
	if (!info_) return;
	s_z_threshold_   = std::make_unique<Slider>("Z Threshold", -1024, 1024, info_->z_threshold);
	s_min_same_type_ = std::make_unique<Slider>("Min Same-Type Distance", 0, 2048, info_->min_same_type_distance);
	s_min_all_       = std::make_unique<Slider>("Min Distance (All)", 0, 2048, info_->min_distance_all);
	int pct = std::max(0, (int)std::round(info_->scale_factor * 100.0f));
	s_scale_pct_     = std::make_unique<Slider>("Scale (%)", 10, 400, pct);
	const SliderStyle* dev_slider = &DevStyles::DefaultSlider();
	if (s_z_threshold_)   s_z_threshold_->set_style(dev_slider);
	if (s_min_same_type_) s_min_same_type_->set_style(dev_slider);
	if (s_min_all_)       s_min_all_->set_style(dev_slider);
	if (s_scale_pct_)     s_scale_pct_->set_style(dev_slider);
	c_passable_ = std::make_unique<Checkbox>("Passable", info_->has_tag("passable"));
	c_flipable_ = std::make_unique<Checkbox>("Flipable (can invert)", info_->flipable);
	t_type_  = std::make_unique<TextBox>("Type", info_->type);
	std::ostringstream oss;
	for (size_t i=0;i<info_->tags.size();++i) { oss << info_->tags[i]; if (i+1<info_->tags.size()) oss << ", "; }
	t_tags_  = std::make_unique<TextBox>("Tags (comma)", oss.str());
	b_config_anim_ = std::make_unique<Button>( "Configure Animations", &DevStyles::PrimaryButton(), 260, Button::height() );
	b_close_ = std::make_unique<Button>( "Close", &DevStyles::SecondaryButton(), Button::width(), Button::height() );
	b_areas_toggle_ = std::make_unique<Button>( "Areas ▸", &DevStyles::SecondaryButton(), 180, Button::height() );
	b_create_area_ = std::make_unique<Button>( "Create New Area", &DevStyles::PrimaryButton(), 220, Button::height() );
	t_new_area_name_ = std::make_unique<TextBox>("Area Name", "");
	t_new_area_name_->set_editing(false);
	prompt_new_area_ = false;
	rebuild_area_widgets();
	scroll_ = 0;
}

void AssetInfoUI::layout_widgets(int screen_w, int screen_h) const {
	(void)screen_h;
	int panel_x = (screen_w * 2) / 3;
	int panel_w = screen_w - panel_x;
	panel_ = SDL_Rect{ panel_x, 0, panel_w, screen_h };
	int x = panel_.x + 16;
	int y = 16 - scroll_;
	if (t_type_) {
		t_type_->set_rect(SDL_Rect{ x, y + 20, std::min(440, panel_w - 32), TextBox::height() });
		y += 20 + TextBox::height() + 14;
	}
	if (t_tags_) {
		t_tags_->set_rect(SDL_Rect{ x, y + 20, std::min(480, panel_w - 32), TextBox::height() });
		y += 20 + TextBox::height() + 30;
	}
	if (s_scale_pct_) {
		s_scale_pct_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
		y += 10 + Slider::height() + 8;
	}
	if (c_flipable_) {
		c_flipable_->set_rect(SDL_Rect{ x, y + 4, panel_w - 32, Checkbox::height() });
		y += 4 + Checkbox::height() + 18;
	}
	if (s_z_threshold_) {
		s_z_threshold_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
		y += 10 + Slider::height() + 8;
	}
	if (s_min_same_type_) {
		s_min_same_type_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
		y += 10 + Slider::height() + 8;
	}
	if (s_min_all_) {
		s_min_all_->set_rect(SDL_Rect{ x, y + 10, panel_w - 32, Slider::height() });
		y += 10 + Slider::height() + 8;
	}
	if (b_config_anim_) {
		int btn_w = 260;
		int btn_h = Button::height();
		b_config_anim_->set_rect(SDL_Rect{
                           panel_.x + 16,
                           panel_.y + panel_.h - btn_h - 16,
                           btn_w, btn_h
                           });
	}
	if (b_close_) {
		int btn_w = 100;
		int btn_h = Button::height();
		b_close_->set_rect(SDL_Rect{
                     panel_.x + panel_.w - btn_w - 16,
                     panel_.y + panel_.h - btn_h - 16,
                     btn_w, btn_h
                     });
	}
	max_scroll_ = std::max(0, y + 20 - panel_.h);
}

void AssetInfoUI::update(const Input& input, int screen_w, int screen_h) {
	if (!visible_) return;
	if (!info_) return;
	layout_widgets(screen_w, screen_h);
	int mx = input.getX();
	int my = input.getY();
	if (mx >= panel_.x && mx < panel_.x + panel_.w && my >= panel_.y && my < panel_.y + panel_.h) {
		int dy = input.getScrollY();
		if (dy != 0) {
			scroll_ -= dy * 40;
			scroll_ = std::max(0, std::min(max_scroll_, scroll_));
		}
	}
}

void AssetInfoUI::handle_event(const SDL_Event& e) {
	if (!visible_ || !info_) return;
	if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
		close();
		return;
	}
	if (b_close_ && b_close_->handle_event(e)) {
		close();
		return;
	}
	if (b_config_anim_ && b_config_anim_->handle_event(e)) {
		save_now();
		std::string path = "SRC/" + info_->name + "/info.json";
		std::string cmd = "python3 scripts/animation_ui.py \"" + path + "\"";
		std::system(cmd.c_str());
		return;
	}
	bool changed = false;
	if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
		const SDL_Point p{ e.button.x, e.button.y };
		auto inside = [&](const std::unique_ptr<TextBox>& tb){ return tb && SDL_PointInRect(&p, &tb->rect()); };
		bool any = (t_type_ && inside(t_type_)) || (t_tags_ && inside(t_tags_));
		if (any) {
			if (t_type_  && !inside(t_type_))  t_type_->set_editing(false);
			if (t_tags_  && !inside(t_tags_))  t_tags_->set_editing(false);
		} else {
			if (t_type_)  t_type_->set_editing(false);
			if (t_tags_)  t_tags_->set_editing(false);
		}
	}
	if (s_z_threshold_ && s_z_threshold_->handle_event(e)) {
		info_->set_z_threshold(s_z_threshold_->value());
		changed = true;
	}
	if (s_min_same_type_ && s_min_same_type_->handle_event(e)) {
		info_->set_min_same_type_distance(s_min_same_type_->value());
		changed = true;
	}
	if (s_min_all_ && s_min_all_->handle_event(e)) {
		info_->set_min_distance_all(s_min_all_->value());
		changed = true;
	}
	if (s_scale_pct_ && s_scale_pct_->handle_event(e)) {
		info_->set_scale_percentage((float)s_scale_pct_->value());
		changed = true;
	}
	if (c_passable_ && c_passable_->handle_event(e)) {
		info_->set_passable(c_passable_->value());
		if (t_tags_) {
			std::ostringstream oss;
			for (size_t i=0;i<info_->tags.size();++i) { oss << info_->tags[i]; if (i+1<info_->tags.size()) oss << ", "; }
			t_tags_->set_value(oss.str());
		}
		changed = true;
	}
	if (c_flipable_ && c_flipable_->handle_event(e)) {
		info_->set_flipable(c_flipable_->value());
		changed = true;
	}
	if (t_type_ && t_type_->handle_event(e)) {
		info_->set_asset_type(t_type_->value());
		changed = true;
	}
	if (t_tags_ && t_tags_->handle_event(e)) {
		std::vector<std::string> tags;
		std::string s = t_tags_->value();
		size_t pos = 0;
		while (pos != std::string::npos) {
			size_t comma = s.find(',', pos);
			std::string tok = (comma == std::string::npos) ? s.substr(pos) : s.substr(pos, comma - pos);
			tok = trim(tok);
			if (!tok.empty()) tags.push_back(tok);
			if (comma == std::string::npos) break;
			pos = comma + 1;
		}
		info_->set_tags(tags);
		if (c_passable_) c_passable_->set_value(info_->has_tag("passable"));
		changed = true;
	}
	if (changed) save_now();
	if (b_areas_toggle_ && b_areas_toggle_->handle_event(e)) {
		areas_expanded_ = !areas_expanded_;
	}
	if (areas_expanded_) {
		for (auto& btn : area_buttons_) {
			if (btn && btn->handle_event(e)) {
					open_area_editor(btn->text());
					break;
			}
		}
		if (b_create_area_ && b_create_area_->handle_event(e)) {
			prompt_new_area_ = true;
			if (t_new_area_name_) t_new_area_name_->set_editing(true);
		}
		if (prompt_new_area_ && t_new_area_name_ && t_new_area_name_->handle_event(e)) {
			std::string nm = t_new_area_name_->value();
			if (!nm.empty() && !t_new_area_name_->is_editing()) {
					open_area_editor(nm);
					prompt_new_area_ = false;
					t_new_area_name_->set_value("");
			}
		}
	}
}

void AssetInfoUI::render(SDL_Renderer* r, int screen_w, int screen_h) const {
	if (!visible_ || !info_) return;
	layout_widgets(screen_w, screen_h);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_Color kInfoPanelBG = Styles::Night(); kInfoPanelBG.a = 160;
	SDL_SetRenderDrawColor(r, kInfoPanelBG.r, kInfoPanelBG.g, kInfoPanelBG.b, kInfoPanelBG.a);
	SDL_RenderFillRect(r, &panel_);
	auto draw_header = [&](const char* title, int& y) {
		const TextStyle& h = TextStyles::MediumSecondary();
		TTF_Font* f = h.open_font();
		if (!f) return;
		SDL_Surface* surf = TTF_RenderText_Blended(f, title, Styles::Gold());
		if (surf) {
			SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
			if (tex) {
					SDL_Rect dst{ panel_.x + 16, y - scroll_, surf->w, surf->h };
					SDL_RenderCopy(r, tex, nullptr, &dst);
					SDL_DestroyTexture(tex);
			}
			SDL_FreeSurface(surf);
		}
		TTF_CloseFont(f);
		y += 22;
	};
	int y = 16;
	draw_header("Identity", y);
	if (t_type_)  t_type_->render(r);
	if (t_tags_)  t_tags_->render(r);
	y = t_tags_ ? (t_tags_->rect().y + t_tags_->rect().h + 18 + scroll_) : y + 60;
	draw_header("Appearance", y);
	if (s_scale_pct_) s_scale_pct_->render(r);
	if (c_flipable_)  c_flipable_->render(r);
	int headerDZ = (s_z_threshold_ ? s_z_threshold_->rect().y - 24 : (panel_.y + 280)) + scroll_;
	headerDZ = std::max(headerDZ, (c_flipable_ ? c_flipable_->rect().y + 16 + scroll_ : headerDZ));
	draw_header("Distances & Z", headerDZ);
	if (s_z_threshold_)   s_z_threshold_->render(r);
	if (s_min_same_type_) s_min_same_type_->render(r);
	if (s_min_all_)       s_min_all_->render(r);
	int flagsHeaderY = (c_passable_ ? c_passable_->rect().y - 24 + scroll_ : panel_.y + 520);
	draw_header("Flags", flagsHeaderY);
	if (c_passable_) c_passable_->render(r);
	int areasHeaderY = (c_passable_ ? c_passable_->rect().y + c_passable_->rect().h + 24 + scroll_ : panel_.y + 560);
	int y2 = areasHeaderY;
	draw_header("Areas", y2);
	if (b_areas_toggle_) {
		b_areas_toggle_->set_rect(SDL_Rect{ panel_.x + 16, y2 - scroll_, 140, Button::height() });
		b_areas_toggle_->set_text(areas_expanded_ ? std::string("Areas ▾") : std::string("Areas ▸"));
		b_areas_toggle_->render(r);
	}
	int yAreas = y2 + Button::height() + 8;
	if (areas_expanded_) {
		int x = panel_.x + 32;
		for (auto& btn : area_buttons_) {
			btn->set_rect(SDL_Rect{ x, yAreas - scroll_, 240, Button::height() });
			btn->render(r);
			yAreas += Button::height() + 6;
		}
		if (b_create_area_) {
			b_create_area_->set_rect(SDL_Rect{ x, yAreas - scroll_, 220, Button::height() });
			b_create_area_->render(r);
			yAreas += Button::height() + 6;
		}
		if (prompt_new_area_ && t_new_area_name_) {
			t_new_area_name_->set_rect(SDL_Rect{ x, yAreas - scroll_, 260, TextBox::height() });
			t_new_area_name_->render(r);
			yAreas += TextBox::height() + 6;
		}
	}
	if (b_config_anim_) b_config_anim_->render(r);
	if (b_close_)        b_close_->render(r);
	last_renderer_ = r;
}

void AssetInfoUI::save_now() const {
	if (info_) (void)info_->update_info_json();
}

void AssetInfoUI::rebuild_area_widgets() {
	area_buttons_.clear();
	if (!info_) return;
	for (const auto& na : info_->areas) {
		auto b = std::make_unique<Button>(na.name, &DevStyles::SecondaryButton(), 240, Button::height());
		area_buttons_.push_back(std::move(b));
	}
}

void AssetInfoUI::open_area_editor(const std::string& name) {
	if (!info_ || !last_renderer_) return;
	Area* base = nullptr;
	for (auto& na : info_->areas) {
		if (na.name == name && na.area) { base = na.area.get(); break; }
	}
	const int canvas_w = std::max(32, (int)std::lround(info_->original_canvas_width  * info_->scale_factor));
	const int canvas_h = std::max(32, (int)std::lround(info_->original_canvas_height * info_->scale_factor));
	auto pick_sprite = [&]() -> SDL_Texture* {
		auto try_get = [&](const std::string& key) -> SDL_Texture* {
			auto it = info_->animations.find(key);
			if (it != info_->animations.end() && !it->second.frames.empty()) {
					return it->second.frames.front();
			}
			return nullptr;
		};
		if (!info_->start_animation.empty()) {
			if (auto t = try_get(info_->start_animation)) return t;
		}
		if (auto t = try_get("default")) return t;
		for (auto& kv : info_->animations) {
			if (!kv.second.frames.empty()) return kv.second.frames.front();
		}
		return nullptr;
	};
	try {
		SDL_Texture* bg = SDL_CreateTexture(last_renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, canvas_w, canvas_h);
		if (!bg) throw std::runtime_error("Failed to create editor canvas");
		SDL_SetTextureBlendMode(bg, SDL_BLENDMODE_BLEND);
		SDL_Texture* prev = SDL_GetRenderTarget(last_renderer_);
		SDL_SetRenderTarget(last_renderer_, bg);
		SDL_SetRenderDrawBlendMode(last_renderer_, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(last_renderer_, 0, 0, 0, 0);
		SDL_RenderClear(last_renderer_);
		if (SDL_Texture* sprite = pick_sprite()) {
			int sw = 0, sh = 0; (void)SDL_QueryTexture(sprite, nullptr, nullptr, &sw, &sh);
			SDL_Rect dst{
					std::max(0, (canvas_w - sw) / 2), std::max(0,  canvas_h - sh), sw, sh };
			SDL_RenderCopy(last_renderer_, sprite, nullptr, &dst);
		}
		if (base) {
			SDL_SetRenderDrawColor(last_renderer_, 0, 200, 255, 180);
			std::vector<SDL_Point> pts; pts.reserve(base->get_points().size() + 1);
			for (const auto& p : base->get_points()) {
					pts.push_back(SDL_Point{ p.x, p.y });
			}
			if (!pts.empty()) {
					pts.push_back(pts.front());
					SDL_RenderDrawLines(last_renderer_, pts.data(), (int)pts.size());
			}
		}
		SDL_SetRenderTarget(last_renderer_, prev);
		Area edited(name, bg, last_renderer_);
		info_->upsert_area_from_editor(edited);
		SDL_DestroyTexture(bg);
		rebuild_area_widgets();
		save_now();
	} catch (...) {
	}
}
