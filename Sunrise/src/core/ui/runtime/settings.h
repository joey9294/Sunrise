#pragma once

#include <Windows.h>

namespace sunrise::core::ui::runtime {

/** UI visibility settings, read at boot. */
struct Settings {
    /** When off, the UI ignores the toggle key. */
    bool enabled{true};
    /** Windows virtual key that shows or hides the UI. */
    UINT toggleVirtualKey{VK_INSERT};
    /**
     * Windows virtual key that shows or hides the console.
     *
     * The grave key is what a reader expects a console to answer to, so it is the default. It is
     * a layout-dependent code, which is the reason the binding is a setting: a keyboard that puts
     * something else there is rebound rather than left without a console.
     */
    UINT consoleToggleVirtualKey{VK_OEM_3};
};

} // namespace sunrise::core::ui::runtime
