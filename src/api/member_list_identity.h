#pragma once

#include "api/channel_features.h"

#include <algorithm>
#include <bit>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace api {
  struct MemberListIdentity {
    // Use the complete signature to share local data; the wire hash can collide.
    std::string signature, wireId;
    bool operator==(const MemberListIdentity&) const = default;
  };

  namespace member_list_detail {
    // MurmurHash3 x86_32, seed zero, as used by Discord's member-list protocol.
    inline std::uint32_t hash(std::string_view value) {
      std::uint32_t result = 0;
      for (std::size_t at = 0; at < value.size(); at += 4) {
        std::uint32_t word = 0;
        const auto count = std::min<std::size_t>(4, value.size() - at);
        for (std::size_t byte = 0; byte < count; ++byte)
          word |= std::uint32_t(static_cast<unsigned char>(value[at + byte])) << (8 * byte);
        word = std::rotl(word * 0xcc9e2d51U, 15) * 0x1b873593U;
        result ^= word;
        if (count == 4)
          result = std::rotl(result, 13) * 5 + 0xe6546b64U;
      }
      result ^= static_cast<std::uint32_t>(value.size());
      result ^= result >> 16;
      result *= 0x85ebca6bU;
      result ^= result >> 13;
      result *= 0xc2b2ae35U;
      return result ^ (result >> 16);
    }
  } // namespace member_list_detail

  // Observed wire identity: discord.py-self's GuildChannel.member_list_id.
  // https://github.com/dolfies/discord.py-self/blob/master/discord/abc.py
  // This identifies equal member visibility; it never establishes account access.
  inline std::optional<MemberListIdentity>
  memberListIdentity(const std::string& guild, std::string_view everyonePermissions, const Channel& channel) {
    if (!validSnowflake(guild)
        || !validSnowflake(channel.id)
        || !channel.overwrites
        || channel.overwrites->size() > 1000)
      return {};
    switch (channel.type) {
    case 0:  // Text
    case 2:  // Voice
    case 5:  // Announcement
    case 13: // Stage
    case 15: // Forum
    case 16: // Media
      break;
    default: // DMs, categories and threads have distinct membership semantics.
      return {};
    }
    const auto everyone = permissionBits(everyonePermissions);
    if (!everyone)
      return {};
    bool denied = false;
    std::set<std::pair<int, std::string>> targets;
    std::vector<std::string> wireEntries, localEntries;
    for (const auto& overwrite : *channel.overwrites) {
      const auto allow = permissionBits(overwrite.allow), deny = permissionBits(overwrite.deny);
      if (!validSnowflake(overwrite.id)
          || (overwrite.type != 0 && overwrite.type != 1)
          || !allow
          || !deny
          || !targets.emplace(overwrite.type, overwrite.id).second)
        return {};
      const bool grantsView = (*allow & viewChannel) != 0, deniesView = (*deny & viewChannel) != 0;
      denied |= deniesView;
      if (grantsView || deniesView) {
        wireEntries.push_back(std::string(grantsView ? "allow:" : "deny:") + overwrite.id);
        localEntries.push_back(
            std::to_string(overwrite.type)
            + ":"
            + overwrite.id
            + ":"
            + (grantsView ? "a" : "")
            + (deniesView ? "d" : "")
        );
      }
    }
    if ((*everyone & viewChannel) && !denied)
      return MemberListIdentity{guild + "/everyone", "everyone"};
    std::ranges::sort(wireEntries);
    std::ranges::sort(localEntries);
    std::string wire, signature = guild + ((*everyone & viewChannel) ? "/1" : "/0");
    for (const auto& entry : wireEntries) {
      if (!wire.empty())
        wire += ',';
      wire += entry;
    }
    for (const auto& entry : localEntries)
      signature += '/' + entry;
    return MemberListIdentity{std::move(signature), std::to_string(member_list_detail::hash(wire))};
  }

  // Gateway-thread state only. A list update has no channel ID, so an unknown
  // subscription must establish its list from SYNC before applying deltas.
  class MemberListStream {
  public:
    bool select(std::string guild, std::string channel, std::optional<MemberListIdentity> identity = {}) {
      const bool sameChannel = guild == m_guild && channel == m_channel;
      const bool retain = !guild.empty()
          && !channel.empty()
          && guild == m_guild
          && ((identity && m_identity && identity->signature == m_identity->signature)
              || (sameChannel && !identity && !m_identity));
      if (!retain && !m_guild.empty() && !m_channel.empty() && !m_wireId.empty()) {
        std::erase_if(m_retired, [&](const auto& old) { return old.guild == m_guild && old.channel == m_channel; });
        if (m_retired.size() == 8)
          m_retired.erase(m_retired.begin());
        m_retired.push_back({m_guild, m_channel, m_wireId});
      }
      m_guild = std::move(guild);
      m_channel = std::move(channel);
      m_identity = std::move(identity);
      if (!retain)
        m_wireId = m_identity ? m_identity->wireId : std::string{};
      return retain;
    }

    bool accepts(const nlohmann::json& update, int firstViewport) {
      if (!validSnowflake(m_guild) || !validSnowflake(m_channel) || !update.is_object())
        return false;
      const auto guild = update.find("guild_id"), id = update.find("id");
      if (guild == update.end() || !guild->is_string() || *guild != m_guild || id == update.end() || !id->is_string())
        return false;
      const auto& wireId = id->get_ref<const std::string&>();
      if (wireId.empty() || wireId.size() > 32 || !std::ranges::all_of(wireId, [](unsigned char c) {
            return c > 32 && c < 127;
          }))
        return false;
      if (!m_wireId.empty())
        return wireId == m_wireId;
      const auto operations = update.find("ops");
      if (operations == update.end() || !operations->is_array() || operations->size() > 1000)
        return false;
      const auto first = std::clamp(firstViewport, 0, 10000000), last = std::min(first + 199, 10000000);
      const bool synced = std::ranges::any_of(*operations, [&](const auto& operation) {
        if (!operation.is_object())
          return false;
        const auto op = operation.find("op"), range = operation.find("range"), items = operation.find("items");
        return op != operation.end()
            && *op == "SYNC"
            && range != operation.end()
            && range->is_array()
            && range->size() == 2
            && (*range)[0].is_number_integer()
            && (*range)[1].is_number_integer()
            && (*range)[0].template get<std::int64_t>() >= 0
            && (*range)[1].template get<std::int64_t>() <= 10000000
            && (*range)[0].template get<std::int64_t>() <= last
            && (*range)[1].template get<std::int64_t>() >= first
            && (*range)[0].template get<std::int64_t>() <= (*range)[1].template get<std::int64_t>()
            && items != operation.end()
            && items->is_array()
            && !items->empty();
      });
      if (!synced)
        return false;
      bool retired = false, returning = false;
      for (const auto& old : m_retired)
        if (old.guild == m_guild && old.wireId == wireId) {
          retired = true;
          returning |= old.channel == m_channel;
        }
      if (retired && !returning)
        return false;
      m_wireId = wireId;
      return true;
    }

    void clear() { *this = MemberListStream{}; }

  private:
    struct Retired {
      std::string guild, channel, wireId;
    };
    std::string m_guild, m_channel, m_wireId;
    std::optional<MemberListIdentity> m_identity;
    std::vector<Retired> m_retired;
  };
} // namespace api
