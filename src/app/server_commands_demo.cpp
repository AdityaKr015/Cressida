#include "app/client_application.h"
#include "ui/controls/select.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  void ClientApplication::demoServerCommandsStep() {
    using Json = nlohmann::json;
    using Section = api::GuildSettingsSection;
    using Verb = api::GuildSettingsVerb;
    const auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    auto& state = m_serverSettings;
    auto& view = m_serverCommands;
    const auto click = [&](const std::string& id) {
      const auto at = m_widgets.find(id);
      require(at != m_widgets.end() && at->second->parent(), "The command editor's retained control must be mounted");
      const auto* node = at->second;
      float x = node->width() / 2, y = node->height() / 2;
      for (const auto* parent = node; parent; parent = parent->parent()) {
        x += parent->x();
        y += parent->y();
      }
      m_window.motion(x, y + m_window.titlebarHeight() - layout::Window::titlebarHeight);
      m_window.click(x, y + m_window.titlebarHeight() - layout::Window::titlebarHeight, 272);
      m_window.released();
    };
    const auto action = [&](const std::string& id) {
      require(serverCommandsAction(id), "The command permission action must be handled by its dedicated editor");
    };
    const auto key = [&](std::uint32_t symbol) { m_window.key({.symbol = symbol}); };
    const auto hasPermission = [&](const Json& permissions, const std::string& id, int type, bool allow) {
      return permissions.is_array() && std::ranges::any_of(permissions, [&](const auto& permission) {
               return permission.value("id", "") == id
                   && permission.value("type", 0) == type
                   && permission.value("permission", !allow) == allow;
             });
    };
    const auto freshSave = [&] {
      api::GuildSettingsRequest read;
      read.section = Section::CommandPermissions;
      read.values = {{"application_id", view.application}};
      require(requestServerSettings(read), "The offline command save must queue its bounded fresh permission read");
      view.preparing = state.pending.at(state.serial);
      state.saving = true;
      return *view.preparing;
    };
    const auto result = [&](const api::GuildSettingsRequest& request, Json data, std::string error = {}) {
      api::Event event;
      event.operation = api::Operation::GuildSettings;
      event.session = m_session;
      event.guildSettingsRequest = request;
      event.guildSettings = std::move(data);
      event.error = std::move(error);
      serverSettingsResult(event);
    };
    const auto latestWrite = [&] {
      const auto at = std::ranges::find_if(state.pending, [](const auto& item) {
        return item.second.section == Section::CommandPermissions && item.second.verb == Verb::Update;
      });
      require(at != state.pending.end(), "The command editor must retain the exact explicit write identity");
      return at->second;
    };
    const auto step = m_demoInteractionStep++;
    std::clog << "Server command permissions replay step " << step << '\n';
    if (step == 0) {
      selectServerSettingsPage("commands", true);
      require(
          view.application == "500" && view.command == "500" && view.indexLoaded && !state.dirty(),
          "The native application permission page starts on its installed app defaults"
      );
    } else if (step == 1) {
      click("server-select:commands:command");
      require(
          m_selectRequest && m_selectOwner == "server-select:commands:command",
          "The retained command selector opens the real native popup host"
      );
    } else if (step == 2) {
      key(XKB_KEY_Down);
      key(XKB_KEY_Return);
      require(
          !m_selectRequest && view.command == "600" && !state.dirty(),
          "Native popup keyboard selection opens the chosen command without a mutation"
      );
      m_scrolling.stop(state.scroll);
      state.scroll = 300;
    } else if (step == 3) {
      // The editor deliberately scrolls this command's explicit override into
      // view before clicking its native Allow/Deny/Remove selector.
      click("server-select:commands:override:1:100");
      require(
          m_selectRequest && m_selectOwner == "server-select:commands:override:1:100",
          "An explicit override uses a working retained native permission selector"
      );
    } else if (step == 4) {
      key(XKB_KEY_Up);
      key(XKB_KEY_Return);
      require(
          state.dirty() && hasPermission(state.draft["permissions"], "100", 1, true),
          "Choosing Allow changes only the local permission draft"
      );
      action("commands:command:601");
      require(view.confirming && view.command == "600", "Switching commands protects unsaved permission changes");
    } else if (step == 5) {
      click("server-button:commands:keep-editing");
      require(!view.confirming && state.dirty(), "Keep editing retains the unsaved command override");
      action("reset");
      require(!state.dirty(), "Reset restores the acknowledged command permissions");
      action("commands:command:601");
      require(
          view.command == "601" && view.baseline.empty(), "A command without overrides follows application defaults"
      );
    } else if (step == 6) {
      view.targetType = 1;
      view.target = "50";
      action("commands:add:allow");
      const auto& draft = state.draft["permissions"];
      require(
          draft.size() == 3
              && hasPermission(draft, "50", 1, true)
              && hasPermission(draft, "100", 1, true)
              && hasPermission(draft, "99", 3, true),
          "The first individual override copies application rules before customizing the command"
      );
      action("save");
      require(
          !state.dirty() && view.baseline.size() == 3,
          "An explicit acknowledged save retains the customized command rules"
      );
      action("commands:inherit");
      require(
          view.restoringDefaults && state.draft["permissions"].empty(),
          "Restoring app defaults stages an empty command record until Save"
      );
    } else if (step == 7) {
      click("server-button:save");
      require(
          view.baseline.empty() && !state.dirty() && !view.restoringDefaults,
          "The native Save button restores inheritance after acknowledgement"
      );
      action("commands:application:501");
      require(
          view.application == "501" && view.command == "501",
          "Changing apps resets the editor to the selected app defaults"
      );
    } else if (step == 8) {
      view.targetType = 2;
      view.target = "42";
      action("commands:add:allow");
      const auto read = freshSave();
      result(
          read,
          Json::array(
              {{{"id", "501"},
                {"application_id", "501"},
                {"guild_id", "100"},
                {"permissions", Json::array({{{"id", "50"}, {"type", 1}, {"permission", false}}})}}}
          )
      );
      const auto write = latestWrite();
      require(
          write.resourceId == "501"
              && write.values["permissions"].size() == 2
              && hasPermission(write.values["permissions"], "50", 1, false)
              && hasPermission(write.values["permissions"], "42", 2, true),
          "The real fresh-read/write path preserves another administrator's unrelated override"
      );
      result(write, Json::object(), "Synthetic permission save failure");
      require(
          state.dirty() && !state.saving && !state.error.empty(), "A failed write retains the draft for explicit retry"
      );
    } else if (step == 9) {
      const auto read = freshSave();
      result(read, Json::array());
      const auto write = latestWrite();
      api::Event challenge;
      challenge.operation = api::Operation::GuildSettings;
      challenge.session = m_session;
      challenge.guildSettingsRequest = write;
      challenge.guildSettingsMfaChallenge = 77;
      challenge.guildSettingsMfaMethods = {"totp"};
      serverSettingsResult(challenge);
      require(
          state.mfaRequest && state.dirty() && state.mfaRequest->resourceId == "501",
          "An action-specific MFA challenge protects the same command and retains its exact draft"
      );
    } else if (step == 10) {
      click("server-button:mfa-cancel");
      require(!state.mfaRequest && state.dirty(), "Cancelling verification keeps the permission draft without writing");
      action("save");
      require(!state.dirty() && !state.saving, "The remaining draft can be saved again explicitly");
      closeServerSettings(true);
      prepareServerSettingsDemo();
      selectServerSettingsPage("commands", true);
    } else if (step == 11) {
      require(
          state.pending.empty() && !state.saving && !state.dirty() && !state.mfaRequest,
          "The command replay finishes with no pending mutation or verification state"
      );
      m_account.id = "43";
      state.draft["permissions"] = Json::array();
      action("save");
      require(
          state.pending.empty() && state.dirty() && !state.error.empty(),
          "Losing management permissions blocks a raced Save and retains the unsaved values"
      );
      m_account.id = "42";
      state.draft = Json::object();
      state.error.clear();
      std::clog << "Offline server command permissions replay passed\n";
    }
    if (step < 11)
      m_window.invalidate();
  }
} // namespace app
