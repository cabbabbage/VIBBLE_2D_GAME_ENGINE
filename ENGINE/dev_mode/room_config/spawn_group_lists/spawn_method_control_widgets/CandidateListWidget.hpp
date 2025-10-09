#pragma once

#include <vector>

#include "dev_mode/room_config/spawn_group_model.hpp"
#include "ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets {

class CandidateListWidget : public ISpawnMethodWidget {
public:
    CandidateListWidget();
    ~CandidateListWidget() override;

    void bind_group(const model::SpawnGroup& group) override;
    model::MethodConfig emit_config() const override;

    void set_candidates(std::vector<model::Candidate> candidates);
    const std::vector<model::Candidate>& candidates() const;

private:
    std::vector<model::Candidate> candidates_;
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
