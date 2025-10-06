#include "AsyncTaskQueue.hpp"

namespace animation_editor {

AsyncTaskQueue::AsyncTaskQueue() = default;

void AsyncTaskQueue::enqueue(std::function<void()> task) {
    (void)task;
    // TODO: Dispatch task onto background worker thread.
}

void AsyncTaskQueue::update() {
    // TODO: Pump completed task callbacks onto the main thread.
}

bool AsyncTaskQueue::is_busy() const {
    // TODO: Indicate whether any queued tasks are still running.
    return false;
}

}  // namespace animation_editor

