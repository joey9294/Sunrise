#include "console_queue.h"

#include <Windows.h>

#include <array>

#include "../invoke/console_invoke.h"

namespace sunrise::core::console::queue {
namespace {

/** One waiting invocation and the ticket its submitter holds. */
struct Pending {
    parser::Invocation invocation{};
    std::uint64_t ticket{};
};

std::array<Pending, kQueueCapacity> g_pending{};
/** Index of the oldest waiting entry. */
std::size_t g_head{};
std::size_t g_count{};
/** Last issued ticket. It only ever rises, so no live ticket is ever reused. */
std::uint64_t g_lastTicket{};
SRWLOCK g_queueLock{SRWLOCK_INIT};

/**
 * Removes the oldest waiting entry.
 * @param output Filled only when one was waiting.
 * @return True when one was removed.
 */
[[nodiscard]] bool take_oldest(Pending& output) noexcept {
    AcquireSRWLockExclusive(&g_queueLock);
    const bool taken = g_count != 0;
    if (taken) {
        output = g_pending[g_head];
        g_pending[g_head] = Pending{};
        g_head = (g_head + 1) % kQueueCapacity;
        --g_count;
    }
    ReleaseSRWLockExclusive(&g_queueLock);
    return taken;
}

} // namespace

/** Takes a checked invocation to run on the draining thread. */
std::uint64_t submit(const parser::Invocation& invocation) noexcept {
    AcquireSRWLockExclusive(&g_queueLock);
    if (g_count >= kQueueCapacity) {
        ReleaseSRWLockExclusive(&g_queueLock);
        return kNoTicket;
    }
    ++g_lastTicket;
    const std::uint64_t ticket = g_lastTicket;
    g_pending[(g_head + g_count) % kQueueCapacity] = Pending{invocation, ticket};
    ++g_count;
    ReleaseSRWLockExclusive(&g_queueLock);
    return ticket;
}

/** Runs every waiting invocation on the calling thread, oldest first. */
std::size_t drain(CompletionCallback completion) noexcept {
    std::size_t ran = 0;
    // The count is read afresh each turn rather than latched, so a handler that submits more work
    // does not have to wait a frame for it. The queue is bounded, so this still terminates: a
    // handler can only add what the depth allows.
    Pending next{};
    while (ran < kQueueCapacity && take_oldest(next)) {
        Result result{};
        invoke::run(next.invocation, result);
        if (completion != nullptr) {
            completion(next.ticket, result);
        }
        ++ran;
    }
    return ran;
}

/** @return Count waiting to run. */
std::size_t pending() noexcept {
    AcquireSRWLockShared(&g_queueLock);
    const std::size_t count = g_count;
    ReleaseSRWLockShared(&g_queueLock);
    return count;
}

/** Drops every waiting invocation without running any. */
void clear() noexcept {
    AcquireSRWLockExclusive(&g_queueLock);
    g_pending = {};
    g_head = 0;
    g_count = 0;
    ReleaseSRWLockExclusive(&g_queueLock);
}

/** Drops every waiting invocation and resets the ticket counter. */
void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_queueLock);
    g_pending = {};
    g_head = 0;
    g_count = 0;
    g_lastTicket = 0;
    ReleaseSRWLockExclusive(&g_queueLock);
}

} // namespace sunrise::core::console::queue
