#pragma once

#include "discord/history.h"

#include <algorithm>
#include <map>
#include <optional>
#include <span>

namespace app {
  class MessageHistory {
  public:
    static constexpr std::size_t windowLimit = 1000;
    void reset() {
      m_pending.reset();
      m_olderEnd = false;
      m_newerEnd = true;
      m_initialized = false;
      m_error.clear();
      m_olderCursor.clear();
      m_newerCursor.clear();
      m_patches.clear();
      ++m_serial;
    }
    bool atLatest() const { return m_newerEnd; }
    bool busy() const { return m_pending.has_value(); }
    bool initialized() const { return m_initialized; }
    bool olderEnd() const { return m_olderEnd; }
    const auto& pending() const { return m_pending; }
    const std::string& error() const { return m_error; }
    discord::HistoryDirection failedDirection() const { return m_failed; }
    std::optional<discord::HistoryQuery>
    begin(discord::HistoryDirection direction, std::span<const discord::Message> messages, bool retry = false) {
      using enum discord::HistoryDirection;
      if (direction != Latest
          && (busy()
              || (!retry && !m_error.empty())
              || messages.empty()
              || (direction == Older ? m_olderEnd : m_newerEnd)))
        return {};
      discord::HistoryQuery query{direction, {}, ++m_serial};
      if (direction != Latest)
        query.cursor = direction == Older ? (m_olderCursor.empty() ? messages.front().id : m_olderCursor)
                                          : (m_newerCursor.empty() ? messages.back().id : m_newerCursor);
      m_pending = query;
      m_error.clear();
      m_patches.clear();
      return query;
    }
    bool accepts(const discord::HistoryQuery& query) const { return m_pending == query; }
    void fail(const discord::HistoryQuery& query, std::string error) {
      if (!accepts(query))
        return;
      m_failed = query.direction;
      m_pending.reset();
      m_error = std::move(error);
      m_patches.clear();
    }
    void changed(const std::string& id, const discord::MessagePatch& patch) {
      if (!m_pending)
        return;
      if (!m_patches.contains(id) && m_patches.size() >= 4096) {
        fail(*m_pending, "Message history changed too much while loading. Try again.");
        return;
      }
      auto& combined = m_patches[id];
      auto merge = [](auto& target, const auto& source) {
        if (source)
          target = source;
      };
      merge(combined.content, patch.content);
      merge(combined.attachments, patch.attachments);
      merge(combined.embeds, patch.embeds);
      merge(combined.mentions, patch.mentions);
      merge(combined.reply, patch.reply);
      merge(combined.reactions, patch.reactions);
    }
    // Apply only contiguous pages. Existing live patches/tombstones win over
    // REST, including messages changed while the request was in flight.
    bool apply(
        const discord::HistoryQuery& query, std::vector<discord::Message>& messages, std::vector<discord::Message> page,
        const std::map<std::string, std::optional<discord::Message>>& live
    ) {
      using enum discord::HistoryDirection;
      if (!accepts(query))
        return false;
      const auto rawCount = page.size();
      std::erase_if(page, [&](const auto& message) {
        return !discord::validSnowflake(message.id)
            || (query.direction == Older && !less(message.id, query.cursor))
            || (query.direction == Newer && !less(query.cursor, message.id));
      });
      const bool advanced = !page.empty();
      sort(page);
      if (!page.empty()) {
        if (query.direction == Older)
          m_olderCursor = page.front().id;
        else if (query.direction == Newer)
          m_newerCursor = page.back().id;
      }
      if (query.direction != Latest && rawCount && !advanced) {
        fail(query, "Message history did not advance. Try again.");
        return false;
      }
      if (query.direction == Latest) {
        // Keep a contiguous cached prefix only when the latest page overlaps it.
        bool overlaps = false;
        for (const auto& message : page)
          if (std::ranges::find(messages, message.id, &discord::Message::id) != messages.end()) {
            overlaps = true;
            break;
          }
        if (!m_initialized || !overlaps) {
          messages.clear();
          m_olderEnd = rawCount < discord::messagePageSize;
        }
        m_newerEnd = true;
      } else if (query.direction == Older)
        m_olderEnd = rawCount < discord::messagePageSize;
      else
        m_newerEnd = rawCount < discord::messagePageSize;
      for (auto& message : page) {
        if (const auto changed = live.find(message.id); changed != live.end()) {
          if (!changed->second)
            continue;
          message = *changed->second;
        }
        const auto at = std::ranges::find(messages, message.id, &discord::Message::id);
        if (at == messages.end())
          messages.push_back(std::move(message));
        else
          *at = std::move(message);
      }
      if (query.direction == Latest || (query.direction == Newer && m_newerEnd))
        for (const auto& [id, changed] : live) {
          if (!changed)
            std::erase_if(messages, [&](const auto& message) { return message.id == id; });
          else if (messages.empty() || !less(id, messages.front().id)) {
            const auto at = std::ranges::find(messages, id, &discord::Message::id);
            if (at == messages.end())
              messages.push_back(*changed);
            else
              *at = *changed;
          }
        }
      for (auto& message : messages)
        if (const auto patch = m_patches.find(message.id); patch != m_patches.end())
          patch->second.apply(message);
      sort(messages);
      trim(messages, query.direction == Older);
      if (query.direction == Latest && !messages.empty()) {
        m_olderCursor = messages.front().id;
        m_newerCursor = messages.back().id;
      }
      m_pending.reset();
      m_patches.clear();
      m_initialized = true;
      m_error.clear();
      return true;
    }
    void trim(std::vector<discord::Message>& messages, bool keepOlder = false) {
      if (messages.size() <= windowLimit)
        return;
      if (keepOlder) {
        messages.resize(windowLimit);
        m_newerEnd = false;
        m_newerCursor = messages.back().id;
      } else {
        messages.erase(messages.begin(), messages.end() - windowLimit);
        m_olderEnd = false;
        m_olderCursor = messages.front().id;
      }
    }
    static bool less(const std::string& a, const std::string& b) {
      return a.size() != b.size() ? a.size() < b.size() : a < b;
    }
    static void sort(std::vector<discord::Message>& messages) {
      std::ranges::sort(messages, [](const auto& a, const auto& b) { return less(a.id, b.id); });
    }

  private:
    std::optional<discord::HistoryQuery> m_pending;
    std::uint64_t m_serial = 0;
    bool m_olderEnd = false, m_newerEnd = true, m_initialized = false;
    std::string m_error;
    std::string m_olderCursor, m_newerCursor;
    std::map<std::string, discord::MessagePatch> m_patches;
    discord::HistoryDirection m_failed = discord::HistoryDirection::Older;
  };
} // namespace app
