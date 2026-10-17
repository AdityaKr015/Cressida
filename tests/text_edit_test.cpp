#include "app/text_edit.h"

#include <iostream>
#include <stdexcept>

int main() {
  try {
    auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    app::TextEdit edit;
    std::string text = "one\ntwo 👩‍💻";
    edit.end(text);
    edit.erase(text, false);
    require(text == "one\ntwo ", "Backspace removes a complete joined emoji");
    edit.replace(text, "🌿");
    edit.move(text, false, true);
    require(edit.selection(text) == "🌿", "Shift movement selects a whole grapheme");
    edit.replace(text, "second\nthird");
    require(text == "one\ntwo second\nthird", "Replacing a selection preserves multiline text");
    edit.anchor = 0;
    edit.caret = 3;
    edit.replace(text, "first");
    require(text.starts_with("first\ntwo") && edit.caret == 5, "Insertion replaces selection and places caret");
    edit.end(text);
    edit.move(text, false, false);
    edit.erase(text, true);
    require(text.ends_with("thir"), "Delete removes the following character");
    edit.anchor = 0;
    edit.caret = text.size();
    edit.erase(text, false);
    require(text.empty() && edit.caret == 0 && edit.anchor == 0, "Deleting an entire selection resets both ends");
    edit.erase(text, false);
    require(text.empty(), "Backspace at start is harmless");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
