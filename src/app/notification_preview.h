#pragma once

#include "api/models.h"
#include "app/message_preferences.h"
#include "app/system_message.h"

#include <functional>
#include <string>

namespace app {
  using NotificationName = std::function<std::string(char, const std::string&)>;
  // Notifications carry plain text. Resolve protocol tokens before the desktop
  // worker performs capability-aware escaping; inserted names are never reparsed.
  inline std::string desktopMessagePreview(const api::Message& message, const NotificationName& name) {
    if (message.type == 7)
      return joinNoticeText(message, name('@', message.authorId));
    if (message.type == 1 || message.type == 2) {
      std::string people;
      for (const auto& [id, fallback] : message.mentions) {
        if (!people.empty())
          people += ", ";
        people += name('@', id);
      }
      if (!people.empty())
        return name('@', message.authorId) + (message.type == 1 ? " added " : " removed ") + people + '.';
    }
    if (message.forwarded && message.content.empty())
      return message.forwarded->attachments.empty() ? "Forwarded a message" : "Forwarded an attachment";
    if (message.content.empty())
      return message.attachments.empty() && message.embeds.empty() ? "New message" : "Sent an attachment";
    const auto text = notificationMessagePreview(message.content);
    std::string result;
    std::size_t code = 0;
    for (std::size_t at = 0; at < text.size();) {
      if (text[at] == '\\' && at + 1 < text.size()) {
        result.append(text, at, 2);
        at += 2;
        continue;
      }
      if (text[at] == '`') {
        std::size_t count = 1;
        while (at + count < text.size() && text[at + count] == '`')
          ++count;
        if (!code)
          code = count;
        else if (code == count)
          code = 0;
        result.append(text, at, count);
        at += count;
        continue;
      }
      if (!code && text[at] == '<' && at + 2 < text.size() && (text[at + 1] == '@' || text[at + 1] == '#')) {
        char kind = text[at + 1];
        std::size_t start = at + 2;
        if (kind == '@' && start < text.size() && (text[start] == '&' || text[start] == '!')) {
          if (text[start] == '&')
            kind = '&';
          ++start;
        }
        const auto end = text.find('>', start);
        if (end != std::string::npos && end - start <= 20) {
          const auto id = text.substr(start, end - start);
          if (api::validSnowflake(id)) {
            auto resolved = name(kind, id);
            if (resolved.empty())
              resolved = kind == '&' ? "unknown-role" : kind == '#' ? "unknown-channel" : "unknown-user";
            result += kind == '#' ? '#' : '@';
            result += resolved;
            at = end + 1;
            continue;
          }
        }
      }
      result += text[at++];
    }
    return result;
  }
} // namespace app
