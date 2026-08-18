/** Shared movement-key sampling for Fly and attached-camera Photo Mode. */

#include "movement_input.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "../../state/runtime/runtime.h"
#include "../hooks/teleport/runtime.h"

namespace sunrise::client::movement::input {
namespace {

namespace bindings = state::account::settings::bindings;

/** High bit Windows sets while a virtual key is physically held. */
constexpr SHORT kKeyHeldBit = static_cast<SHORT>(0x8000);
/** Squared-length floor below which a direction is treated as zero. */
constexpr float kMinimumLengthSquared = 0.000001F;
/** Cartesian lane indices used by camera-relative vector arithmetic. */
constexpr std::size_t kHorizontalX = 0;
constexpr std::size_t kHorizontalY = 1;
constexpr std::size_t kVertical = 2;

/** Logical movement directions populated from the account's authored actions. */
enum class Direction : std::size_t {
    forward,
    backward,
    left,
    right,
    up,
    down,
    count,
};

/** One replicated account action and the logical direction it contributes. */
struct ActionDirection {
    bindings::Action action;
    Direction direction;
};

/** Account actions sampled by Fly and Photo Mode, including both crouch forms. */
constexpr std::array<ActionDirection, 7> kActions{{
    {bindings::Action::moveForward, Direction::forward},
    {bindings::Action::moveBackward, Direction::backward},
    {bindings::Action::moveLeft, Direction::left},
    {bindings::Action::moveRight, Direction::right},
    {bindings::Action::jump, Direction::up},
    {bindings::Action::toggleCrouch, Direction::down},
    {bindings::Action::holdCrouch, Direction::down},
}};

/** First configured account snapshot cached until movement settings shut down. */
std::array<bindings::Binding, kActions.size()> g_bindings{};
bool g_bindingsRead{};
SRWLOCK g_bindingsLock{SRWLOCK_INIT};

/** Publishes one complete binding set after account settings become available. */
[[nodiscard]] bool read_bindings(std::array<bindings::Binding, kActions.size()>& output) noexcept {
    AcquireSRWLockShared(&g_bindingsLock);
    if (g_bindingsRead) {
        output = g_bindings;
        ReleaseSRWLockShared(&g_bindingsLock);
        return true;
    }
    ReleaseSRWLockShared(&g_bindingsLock);

    const state::AccountState account = state::account_snapshot();
    if (!account.settings.keyBindings.configured) {
        return false;
    }
    std::array<bindings::Binding, kActions.size()> discovered{};
    for (std::size_t index = 0; index < kActions.size(); ++index) {
        discovered[index] =
            account.settings.keyBindings.values[static_cast<std::size_t>(kActions[index].action)];
    }

    AcquireSRWLockExclusive(&g_bindingsLock);
    if (!g_bindingsRead) {
        g_bindings = discovered;
        g_bindingsRead = true;
    }
    output = g_bindings;
    ReleaseSRWLockExclusive(&g_bindingsLock);
    return true;
}

/** Appends one nonzero key once, preserving the authored action order. */
void append_unique(std::uint32_t key, Sample& output) noexcept {
    if (key == 0) {
        return;
    }
    for (std::size_t index = 0; index < output.virtualKeyCount; ++index) {
        if (output.virtualKeys[index] == key) {
            return;
        }
    }
    if (output.virtualKeyCount < output.virtualKeys.size()) {
        output.virtualKeys[output.virtualKeyCount++] = key;
    }
}

/** Resolves one optional binding half, records its key, and optionally reads its state. */
[[nodiscard]] bool
half_down(const std::optional<std::uint16_t>& half, bool readState, Sample& output) noexcept {
    if (!half.has_value()) {
        return false;
    }
    const std::uint32_t key = hooks::teleport::action_key(*half);
    append_unique(key, output);
    return readState && key != 0 && (GetAsyncKeyState(static_cast<int>(key)) & kKeyHeldBit) != 0;
}

/** @return Normalized horizontal right vector for one camera-forward vector. */
[[nodiscard]] Vector right_of(const Vector& forward) noexcept {
    Vector right{forward[kHorizontalY], -forward[kHorizontalX], 0.0F};
    const float lengthSquared =
        right[kHorizontalX] * right[kHorizontalX] + right[kHorizontalY] * right[kHorizontalY];
    if (lengthSquared <= kMinimumLengthSquared) {
        return {};
    }
    const float length = std::sqrt(lengthSquared);
    right[kHorizontalX] /= length;
    right[kHorizontalY] /= length;
    return right;
}

} // namespace

/** Resolves authored movement keys and optionally samples their held state. */
bool sample(bool readState, Sample& output) noexcept {
    output = {};
    std::array<bindings::Binding, kActions.size()> authored{};
    if (!read_bindings(authored)) {
        return false;
    }

    std::array<bool, static_cast<std::size_t>(Direction::count)> pressed{};
    for (std::size_t index = 0; index < kActions.size(); ++index) {
        const bindings::Binding& binding = authored[index];
        const bool primary = half_down(binding.primary, readState, output);
        const bool secondary = half_down(binding.secondary, readState, output);
        if (primary || secondary) {
            pressed[static_cast<std::size_t>(kActions[index].direction)] = true;
        }
    }

    output.axes[0] = static_cast<float>(pressed[static_cast<std::size_t>(Direction::forward)])
                     - static_cast<float>(pressed[static_cast<std::size_t>(Direction::backward)]);
    output.axes[1] = static_cast<float>(pressed[static_cast<std::size_t>(Direction::right)])
                     - static_cast<float>(pressed[static_cast<std::size_t>(Direction::left)]);
    output.axes[2] = static_cast<float>(pressed[static_cast<std::size_t>(Direction::up)])
                     - static_cast<float>(pressed[static_cast<std::size_t>(Direction::down)]);
    return output.virtualKeyCount != 0;
}

/** Converts sampled camera-frame axes into one normalized world-space direction. */
Vector camera_relative(const Sample& sampled, const Vector& forward) noexcept {
    const Vector right = right_of(forward);
    Vector direction{};
    for (std::size_t lane = 0; lane < kVectorLanes; ++lane) {
        direction[lane] = forward[lane] * sampled.axes[0] + right[lane] * sampled.axes[1];
    }
    direction[kVertical] += sampled.axes[2];

    float lengthSquared = 0.0F;
    for (const float lane : direction) {
        lengthSquared += lane * lane;
    }
    if (lengthSquared <= kMinimumLengthSquared) {
        return {};
    }
    const float length = std::sqrt(lengthSquared);
    for (float& lane : direction) {
        lane /= length;
    }
    return direction;
}

/** Drops cached authored bindings under the same lock used for publication. */
void reset() noexcept {
    AcquireSRWLockExclusive(&g_bindingsLock);
    g_bindings = {};
    g_bindingsRead = false;
    ReleaseSRWLockExclusive(&g_bindingsLock);
}

} // namespace sunrise::client::movement::input
