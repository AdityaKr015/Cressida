#pragma once

#include "discord/models.h"

#include <nlohmann/json.hpp>

namespace discord {
  enum class InteractionKind {
    AddReaction,
    RemoveReaction,
    EditMessage,
    DeleteMessage,
    Block,
    Unblock,
    AddFriend,
    RemoveFriend,
    Nickname,
    OpenDm,
    UserProfile,
    Mute,
    Unmute,
    ListReactions,
    GuildProfile,
    JoinGuild
  };
  struct Interaction {
    InteractionKind kind = InteractionKind::AddReaction;
    std::string channel, message, user, emoji, text, guild;
    bool burst = false;
    std::uint64_t revision = 0; // UI request identity, never sent to Discord.
  };
  struct InteractionRequest {
    std::string path, method;
    nlohmann::json body;
  };
  // Fixed routes and validated identifiers only; never accept an arbitrary URL.
  std::optional<InteractionRequest> interactionRequest(const Interaction& action);
  nlohmann::json messageBody(const std::string& content, const std::optional<MessageReference>& reply = {});
  std::string reactionKey(const Reaction& reaction);
  nlohmann::json decodeRestBody(long status, std::string_view bytes);
  std::vector<Account> parseReactors(const nlohmann::json& value);
} // namespace discord
