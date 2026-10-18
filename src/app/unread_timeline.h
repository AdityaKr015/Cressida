#pragma once

#include "api/models.h"
#include "api/read_state.h"

#include <algorithm>
#include <map>
#include <optional>
#include <span>

namespace app {
  // Server cursors describe actual unread messages. A local high-water mark
  // prevents delayed Gateway snapshots from undoing an acknowledgement in flight.
  class UnreadTimeline {
  public:
    void reset() { *this = {}; }
    void open(const std::string& channel) {
      m_channel = channel;
      m_positionPending = !channel.empty();
    }
    void snapshot(std::map<std::string, api::UnreadCursor> unread, const std::string& rewind = {}) {
      m_unread = std::move(unread);
      if (rewind == "reset")
        m_readThrough.clear();
      else if (!rewind.empty())
        m_readThrough.erase(rewind);
      m_receivedState = true;
    }
    void acknowledged(const std::string& channel, const std::string& message) {
      if (api::validSnowflake(message) && newer(message, m_readThrough[channel]))
        m_readThrough[channel] = message;
    }
    void failed(const std::string& channel, const std::string& message) {
      const auto at = m_readThrough.find(channel);
      if (at != m_readThrough.end() && at->second == message)
        m_readThrough.erase(at);
    }
    bool waiting() const { return !m_channel.empty() && !m_receivedState; }
    bool positionPending() const { return m_positionPending; }
    void positioned() { m_positionPending = false; }
    std::optional<api::UnreadCursor> cursor(const std::string& channel) const {
      const auto at = m_unread.find(channel);
      if (at == m_unread.end())
        return {};
      auto value = at->second;
      if (const auto read = m_readThrough.find(channel); read != m_readThrough.end()) {
        if (!newer(value.latest, read->second))
          return {};
        if (newer(read->second, value.after)) {
          value.after = read->second;
          // Imported counts can include mentions just read locally, while the
          // server ACK is in flight. Only known newer live mentions remain.
          value.mentions = 0;
          for (const auto& message : value.mentionMessages)
            if (newer(message, read->second))
              ++value.mentions;
          value.mentions = std::min(value.mentions, at->second.mentions);
        }
      }
      return value;
    }
    int mentions(const std::string& channel) const {
      const auto value = cursor(channel);
      return value ? value->mentions : 0;
    }
    bool unread() const { return cursor(m_channel).has_value(); }
    std::optional<std::size_t> first(std::span<const api::Message> messages) const {
      const auto value = cursor(m_channel);
      if (!value)
        return {};
      for (std::size_t i = 0; i < messages.size(); ++i)
        if (!messages[i].deleted
            && !(messages[i].flags & 64)
            && api::validSnowflake(messages[i].id)
            && newer(messages[i].id, value->after))
          return i;
      return {};
    }
    bool estimated(std::span<const api::Message> messages) const {
      const auto value = cursor(m_channel);
      const auto first =
          std::ranges::find_if(messages, [](const auto& message) { return !message.deleted && !(message.flags & 64); });
      return value && (value->after.empty() || first == messages.end() || newer(first->id, value->after));
    }

  private:
    static bool newer(const std::string& a, const std::string& b) {
      return a.size() != b.size() ? a.size() > b.size() : a > b;
    }
    std::map<std::string, api::UnreadCursor> m_unread;
    std::map<std::string, std::string> m_readThrough;
    std::string m_channel;
    bool m_receivedState = false, m_positionPending = false;
  };
} // namespace app
