#include "AnimationHistory.hpp"

namespace animation_editor {

AnimationHistory::AnimationHistory() = default;

void AnimationHistory::set_snapshot_producer(SnapshotProducer producer) { producer_ = std::move(producer); }

void AnimationHistory::set_snapshot_consumer(SnapshotConsumer consumer) { consumer_ = std::move(consumer); }

void AnimationHistory::clear() {
    history_.clear();
    cursor_ = 0;
}

void AnimationHistory::push_state() {
    if (!producer_) return;
    std::string snapshot = producer_();

    if (history_.empty()) {
        history_.push_back(std::move(snapshot));
        cursor_ = 0;
        return;
    }

    if (cursor_ + 1 < history_.size()) {
        using difference_type = std::vector<std::string>::difference_type;
        history_.erase(history_.begin() + static_cast<difference_type>(cursor_ + 1), history_.end());
    }

    if (!history_.empty() && history_.back() == snapshot) {
        cursor_ = history_.size() - 1;
        return;
    }

    history_.push_back(std::move(snapshot));
    cursor_ = history_.size() - 1;
}

bool AnimationHistory::can_undo() const {
    return !history_.empty() && cursor_ > 0 && cursor_ < history_.size();
}

bool AnimationHistory::can_redo() const {
    return !history_.empty() && cursor_ + 1 < history_.size();
}

void AnimationHistory::undo() {
    if (!can_undo() || !consumer_) return;
    --cursor_;
    consumer_(history_[cursor_]);
}

void AnimationHistory::redo() {
    if (!can_redo() || !consumer_) return;
    ++cursor_;
    consumer_(history_[cursor_]);
}

}  // namespace animation_editor

