#include "loading_status_notifier.hpp"

#include <mutex>
#include <iostream>

namespace {
std::mutex& notifier_mutex() {
    static std::mutex m;
    return m;
}

loading_status::Notifier& notifier_slot() {
    static loading_status::Notifier notifier;
    return notifier;
}
}  // namespace

namespace loading_status {

void set_notifier(Notifier notifier) {
    std::lock_guard<std::mutex> lock(notifier_mutex());
    notifier_slot() = std::move(notifier);
}

void clear_notifier() {
    set_notifier(nullptr);
}

void notify(const std::string& status) {
    Notifier copy;
    {
        std::lock_guard<std::mutex> lock(notifier_mutex());
        copy = notifier_slot();
    }
    if (!status.empty()) {
        std::cout << "[Loading] " << status << "\n";
    }
    if (copy) {
        copy(status);
    }
}

ScopedNotifier::ScopedNotifier(Notifier notifier) {
    std::lock_guard<std::mutex> lock(notifier_mutex());
    previous_ = notifier_slot();
    notifier_slot() = std::move(notifier);
}

ScopedNotifier::~ScopedNotifier() {
    std::lock_guard<std::mutex> lock(notifier_mutex());
    notifier_slot() = std::move(previous_);
}

}  // namespace loading_status
