#pragma once

#include "api/models.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace app {
  inline constexpr int autoModerationActionType = 24;
  inline const api::Embed* autoModerationEmbed(const api::Message& message) {
    if (message.type != autoModerationActionType)
      return nullptr;
    const auto found = std::ranges::find(message.embeds, "auto_moderation_message", &api::Embed::type);
    return found == message.embeds.end() ? nullptr : &*found;
  }
  inline std::string_view autoModerationField(const api::Embed* embed, std::string_view name) {
    if (!embed)
      return {};
    const auto found = std::ranges::find(embed->fields, name, &api::EmbedField::name);
    return found == embed->fields.end() ? std::string_view{} : found->value;
  }
  inline std::optional<std::pair<std::string, std::string>> autoModerationJump(const api::Message& message) {
    const auto* embed = autoModerationEmbed(message);
    const std::string channel(autoModerationField(embed, "channel_id"));
    if (!api::validSnowflake(channel) || !api::validSnowflake(message.id))
      return {};
    // The alert's snowflake anchors the surrounding timeline even when the
    // blocked source message was never created. A flagged-message link is a
    // distinct exact-message jump in Discord's preview.
    return std::pair{channel, message.id};
  }
} // namespace app
