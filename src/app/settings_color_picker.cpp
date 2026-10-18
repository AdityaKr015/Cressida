#include "app/settings_color_picker.h"

#include "render/scene/rect_node.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/slider.h"
#include "ui/palette.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace app {
  namespace {
    constexpr float controlHeight = 28, rowStep = 36, firstRow = 40,
                    pickerHeight = firstRow + 2 * rowStep + controlHeight;

    unsigned byte(float value) { return static_cast<unsigned>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255)); }

    Color normalized(Color value) {
      for (auto* channel : {&value.r, &value.g, &value.b, &value.a})
        *channel = std::isfinite(*channel) ? std::clamp(*channel, 0.0F, 1.0F) : 0.0F;
      return value;
    }

    void compact(Input& input) {
      input.setControlHeight(controlHeight);
      input.setFontSize(12);
      input.setHorizontalPadding(8);
      input.setFrameRadius(5);
      input.setMinLayoutWidth(36);
      input.setOnSubmit([](const std::string&) {});
    }
  } // namespace

  SettingsColorPicker::SettingsColorPicker() {
    setSize(m_pickerWidth, pickerHeight);
    auto preview = std::make_unique<RectNode>();
    preview->setHitTestVisible(false);
    m_preview = static_cast<RectNode*>(addChild(std::move(preview)));

    auto hexadecimal = std::make_unique<Input>();
    compact(*hexadecimal);
    hexadecimal->setPlaceholder("#RRGGBB");
    hexadecimal->setOnChange([this](const std::string& value) { changeHex(value); });
    m_hexInput = static_cast<Input*>(addChild(std::move(hexadecimal)));

    constexpr std::array names{"R", "G", "B"};
    for (unsigned channel = 0; channel < 3; ++channel) {
      auto label = std::make_unique<Label>();
      label->setText(names[channel]);
      label->setFontSize(12);
      label->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
      label->setBaselineMode(LabelBaselineMode::TextFixedHeight);
      m_labels[channel] = static_cast<Label*>(addChild(std::move(label)));

      auto slider = std::make_unique<Slider>();
      slider->setRange(0, 255);
      slider->setStep(1);
      slider->setTrackHeight(4);
      slider->setThumbSize(12);
      slider->setControlHeight(controlHeight);
      slider->setWheelAdjustEnabled(false);
      slider->setOnValueChanged([this, channel](double value) {
        if (!m_syncing && m_enabled)
          changeChannel(channel, static_cast<unsigned>(std::lround(value)));
      });
      m_sliders[channel] = static_cast<Slider*>(addChild(std::move(slider)));

      auto input = std::make_unique<Input>();
      compact(*input);
      input->setTextAlign(TextAlign::Center);
      input->setOnChange([this, channel](const std::string& value) { changeNumber(channel, value); });
      m_values[channel] = static_cast<Input*>(addChild(std::move(input)));
    }
    syncFields();
  }

  void SettingsColorPicker::setPickerWidth(float width) {
    const auto next = std::isfinite(width) ? std::max(96.0F, width) : 240.0F;
    if (m_pickerWidth == next)
      return;
    m_pickerWidth = next;
    setSize(m_pickerWidth, pickerHeight);
    markLayoutDirty();
  }

  void SettingsColorPicker::setColor(const Color& color) {
    const auto next = normalized(color);
    if (m_color == next)
      return;
    m_color = next;
    syncFields();
  }

  void SettingsColorPicker::setEnabled(bool enabled) {
    if (m_enabled == enabled)
      return;
    m_enabled = enabled;
    m_hexInput->setEnabled(enabled);
    for (unsigned channel = 0; channel < 3; ++channel) {
      m_values[channel]->setEnabled(enabled);
      m_sliders[channel]->setEnabled(enabled);
    }
    updatePreview();
  }

  void SettingsColorPicker::setOnColorChanged(std::function<void(const Color&)> callback) {
    m_onColorChanged = std::move(callback);
  }

  float SettingsColorPicker::intrinsicColumnHeight(float /*pickerWidth*/, float scale) {
    return pickerHeight * std::max(0.1F, scale);
  }

  InputArea* SettingsColorPicker::primaryInputArea() const noexcept { return m_hexInput->inputArea(); }

  void SettingsColorPicker::updatePreview() {
    m_preview->setStyle(
        {.fill = m_color,
         .border = colorForRole(ColorRole::Outline),
         .fillMode = FillMode::Solid,
         .radius = 5,
         .borderWidth = 1}
    );
    m_preview->setOpacity(m_enabled ? 1.0F : 0.5F);
  }

  void SettingsColorPicker::syncFields(Input* editing) {
    m_syncing = true;
    const auto hexadecimal = formatRgbHex(m_color);
    if (editing != m_hexInput && m_hexInput->value() != hexadecimal)
      m_hexInput->setValue(hexadecimal);
    m_hexInput->setInvalid(false);
    const std::array values{byte(m_color.r), byte(m_color.g), byte(m_color.b)};
    for (unsigned channel = 0; channel < 3; ++channel) {
      const auto number = std::to_string(values[channel]);
      if (editing != m_values[channel] && m_values[channel]->value() != number)
        m_values[channel]->setValue(number);
      m_values[channel]->setInvalid(false);
      m_sliders[channel]->setValue(values[channel]);
    }
    m_syncing = false;
    updatePreview();
  }

  void SettingsColorPicker::changeChannel(unsigned channel, unsigned value, Input* editing) {
    if (!m_enabled || m_syncing || channel >= 3 || value > 255)
      return;
    auto next = m_color;
    auto* selected = channel == 0 ? &next.r : channel == 1 ? &next.g : &next.b;
    *selected = colorByte(value);
    const bool changed = next != m_color;
    m_color = next;
    syncFields(editing);
    if (changed && m_onColorChanged)
      m_onColorChanged(m_color);
  }

  void SettingsColorPicker::changeNumber(unsigned channel, const std::string& text) {
    if (m_syncing || !m_enabled)
      return;
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    const bool valid =
        !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size() && value <= 255;
    m_values[channel]->setInvalid(!valid);
    if (valid)
      changeChannel(channel, value, m_values[channel]);
  }

  void SettingsColorPicker::changeHex(const std::string& text) {
    if (m_syncing || !m_enabled)
      return;
    std::string_view digits = text;
    if (digits.starts_with('#'))
      digits.remove_prefix(1);
    Color next;
    const bool valid = (digits.size() == 3 || digits.size() == 6) && tryParseHexColor(text, next);
    m_hexInput->setInvalid(!valid);
    if (!valid)
      return;
    next.a = m_color.a;
    const bool changed = next != m_color;
    m_color = next;
    // Keep the text being edited and its caret/undo history intact. Other controls
    // follow immediately; a slider change later restores canonical hex notation.
    syncFields(m_hexInput);
    if (changed && m_onColorChanged)
      m_onColorChanged(m_color);
  }

  LayoutSize SettingsColorPicker::doMeasure(Renderer& /*renderer*/, const LayoutConstraints& constraints) {
    return constraints.constrain({m_pickerWidth, pickerHeight});
  }

  void SettingsColorPicker::doLayout(Renderer& renderer) {
    const auto available = std::max(96.0F, width());
    m_preview->setPosition(0, 0);
    m_preview->setSize(controlHeight, controlHeight);
    m_hexInput->setPosition(40, 0);
    m_hexInput->setSize(available - 40, controlHeight);
    for (unsigned channel = 0; channel < 3; ++channel) {
      const auto y = firstRow + rowStep * channel;
      m_labels[channel]->setPosition(0, y + 5);
      m_sliders[channel]->setPosition(20, y);
      m_sliders[channel]->setFrameSize(std::max(8.0F, available - 76), controlHeight);
      m_values[channel]->setPosition(available - 44, y);
      m_values[channel]->setSize(44, controlHeight);
    }
    updatePreview();
    Node::doLayout(renderer);
  }
} // namespace app
