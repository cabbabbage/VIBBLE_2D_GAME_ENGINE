#pragma once

#include "dev_mode/spawn_group_config/widgets/ISpawnMethodWidget.hpp"

namespace vibble::dev_mode::spawn_group_config::widgets {

namespace model = vibble::dev_mode::room_config::model;

class EdgeWidget : public ISpawnMethodWidget {
public:
    EdgeWidget() = default;

    void bind(model::SpawnGroup& group) override;
    void sync_from_model() override;
    void clear_method_state() override;

    int min_number() const;
    int max_number() const;
    int inset_percent() const;

    void set_min_number(int value);
    void set_max_number(int value);
    void set_inset_percent(int value);

private:
    model::MethodConfig::Edge& ensure_config();
    const model::MethodConfig::Edge* read_config() const;

    model::SpawnGroup* group_ = nullptr;
};

}

