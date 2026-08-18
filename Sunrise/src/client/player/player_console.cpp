#include "player_console.h"

#include <array>

#include "../../core/console/registry/console_registry.h"
#include "player_settings_store.h"

namespace sunrise::client::player::console {
namespace {

namespace entry = core::console;
namespace registry = core::console::registry;

/** Reads whether ammunition is unlimited. */
[[nodiscard]] bool read_infinite_ammo(entry::Value& output) noexcept {
    output = entry::Value{};
    output.type = entry::Type::boolean;
    output.boolean = get().infiniteAmmoEnabled;
    return true;
}

/**
 * Writes whether ammunition is unlimited.
 *
 * The whole set is published back because that is the only interface the module offers. Nothing
 * can interleave: console handlers and the pages both run on the thread that draws.
 */
[[nodiscard]] entry::Status write_infinite_ammo(const entry::Value& value) noexcept {
    Settings settings = get();
    settings.infiniteAmmoEnabled = value.boolean;
    return publish(settings) ? entry::Status::ok : entry::Status::failed;
}

} // namespace

/** Publishes the player settings as console variables. */
bool initialize() noexcept {
    registry::Descriptor ammo{};
    ammo.name = "player.infinite_ammo";
    ammo.help = "Whether magazines stop consuming ammunition.";
    ammo.kind = registry::Kind::variable;
    ammo.type = entry::Type::boolean;
    ammo.read = &read_infinite_ammo;
    ammo.write = &write_infinite_ammo;

    const std::array entries{ammo};
    return registry::register_entries(entries) == registry::RegistrationResult::registered;
}

/** Removes the player entries. */
void shutdown() noexcept {
    static_cast<void>(registry::unregister_prefix(kPrefix));
}

} // namespace sunrise::client::player::console
