#include "app/server_commands.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

int main() {
  using Json = nlohmann::json;
  const auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  const auto permission = [](std::string id, int type, bool allow) {
    return Json{{"id", std::move(id)}, {"type", type}, {"permission", allow}};
  };
  const auto throws = [&](auto function, const char* message) {
    bool failed = false;
    try {
      function();
    } catch (const std::exception&) {
      failed = true;
    }
    require(failed, message);
  };
  try {
    const auto baseline = Json::array({permission("10", 1, true), permission("20", 3, false)});
    auto draft = app::editCommandPermission(baseline, "10", 1, false);
    require(
        std::ranges::any_of(
            draft, [](const auto& value) { return value["id"] == "10" && value["permission"] == false; }
        ),
        "Deny is a real explicit override, distinct from removing an override"
    );
    draft = app::editCommandPermission(draft, "20", 3, std::nullopt);
    draft = app::editCommandPermission(draft, "30", 2, true);
    const auto current =
        Json::array({permission("10", 1, true), permission("20", 3, false), permission("40", 1, false)});
    const auto merged = app::mergeCommandPermissionEdits(current, baseline, draft);
    require(
        merged.size() == 3
            && std::ranges::none_of(merged, [](const auto& value) { return value["id"] == "20"; })
            && std::ranges::any_of(
                merged, [](const auto& value) { return value["id"] == "40" && value["permission"] == false; }
            )
            && std::ranges::any_of(
                merged, [](const auto& value) { return value["id"] == "10" && value["permission"] == false; }
            ),
        "A save merges only edited subjects into a fresh permission snapshot, preserving another administrator's "
        "entries"
    );
    const auto remoteChanged = Json::array({permission("10", 1, false), permission("20", 3, true)});
    const auto unchanged = app::mergeCommandPermissionEdits(remoteChanged, baseline, baseline);
    require(
        unchanged == remoteChanged, "Unedited subjects keep fresh server values instead of the editor's older baseline"
    );
    require(
        app::mergeCommandPermissionEdits(current, baseline, Json::array(), true).empty(),
        "Use application defaults explicitly clears all command-specific overrides"
    );
    require(
        app::allCommandChannelsId("1000000000000000000") == "999999999999999999"
            && app::allCommandChannelsId("18446744073709551615") == "18446744073709551614",
        "The All Channels sentinel subtracts one without floating-point conversion"
    );
    require(
        app::allCommandChannelsId("invalid").empty()
            && app::allCommandChannelsId("99999999999999999999").empty()
            && app::allCommandChannelsId("0").empty(),
        "Reject malformed and overflowing guild IDs"
    );
    const auto sameId = app::editCommandPermission(Json::array({permission("10", 1, true)}), "10", 2, false);
    require(sameId.size() == 2, "Permission identities include both target type and ID");
    throws(
        [&] { app::editCommandPermission(Json::array(), "bad", 1, true); },
        "Reject invalid target IDs before building an account mutation"
    );
    throws([&] { app::editCommandPermission(Json::array(), "10", 4, true); }, "Reject unsupported target types");
    throws(
        [&] {
          app::mergeCommandPermissionEdits(
              Json::array({permission("10", 1, true), permission("10", 1, false)}), baseline, draft
          );
        },
        "Duplicate server subjects cannot silently collapse during a save"
    );
    auto future = permission("10", 1, true);
    future["future_field"] = "preserve";
    throws(
        [&] { app::mergeCommandPermissionEdits(Json::array({future}), baseline, draft); },
        "A future unknown permission structure cannot be overwritten by this editor"
    );
    auto full = Json::array();
    for (unsigned i = 1; i <= 100; ++i)
      full.push_back(permission(std::to_string(i), 1, true));
    throws(
        [&] { app::editCommandPermission(full, "101", 1, true); }, "The editor enforces Discord's 100-override limit"
    );
    require(
        app::editCommandPermission(full, "1", 1, false).size() == 100,
        "Existing subjects remain editable when the permission record reaches its limit"
    );
    std::cout << "server command permission tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
