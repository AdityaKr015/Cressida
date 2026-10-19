#include "app/autocomplete.h"
#include "app/discord_link.h"
#include "app/message_layout.h"

#include <algorithm>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  const auto link = app::discordLink("https://discord.com/channels/123/456/789?jump=true");
  require(link && *link == app::DiscordLink{"123", "456", "789"}, "Parse channel and message destinations");
  require(app::discordLink("https://PTB.Discord.com/channels/@me/456/").has_value(), "Support DM and official hosts");
  for (const auto bad :
       {"https://discord.com.evil/channels/123/456", "https://discord.com@evil/channels/123/456",
        "https://discord.com/channels/123/456/789/101", "https://discord.com/channels/123/0",
        "https://discord.com/channels/123/18446744073709551616", "https://discord.com/channels/123/x",
        "https://evil/channels/123/456", "javascript://discord.com/channels/123/456"})
    require(!app::discordLink(bad), "Reject unsafe or malformed destinations");
  const auto query = app::completionQuery("See #gene", 9, 9);
  require(query && query->trigger == '#' && query->text == "gene", "Channel completion has its own token");
  for (const std::string bad : {"https://site/#gene", "`#gene", "<#456>"})
    require(!app::completionQuery(bad, bad.size(), bad.size()), "Do not complete code, URLs or protocol mentions");

  const app::MessageMeasure measure = [](std::string_view text, const app::MessageStyle&) {
    return text.size() * 7.0F;
  };
  app::MessageOptions options;
  options.channelMention = [](std::string_view, std::string_view channel) -> app::MessageOptions::ChannelMention {
    if (channel == "456")
      return {"general", "https://discord.com/channels/123/456"};
    if (channel == "789")
      return {"general > thread", "https://discord.com/channels/123/789"};
    return {"unknown", {}};
  };
  const auto layout = app::layoutMessage(
      "<@123> <@!124> <@&125> <#456> <#789> <#999>", 1000, measure, 14, {{"123", "<safe>"}, {"125", "Role"}}, options
  );
  require(layout.plainText.find("@<safe>") != std::string::npos, "Mention display names stay literal");
  require(layout.plainText.find("#general > thread") != std::string::npos, "Threads retain their parent label");
  require(layout.plainText.find("#unknown") != std::string::npos, "Unavailable channels have a generic label");
  require(
      std::ranges::any_of(layout.runs, [](const auto& run) { return run.style.userMention == "123"; }),
      "Users carry typed profile targets"
  );
  require(
      std::ranges::none_of(layout.runs, [](const auto& run) { return run.style.userMention == "125"; }),
      "Role mentions must not open user profiles"
  );
  const auto url = app::layoutMessage("https://discord.com/channels/123/789/555", 1000, measure, 14, {}, options);
  require(
      std::ranges::any_of(
          url.runs,
          [](const auto& run) {
            return run.channelChip && std::ranges::any_of(run.channelChip->parts, [](const auto& part) {
                     return part.kind == app::MessageRun::ChannelChip::Part::Text && part.value == "general > thread";
                   });
          }
      ),
      "Bare channel URLs render the resolved channel label inside a pill"
  );
  require(
      std::ranges::any_of(
          url.runs, [](const auto& run) { return run.style.link == "https://discord.com/channels/123/789/555"; }
      ),
      "Message links retain their jump target"
  );
  const auto hidden = app::layoutMessage("<#999>", 1000, measure, 14, {{"999", "secret name"}}, options);
  require(
      hidden.plainText.starts_with("#unknown")
          && hidden.plainText.find("secret") == std::string::npos
          && std::ranges::all_of(hidden.runs, [](const auto& run) { return run.style.link.empty(); }),
      "Permission-aware resolution overrides untrusted channel names"
  );
  const auto code = app::layoutMessage("`<@123> <#456>`", 1000, measure, 14, {}, options);
  require(
      std::ranges::all_of(
          code.runs, [](const auto& run) { return run.style.userMention.empty() && run.style.link.empty(); }
      ),
      "Code has no mention actions"
  );
  const auto spoiler = app::layoutMessage("||<@123>||", 1000, measure, 14, {}, options);
  require(
      std::ranges::any_of(
          spoiler.runs, [](const auto& run) { return run.style.spoiler && run.style.userMention == "123"; }
      ),
      "Spoiler targets remain concealed until revealed"
  );
  options.channelMention = [](std::string_view, std::string_view) -> app::MessageOptions::ChannelMention {
    return {
        "forum > discussion",
        "https://discord.com/channels/123/789",
        "Server <safe>",
        "cached-icon",
        "discussion",
        "forum",
        "message",
        "messages",
        true
    };
  };
  for (const float width : {32.0F, 72.0F, 180.0F, 500.0F}) {
    const auto chip = app::layoutMessage("https://discord.com/channels/123/789/555", width, measure, 14, {}, options);
    require(
        chip.runs.size() == 1 && chip.runs.front().channelChip.has_value(),
        "Channel URLs are atomic measured pills, not underlined text fragments"
    );
    const auto& run = chip.runs.front();
    require(
        run.x >= 0 && run.x + run.width <= width && run.style.link.ends_with("/555"),
        "Pills stay within narrow messages and retain the original message target"
    );
    for (const auto& part : run.channelChip->parts)
      require(part.x >= 0 && part.x + part.width <= run.width, "Every breadcrumb segment fits inside its pill");
    require(
        chip.plainText.starts_with("https://discord.com/channels/123/789/555"),
        "Channel URL pills preserve the original URL when copied"
    );
    if (width >= 180)
      require(
          std::ranges::any_of(
              run.channelChip->parts,
              [](const auto& part) {
                return part.kind == app::MessageRun::ChannelChip::Part::GuildIcon && part.value == "cached-icon";
              }
          )
              && std::ranges::any_of(
                  run.channelChip->parts,
                  [](const auto& part) {
                    return part.kind == app::MessageRun::ChannelChip::Part::Glyph && part.value == "messages";
                  }
              ),
          "Wide pills include the server icon and the actual forum icon"
      );
  }
  const auto masked =
      app::layoutMessage("[custom label](https://discord.com/channels/123/789)", 500, measure, 14, {}, options);
  require(
      masked.plainText.starts_with("custom label")
          && std::ranges::none_of(masked.runs, [](const auto& run) { return run.channelChip.has_value(); }),
      "Custom Markdown link labels remain custom labels"
  );
  const auto concealed = app::layoutMessage("||<#789>||", 500, measure, 14, {}, options);
  require(
      std::ranges::any_of(concealed.runs, [](const auto& run) { return run.channelChip && run.style.spoiler; }),
      "Spoiler channel pills remain concealed until revealed"
  );
}
