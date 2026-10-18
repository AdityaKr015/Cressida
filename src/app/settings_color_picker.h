#pragma once

#include "render/core/color.h"
#include "render/scene/node.h"

#include <array>
#include <functional>

class Input;
class InputArea;
class Label;
class RectNode;
class Slider;

namespace app {

  // Application-owned RGB editor built from the retained native controls.
  // No image assets, textures, shell services or external color-dialog helpers.
  class SettingsColorPicker final : public Node {
  public:
    SettingsColorPicker();

    void setPickerWidth(float width);
    void setColor(const Color& color);
    [[nodiscard]] const Color& color() const noexcept { return m_color; }
    void setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const noexcept { return m_enabled; }
    void setOnColorChanged(std::function<void(const Color&)> callback);

    [[nodiscard]] static float intrinsicColumnHeight(float pickerWidth, float scale);
    [[nodiscard]] Input* hexInput() const noexcept { return m_hexInput; }
    [[nodiscard]] std::array<Input*, 3> rgbInputs() const noexcept { return m_values; }
    [[nodiscard]] InputArea* primaryInputArea() const noexcept;

  private:
    void doLayout(Renderer& renderer) override;
    LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
    void syncFields(Input* editing = nullptr);
    void updatePreview();
    void changeChannel(unsigned channel, unsigned value, Input* editing = nullptr);
    void changeHex(const std::string& text);
    void changeNumber(unsigned channel, const std::string& text);

    RectNode* m_preview = nullptr;
    Input* m_hexInput = nullptr;
    std::array<Label*, 3> m_labels{};
    std::array<Slider*, 3> m_sliders{};
    std::array<Input*, 3> m_values{};
    Color m_color = rgba(1, 1, 1);
    float m_pickerWidth = 240;
    bool m_enabled = true, m_syncing = false;
    std::function<void(const Color&)> m_onColorChanged;
  };

} // namespace app
