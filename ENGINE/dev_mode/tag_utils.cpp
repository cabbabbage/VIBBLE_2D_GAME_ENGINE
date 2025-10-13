#include "tag_utils.hpp"

#include <atomic>

namespace {
std::atomic<std::uint64_t> g_tag_version{0};
}

namespace tag_utils {

std::uint64_t tag_version() {
    return g_tag_version.load(std::memory_order_acquire);
}

void notify_tags_changed() {
    g_tag_version.fetch_add(1, std::memory_order_acq_rel);
}

}
