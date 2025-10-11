#pragma once

#include <functional>
#include <vector>

namespace vibble::dev_mode::spawn_group_config {

template <typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;

    Signal() = default;

    int connect(Slot slot) {
        slots_.push_back(std::move(slot));
        return static_cast<int>(slots_.size()) - 1;
    }

    void emit(Args... args) {
        for (auto& slot : slots_) {
            if (slot) {
                slot(args...);
            }
        }
    }

    void clear() { slots_.clear(); }

    bool empty() const { return slots_.empty(); }

private:
    std::vector<Slot> slots_;
};

}

