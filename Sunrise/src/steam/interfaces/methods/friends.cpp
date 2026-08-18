#include "../../../core/settings/settings.h"
#include "../../../client/player/player_settings_store.h"
#include "../internal.h"

namespace sunrise::steam::interfaces::methods {

/** @return Persona name from settings. It lasts for the whole process. */
const char* persona_name([[maybe_unused]] void* self) noexcept {
    thread_local client::player::Settings playerSettings{};
    playerSettings = client::player::get();
    if (playerSettings.personaName[0] != '\0') {
        return playerSettings.personaName.data();
    }
    return core::settings::get().steam.user.personaName.data();
}

} // namespace sunrise::steam::interfaces::methods
