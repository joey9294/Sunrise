#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sunrise::client::movement::input {

/** Three Cartesian lanes shared by movement samples and camera vectors. */
inline constexpr std::size_t kVectorLanes = 3;
/** Seven actions can each contribute two distinct bindings. */
inline constexpr std::size_t kMaximumVirtualKeys = 14;

using Vector = std::array<float, kVectorLanes>;

/** One binding sample, independent of whether a camera basis is currently available. */
struct Sample {
    /** Forward, right, and vertical intent in the camera frame. */
    Vector axes{};
    std::array<std::uint32_t, kMaximumVirtualKeys> virtualKeys{};
    std::size_t virtualKeyCount{};
};

/**
 * Resolves the movement bindings and optionally reads their current key state.
 * @param readState False when another interface or application owns keyboard input.
 * @param output Receives authored virtual keys and camera-frame intent.
 * @return True once account bindings and their virtual keys are available.
 */
[[nodiscard]] bool sample(bool readState, Sample& output) noexcept;

/**
 * Converts sampled intent to one normalized world-space direction.
 * @param sampled Binding sample produced by sample().
 * @param forward Current camera forward vector.
 */
[[nodiscard]] Vector camera_relative(const Sample& sampled, const Vector& forward) noexcept;

/** Drops the cached authored bindings. */
void reset() noexcept;

} // namespace sunrise::client::movement::input
