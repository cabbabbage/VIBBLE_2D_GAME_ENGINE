#include "CandidateListWidget.hpp"

#include <utility>

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

CandidateListWidget::CandidateListWidget() = default;
CandidateListWidget::~CandidateListWidget() = default;

void CandidateListWidget::bind_group(const model::SpawnGroup& group) {
    if (const auto* weighted = group.method_config.as_weighted_list()) {
        candidates_ = weighted->candidates;
    } else {
        candidates_.clear();
    }
}

model::MethodConfig CandidateListWidget::emit_config() const {
    return model::MethodConfig::make_weighted_list(candidates_);
}

void CandidateListWidget::set_candidates(std::vector<model::Candidate> candidates) {
    candidates_ = std::move(candidates);
}

const std::vector<model::Candidate>& CandidateListWidget::candidates() const {
    return candidates_;
}

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
