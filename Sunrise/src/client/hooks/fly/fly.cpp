/**
 * Velocity fly. The movement keys set, rather than add to, player velocity every tick so releasing
 * every key stops prior momentum and preserves hover.
 */

#include "fly.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../../../core/logging/log.h"
#include "../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../input/window_focus.h"
#include "../../movement/movement_input.h"
#include "../../movement/movement_settings_store.h"
#include "../noclip/runtime.h"
#include "../photo_mode/photo_mode.h"
#include "../teleport/runtime.h"

namespace sunrise::client::hooks::fly {
namespace {

/** High bit Windows sets while a virtual key is physically held. */
constexpr SHORT kKeyHeldBit = static_cast<SHORT>(0x8000);

std::atomic_bool g_toggleDown{false};
float g_heightBeforeStep{};
bool g_heightValid{};
bool g_steered{};

/** Effective Fly switch and speed after applying Photo Mode's runtime override. */
struct DriveState {
    bool active{};
    float speed{};
};

/** @return Persisted Fly state overridden only while Photo Mode is active. */
[[nodiscard]] DriveState drive_state(const movement::Settings& settings) noexcept {
    const photo_mode::Status photoMode = photo_mode::status();
    if (photoMode.phase == photo_mode::Phase::active) {
        return {true, settings.flySpeed};
    }
    return {settings.flyEnabled, settings.flySpeed};
}

/** @return True while Photo Mode owns movement and must suppress Fly's persisted toggle key. */
[[nodiscard]] bool photo_mode_owns_toggle() noexcept {
    const photo_mode::Status photoMode = photo_mode::status();
    return photoMode.requested || photoMode.phase == photo_mode::Phase::entering
           || photoMode.phase == photo_mode::Phase::active
           || photoMode.phase == photo_mode::Phase::exiting;
}

/** @return One flag per direction, true while any key bound to it is down. */
[[nodiscard]] std::array<bool, kDirectionCount> pressed_directions() noexcept {
    std::array<bool, kDirectionCount> pressed{};
    for (std::size_t index = 0; index < kActions.size(); ++index) {
        const bindings::Binding& binding = g_bindings[index];
        if (half_down(binding.primary) || half_down(binding.secondary)) {
            pressed[static_cast<std::size_t>(kActions[index].direction)] = true;
        }
    }
    return pressed;
}

/**
 * Forward turned about the up axis. Which turn is right is unverified: if strafing is mirrored,
 * negate both lanes.
 * @return The strafe axis, or zeroes when the camera looks straight up or down.
 */
[[nodiscard]] teleport::Vector right_of(const teleport::Vector& forward) noexcept {
    teleport::Vector right{forward[kLaneY], -forward[kLaneX], 0.0F};
    const float lengthSquared = right[kLaneX] * right[kLaneX] + right[kLaneY] * right[kLaneY];
    if (lengthSquared <= kMinimumLengthSquared) {
        return teleport::Vector{};
    }
    const float length = std::sqrt(lengthSquared);
    right[kLaneX] /= length;
    right[kLaneY] /= length;
    return right;
}

/**
 * Composes the pressed directions into one unit vector.
 * @param pressed One flag per direction.
 * @param forward Camera forward vector.
 * @return The direction to fly, or all zeroes when nothing is pressed.
 */
[[nodiscard]] teleport::Vector travel(const std::array<bool, kDirectionCount>& pressed,
                                      const teleport::Vector& forward) noexcept {
    const teleport::Vector right = right_of(forward);
    teleport::Vector move{};
    const auto add = [&move](const teleport::Vector& axis, float scale) noexcept {
        for (std::size_t lane = 0; lane < teleport::kVectorLanes; ++lane) {
            move[lane] += axis[lane] * scale;
        }
    };
    if (pressed[static_cast<std::size_t>(Direction::forward)]) {
        add(forward, 1.0F);
    }
    if (pressed[static_cast<std::size_t>(Direction::backward)]) {
        add(forward, -1.0F);
    }
    if (pressed[static_cast<std::size_t>(Direction::right)]) {
        add(right, 1.0F);
    }
    if (pressed[static_cast<std::size_t>(Direction::left)]) {
        add(right, -1.0F);
    }
    if (pressed[static_cast<std::size_t>(Direction::up)]) {
        move[teleport::kVerticalLane] += 1.0F;
    }
    if (pressed[static_cast<std::size_t>(Direction::down)]) {
        move[teleport::kVerticalLane] -= 1.0F;
    }
    float lengthSquared = 0.0F;
    for (const float lane : move) {
        lengthSquared += lane * lane;
    }
    if (lengthSquared <= kMinimumLengthSquared) {
        return teleport::Vector{};
    }
    // Normalised, so a diagonal is not faster than a straight line.
    const float length = std::sqrt(lengthSquared);
    for (float& lane : move) {
        lane /= length;
    }
    return move;
}

/** Caps one velocity vector without changing a vector already under the supported limit. */

void cap_speed(teleport::Vector& velocity, float limit) noexcept {
    float speedSquared = 0.0F;
    for (const float lane : velocity) {
        speedSquared += lane * lane;
    }
    if (speedSquared <= limit * limit) {
        return;
    }
    const float scale = limit / std::sqrt(speedSquared);
    for (float& lane : velocity) {
        lane *= scale;
    }
}

/**
 * Samples bindings before the camera basis. A missing camera sample therefore produces a valid
 * zero velocity and never releases Photo Mode's game-input suppression.
 */
[[nodiscard]] bool desired_velocity(const DriveState& drive, teleport::Vector& velocity) noexcept {
    const bool readState = !core::ui::runtime::snapshot().visible && input::game_focused();
    movement::input::Sample sampled{};
    if (!movement::input::sample(readState, sampled)) {
        g_steered = false;
        return false;
    }
    movement::input::Vector forward{};
    movement::input::Vector direction{};
    if (teleport::camera_forward(forward)) {
        direction = movement::input::camera_relative(sampled, forward);
    }
    g_steered = direction[teleport::kVerticalLane] != 0.0F;
    for (std::size_t lane = 0; lane < teleport::kVectorLanes; ++lane) {
        velocity[lane] = direction[lane] * drive.speed;
    }
    return true;
}

} // namespace

void poll_toggle() noexcept {
    const movement::Settings settings = movement::get();
    if (settings.flyToggleKey == movement::kNoKey) {
        g_toggleDown.store(false, std::memory_order_relaxed);
        return;
    }
    const bool down =
        input::game_focused()
        && (GetAsyncKeyState(static_cast<int>(settings.flyToggleKey)) & kKeyHeldBit) != 0;
    if (core::ui::runtime::snapshot().visible || photo_mode_owns_toggle()) {
        g_toggleDown.store(down, std::memory_order_relaxed);
        return;
    }
    if (down && !g_toggleDown.exchange(true, std::memory_order_acq_rel)) {
        movement::Settings updated = settings;
        updated.flyEnabled = !settings.flyEnabled;
        if (!movement::publish(updated)) {
            return;
        }
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         updated.flyEnabled ? "ev=fly stage=toggle enabled=1"
                                            : "ev=fly stage=toggle enabled=0");
        return;
    }
    if (!down) {
        g_toggleDown.store(false, std::memory_order_release);
    }
}

void apply(void* component) noexcept {
    const DriveState drive = drive_state(movement::get());
    if (!drive.active || component == nullptr || !teleport::owns_local_player(component)) {
        return;
    }
    teleport::Vector velocity{};
    if (!desired_velocity(drive, velocity)) {
        return;
    }
    cap_speed(velocity, kPublishedSpeedCap);
    (void)teleport::write_velocity(component, velocity);
}

bool enabled() noexcept {
    return drive_state(movement::get()).active;
}

void before_step(void* body) noexcept {
    g_heightValid = false;
    if (body == nullptr) {
        return;
    }
    const DriveState drive = drive_state(movement::get());
    if (!drive.active) {
        return;
    }
    teleport::Vector velocity{};
    if (!desired_velocity(drive, velocity)) {
        return;
    }
    noclip::write_body_velocity(body, velocity);
    noclip::Vector position{};
    noclip::read_body_position(body, position);
    g_heightBeforeStep = position[teleport::kVerticalLane];
    g_heightValid = true;
}

void after_step(void* body, bool heldElsewhere) noexcept {
    if (body == nullptr || heldElsewhere || g_steered || !g_heightValid) {
        return;
    }
    noclip::Vector position{};
    noclip::read_body_position(body, position);
    position[teleport::kVerticalLane] = g_heightBeforeStep;
    noclip::write_body_position(body, position);
    noclip::Vector velocity{};
    noclip::read_body_velocity(body, velocity);
    velocity[teleport::kVerticalLane] = 0.0F;
    noclip::write_body_velocity(body, velocity);
}

void reset() noexcept {
    g_toggleDown.store(false, std::memory_order_release);
    g_heightValid = false;
}

} // namespace sunrise::client::hooks::fly
