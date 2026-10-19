#pragma once

#include <cstdint>
#include <functional>

enum class KeybindAction : std::uint8_t {
  Validate = 0,
  Cancel = 1,
  Left = 2,
  Right = 3,
  Up = 4,
  Down = 5,
  TabNext = 6,
  TabPrevious = 7,
  Delete = 8,
  Copy = 9,
  Save = 10,
};

// Application-configured key matchers for retained native controls.
namespace KeybindMatcher {

  using Matcher = std::function<bool(std::uint32_t sym, std::uint32_t modifiers)>;

  void setMatcher(KeybindAction action, Matcher matcher);
  bool matches(KeybindAction action, std::uint32_t sym, std::uint32_t modifiers);

} // namespace KeybindMatcher
