#pragma once

#include <cstddef>
#include <cstdint>

namespace sunrise::client::hooks::photo_mode {

enum class Phase : std::uint8_t {
    unavailable,
    inactive,
    waiting,
    entering,
    active,
    exiting,
    fault,
};

/** One coherent snapshot of Photo Mode and its reusable presentation dependencies. */
struct Status {
    Phase phase{Phase::unavailable};
    bool requested{};
    bool presentationReady{};
    bool movementReady{};
    bool collisionBypassReady{};
    bool inputSuppressionReady{};
    bool hudReady{};
    bool rigSuppressed{};
    bool hudSuppressed{};
    bool hudFault{};
};

/** Initializes the runtime-only composite mode. */
[[nodiscard]] bool install() noexcept;

/** Clears the composite request and restores its owned input policy. */
[[nodiscard]] bool uninstall() noexcept;

/**
 * Publishes a runtime-only activation request.
 * @param active True to enter when every dependency is ready; false to leave.
 * @return False only when an activation request cannot be accepted.
 */
[[nodiscard]] bool request_active(bool active) noexcept;

/** Returns one coherent readiness and phase snapshot. */
[[nodiscard]] Status status() noexcept;

/**
 * Owns Photo Mode session transitions from the existing camera hook.
 * @param playerIndex Local player whose camera transform just ran.
 * @param cameraBlock Ephemeral camera pose, or null while no local camera is available.
 */
void apply(std::uint32_t playerIndex, std::byte* cameraBlock) noexcept;

} // namespace sunrise::client::hooks::photo_mode
