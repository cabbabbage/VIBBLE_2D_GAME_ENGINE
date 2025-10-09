#include "CandidateListWidget.hpp"

#include <utility>

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

CandidateListWidget::CandidateListWidget() = default;
CandidateListWidget::~CandidateListWidget() = default;

void CandidateListWidget::bind_group(const model::SpawnGroup& group) {
    bound_group_ = &group;
    assign_from_group();
}

model::MethodConfig CandidateListWidget::emit_config() const {
    return model::MethodConfig::make_weighted_list(candidates_);
}

void CandidateListWidget::set_candidates(std::vector<model::Candidate> candidates) {
    candidates_ = std::move(candidates);
    on_changed_.emit(candidates_);
}

const std::vector<model::Candidate>& CandidateListWidget::candidates() const {
    return candidates_;
}

void CandidateListWidget::sync_from_model() {
    assign_from_group();
}

Signal<void(const std::vector<model::Candidate>&)>& CandidateListWidget::on_changed() {
    return on_changed_;
}

void CandidateListWidget::assign_from_group() {
    if (!bound_group_) {
        candidates_.clear();
        return;
    }
    if (const auto* weighted = bound_group_->method_config.as_weighted_list()) {
        candidates_ = weighted->candidates;
    } else {
        candidates_ = bound_group_->candidates;
    }
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
