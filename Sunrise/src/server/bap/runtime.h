#pragma once

#include "../../client/network/consumer.h"

namespace sunrise::server::bap {

/** Applies one connection-scoped BAP lifecycle event. */
[[nodiscard]] bool consume(const client::network::BapRequest& request,
                           client::network::BapResponse& response) noexcept;

/**
 * Arms a full Family-4 account refresh for every authenticated active client.
 * UI-originated account edits use this because they have no request session to publish through.
 * @return True when at least one active client was armed.
 */
[[nodiscard]] bool request_account_refresh() noexcept;


/** Wipes every connection-owned nonce and transform buffer. */
void shutdown() noexcept;

} // namespace sunrise::server::bap
