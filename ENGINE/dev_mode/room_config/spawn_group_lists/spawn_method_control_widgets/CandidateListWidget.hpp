#pragma once

#include <vector>

#include "dev_mode/room_config/spawn_group_model.hpp"
#include "dev_mode/room_config/spawn_group_lists/Signal.hpp"
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
    void sync_from_model();

    Signal<void(const std::vector<model::Candidate>&)>& on_changed();

private:
    void assign_from_group();

    const model::SpawnGroup* bound_group_ = nullptr;
    std::vector<model::Candidate> candidates_;
    Signal<void(const std::vector<model::Candidate>&)> on_changed_{};
};

}  // namespace vibble::dev_mode::room_config::spawn_group_lists::spawn_method_control_widgets
