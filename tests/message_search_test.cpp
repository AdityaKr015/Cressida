#include "api/channel_features.h"
#include "api/message_search.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  const auto parameter = [](const api::MessageSearch& search, std::string key, std::string value) {
    return std::ranges::find(search.parameters, std::pair(std::move(key), std::move(value))) != search.parameters.end();
  };
  try {
    const auto resolve = [](std::string_view key, std::string_view value) -> std::optional<std::string> {
      if (key == "in" && value == "design")
        return "20";
      if ((key == "from" || key == "mentions") && value == "Alice Smith")
        return "30";
      return {};
    };
    auto search = api::parseMessageSearch("project notes from:\"Alice Smith\" in:design has:video", resolve);
    require(
        search
            && search.content == "project notes"
            && parameter(search, "author_id", "30")
            && parameter(search, "channel_id", "20")
            && parameter(search, "has", "video"),
        "Quoted names resolve independently of ordinary search content"
    );
    require(
        api::parseMessageSearch(search.canonical).parameters == search.parameters,
        "Canonical searches can be validated again by the backend"
    );
    auto route = api::channelPagePath({api::ChannelPage::Search, "1", "10", search.canonical});
    require(
        route == "/guilds/1/messages/search?content=project%20notes&author_id=30&channel_id=20&has=video&offset=0",
        "Search filters become distinct encoded query parameters"
    );
    search = api::parseMessageSearch("from:<@!30> mentions:<@40> in:<#20> has:image has:-video has:gif");
    require(
        search
            && search.content.empty()
            && parameter(search, "author_id", "30")
            && parameter(search, "mentions", "40")
            && parameter(search, "channel_id", "20")
            && parameter(search, "has", "-video")
            && parameter(search, "embed_type", "gif"),
        "Filter-only searches accept mention syntax, negative media types and GIF indexing"
    );
    require(
        api::channelPagePath({api::ChannelPage::Search, {}, "10", "has:file"})
            == "/channels/10/messages/search?has=file&offset=0",
        "A DM can search by attachment type without any text"
    );
    search = api::parseMessageSearch("from:30 from:30 from:40 has:media author:bot");
    require(
        search
            && parameter(search, "author_id", "30")
            && parameter(search, "author_id", "40")
            && parameter(search, "has", "image")
            && parameter(search, "has", "video")
            && parameter(search, "author_type", "bot")
            && search.parameters.size() == 5,
        "Repeated filters retain distinct values without duplicate parameters"
    );
    search = api::parseMessageSearch("https://example.com/from:30 \"from:40\" status:open");
    require(
        search
            && search.parameters.empty()
            && search.content.find("from:30") != std::string::npos
            && search.content.find("\"from:40\"") != std::string::npos,
        "URLs, quoted literal text and unknown prefixes remain ordinary content"
    );
    search = api::parseMessageSearch("from: \"Alice Smith\" has: image", resolve);
    require(
        search && parameter(search, "author_id", "30") && parameter(search, "has", "image"),
        "Spaces after a filter colon do not lose its value"
    );
    for (const auto value :
         {"", "   ", "from:", "from: has:image", "in:unknown", "has:unknown", "author:unknown", "during:2026-02-30",
          "before:2014-12-31", "before:2026-01-01 after:2026-02-01", "from:\"unfinished", "test\ntext"}) {
      const auto invalid = api::parseMessageSearch(value, resolve);
      require(!invalid && !invalid.error.empty(), "Invalid searches explain the problem and cannot form a request");
      require(
          api::channelPagePath({api::ChannelPage::Search, "1", "10", value}).empty(),
          "The transport independently rejects invalid filters"
      );
    }
    search = api::parseMessageSearch("during:2026-10-21");
    require(
        search
            && search.parameters.size() == 2
            && search.parameters[0].first == "min_id"
            && search.parameters[1].first == "max_id",
        "Date-only searches create lower and upper message bounds"
    );
    const auto minimum = std::stoull(search.parameters[0].second), maximum = std::stoull(search.parameters[1].second);
    require(
        maximum - minimum == (86400000ULL << 22) + 1,
        "A UTC day includes all message IDs through the last millisecond without crossing midnight"
    );
    require(
        api::parseMessageSearch("during:2024-02-29") && !api::parseMessageSearch("during:2025-02-29"),
        "Gregorian date validation handles leap years"
    );
    auto page = api::ChannelPageQuery{api::ChannelPage::Search, "1", "10", "has:image from:30"};
    page.offset = 25;
    require(
        api::channelPagePath(page).ends_with("has=image&author_id=30&offset=25"),
        "Pagination retains the parsed filters"
    );
    require(
        api::channelPagePath({api::ChannelPage::Search, "1", "10", "a&channel_id=999"})
            == "/guilds/1/messages/search?content=a%26channel_id%3D999&offset=0",
        "Search content cannot inject URL parameters"
    );
    require(!api::parseMessageSearch(std::string(1025, 'a')), "Search input is bounded before parsing");
    std::cout << "Message search filters, dates, safe routes and canonical resolution passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
