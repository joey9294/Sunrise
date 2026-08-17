#include "client_ui_module_runtime.h"

#include <string_view>

#include "../../../core/ui/modules/registry/ui_module_registry.h"
#include "../../../core/ui/modules/ui_module_descriptor.h"
#include "../movement/movement_panel.h"
#include "../player/player_panel.h"

namespace sunrise::client::ui::runtime {
namespace {

/** Namespaced stable ID prevents Client modules from colliding with Server modules. */
constexpr std::string_view kMovementStableId = "client.movement";
constexpr std::string_view kPlayerStableId = "client.player";
/** Short menu label for the shared teleport and noclip page. */
constexpr std::string_view kMovementDisplayName = "Movement";
constexpr std::string_view kPlayerDisplayName = "Player";

core::ui::modules::registry::PageRegistration g_movementPage;
core::ui::modules::registry::PageRegistration g_playerPage;

} // namespace

/** @return True when the Client module owns its Core UI registry slot. */
bool initialize() noexcept {
    if (!g_movementPage.acquire(core::ui::modules::Owner::client,
                                kMovementStableId,
                                kMovementDisplayName,
                                &movement::draw)) {
        return false;
    }
    if (!g_playerPage.acquire(
            core::ui::modules::Owner::client, kPlayerStableId, kPlayerDisplayName, &player::draw)) {
        g_movementPage.release();
        return false;
    }
    return true;
}

/** Removes the Client module from the Core UI registry. */
void shutdown() noexcept {
    g_playerPage.release();
    g_movementPage.release();
}

} // namespace sunrise::client::ui::runtime
