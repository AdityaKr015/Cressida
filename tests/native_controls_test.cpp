#include "app/brand_logo.h"
#include "app/member_list_layout.h"
#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/input/keybind_matcher.h"
#include "core/text_clipboard.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "render/text/glyph_registry.h"
#include "ui/controls/input.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include "ui/controls/toggle.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

struct Clipboard final : TextClipboard {
  std::string text;
  std::optional<std::string> clipboardText() override { return text; }
  void setClipboardText(std::string value) override { text = std::move(value); }
};
int main() {
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    std::ifstream logoFile(paths::assetPath("cressida-logo.svg"), std::ios::binary);
    const std::string logo(std::istreambuf_iterator<char>{logoFile}, std::istreambuf_iterator<char>{});
    require(
        !logo.empty() && logo.find("<path") != std::string::npos,
        "The native UI ships the original Cressida vector artwork"
    );
    for (const bool light : {false, true}) {
      const auto colors = app::logoGradientColors(
          rgbHex(0xA6B975), rgbHex(light ? 0x162027 : 0xF1EBD5), rgbHex(light ? 0xF8F8F2 : 0x0B1924), light
      );
      const auto svg = app::themedLogoSvg(logo, colors);
      require(
          svg.substr(svg.find("<path")) == logo.substr(logo.find("<path")),
          "Theme adaptation preserves every logo path and its drawing order"
      );
      require(
          colors[0] != colors[2] && colors[1] != colors[3],
          "The logo retains distinct highlight and shadow gradients in both modes"
      );
      for (const auto color : colors)
        require(
            color.a == 1 && svg.find(app::logoColorHex(color)) != std::string::npos,
            "Themed gradients remain opaque and visible instead of becoming transparency"
        );
    }
    struct MemberRow {
      int index;
      std::string group;
    };
    const std::vector<MemberRow> members{{0, "Online"}, {1, {}}, {2, "Offline"}, {3, {}}, {1000, "Other"}};
    const app::layout::MemberListLayout layout(members, 1002);
    require(
        layout.top(1) == app::layout::Members::groupHeight
            && layout.top(3) == app::layout::Members::groupHeight * 2 + app::layout::Members::rowHeight,
        "Member groups compact independently of ordinary member rows"
    );
    for (const auto index : {0, 1, 2, 3, 999, 1000, 1001})
      require(
          layout.indexAt(layout.top(index)) == index,
          "Sparse remote member indices map back from their compact scroll positions"
      );
    require(
        layout.maximumScroll(layout.height() + app::layout::Members::rowHeight) == 0
            && layout.indexAt(layout.height()) == 1001,
        "Member scrolling stays bounded across unloaded pages and at the last member"
    );
    for (const auto scale : {0.0F, 0.5F, 2.0F}) {
      Style::setCornerRadiusScale(scale);
      require(
          Style::scaledRadiusXl() == Style::radiusXl * scale
              && Style::scaledShapeRadius(Style::surfaceRadius, Style::spaceSection, Style::spaceXl)
                  == Style::surfaceRadius * scale,
          "All surface and control corners use the same global radius scale"
      );
      require(
          Style::scaledShapeRadius(Style::spaceSm, Style::spaceLg, Style::spaceLg) == Style::spaceSm,
          "Circular identities retain their shape while UI corners change"
      );
    }
    Style::setCornerRadiusScale(-1);
    require(Style::cornerRadiusScale() == 0, "Corner scaling clamps below square");
    Style::setCornerRadiusScale(3);
    require(Style::cornerRadiusScale() == 2, "Corner scaling clamps to its maximum");
    Style::setCornerRadiusScale(1);
    FT_Library fonts = nullptr;
    require(FT_Init_FreeType(&fonts) == 0, "Initialize bundled icon font verification");
    FT_Face face = nullptr;
    const auto font = paths::assetPath("fonts/tabler-icons.ttf").string();
    require(FT_New_Face(fonts, font.c_str(), 0, &face) == 0, "Load the copied Tabler font");
    for (const auto* icon :
         {"message-circle", "settings", "users", "clipboard-plus", "mood-smile", "x", "minus", "square", "squares",
          "chevron-left", "chevron-right", "chevron-down", "zoom-in", "zoom-out", "loader"})
      require(
          GlyphRegistry::contains(icon) && FT_Get_Char_Index(face, GlyphRegistry::lookup(icon)) != 0,
          "Every native action glyph resolves to a bundled font character"
      );
    FT_Done_Face(face);
    FT_Done_FreeType(fonts);
    Clipboard clipboard;
    Input::setTextClipboard(&clipboard);
    Input::setValidateKeyMatcher([](std::uint32_t key, std::uint32_t) { return key == XKB_KEY_Return; });
    Input field;
    auto key = [&](std::uint32_t symbol, std::uint32_t modifiers = 0, std::uint32_t unicode = 0) {
      field.inputArea()->dispatchKey(symbol, unicode, modifiers, true);
    };
    field.setTextPresenter([](std::string_view text) {
      std::vector<TextReplacement> result;
      if (const auto at = text.find("<@123>"); at != std::string_view::npos)
        result.push_back({at, at + std::string_view("<@123>").size(), "@Maya"});
      return result;
    });
    field.setValue("Hello <@123>!");
    require(field.displayValue() == "Hello @Maya!", "Inputs present readable text while retaining protocol syntax");
    field.moveCaretLeft();
    field.moveCaretLeft();
    require(field.textInputState().cursor == 6, "Caret navigation treats a presented mention as one editing unit");
    field.moveCaretRight();
    key(XKB_KEY_BackSpace);
    require(field.value() == "Hello !", "Backspace removes a complete mention instead of corrupting its ID");
    key(XKB_KEY_z, KeyMod::Ctrl);
    require(
        field.value() == "Hello <@123>!" && field.displayValue() == "Hello @Maya!",
        "Undo restores mention source and presentation together"
    );
    key(XKB_KEY_a, KeyMod::Ctrl);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "Hello <@123>!", "Copy preserves the original mention identity");
    TextInputEdit preedit;
    preedit.hasPreedit = true;
    preedit.preeditText = "日本";
    field.clearSelection();
    field.textInputApplyEdit(preedit);
    require(
        field.textInputState().surroundingText == "Hello <@123>!", "IME surrounding text retains source byte offsets"
    );
    field.textInputResetPreedit();
    field.setValue("<@123>");
    TextInputEdit eraseMention;
    eraseMention.hasDelete = true;
    eraseMention.deleteBeforeLength = 1;
    field.textInputApplyEdit(eraseMention);
    require(field.value().empty(), "IME deletion expands to the complete displayed mention");
    key(XKB_KEY_z, KeyMod::Ctrl);
    require(
        field.value() == "<@123>" && field.displayValue() == "@Maya", "IME deletion remains one undoable source edit"
    );
    key(XKB_KEY_Home);
    TextInputEdit insertBefore;
    insertBefore.hasDelete = insertBefore.hasCommitText = true;
    insertBefore.commitText = "前 ";
    field.textInputApplyEdit(insertBefore);
    require(
        field.value() == "前 <@123>" && field.displayValue() == "前 @Maya",
        "A zero-length IME delete does not remove the following mention"
    );
    field.setTextPresenter({});
    field.setValue("one two");
    key(XKB_KEY_a, KeyMod::Ctrl);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "one two", "Native inputs implement Ctrl+A and Ctrl+C");
    key(XKB_KEY_x, KeyMod::Ctrl);
    require(field.value().empty(), "Native inputs cut the selection");
    key(XKB_KEY_z, KeyMod::Ctrl);
    require(field.value() == "one two", "Native input undo restores a cut");
    key(XKB_KEY_z, KeyMod::Ctrl | KeyMod::Shift);
    require(field.value().empty(), "Native input redo restores the cut");
    key(XKB_KEY_v, KeyMod::Ctrl);
    require(field.value() == "one two", "Native input paste uses its clipboard adapter");
    key(XKB_KEY_BackSpace, KeyMod::Ctrl);
    require(field.value() == "one ", "Ctrl+Backspace deletes the previous word");
    key(XKB_KEY_Home);
    key(XKB_KEY_Right, KeyMod::Ctrl | KeyMod::Shift);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "one ", "Ctrl+Shift+Arrow selects words");
    field.setValue("hello 👩‍💻");
    key(XKB_KEY_BackSpace);
    require(field.value() == "hello ", "Native composer deletes complete graphemes and joined emoji");
    field.setMultiline(true);
    field.setSubmitOnEnter(true);
    field.setValue("first");
    int submitted = 0;
    field.setOnSubmit([&](const std::string&) { ++submitted; });
    key(XKB_KEY_Return, KeyMod::Shift);
    require(field.value() == "first\n" && submitted == 0, "Shift+Enter inserts a line in chat mode");
    key(XKB_KEY_Return);
    require(submitted == 1 && field.value() == "first\n", "Enter submits a multiline message once");
    field.setMultiline(false);
    field.setPasswordMode(true);
    field.setValue("synthetic-secret");
    clipboard.text = "previous";
    key(XKB_KEY_a, KeyMod::Ctrl);
    key(XKB_KEY_c, KeyMod::Ctrl);
    require(clipboard.text == "previous", "Password inputs do not expose their selection through copy");
    Input::setTextClipboard(nullptr);
    KeybindMatcher::setMatcher(KeybindAction::Validate, [](std::uint32_t key, std::uint32_t modifiers) {
      return key == XKB_KEY_space && modifiers == 0;
    });
    Toggle toggle;
    bool changed = false;
    toggle.setOnChange([&](bool value) { changed = value; });
    auto* area = dynamic_cast<InputArea*>(toggle.children().back().get());
    require(area != nullptr, "Toggle owns a native input area");
    area->dispatchKey(XKB_KEY_space, ' ', 0, true);
    require(changed && toggle.checked(), "Toggle supports keyboard activation");
    toggle.setEnabled(false);
    changed = false;
    area->dispatchKey(XKB_KEY_space, ' ', 0, true);
    require(!changed && toggle.checked(), "Disabled account toggles cannot mutate values");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
