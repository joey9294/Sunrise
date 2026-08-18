/** Player settings and runtime-only Photo Mode controls. */

#include "player_panel.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <imgui.h>

#include "../../../core/ui/components/label/ui_label_component.h"
#include "../../../core/ui/components/toggle/ui_toggle_component.h"
#include "../../../steam/runtime/runtime.h"
#include "../../hooks/photo_mode/photo_mode.h"
#include "../../movement/movement_settings_store.h"

#include "../../player/player_settings_store.h"
#include "../components/key_picker/client_key_picker.h"

namespace sunrise::client::ui::player {
namespace {


constexpr int kPersonaStateChangeCallback = 304;
constexpr std::uint64_t kLocalSteamId = 0x0110000130AA9EC5ULL;
constexpr int kPersonaChangeName = 1;

struct PersonaStateChange {
    std::uint64_t steamId{};
    int changeFlags{};
    std::uint32_t padding{};
};
static_assert(sizeof(PersonaStateChange) == 16);
}


namespace label = core::ui::components::label;

/** Draws the most specific current Photo Mode phase or dependency message. */
void photo_mode_status(const hooks::photo_mode::Status& status) noexcept {
    if (!status.presentationReady || !status.movementReady || !status.collisionBypassReady
        || !status.inputSuppressionReady) {
        ImGui::TextDisabled("Unavailable:%s%s%s%s",
                            status.presentationReady ? "" : " presentation",
                            status.movementReady ? "" : " movement",
                            status.collisionBypassReady ? "" : " collision",
                            status.inputSuppressionReady ? "" : " input");
        return;
    }

    const char* message = nullptr;
    switch (status.phase) {
    case hooks::photo_mode::Phase::waiting:
        message = "Waiting for in-world camera and movement bindings.";
        break;
    case hooks::photo_mode::Phase::entering:
        message = "Entering: movement input blocked; waiting for weapon suppression.";
        break;
    case hooks::photo_mode::Phase::active:
        if (!status.hudReady) {
            message = "Active: weapon hidden; HUD removal unavailable.";
        } else if (status.hudFault) {
            message = "Active: weapon hidden; HUD removal waiting.";
        } else if (status.hudSuppressed) {
            message = "Active: weapon and HUD hidden; collision ignored.";
        } else {
            message = "Active: newer HUD setting preserved; collision ignored.";
        }
        break;
    case hooks::photo_mode::Phase::exiting:
        message = "Exiting: restoring independently disabled presentation effects.";
        break;
    case hooks::photo_mode::Phase::fault:
        message = "Stopped after a runtime dependency or player transition.";
        break;
    default:
        break;
    }
    if (message != nullptr) {
        ImGui::TextDisabled("%s", message);
    }
}

} // namespace

/** Draws persisted Player settings and the runtime-only Photo Mode request. */
void draw() noexcept {
    client::player::Settings settings = client::player::get();
    static std::array<char, 64> name{};
    static bool nameLoaded{};
    static std::array<char, 96> message{};
    if (!nameLoaded) {
        if (settings.personaName[0] != '\0') {
            name = settings.personaName;
        } else {
            (void)std::snprintf(name.data(), name.size(), "%s", "Player");
        }
        nameLoaded = true;
    }

    ImGui::TextUnformatted("Persona Name");
    ImGui::Separator();
    ImGui::TextWrapped("Changes the account name shown by Destiny after the next full restart.");
    ImGui::SetNextItemWidth(280.0F);
    (void)ImGui::InputText("##persona_name", name.data(), name.size());
    ImGui::SameLine();
    if (ImGui::Button("Save Name")) {
        bool valid = name[0] != '\0';
        for (const char* cursor = name.data(); valid && *cursor != '\0'; ++cursor) {
            const unsigned char value = static_cast<unsigned char>(*cursor);
            valid = value >= 0x20 && value <= 0x7E && value != '"' && value != '\\';
        }
        if (!valid) {
            (void)std::snprintf(message.data(), message.size(),
                                "Use 1-63 ordinary characters; quotes and backslashes are not allowed.");
        } else {
            settings.personaName = name;
            (void)client::player::publish(settings);
            const PersonaStateChange changed{kLocalSteamId, kPersonaChangeName, 0};
            const bool queued = steam::queue_callback(
                kPersonaStateChangeCallback, 0, &changed, sizeof(changed));
            (void)std::snprintf(message.data(), message.size(), "%s",
                                queued ? "Name saved; live persona refresh queued."
                                       : "Name saved; restart if this screen keeps the cached name.");
        }
    }
    if (message[0] != '\0') {
        ImGui::TextDisabled("%s", message.data());
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Infinite Ammo");
    ImGui::Separator();
    ImGui::TextWrapped("Keep every weapon's reserves full.");
    ImGui::Spacing();
    if (core::ui::components::toggle::control("Enabled##infinite_ammo",
                                              settings.infiniteAmmoEnabled)) {
        (void)client::player::publish(settings);
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextUnformatted("Photo Mode");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Move the real player with Fly speed while the stock camera stays attached. "
        "Photo Mode also removes the HUD and hides the weapon without changing their "
        "independent HUD-page switches.");
    ImGui::Spacing();
    hooks::photo_mode::Status photoMode = hooks::photo_mode::status();
    bool requested = photoMode.requested;
    if (core::ui::components::toggle::control("Active##photo_mode", requested)) {
        (void)hooks::photo_mode::request_active(requested);
        photoMode = hooks::photo_mode::status();
    }
    photo_mode_status(photoMode);

    client::movement::Settings movementSettings = client::movement::get();
    const float labelWidth =
        label::inset() + ImGui::CalcTextSize("Toggle key").x + ImGui::GetStyle().ItemSpacing.x * 2;
    const float controlWidth = ImGui::GetContentRegionAvail().x - labelWidth;
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    label::align();
    ImGui::TextUnformatted("Toggle key");
    ImGui::SameLine(labelWidth);
    if (components::key_picker::control(
            "photo_mode_key", movementSettings.photoModeToggleKey, controlWidth)) {
        (void)client::movement::publish(movementSettings);
    }
}

} // namespace sunrise::client::ui::player
