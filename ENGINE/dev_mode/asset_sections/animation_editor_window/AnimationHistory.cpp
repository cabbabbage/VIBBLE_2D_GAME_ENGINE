#include "AnimationHistory.hpp"

namespace animation_editor {

AnimationHistory::AnimationHistory() = default;

void AnimationHistory::set_snapshot_producer(SnapshotProducer producer) {
    (void)producer;
    // TODO: Store callback that captures current document state as JSON.
}

void AnimationHistory::set_snapshot_consumer(SnapshotConsumer consumer) {
    (void)consumer;
    // TODO: Store callback that restores document state from JSON snapshot.
}

void AnimationHistory::clear() {
    // TODO: Reset undo/redo stacks and cursor.
}

void AnimationHistory::push_state() {
    // TODO: Capture new snapshot and append to history, trimming redo branch.
}

bool AnimationHistory::can_undo() const {
    // TODO: Return whether an undo operation is available.
    return false;
}

bool AnimationHistory::can_redo() const {
    // TODO: Return whether a redo operation is available.
    return false;
}

void AnimationHistory::undo() {
    // TODO: Move cursor backwards and replay snapshot consumer.
}

void AnimationHistory::redo() {
    // TODO: Move cursor forwards and replay snapshot consumer.
}

}  // namespace animation_editor

