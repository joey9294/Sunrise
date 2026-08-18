/** Player settings and runtime-only Photo Mode controls. */

#include "player_panel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <imgui.h>

#include "../../../core/ui/components/label/ui_label_component.h"
#include "../../../core/ui/components/toggle/ui_toggle_component.h"
#include "../../hooks/inactivity/inactivity_override.h"
#include "../../inactivity/inactivity_settings_store.h"
#include "../../hooks/photo_mode/photo_mode.h"
#include "../../movement/movement_settings_store.h"

#include "../../player/player_settings_store.h"
#include "../components/key_picker/client_key_picker.h"

namespace sunrise::client::ui::player {
namespace {

namespace inactivity = client::inactivity;
namespace toggle = core::ui::components::toggle;

constexpr int kLaneColumns = 7;

/**
 * Draws one lane's field and, under it, the milliseconds the Client is holding in that lane now.
 * @param index Lane in block order.
 * @param configured Configuration updated on an edit.
 * @param status What the override reached, for the live figure.
 * @return True when this lane changed.
 */
[[nodiscard]] bool draw_lane(std::size_t index,
                             inactivity::Settings& configured,
                             const hooks::inactivity::Status& status) noexcept {
    const bool orbit = index == inactivity::kOrbitLane;
    bool changed = false;
    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginDisabled(orbit);
    ImGui::TextUnformatted(inactivity::kActivities[index].column.data());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", inactivity::kActivities[index].name.data());
    }
    // The live figure is the one the Client is timing by, so it reads as active and the set value
    // is dimmed. A lane held at its longest is timing nothing, so neither is active.
    const std::uint32_t live = status.liveValid ? status.live[index] : 0;
    const bool liveActive = status.liveValid && live != inactivity::kMaximumTimeoutMs;

    ImGui::SetNextItemWidth(-FLT_MIN);
    std::uint32_t milliseconds = configured.timeouts[index];
    if (liveActive) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }
    ImGui::InputScalar("##lane",
                       ImGuiDataType_U32,
                       &milliseconds,
                       nullptr,
                       nullptr,
                       "%u",
                       ImGuiInputTextFlags_CharsDecimal);
    if (liveActive) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        configured.timeouts[index] =
            std::clamp(milliseconds, inactivity::kMinimumTimeoutMs, inactivity::kMaximumTimeoutMs);
        changed = true;
    }
    ImGui::EndDisabled();
    // Outside the disabled block: the live value reads the same whether or not the field can be
    // edited.
    if (!status.liveValid) {
        ImGui::TextDisabled("-");
    } else if (liveActive) {
        ImGui::Text("%u", live);
    } else {
        ImGui::TextDisabled("%u", live);
    }
    ImGui::PopID();
    return changed;
}

/** @param status What the override reached, for the live grace. */
void draw_inactivity_clocks(const hooks::inactivity::Status& status) noexcept {
    // Read from the draw rather than the hold, so the Client is only asked while this section is
    // on screen to answer to.
    const hooks::inactivity::Timers timers = hooks::inactivity::timers();
    if (timers.idleValid) {
        ImGui::Text("Idle %.1f s", static_cast<double>(timers.idleMs) / 1000.0);
    } else {
        ImGui::TextDisabled("Idle -");
    }
    ImGui::SameLine();
    if (timers.sessionValid) {
        ImGui::Text("Session %.1f s", static_cast<double>(timers.sessionMs) / 1000.0);
    } else {
        ImGui::TextDisabled("Session -");
    }
    if (!status.liveGraceValid) {
        return;
    }
    const bool passed = status.liveGraceMs == 0
                        || (timers.sessionValid && timers.sessionMs > status.liveGraceMs);
    ImGui::SameLine();
    // Reported, never written.
    const double grace = static_cast<double>(status.liveGraceMs) / 1000.0;
    if (passed) {
        ImGui::TextDisabled("Grace %.1f s (passed)", grace);
    } else {
        ImGui::Text("Grace %.1f s (no kick until then)", grace);
    }
}

void draw_inactivity() noexcept {
    inactivity::Settings configured = inactivity::get();
    // Taken once, so every line below and the grid all describe the same poll.
    const hooks::inactivity::Status status = hooks::inactivity::status();

    ImGui::TextUnformatted("Inactivity");
    ImGui::Separator();
    ImGui::TextWrapped("Disable AFK timeouts from activities kicking to orbit and the title "
                       "screen.");
    ImGui::Spacing();

    // The two switches are exclusive, so turning this one on drops the set timeouts.
    bool changed = toggle::control("Enabled##inactivity", configured.enabled);
    if (changed && configured.enabled) {
        configured.custom = false;
    }

    if (ImGui::CollapsingHeader("Advanced##inactivity")) {
        // A per-lane timeout has nothing to act on once every lane is already removed.
        ImGui::BeginDisabled(configured.enabled);
        if (toggle::control("Use set timeouts##inactivity_custom", configured.custom)) {
            if (configured.custom) {
                configured.enabled = false;
            }
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("In milliseconds");
        ImGui::Spacing();
        ImGui::BeginDisabled(configured.enabled || !configured.custom);
        if (ImGui::BeginTable("lanes", kLaneColumns, ImGuiTableFlags_SizingStretchSame)) {
            for (std::size_t index = 0; index < inactivity::kActivityCount; ++index) {
                ImGui::TableNextColumn();
                changed = draw_lane(index, configured, status) || changed;
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        draw_inactivity_clocks(status);
    }

    if (changed) {
        (void)inactivity::publish(configured);
    }
}

} // namespace


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

    draw_inactivity();

}

} // namespace sunrise::client::ui::player
