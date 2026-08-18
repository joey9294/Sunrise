#pragma once

#include <cstdint>

namespace sunrise::client::ui::components::key_picker {

/**
 * Draws one exclusive keyboard-binding picker.
 * @param id Stable string-literal identity retained only while this picker owns capture.
 * @param virtualKey Binding to display and replace; Escape writes the unbound value.
 * @param width Requested button width.
 * @return True when capture produced a new binding.
 */
[[nodiscard]] bool control(const char* id, std::uint32_t& virtualKey, float width) noexcept;

} // namespace sunrise::client::ui::components::key_picker
