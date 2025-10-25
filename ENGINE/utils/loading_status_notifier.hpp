#pragma once

#include <functional>
#include <string>

namespace loading_status {

using Notifier = std::function<void(const std::string&)>;

void set_notifier(Notifier notifier);
void clear_notifier();
void notify(const std::string& status);

class ScopedNotifier {
public:
    explicit ScopedNotifier(Notifier notifier);
    ~ScopedNotifier();

    ScopedNotifier(const ScopedNotifier&) = delete;
    ScopedNotifier& operator=(const ScopedNotifier&) = delete;

private:
    Notifier previous_{};
};

}
