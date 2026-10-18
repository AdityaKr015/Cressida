#pragma once

#include "render/scene/node.h"
#include "ui/controls/button.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace app {
  // Application form composition shared by independent server settings pages.
  // All callbacks build retained controls on the owning UI thread.
  struct ServerSettingsForm {
    using Json = nlohmann::json;
    using Choices = std::vector<std::pair<std::string, Json>>;
    Node& node;
    float& y;
    float width;
    std::function<void(const std::string&)> section;
    std::function<void(const std::string&, const std::string&)> detail;
    std::function<std::pair<float, float>(const std::string&, const std::string&, float)> rowControl;
    std::function<void(const std::string&, const std::string&, const std::string&, float, int)> fieldControl;
    std::function<void(const std::string&, const std::string&, const std::string&, Choices)> choose;
    std::function<void(
        const std::string&, const std::string&, const std::string&, std::vector<std::string>, std::size_t,
        std::function<void(std::size_t)>, bool
    )>
        chooseControl;
    std::function<void(const std::string&, const std::string&, const std::string&)> toggle;
    std::function<
        void(const std::string&, const std::string&, const std::string&, bool, bool, std::function<void(bool)>)>
        toggleCallback;
    std::function<void(const std::string&, const std::string&, const std::string&, int)> channelControl;
    std::function<void(const std::string&, const std::string&, const std::string&)> upload;
    std::function<void(const std::string&, const std::string&, const std::string&)> lineList;
    std::function<void(const std::string&, const std::string&, bool)> channelsAndRoles;
    std::function<void(const std::string&, const std::string&, const std::string&, bool)> dateControl;
    std::function<void(const std::string&, const std::string&, const std::string&)> colorField;
    std::function<void(Node&, float, float, float, const std::string&, const std::string&, ButtonVariant, bool)>
        buttonControl;
    std::function<Json(const std::string&, Json)> valueControl;

    auto row(const std::string& title, const std::string& description = {}, float trailing = 0) {
      return rowControl(title, description, trailing);
    }
    void field(
        const std::string& title, const std::string& key, const std::string& description, float height = 32,
        int kind = 0
    ) {
      fieldControl(title, key, description, height, kind);
    }
    void chooseCallback(
        const std::string& key, const std::string& title, const std::string& description,
        std::vector<std::string> options, std::size_t selected, std::function<void(std::size_t)> callback,
        bool enabled = true
    ) {
      chooseControl(key, title, description, std::move(options), selected, std::move(callback), enabled);
    }
    void channel(const std::string& title, const std::string& key, const std::string& description, int type = 0) {
      channelControl(title, key, description, type);
    }
    void dateTimeField(
        const std::string& title, const std::string& key, const std::string& description, bool optional = false
    ) {
      dateControl(title, key, description, optional);
    }
    void button(
        const std::string& title, const std::string& action, ButtonVariant variant = ButtonVariant::Default,
        float width = 128, bool enabled = true
    ) {
      buttonControl(node, 0, y, width, title, action, variant, enabled);
      y += 38;
    }
    Json value(const std::string& key, Json fallback = Json()) { return valueControl(key, std::move(fallback)); }
  };
} // namespace app
