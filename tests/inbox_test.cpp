#include "api/inbox.h"

#include <iostream>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* label) {
    if (!ok)
      throw std::runtime_error(label);
  };
  const auto rejects = [&](const auto& fn) {
    try {
      fn();
      return false;
    } catch (const std::exception&) {
      return true;
    }
  };
  using Json = nlohmann::json;
  try {

    const api::InboxQuery bookmarks{.kind = api::InboxKind::Bookmarks};
    const api::InboxQuery reminders{.kind = api::InboxKind::Reminders};
    require(
        api::inboxMissingCollection(bookmarks, 404, 10133) && api::inboxMissingCollection(reminders, 404, 10133),
        "A missing saved-message collection is an empty initial saved page"
    );
    for (const auto kind :
         {api::InboxKind::Save, api::InboxKind::Remove, api::InboxKind::Dismiss, api::InboxKind::Mentions,
          api::InboxKind::Unread})
      require(
          !api::inboxMissingCollection({.kind = kind}, 404, 10133),
          "Missing-collection handling cannot hide a failed write or unrelated read"
      );
    require(
        !api::inboxMissingCollection(bookmarks, 404, 0)
            && !api::inboxMissingCollection(bookmarks, 404, 10008)
            && !api::inboxMissingCollection(bookmarks, 401, 10133)
            && !api::inboxMissingCollection(bookmarks, 403, 10133)
            && !api::inboxMissingCollection(bookmarks, 429, 10133)
            && !api::inboxMissingCollection(bookmarks, 500, 10133)
            && !api::inboxMissingCollection(
                {.kind = api::InboxKind::Bookmarks, .before = "2026-10-21T10:00:00Z"}, 404, 10133
            ),
        "Unknown routes, message failures, auth, rate limits, server failures and older-page failures remain errors"
    );
    api::InboxQuery query;
    require(
        api::inboxHttpRequest(query)->path == "/users/@me/mentions?limit=25&roles=true&everyone=true",
        "Mentions use the account endpoint with explicit filters"
    );
    query.guild = "10";
    query.before = "999";
    query.roles = false;
    require(
        api::inboxHttpRequest(query)->path.ends_with("&guild_id=10&before=999"),
        "Mention pagination retains server scope"
    );
    query.guild = "../guild";
    require(!api::inboxHttpRequest(query), "Route components reject path injection");
    query = {.kind = api::InboxKind::Bookmarks, .before = "2026-10-21T10:00:00Z"};
    require(
        api::inboxHttpRequest(query)->path.ends_with("&before=2026-10-21T10%3A00%3A00Z"),
        "Bookmark cursors encode ISO timestamps"
    );
    query.before = "invalid";
    require(!api::inboxHttpRequest(query), "Invalid bookmark cursors cannot issue requests");
    query = {.kind = api::InboxKind::Reminders};
    require(
        api::inboxHttpRequest(query)->path.ends_with("?bookmark_ids=true"),
        "Reminders request the current split response"
    );
    query = {.kind = api::InboxKind::Unread, .channel = "10", .before = "20"};
    require(
        api::inboxHttpRequest(query)->path == "/channels/10/messages?limit=3&after=20",
        "Unread previews stay bounded and request after the read cursor"
    );
    query = {.kind = api::InboxKind::Save, .channel = "10", .message = "20"};
    require(
        api::inboxHttpRequest(query)->method == "PUT" && api::inboxHttpRequest(query)->body.at("due_at").is_null(),
        "Saving a bookmark clears any reminder explicitly"
    );
    query.due = "2026-10-21T12:00:00Z";
    require(api::inboxHttpRequest(query)->body.at("due_at") == query.due, "Reminder bodies carry only their due date");
    query.due = "not a time";
    require(!api::inboxHttpRequest(query), "Malformed reminders cannot mutate account state");
    query = {.kind = api::InboxKind::Dismiss, .message = "20"};
    require(
        api::inboxHttpRequest(query)->method == "DELETE"
            && api::inboxHttpRequest(query)->path == "/users/@me/mentions/20",
        "Dismissal targets only the selected mention"
    );
    auto saved = Json::parse(
        R"JSON({"message":{"id":"20","channel_id":"10","content":"Saved","author":{"id":"30","username":"Maya"}},
        "save_data":{"channel_id":"10","message_id":"20","guild_id":"40","saved_at":"2026-10-21T10:00:00Z","due_at":null}})JSON"
    );
    const auto bookmark = api::parseSavedMessage(saved);
    require(
        bookmark.channel == "10" && bookmark.guild == "40" && bookmark.message->content == "Saved",
        "Saved messages preserve navigation identity and rich content"
    );
    saved["message"]["id"] = "21";
    require(rejects([&] { api::parseSavedMessage(saved); }), "Mismatched saved content is rejected");
    saved["message"] = nullptr;
    require(!api::parseSavedMessage(saved).message, "Deleted and inaccessible saved messages keep their summaries");
    query = {.kind = api::InboxKind::Bookmarks};
    auto page = api::parseInboxPage(Json{{"results", Json::array({saved, saved})}, {"has_more", true}}, query);
    require(
        page.saved.size() == 1 && page.more && page.before == "2026-10-21T10:00:00Z",
        "Bookmark pages deduplicate messages and retain server continuation"
    );
    query.before = page.before;
    require(
        rejects([&] { api::parseInboxPage(Json{{"results", Json::array({saved})}, {"has_more", true}}, query); }),
        "Nonadvancing pagination cannot loop"
    );
    saved["save_data"]["due_at"] = "2026-10-21T12:00:00Z";
    page = api::parseInboxPage(
        Json{{"reminders", Json::array({saved})}, {"bookmark_ids", Json::array({"21"})}},
        {.kind = api::InboxKind::Reminders}
    );
    require(
        page.saved.size() == 1 && !page.more, "Current reminder envelopes do not confuse bookmark IDs with messages"
    );
    page = api::parseInboxPage(
        Json::array(
            {{{"id", "99"}, {"channel_id", "10"}, {"guild_id", "40"}, {"content", "one"}},
             {{"id", "100"}, {"channel_id", "10"}, {"guild_id", "40"}, {"content", "two"}}}
        ),
        {}
    );
    require(
        page.before == "99" && page.guilds.at("10") == "40" && page.messages.front().id == "100",
        "Mention cursors compare numeric snowflakes and preserve guild context"
    );
    require(
        rejects([&] {
          api::parseInboxPage(
              Json::array({{{"id", "20"}, {"channel_id", "11"}}}), {.kind = api::InboxKind::Unread, .channel = "10"}
          );
        }),
        "Previews cannot leak another channel into a card"
    );
    require(!api::inboxTime("2026-02-30T10:00:00Z"), "Impossible dates fail validation");
    const auto time = api::inboxTime("2026-10-21T10:00:00Z");
    require(time && api::inboxTime(api::inboxTimestamp(*time)) == time, "Reminder timestamps round-trip in UTC");
    std::cout << "Inbox routes, response identities, split pages, cursors and reminders passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
