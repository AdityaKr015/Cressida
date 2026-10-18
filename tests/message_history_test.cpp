#include "app/message_history.h"
#include "app/timeline_anchor.h"

#include <iostream>
#include <stdexcept>

int main() {
  using enum api::HistoryDirection;
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  auto page = [](int first, int count) {
    std::vector<api::Message> result;
    for (int i = 0; i < count; ++i)
      result.push_back({std::to_string(first + i), "10", "Maya", "A message"});
    return result;
  };
  try {
    require(
        api::messageHistoryPath("10", {Older, "100"}) == "/channels/10/messages?limit=100&before=100"
            && api::messageHistoryPath("10", {Newer, "200"}) == "/channels/10/messages?limit=100&after=200"
            && api::messageHistoryPath("10", {}) == "/channels/10/messages?limit=100",
        "Build bounded, mutually exclusive history queries"
    );
    require(
        api::messageHistoryPath("../token", {}).empty()
            && api::messageHistoryPath("10", {Older, "1&limit=999"}).empty()
            && api::messageHistoryPath("10", {Latest, "100"}).empty(),
        "Reject invalid history routes locally"
    );
    app::MessageHistory history;
    std::vector<api::Message> messages;
    auto query = *history.begin(Latest, messages);
    require(history.apply(query, messages, page(2000, 100), {}), "Apply initial history");
    require(
        history.atLatest() && !history.olderEnd() && messages.size() == 100, "Initial full page permits older reads"
    );
    for (int start = 1900; start >= 0; start -= 100) {
      query = *history.begin(Older, messages);
      require(!history.begin(Older, messages), "Only one page may be in flight");
      auto older = page(start, 100);
      require(history.apply(query, messages, std::move(older), {}), "Apply older page after scrolling upward");
      require(
          messages.front().id == std::to_string(start) && messages.size() <= 1000,
          "Keep loading past the memory limit by sliding the window toward older history"
      );
    }
    require(
        !history.atLatest() && messages.back().id == "999", "Trimmed newer rows must not be treated as the present"
    );
    query = *history.begin(Older, messages);
    require(
        history.apply(query, messages, {}, {}) && history.olderEnd() && !history.begin(Older, messages),
        "An empty page ends older loading without repeated requests"
    );
    for (int start = 1000; start <= 2000; start += 100) {
      query = *history.begin(Newer, messages);
      require(query.cursor == std::to_string(start - 1), "Use the adjacent newer cursor");
      require(history.apply(query, messages, page(start, 100), {}), "Scroll back toward newer history");
    }
    query = *history.begin(Newer, messages);
    require(
        history.apply(query, messages, {}, {}) && history.atLatest(), "Confirm the newest edge before automatic reads"
    );
    query = *history.begin(Older, messages);
    history.fail(query, "Synthetic network error");
    require(!history.error().empty() && !history.begin(Older, messages), "Errors stay visible without retry loops");
    query = *history.begin(Older, messages, true);
    const auto stale = query;
    history.reset();
    query = *history.begin(Latest, messages);
    require(
        !history.apply(stale, messages, page(1, 100), {}),
        "Navigation rejects stale responses even for the same channel"
    );
    std::map<std::string, std::optional<api::Message>> live;
    live["105"] = std::nullopt;
    live["106"] = page(106, 1).front();
    live["106"]->content = "Edited while fetching";
    require(history.apply(query, messages, page(100, 100), live), "Merge live changes into REST history");
    require(
        std::ranges::find(messages, "105", &api::Message::id) == messages.end()
            && std::ranges::find(messages, "106", &api::Message::id)->content == "Edited while fetching",
        "Pages cannot resurrect deleted messages or undo in-flight edits"
    );
    query = *history.begin(Older, messages);
    live.clear();
    for (const auto& message : page(0, 100))
      live[message.id] = std::nullopt;
    require(
        history.apply(query, messages, page(0, 100), live), "A fully deleted page still advances the server cursor"
    );
    query = *history.begin(Older, messages);
    require(query.cursor == "0", "Deleted pages must not request the same boundary forever");
    history.fail(query, "Synthetic cancellation");
    history.reset();
    query = *history.begin(Latest, messages);
    api::MessagePatch unknownEdit;
    unknownEdit.content = "Edited before this row was loaded";
    history.changed("123", unknownEdit);
    unknownEdit = {};
    unknownEdit.embeds = std::vector<api::Embed>{};
    history.changed("123", unknownEdit);
    require(
        history.apply(query, messages, page(100, 100), {}), "Apply history with patches to previously unloaded rows"
    );
    require(
        std::ranges::find(messages, "123", &api::Message::id)->content == "Edited before this row was loaded",
        "Partial in-flight edits merge even when the original message was not yet in the window"
    );
    query = *history.begin(Older, messages);
    for (int i = 0; i <= 4096; ++i)
      history.changed(std::to_string(i), {});
    require(
        !history.busy() && !history.error().empty() && history.failedDirection() == Older,
        "An overflowing live patch journal cancels the page safely and retains an explicit retry"
    );

    app::TimelineAnchor anchor;
    anchor.remember({{"10", 100}, {"11", 150}}, 200);
    require(
        anchor.correction({{"10", 85}, {"11", 135}}, 185) == 0,
        "User downward wheel movement must not be cancelled by anchoring"
    );
    require(
        anchor.correction({{"10", 55}, {"11", 105}}, 215) == 60,
        "Appending live messages preserves the visible row and upward user motion"
    );
    require(
        anchor.correction({{"10", 115}, {"11", 165}}, 215) == 0,
        "Prepending older rows does not change a bottom-relative anchor"
    );
    require(
        anchor.correction({{"11", 110}}, 200) == 40, "Deleting the first visible row anchors its surviving successor"
    );
    anchor.reset();
    require(!anchor.correction({{"10", 100}}, 200), "Navigation clears prior anchors");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
