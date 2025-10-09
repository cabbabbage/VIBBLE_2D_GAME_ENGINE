#include "SpawnGroupRow.hpp"

#include <utility>
#include <vector>

#include "widgets/ExactWidget.hpp"
#include "widgets/ISpawnMethodWidget.hpp"
#include "widgets/PerimeterWidget.hpp"
#include "widgets/RandomWidget.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {
namespace {

using widgets::ExactWidget;
using widgets::ISpawnMethodWidget;
using widgets::PerimeterWidget;
using widgets::RandomWidget;

std::unique_ptr<ISpawnMethodWidget> create_widget_for_method(const model::SpawnMethodId& method) {
    if (method == "Random") {
        return std::make_unique<RandomWidget>();
    }
    if (method == "Perimeter") {
        return std::make_unique<PerimeterWidget>();
    }
    if (method == "Exact") {
        return std::make_unique<ExactWidget>();
    }
    return nullptr;
}

}  // namespace

SpawnGroupRow::SpawnGroupRow(model::SpawnGroup group) : group_(std::move(group)) {
    refresh_from_group();
    set_method_widget(create_widget_for_method(group_.method));
    header_.method_dropdown.on_method_selected().connect([this](const model::SpawnMethodId& method) {
        if (suppress_method_change_) {
            return;
        }
        model::switch_method(group_, method);
        set_method_widget(create_widget_for_method(group_.method));
    });
}

SpawnGroupRow::~SpawnGroupRow() = default;

const model::SpawnGroup& SpawnGroupRow::group() const {
    return group_;
}

void SpawnGroupRow::set_group(model::SpawnGroup group) {
    group_ = std::move(group);
    refresh_from_group();
    set_method_widget(create_widget_for_method(group_.method));
}

SpawnGroupRow::Header& SpawnGroupRow::header() {
    return header_;
}

const SpawnGroupRow::Header& SpawnGroupRow::header() const {
    return header_;
}

SpawnGroupRow::Body& SpawnGroupRow::body() {
    return body_;
}

const SpawnGroupRow::Body& SpawnGroupRow::body() const {
    return body_;
}

void SpawnGroupRow::set_display_name(std::string name) {
    group_.display_name = std::move(name);
    header_.name = group_.display_name;
}

const std::string& SpawnGroupRow::display_name() const {
    return header_.name;
}

void SpawnGroupRow::set_area_id(std::string area_id) {
    area_id_ = std::move(area_id);
    header_.link_to_area.set_target_area(area_id_);
}

const std::string& SpawnGroupRow::area_id() const {
    return area_id_;
}

void SpawnGroupRow::set_available_methods(std::vector<model::SpawnMethodId> methods) {
    header_.method_dropdown.set_available_methods(std::move(methods));
}

bool SpawnGroupRow::is_open() const {
    return open_;
}

void SpawnGroupRow::set_open(bool open) {
    if (open_ == open) {
        return;
    }
    open_ = open;
    if (open_) {
        on_open_.emit();
    }
}

Signal<void()>& SpawnGroupRow::on_open() {
    return on_open_;
}

Signal<void()>& SpawnGroupRow::on_move_up() {
    return on_move_up_;
}

Signal<void()>& SpawnGroupRow::on_move_down() {
    return on_move_down_;
}

Signal<void()>& SpawnGroupRow::on_delete() {
    return on_delete_;
}

void SpawnGroupRow::trigger_open() {
    on_open_.emit();
}

void SpawnGroupRow::trigger_move_up() {
    on_move_up_.emit();
}

void SpawnGroupRow::trigger_move_down() {
    on_move_down_.emit();
}

void SpawnGroupRow::trigger_delete() {
    on_delete_.emit();
}

void SpawnGroupRow::set_method_widget(std::unique_ptr<ISpawnMethodWidget> widget) {
    if (method_widget_) {
        method_widget_->clear_method_state();
    }
    method_widget_ = std::move(widget);
    body_.method_widget = method_widget_.get();
    if (method_widget_) {
        method_widget_->bind(group_);
        method_widget_->sync_from_model();
    }
}

ISpawnMethodWidget* SpawnGroupRow::method_widget() {
    return method_widget_.get();
}

const ISpawnMethodWidget* SpawnGroupRow::method_widget() const {
    return method_widget_.get();
}

void SpawnGroupRow::refresh_from_group() {
    header_.name = group_.display_name;
    set_area_id(group_.id);
    suppress_method_change_ = true;
    header_.method_dropdown.set_selected_method(group_.method);
    suppress_method_change_ = false;
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
