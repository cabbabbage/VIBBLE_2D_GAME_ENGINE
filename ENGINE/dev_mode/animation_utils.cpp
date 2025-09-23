#include "animation_utils.hpp"

#include <algorithm>
#include <climits>
#include <cctype>
#include "dm_styles.hpp"

namespace fs = std::filesystem;

namespace animation {

bool is_numbered_png(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if(lower.size() < 5 || lower.substr(lower.size() - 4) != ".png")
        return false;
    return std::all_of(lower.begin(), lower.end() - 4, ::isdigit);
}

std::vector<fs::path> get_image_paths(const fs::path& folder) {
    std::vector<fs::path> out;
    if(!fs::exists(folder) || !fs::is_directory(folder))
        return out;
    for(const auto& entry : fs::directory_iterator(folder)) {
        if(!entry.is_regular_file())
            continue;
        auto fname = entry.path().filename().string();
        if(is_numbered_png(fname))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        int ia = 0;
        int ib = 0;
        try { ia = std::stoi(a.stem().string()); } catch(...) {}
        try { ib = std::stoi(b.stem().string()); } catch(...) {}
        return ia < ib;
    });
    return out;
}

Bounds compute_union_bounds(const std::vector<fs::path>& image_paths, int alpha_threshold) {
    Bounds res;
    int unionL = INT_MAX, unionT = INT_MAX, unionR = -1, unionB = -1;
    for(const auto& path : image_paths) {
        SDL_Surface* loaded = IMG_Load(path.string().c_str());
        if(!loaded)
            continue;
        SDL_Surface* img = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if(!img)
            continue;
        if(res.base_w == 0) {
            res.base_w = img->w;
            res.base_h = img->h;
        }
        int l = img->w, t = img->h, r = -1, b = -1;
        auto* pixels = static_cast<uint8_t*>(img->pixels);
        for(int y=0; y<img->h; ++y) {
            uint8_t* row = pixels + y * img->pitch;
            for(int x=0; x<img->w; ++x) {
                uint8_t a = row[x*4 + 3];
                if(a > alpha_threshold) {
                    if(x < l) l = x;
                    if(y < t) t = y;
                    if(x > r) r = x;
                    if(y > b) b = y;
                }
            }
        }
        SDL_FreeSurface(img);
        if(r >= l && b >= t) {
            if(l < unionL) unionL = l;
            if(t < unionT) unionT = t;
            if(r > unionR) unionR = r;
            if(b > unionB) unionB = b;
        }
    }
    if(unionR < unionL || unionB < unionT || res.base_w == 0)
        return res;
    res.top = unionT;
    res.left = unionL;
    res.right = res.base_w - unionR - 1;
    res.bottom = res.base_h - unionB - 1;
    return res;
}

int crop_images_with_bounds(const std::vector<fs::path>& image_paths,
                            int crop_top,
                            int crop_bottom,
                            int crop_left,
                            int crop_right) {
    int count = 0;
    for(const auto& path : image_paths) {
        SDL_Surface* loaded = IMG_Load(path.string().c_str());
        if(!loaded)
            continue;
        SDL_Surface* img = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if(!img)
            continue;
        int w = img->w;
        int h = img->h;
        int L = crop_left;
        int T = crop_top;
        int R = w - crop_right;
        int B = h - crop_bottom;
        if(L >= R || T >= B) {
            SDL_FreeSurface(img);
            continue;
        }
        int newW = R - L;
        int newH = B - T;
        SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(0, newW, newH, 32, SDL_PIXELFORMAT_RGBA32);
        if(out) {
            SDL_Rect srcRect{L, T, newW, newH};
            SDL_BlitSurface(img, &srcRect, out, nullptr);
            IMG_SavePNG(out, path.string().c_str());
            SDL_FreeSurface(out);
            ++count;
        }
        SDL_FreeSurface(img);
    }
    return count;
}

// HistoryManager -------------------------------------------------
HistoryManager::HistoryManager(size_t limit) : limit_(limit) {}

void HistoryManager::snapshot(const nlohmann::json& data) {
    stack_.push_back(data);
    if(stack_.size() > limit_) {
        stack_.erase(stack_.begin(), stack_.begin() + (stack_.size() - limit_));
    }
}

bool HistoryManager::can_undo() const { return !stack_.empty(); }

std::optional<nlohmann::json> HistoryManager::undo() {
    if(stack_.empty())
        return std::nullopt;
    nlohmann::json last = stack_.back();
    stack_.pop_back();
    return last;
}

// ViewStateManager -----------------------------------------------
ViewState ViewStateManager::capture(const IViewWindow& win, const IViewCanvas& canvas) const {
    ViewState st;
    st.geometry = win.geometry();
    st.zoom = canvas.zoom();
    st.xview = canvas.xview();
    st.yview = canvas.yview();
    return st;
}

void ViewStateManager::apply(IViewWindow& win, IViewCanvas& canvas, const ViewState& state) const {
    win.set_geometry(state.geometry);
    canvas.set_zoom(state.zoom);
    canvas.set_xview(state.xview);
    canvas.set_yview(state.yview);
}

// MovementModal --------------------------------------------------
MovementModal::MovementModal() : history_(200) {}

void MovementModal::open(const std::vector<Position>& positions) {
    positions_ = positions;
    if(positions_.empty())
        positions_.push_back({0,0});
    current_frame_ = 0;
    history_.snapshot(nlohmann::json(positions_));
    open_ = true;
}

bool MovementModal::is_open() const { return open_; }

bool MovementModal::handle_event(const SDL_Event& e) {
    if(!open_)
        return false;
    if(e.type == SDL_KEYDOWN) {
        if(e.key.keysym.sym == SDLK_ESCAPE) {
            open_ = false;
            return true;
        }
        if(e.key.keysym.sym == SDLK_z && (e.key.keysym.mod & KMOD_CTRL)) {
            auto prev = history_.undo();
            if(prev) {
                positions_.clear();
                for(auto& p : *prev) {
                    try {
                        if(p.is_array() && p.size() >= 2)
                            positions_.emplace_back(p[0].get<int>(), p[1].get<int>());
                    } catch(...) {}
                }
                if(positions_.empty()) positions_.push_back({0,0});
                current_frame_ = std::min<int>(current_frame_, positions_.size()-1);
            }
            return true;
        }
        if(e.key.keysym.sym == SDLK_LEFT) {
            if(current_frame_ > 0) current_frame_--;
            return true;
        }
        if(e.key.keysym.sym == SDLK_RIGHT) {
            if(current_frame_ + 1 < (int)positions_.size()) current_frame_++;
            return true;
        }
    }
    if(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        int x = e.button.x;
        int y = e.button.y;
        if(current_frame_ >= (int)positions_.size())
            positions_.resize(current_frame_ + 1, {0,0});
        positions_[current_frame_] = {x,y};
        history_.snapshot(nlohmann::json(positions_));
        return true;
    }
    return false;
}

void MovementModal::render(SDL_Renderer* r) {
    if(!open_)
        return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Rect bg{0,0,w,h};
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_RenderFillRect(r, &bg);

    Position pos{0,0};
    if(current_frame_ < (int)positions_.size())
        pos = positions_[current_frame_];
    const SDL_Color accent = DMStyles::AccentButton().hover_bg;
    SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 255);
    SDL_RenderDrawLine(r, pos.first - 5, pos.second, pos.first + 5, pos.second);
    SDL_RenderDrawLine(r, pos.first, pos.second - 5, pos.first, pos.second + 5);
}

} // namespace animation

