#include "scene_renderer.hpp"
#include "core/AssetsManager.hpp"
#include "asset/Asset.hpp"
#include "light_map.hpp"
#include "render/camera.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <tuple>
#include <vector>

static constexpr SDL_Color SLATE_COLOR = {69, 101, 74, 255};
static constexpr float MIN_VISIBLE_SCREEN_RATIO = 0.015f;

// Motion blur disabled

SceneRenderer::SceneRenderer(SDL_Renderer* renderer,
                             Assets* assets,
                             int screen_width,
                             int screen_height,
                             const std::string& map_path)
: map_path_(map_path),
  renderer_(renderer),
  assets_(assets),
  screen_width_(screen_width),
  screen_height_(screen_height),
  main_light_source_(renderer, SDL_Point{ screen_width / 2, screen_height / 2 },
                     screen_width, SDL_Color{255, 255, 255, 255}, map_path),
  fullscreen_light_tex_(nullptr),
  render_asset_(renderer, assets->getView(), main_light_source_, assets->player)
{
	fullscreen_light_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, screen_width_, screen_height_);
	if (fullscreen_light_tex_) {
		SDL_SetTextureBlendMode(fullscreen_light_tex_, SDL_BLENDMODE_BLEND);
		SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
		SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
		SDL_Color color = main_light_source_.get_current_color();
		SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
		SDL_RenderClear(renderer_);
		SDL_SetRenderTarget(renderer_, prev);
	} else {
		std::cerr << "[SceneRenderer] Failed to create fullscreen light texture: "
		          << SDL_GetError() << "\n";
	}

	// No accumulation texture; render directly to default target

        z_light_pass_ = std::make_unique<LightMap>(renderer_, assets_, main_light_source_, screen_width_, screen_height_, fullscreen_light_tex_);
        main_light_source_.update();
        z_light_pass_->render(debugging);
}

void SceneRenderer::apply_map_light_config(const nlohmann::json& data) {
        main_light_source_.apply_config(data);
        if (!renderer_ || !fullscreen_light_tex_) {
                return;
        }
        SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
        SDL_SetRenderTarget(renderer_, fullscreen_light_tex_);
        SDL_Color color = main_light_source_.get_current_color();
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderClear(renderer_);
        SDL_SetRenderTarget(renderer_, prev);
}

void SceneRenderer::update_shading_groups() {
        ++current_shading_group_;
        if (current_shading_group_ > num_groups_)
                current_shading_group_ = 1;
}

bool SceneRenderer::shouldRegen(Asset* a) {
	if (!a->get_final_texture()) { return true; }
	return (a->get_shading_group() > 0 &&
	        a->get_shading_group() == current_shading_group_) ||
	       (!a->get_final_texture() || !a->static_frame || a->get_render_player_light());
}

SDL_Rect SceneRenderer::get_scaled_position_rect(Asset* a, int fw, int fh,
                                                 float inv_scale, int min_w, int min_h) {
        float base_sw = static_cast<float>(fw) * inv_scale;
        float base_sh = static_cast<float>(fh) * inv_scale;

        if (base_sw < min_w && base_sh < min_h) {
                return {0, 0, 0, 0};
        }

        SDL_Point cp = assets_->getView().map_to_screen(SDL_Point{a->pos.x, a->pos.y});

        float screen_y_ratio = static_cast<float>(cp.y) / static_cast<float>(screen_height_);
        screen_y_ratio = std::clamp(screen_y_ratio, 0.0f, 1.0f);

        float normalized_height = base_sh / static_cast<float>(screen_height_);
        normalized_height = std::clamp(normalized_height, 0.0f, 1.0f);

        constexpr float POSITION_SQUASH_STRENGTH = 0.45f;
        constexpr float HEIGHT_SQUASH_STRENGTH = 0.35f;
        float squash_amount = screen_y_ratio * POSITION_SQUASH_STRENGTH +
                              normalized_height * HEIGHT_SQUASH_STRENGTH;
        squash_amount = std::clamp(squash_amount, 0.0f, 0.6f);
        float vertical_squash = 1.0f - squash_amount;

        int sw = static_cast<int>(std::round(base_sw));
        int sh = static_cast<int>(std::round(base_sh * vertical_squash));
        sw = std::max(sw, 1);
        sh = std::max(sh, 1);

        if (sw < min_w && sh < min_h) {
                return {0, 0, 0, 0};
        }

        return SDL_Rect{ cp.x - sw / 2, cp.y - sh, sw, sh };
}

void SceneRenderer::render() {
	static int render_call_count = 0;
	++render_call_count;

	update_shading_groups();

	// Camera already centers on player via update_zoom

	main_light_source_.update();

	SDL_SetRenderTarget(renderer_, nullptr);
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer_, SLATE_COLOR.r, SLATE_COLOR.g, SLATE_COLOR.b, 255);
	SDL_RenderClear(renderer_);

	const auto& camera_state = assets_->getView();
	float scale = camera_state.get_scale();
	float inv_scale = 1.0f / scale;
	int min_visible_w = static_cast<int>(screen_width_  * MIN_VISIBLE_SCREEN_RATIO);
	int min_visible_h = static_cast<int>(screen_height_ * MIN_VISIBLE_SCREEN_RATIO);

        const auto& active_assets = assets_->getActive();
        for (Asset* a : active_assets) {
		if (!a || !a->info) continue;

		if (shouldRegen(a)) {
			SDL_Texture* tex = render_asset_.regenerateFinalTexture(a);
			a->set_final_texture(tex);
		}

		SDL_Texture* final_tex = a->get_final_texture();
		if (!final_tex) continue;

		int fw = a->cached_w;
		int fh = a->cached_h;
		if (fw == 0 || fh == 0) {
			SDL_QueryTexture(final_tex, nullptr, nullptr, &fw, &fh);
			a->cached_w = fw;
			a->cached_h = fh;
		}

		SDL_Rect fb = get_scaled_position_rect(a, fw, fh, inv_scale, min_visible_w, min_visible_h);
		if (fb.w == 0 && fb.h == 0) continue;

		if (a->is_highlighted()) {
			SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
			SDL_SetRenderDrawColor(renderer_, 200, 5, 5, 100);
			SDL_Rect outline = fb;
			outline.x -= 2; outline.y -= 2;
			outline.w += 4; outline.h += 4;
			SDL_RenderFillRect(renderer_, &outline);
			SDL_SetTextureColorMod(final_tex, 255, 200, 200);
		}
		else if (a->is_selected()) {
			SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
			SDL_SetRenderDrawColor(renderer_, 5, 5, 200, 100);
			SDL_Rect outline = fb;
			outline.x -= 2; outline.y -= 2;
			outline.w += 4; outline.h += 4;
			SDL_RenderFillRect(renderer_, &outline);
			SDL_SetTextureColorMod(final_tex, 255, 200, 200);
		}
		else {
			SDL_SetTextureColorMod(final_tex, 255, 255, 255);
		}

		SDL_RenderCopyEx(renderer_, final_tex, nullptr, &fb, 0, nullptr, a->flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
		SDL_SetTextureColorMod(final_tex, 255, 255, 255);
	}

	SDL_SetRenderTarget(renderer_, nullptr);
        z_light_pass_->render(debugging);
        if (assets_) {
                assets_->render_overlays(renderer_);
        }
        SDL_RenderPresent(renderer_);
}
