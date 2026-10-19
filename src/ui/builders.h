#pragma once

#include "ui/controls/box.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/separator.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ui {

  // out fields borrow controls owned by the returned subtree.
  struct LabelProps {
    Label** out = nullptr;
    std::optional<std::string> text = std::nullopt;
    std::optional<float> fontSize = std::nullopt;
    std::optional<FontWeight> fontWeight = std::nullopt;
    std::optional<std::string> fontFamily = std::nullopt;
    std::optional<ColorSpec> color = std::nullopt;
    std::optional<float> width = std::nullopt;
    std::optional<float> height = std::nullopt;
    std::optional<float> minWidth = std::nullopt;
    std::optional<float> maxWidth = std::nullopt;
    std::optional<int> maxLines = std::nullopt;
    std::optional<TextAlign> textAlign = std::nullopt;
    std::optional<TextEllipsize> ellipsize = std::nullopt;
    std::optional<LabelBaselineMode> baselineMode = std::nullopt;
    std::optional<bool> autoScroll = std::nullopt;
    std::optional<float> autoScrollSpeed = std::nullopt;
    std::optional<bool> autoScrollOnlyWhenHovered = std::nullopt;
    std::optional<float> flexGrow = std::nullopt;
    std::optional<float> opacity = std::nullopt;
    std::optional<bool> visible = std::nullopt;
    std::optional<bool> participatesInLayout = std::nullopt;
    std::function<void(Label&)> configure = nullptr;
  };

  struct BoxProps {
    Box** out = nullptr;
    std::optional<ColorSpec> fill = std::nullopt;
    std::optional<ColorSpec> border = std::nullopt;
    std::optional<float> borderWidth = std::nullopt;
    std::optional<float> radius = std::nullopt;
    std::optional<float> softness = std::nullopt;
    std::optional<float> cardStyleScale = std::nullopt;
    std::optional<float> cardStyleFillOpacity = std::nullopt;
    std::optional<float> width = std::nullopt;
    std::optional<float> height = std::nullopt;
    std::optional<float> flexGrow = std::nullopt;
    std::optional<float> opacity = std::nullopt;
    std::optional<bool> visible = std::nullopt;
    std::optional<bool> participatesInLayout = std::nullopt;
    std::function<void(Box&)> configure = nullptr;
  };

  struct GlyphProps {
    Glyph** out = nullptr;
    std::optional<std::string> glyph = std::nullopt;
    std::optional<char32_t> codepoint = std::nullopt;
    std::optional<float> glyphSize = std::nullopt;
    std::optional<ColorSpec> color = std::nullopt;
    std::optional<float> width = std::nullopt;
    std::optional<float> height = std::nullopt;
    std::optional<float> flexGrow = std::nullopt;
    std::optional<float> opacity = std::nullopt;
    std::optional<bool> visible = std::nullopt;
    std::optional<bool> participatesInLayout = std::nullopt;
    std::function<void(Glyph&)> configure = nullptr;
  };

  struct SeparatorProps {
    Separator** out = nullptr;
    std::optional<ColorSpec> color = std::nullopt;
    std::optional<float> thickness = std::nullopt;
    std::optional<float> spacing = std::nullopt;
    std::optional<SeparatorOrientation> orientation = std::nullopt;
    std::optional<bool> gradientEdges = std::nullopt;
    std::optional<float> width = std::nullopt;
    std::optional<float> height = std::nullopt;
    std::optional<float> flexGrow = std::nullopt;
    std::optional<float> opacity = std::nullopt;
    std::optional<bool> visible = std::nullopt;
    std::optional<bool> participatesInLayout = std::nullopt;
    std::function<void(Separator&)> configure = nullptr;
  };

  [[nodiscard]] std::unique_ptr<Label> label(LabelProps props);
  [[nodiscard]] std::unique_ptr<Box> box(BoxProps props);
  [[nodiscard]] std::unique_ptr<Glyph> glyph(GlyphProps props);
  [[nodiscard]] std::unique_ptr<Separator> separator(SeparatorProps props);

} // namespace ui
