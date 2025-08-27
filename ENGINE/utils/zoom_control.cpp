
#include "zoom_control.hpp"
#include "find_current_room.hpp"
#include "asset\Asset.hpp"
#include "room\room.hpp"
#include "Area.hpp"
#include "view.hpp"

#include <limits>
#include <algorithm>

static constexpr double BASE_RATIO = 1.1;

namespace {
    inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
}

ZoomControl::ZoomControl(view& window, std::vector<Room*>& rooms, Asset*& player)
    : window_(window),
      rooms_(rooms),
      player_(player),
      finder_(new CurrentRoomFinder(rooms_, player_)),
      current_room_(nullptr),
      starting_room_(nullptr),
      starting_area_(1.0)
{
}

void ZoomControl::set_up_rooms() {
    if (rooms_.empty()) return;
    current_room_ = finder_->getCurrentRoom();
    if (!current_room_) return;

    starting_room_ = current_room_;
    if (starting_room_ && starting_room_->room_area) {
        starting_area_ = starting_room_->room_area->get_size();
        if (starting_area_ <= 0.0) starting_area_ = 1.0;
    }
}

double ZoomControl::compute_room_scale_from_area(const Room* room) const {
    if (!room || !room->room_area || starting_area_ <= 0.0) return BASE_RATIO;
    double a = room->room_area->get_size();
    if (a <= 0.0 || room->type == "trail") return BASE_RATIO * 0.8;

    double s = (a / starting_area_) * BASE_RATIO;
    s = std::clamp(s, BASE_RATIO * 0.9, BASE_RATIO * 1.05);
    return s;
}

void ZoomControl::update(Room* cur) {
    if (!player_ || rooms_.empty() || !starting_room_) return;

    window_.update();

    
    if (!cur) return;

    Room* neigh = finder_->getNeighboringRoom(cur);
    if (!neigh) neigh = cur;

    current_room_ = cur;

    const double sa = compute_room_scale_from_area(cur);
    const double sb = compute_room_scale_from_area(neigh);

    auto [ax, ay] = cur->room_area->get_center();
    auto [bx, by] = neigh->room_area->get_center();

    const double pax = double(player_->pos_X);
    const double pay = double(player_->pos_Y);

    const double vx = double(bx - ax);
    const double vy = double(by - ay);
    const double wx = double(pax - ax);
    const double wy = double(pay - ay);

    const double vlen2 = vx * vx + vy * vy;
    double t = (vlen2 > 0.0) ? ((wx * vx + wy * vy) / vlen2) : 0.0;
    t = std::clamp(t, 0.0, 1.0);

    double target_zoom = lerp(sa, sb, t);
    target_zoom = std::clamp(target_zoom, BASE_RATIO * 0.7, BASE_RATIO * 1.3);

    window_.zoom_scale(target_zoom, 35);
}
