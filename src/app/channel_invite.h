#pragma once

#include "api/actions.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace app {
  struct ChannelInviteBounds {
    float x = 0, y = 0, width = 0, height = 0;
    bool contains(float px, float py) const { return px >= x && px < x + width && py >= y && py < y + height; }
  };
  struct ChannelInviteRecipient {
    std::string key, channel, user, name, username, avatar;
  };
  struct ChannelInviteState {
    bool editing = false, copied = false, temporary = false, draftTemporary = false;
    int maxAge = 604800, maxUses = 0, draftMaxAge = 604800, draftMaxUses = 0, dropdown = 0, renderedDropdown = 0;
    std::uint64_t generation = 0;
    float scroll = 0, formScroll = 0;
    ChannelInviteBounds listBounds, formBounds, dropdownBounds;
    std::string query;
    std::vector<ChannelInviteRecipient> recipients;
    std::vector<std::size_t> matches;
    std::map<std::string, api::Interaction> pending;
    std::set<std::string> sent;
    std::map<std::string, std::string> errors;
  };
} // namespace app
