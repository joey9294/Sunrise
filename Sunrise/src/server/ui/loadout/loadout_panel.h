#pragma once

namespace sunrise::server::ui::loadout {

/** Draws the live account loadout inspector and validated equipment actions. */
void draw() noexcept;

/** Releases Panoptes image resources owned by the Loadout page. */
void shutdown() noexcept;

} // namespace sunrise::server::ui::loadout
