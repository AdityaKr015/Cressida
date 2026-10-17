// Primitive builders copied from the author's native controls. Other host
// builders stay unlinked until their services are integrated.
#include "ui/builders.h"

#include "ui/controls/scroll_view.h"

#include <utility>

namespace ui {

  namespace {

    template <typename Control, typename Props> void applyNodeProps(Control& control, const Props& props) {
      if (props.width.has_value() || props.height.has_value()) {
        control.setSize(props.width.value_or(control.width()), props.height.value_or(control.height()));
      }
      if (props.flexGrow.has_value()) {
        control.setFlexGrow(*props.flexGrow);
      }
      if (props.opacity.has_value()) {
        control.setOpacity(*props.opacity);
      }
      if (props.visible.has_value()) {
        control.setVisible(*props.visible);
      }
      if (props.participatesInLayout.has_value()) {
        control.setParticipatesInLayout(*props.participatesInLayout);
      }
    }

  } // namespace

  std::unique_ptr<Label> label(LabelProps props) {
    auto control = std::make_unique<Label>();
    if (props.text.has_value()) {
      control->setText(*props.text);
    }
    if (props.fontSize.has_value()) {
      control->setFontSize(*props.fontSize);
    }
    if (props.fontFamily.has_value()) {
      control->setFontFamily(std::move(*props.fontFamily));
    }
    if (props.color.has_value()) {
      control->setColor(*props.color);
    }
    if (props.minWidth.has_value()) {
      control->setMinWidth(*props.minWidth);
    }
    if (props.maxWidth.has_value()) {
      control->setMaxWidth(*props.maxWidth);
    }
    if (props.maxLines.has_value()) {
      control->setMaxLines(*props.maxLines);
    }
    if (props.fontWeight.has_value()) {
      control->setFontWeight(*props.fontWeight);
    }
    if (props.textAlign.has_value()) {
      control->setTextAlign(*props.textAlign);
    }
    if (props.ellipsize.has_value()) {
      control->setEllipsize(*props.ellipsize);
    }
    if (props.baselineMode.has_value()) {
      control->setBaselineMode(*props.baselineMode);
    }
    if (props.autoScroll.has_value()) {
      control->setAutoScroll(*props.autoScroll);
    }
    if (props.autoScrollSpeed.has_value()) {
      control->setAutoScrollSpeed(*props.autoScrollSpeed);
    }
    if (props.autoScrollOnlyWhenHovered.has_value()) {
      control->setAutoScrollOnlyWhenHovered(*props.autoScrollOnlyWhenHovered);
    }
    applyNodeProps(*control, props);
    if (props.configure) {
      props.configure(*control);
    }
    if (props.out != nullptr) {
      *props.out = control.get();
    }
    return control;
  }

  std::unique_ptr<Box> box(BoxProps props) {
    auto control = std::make_unique<Box>();
    if (props.cardStyleScale.has_value()) {
      control->setCardStyle(*props.cardStyleScale, props.cardStyleFillOpacity.value_or(1.0F));
    }
    if (props.fill.has_value()) {
      control->setFill(*props.fill);
    }
    if (props.border.has_value()) {
      control->setBorder(*props.border, props.borderWidth.value_or(1.0F));
    }
    if (props.radius.has_value()) {
      control->setRadius(*props.radius);
    }
    if (props.softness.has_value()) {
      control->setSoftness(*props.softness);
    }
    applyNodeProps(*control, props);
    if (props.configure) {
      props.configure(*control);
    }
    if (props.out != nullptr) {
      *props.out = control.get();
    }
    return control;
  }

  std::unique_ptr<Glyph> glyph(GlyphProps props) {
    auto control = std::make_unique<Glyph>();
    if (props.glyph.has_value()) {
      (void)control->setGlyph(*props.glyph);
    }
    if (props.codepoint.has_value()) {
      (void)control->setCodepoint(*props.codepoint);
    }
    if (props.glyphSize.has_value()) {
      control->setGlyphSize(*props.glyphSize);
    }
    if (props.color.has_value()) {
      control->setColor(*props.color);
    }
    applyNodeProps(*control, props);
    if (props.configure) {
      props.configure(*control);
    }
    if (props.out != nullptr) {
      *props.out = control.get();
    }
    return control;
  }

  std::unique_ptr<Separator> separator(SeparatorProps props) {
    auto control = std::make_unique<Separator>();
    if (props.color.has_value()) {
      control->setColor(*props.color);
    }
    if (props.thickness.has_value()) {
      control->setThickness(*props.thickness);
    }
    if (props.spacing.has_value()) {
      control->setSpacing(*props.spacing);
    }
    if (props.orientation.has_value()) {
      control->setOrientation(*props.orientation);
    }
    if (props.gradientEdges.has_value()) {
      control->setGradientEdges(*props.gradientEdges);
    }
    applyNodeProps(*control, props);
    if (props.configure) {
      props.configure(*control);
    }
    if (props.out != nullptr) {
      *props.out = control.get();
    }
    return control;
  }

} // namespace ui
