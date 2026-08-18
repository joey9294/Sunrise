/** Movement settings for teleport, noclip, fly and sword-skate behavior. */

#include "movement_panel.h"

#include <imgui.h>

#include "../../../core/ui/components/label/ui_label_component.h"
#include "../../../core/ui/components/toggle/ui_toggle_component.h"
#include "../../movement/movement_settings_store.h"
#include "../components/key_picker/client_key_picker.h"

namespace sunrise::client::ui::movement {
namespace {

namespace label = core::ui::components::label;

/** Draws one labeled key picker in the page's shared control column. */
void key_control(const char* labelText,
                 const char* id,
                 std::uint32_t& key,
                 float labelWidth,
                 float controlWidth,
                 bool& changed) noexcept {
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    label::align();
    ImGui::TextUnformatted(labelText);
    ImGui::SameLine(labelWidth);
    changed = components::key_picker::control(id, key, controlWidth) || changed;
}

} // namespace

/** Draws every persisted movement feature in one shared page. */
void draw() noexcept {
    client::movement::Settings settings = client::movement::get();
    bool changed = false;
    const float labelWidth =
        label::inset() + ImGui::CalcTextSize("Toggle key").x + ImGui::GetStyle().ItemSpacing.x * 2;
    const float controlWidth = ImGui::GetContentRegionAvail().x - labelWidth;

    ImGui::TextUnformatted("Teleport");
    ImGui::Separator();
    ImGui::TextWrapped("Teleports you forward in the facing direction. Cancels vertical momentum.");
    ImGui::Spacing();
    changed =
        core::ui::components::toggle::control("Enabled##teleport", settings.enabled) || changed;

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    label::align();
    ImGui::TextUnformatted("Distance");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(controlWidth);
    float distance = settings.distance;
    if (ImGui::SliderFloat("##distance",
                           &distance,
                           client::movement::kMinimumDistance,
                           client::movement::kMaximumDistance,
                           "%.0f units")) {
        settings.distance = distance;
        changed = true;
    }
    key_control("Key", "teleport_key", settings.virtualKey, labelWidth, controlWidth, changed);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextUnformatted("Noclip");
    ImGui::Separator();
    ImGui::TextWrapped("Disable collision on the horizontal axis.");
    ImGui::Spacing();
    changed =
        core::ui::components::toggle::control("Enabled##noclip", settings.noclipEnabled) || changed;
    key_control(
        "Toggle key", "noclip_key", settings.noclipToggleKey, labelWidth, controlWidth, changed);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextUnformatted("Fly");
    ImGui::Separator();
    ImGui::TextWrapped("Fly with your movement keys.");
    ImGui::Spacing();
    changed = core::ui::components::toggle::control("Enabled##fly", settings.flyEnabled) || changed;
    key_control("Toggle key", "fly_key", settings.flyToggleKey, labelWidth, controlWidth, changed);

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    label::align();
    ImGui::TextUnformatted("Speed");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(controlWidth);
    float flySpeed = settings.flySpeed;
    if (ImGui::SliderFloat("##fly_speed",
                           &flySpeed,
                           client::movement::kMinimumFlySpeed,
                           client::movement::kMaximumFlySpeed,
                           "%.0f units/s")) {
        settings.flySpeed = flySpeed;
        changed = true;
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextUnformatted("Sword Skate Fix");
    ImGui::Separator();
    ImGui::TextWrapped("Disable sword swings blocking ability usage.");
    ImGui::Spacing();
    changed =
        core::ui::components::toggle::control("Enabled##sword_skate", settings.swordSkateEnabled)
        || changed;

    if (changed && !client::movement::publish(settings)) {
        ImGui::Spacing();
        ImGui::TextUnformatted("value out of range, not saved");
    }
}

} // namespace sunrise::client::ui::movement
