/** Shared exclusive keyboard-binding picker for Client pages. */

#include "client_key_picker.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <imgui.h>

#include "../../../movement/movement_settings_store.h"

namespace sunrise::client::ui::components::key_picker {
namespace {

/** Windows virtual-key scan bounds; zero means unbound and mouse buttons are never captured. */
constexpr int kFirstVirtualKey = 1;
constexpr int kLastVirtualKey = 254;
constexpr int kLastMouseKey = 6;
/** Longest display name accepted from Windows, including its terminator. */
constexpr std::size_t kKeyNameCapacity = 64;

/** String-literal identity of the one picker currently consuming keyboard input. */
const char* g_capturing{};

/** Names one virtual key for display, falling back to its numeric value. */
void key_name(std::uint32_t virtualKey, std::array<char, kKeyNameCapacity>& output) noexcept {
    if (virtualKey == client::movement::kNoKey) {
        (void)std::snprintf(output.data(), output.size(), "None");
        return;
    }

    const UINT scanCode = MapVirtualKeyExW(virtualKey, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
    LONG nameCode = static_cast<LONG>((scanCode & 0xFFU) << 16U);
    if ((scanCode & 0xFF00U) != 0) {
        nameCode |= 1L << 24;
    }
    std::array<wchar_t, kKeyNameCapacity> wide{};
    const int written =
        scanCode != 0 ? GetKeyNameTextW(nameCode, wide.data(), static_cast<int>(wide.size())) : 0;
    if (written <= 0
        || WideCharToMultiByte(CP_UTF8,
                               0,
                               wide.data(),
                               written,
                               output.data(),
                               static_cast<int>(output.size() - 1),
                               nullptr,
                               nullptr)
               <= 0) {
        (void)std::snprintf(
            output.data(), output.size(), "Key 0x%02X", static_cast<unsigned>(virtualKey));
    }
}

/** Takes the first held non-mouse key; Escape clears the binding. */
[[nodiscard]] bool capture_key(std::uint32_t& picked) noexcept {
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
        picked = client::movement::kNoKey;
        return true;
    }
    for (int key = kFirstVirtualKey; key <= kLastVirtualKey; ++key) {
        if (key <= kLastMouseKey) {
            continue;
        }
        if ((GetAsyncKeyState(key) & 0x8000) != 0) {
            picked = static_cast<std::uint32_t>(key);
            return true;
        }
    }
    return false;
}

} // namespace

/** Draws one picker while keeping keyboard capture exclusive across Client pages. */
bool control(const char* id, std::uint32_t& virtualKey, float width) noexcept {
    ImGui::PushID(id);
    if (g_capturing == id) {
        if (ImGui::Button("...", ImVec2(width, 0.0F))) {
            g_capturing = nullptr;
        }
        ImGui::PopID();
        std::uint32_t picked = client::movement::kNoKey;
        if (capture_key(picked)) {
            virtualKey = picked;
            g_capturing = nullptr;
            return true;
        }
        return false;
    }

    std::array<char, kKeyNameCapacity> name{};
    key_name(virtualKey, name);
    const bool clicked = ImGui::Button(name.data(), ImVec2(width, 0.0F));
    ImGui::PopID();
    if (clicked) {
        g_capturing = id;
    }
    return false;
}

} // namespace sunrise::client::ui::components::key_picker
