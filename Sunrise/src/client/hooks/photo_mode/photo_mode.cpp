/** Runtime-only Photo Mode coordinator for movement, collision, input and presentation. */

#include "photo_mode.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../input/window_focus.h"
#include "../../movement/movement_input.h"
#include "../../movement/movement_settings_store.h"
#include "../noclip/runtime.h"
#include "../polled_input/runtime.h"
#include "../presentation/presentation.h"
#include "../teleport/internal.h"
#include "../teleport/runtime.h"

namespace sunrise::client::hooks::photo_mode {
namespace {

/** High bit Windows sets while a virtual key is physically held. */
constexpr SHORT kKeyHeldBit = static_cast<SHORT>(0x8000);

/** Camera-thread state owned only while one Photo Mode request is in-world. */
struct Session {
    std::uint32_t playerIndex{teleport::kInvalidHandle};
    bool entered{};
};

std::atomic<Phase> g_phase{Phase::unavailable};
std::atomic_bool g_requested{};
std::atomic_bool g_installed{};
std::atomic_bool g_stopping{};
SRWLOCK g_sessionLock{SRWLOCK_INIT};
Session g_session{};
bool g_toggleDown{};

/** Writes one bounded structured Photo Mode event. */
void report(const char* stage, const char* result, const char* reason = nullptr) noexcept {
    std::array<char, 176> line{};
    const int written =
        reason == nullptr
            ? std::snprintf(
                  line.data(), line.size(), "ev=photo_mode stage=%s result=%s", stage, result)
            : std::snprintf(line.data(),
                            line.size(),
                            "ev=photo_mode stage=%s result=%s reason=%s",
                            stage,
                            result,
                            reason);
    if (written > 0) {
        const auto length = static_cast<std::size_t>(written) < line.size()
                                ? static_cast<std::size_t>(written)
                                : line.size() - 1;
        core::log::write(core::log::Channel::client,
                         result == std::string_view{"ok"} ? core::log::Level::info
                                                          : core::log::Level::warn,
                         {line.data(), length});
    }
}

/** @return Current mode state with readiness derived from each owning module. */
[[nodiscard]] Status snapshot() noexcept {
    const presentation::Status presentationStatus = presentation::status();
    return Status{g_phase.load(std::memory_order_acquire),
                  g_requested.load(std::memory_order_acquire),
                  presentationStatus.weaponReady,
                  teleport::is_installed(),
                  noclip::is_installed(),
                  polled_input::is_installed(),
                  presentationStatus.hudReady,
                  presentationStatus.weaponHidden,
                  presentationStatus.hudHidden,
                  presentationStatus.hudFault};
}

/** @return True when all modules required to enter currently own their targets. */
[[nodiscard]] bool ready(const Status& value) noexcept {
    return g_installed.load(std::memory_order_acquire) && value.presentationReady
           && value.movementReady && value.collisionBypassReady && value.inputSuppressionReady;
}

/** Releases only state Photo Mode acquired after entering an in-world session. */
void leave_session() noexcept {
    if (!g_session.entered) {
        return;
    }
    polled_input::clear_blocked_keys();
    presentation::set_photo_mode_active(false);
    g_session = {};
    report("exit", "ok");
}

/** Polls the persisted edge-triggered key on the camera thread. */
void poll_toggle() noexcept {
    const movement::Settings settings = movement::get();
    if (settings.photoModeToggleKey == movement::kNoKey) {
        g_toggleDown = false;
        return;
    }
    const bool down =
        input::game_focused()
        && (GetAsyncKeyState(static_cast<int>(settings.photoModeToggleKey)) & kKeyHeldBit) != 0;
    if (core::ui::runtime::snapshot().visible) {
        g_toggleDown = down;
        return;
    }
    if (down && !g_toggleDown) {
        (void)request_active(!g_requested.load(std::memory_order_acquire));
    }
    g_toggleDown = down;
}

} // namespace

/** Initializes the composite state before publishing it to the camera hook. */
bool install() noexcept {
    if (g_installed.load(std::memory_order_acquire)) {
        return true;
    }
    g_stopping.store(false, std::memory_order_release);
    g_requested.store(false, std::memory_order_release);
    g_phase.store(Phase::inactive, std::memory_order_release);
    g_session = {};
    g_toggleDown = false;
    g_installed.store(true, std::memory_order_release);
    report("install", "ok");
    return true;
}

/** Clears the request and releases input and presentation ownership. */
bool uninstall() noexcept {
    g_stopping.store(true, std::memory_order_release);
    g_requested.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&g_sessionLock);
    leave_session();
    g_toggleDown = false;
    ReleaseSRWLockExclusive(&g_sessionLock);
    g_installed.store(false, std::memory_order_release);
    g_phase.store(Phase::unavailable, std::memory_order_release);
    report("uninstall", "ok");
    return true;
}

/** Accepts an activation only when every required owner reports ready. */
bool request_active(bool active) noexcept {
    if (active && (g_stopping.load(std::memory_order_acquire) || !ready(snapshot()))) {
        g_requested.store(false, std::memory_order_release);
        g_phase.store(Phase::unavailable, std::memory_order_release);
        report("request", "rejected", "dependency_unavailable");
        return false;
    }
    const bool before = g_requested.exchange(active, std::memory_order_acq_rel);
    if (active && !before) {
        g_phase.store(Phase::waiting, std::memory_order_release);
        report("request", "ok", "active_1");
    } else if (!active && before) {
        report("request", "ok", "active_0");
    }
    return true;
}

/** @return One lock-free snapshot for hooks and the Player page. */
Status status() noexcept {
    return snapshot();
}

/** Advances one session after the stock camera transform has produced its current pose. */
void apply(std::uint32_t playerIndex, std::byte* cameraBlock) noexcept {
    if (!g_installed.load(std::memory_order_acquire)) {
        return;
    }
    AcquireSRWLockExclusive(&g_sessionLock);
    if (g_stopping.load(std::memory_order_acquire)) {
        leave_session();
        g_phase.store(Phase::exiting, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_sessionLock);
        return;
    }
    poll_toggle();
    Status current = snapshot();
    if (!current.requested) {
        leave_session();
        g_phase.store(Phase::inactive, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_sessionLock);
        return;
    }
    if (!ready(current)) {
        g_requested.store(false, std::memory_order_release);
        leave_session();
        g_phase.store(Phase::fault, std::memory_order_release);
        report("frame", "fault", "dependency_lost");
        ReleaseSRWLockExclusive(&g_sessionLock);
        return;
    }
    if (playerIndex == teleport::kInvalidHandle || cameraBlock == nullptr
        || !teleport::local_player_available()) {
        if (g_session.entered) {
            g_requested.store(false, std::memory_order_release);
            leave_session();
            g_phase.store(Phase::fault, std::memory_order_release);
            report("frame", "fault", "player_transition");
        } else {
            g_phase.store(Phase::waiting, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&g_sessionLock);
        return;
    }

    movement::input::Sample sampled{};
    if (!movement::input::sample(false, sampled)) {
        polled_input::clear_blocked_keys();
        g_phase.store(Phase::waiting, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_sessionLock);
        return;
    }
    polled_input::set_blocked_keys(std::span(sampled.virtualKeys.data(), sampled.virtualKeyCount));

    if (!g_session.entered) {
        g_session = Session{playerIndex, true};
        presentation::set_photo_mode_active(true);
        g_phase.store(Phase::entering, std::memory_order_release);
        report("enter", "ok", "pending_presentation");
    } else if (g_session.playerIndex != playerIndex) {
        g_requested.store(false, std::memory_order_release);
        leave_session();
        g_phase.store(Phase::fault, std::memory_order_release);
        report("frame", "fault", "player_transition");
        ReleaseSRWLockExclusive(&g_sessionLock);
        return;
    }

    current = snapshot();
    g_phase.store(current.rigSuppressed ? Phase::active : Phase::entering,
                  std::memory_order_release);
    ReleaseSRWLockExclusive(&g_sessionLock);
}

} // namespace sunrise::client::hooks::photo_mode
