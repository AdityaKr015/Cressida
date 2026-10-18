#pragma once

#include "api/models.h"

#include <algorithm>
#include <span>

namespace app {
  // Use Discord's resolved mention metadata, never token-looking message text or
  // the larger name lookup assembled for Markdown rendering.
  inline bool
  messageMentions(const api::Message& message, const std::string& account, std::span<const std::string> roles = {}) {
    if (message.deleted || account.empty())
      return false;
    if (message.mentions.contains(account) || message.mentionEveryone)
      return true;
    return std::ranges::any_of(message.mentionRoles, [&](const auto& role) {
      return std::ranges::find(roles, role) != roles.end();
    });
  }
} // namespace app
