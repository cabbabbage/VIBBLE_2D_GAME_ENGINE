#include "SpawnGroupRow.hpp"

#include <utility>
#include <vector>

#include "spawn_method_control_widgets/CandidateListWidget.hpp"
#include "spawn_method_control_widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists {

using spawn_method_control_widgets::CandidateListWidget;
using spawn_method_control_widgets::ISpawnMethodWidget;

SpawnGroupRow::SpawnGroupRow(model::SpawnGroup group) : group_(std::move(group)) {
    refresh_from_group();
    header_.method_dropdown.on_method_selected().connect([this](const model::SpawnMethodId& method) {
        group_.method = method;
        refresh_method_config();
    });
}

SpawnGroupRow::~SpawnGroupRow() = default;

const model::SpawnGroup& SpawnGroupRow::group() const {
    return group_;
}

void SpawnGroupRow::set_group(model::SpawnGroup group) {
    group_ = std::move(group);
    refresh_from_group();
    bind_method_widget();
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
    method_widget_ = std::move(widget);
    candidate_widget_ = dynamic_cast<CandidateListWidget*>(method_widget_.get());
    if (candidate_widget_) {
        candidate_widget_->on_changed().connect([this](const std::vector<model::Candidate>& candidates) {
            group_.candidates = candidates;
            refresh_method_config();
        });
    }
    bind_method_widget();
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
    header_.method_dropdown.set_selected_method(group_.method);
}

void SpawnGroupRow::bind_method_widget() {
    body_.method_widget = method_widget_.get();
    if (!method_widget_) {
        return;
    }
    method_widget_->bind_group(group_);
    if (candidate_widget_) {
        candidate_widget_->sync_from_model();
        group_.candidates = candidate_widget_->candidates();
    }
    refresh_method_config();
}

void SpawnGroupRow::refresh_method_config() {
    if (!method_widget_) {
        return;
    }
    group_.method_config = method_widget_->emit_config();
    if (candidate_widget_) {
        group_.candidates = candidate_widget_->candidates();
    }
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists
