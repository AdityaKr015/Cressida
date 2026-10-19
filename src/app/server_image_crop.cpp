#include "app/client_application.h"
#include "app/theme.h"
#include "render/core/image_decoder.h"
#include "render/core/image_encoder.h"
#include "render/core/texture_manager.h"
#include "ui/controls/slider.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace app {
  using namespace theme;
  namespace {
    double aspect(const std::string& field) {
      return field == "custom_banner"
              || field == "banner"
              || field == "splash"
              || field == "discovery_splash"
              || field == "image"
              || field == "home_header"
          ? 16.0 / 9
          : 1;
    }
    struct Bounds {
      double x, y, width, height;
    };
    Bounds cropBounds(const ServerImageCrop& crop) {
      const auto ratio = aspect(crop.field);
      const auto width = std::min(static_cast<double>(crop.width), crop.height * ratio) / crop.zoom;
      const auto height = width / ratio;
      return {(crop.width - width) * crop.x, (crop.height - height) * crop.y, width, height};
    }
    bool animated(const std::string& mime, const std::vector<std::uint8_t>& bytes) {
      if (mime == "image/gif")
        return true;
      const auto has = [&](std::string_view marker) {
        return std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) != bytes.end();
      };
      return (mime == "image/webp" && has("ANIM")) || (mime == "image/png" && has("acTL"));
    }
  } // namespace
  void ClientApplication::beginServerImageCrop(
      const std::string& field, const std::string& mime, std::vector<std::uint8_t> bytes, DecodedRasterImage raster
  ) {
    auto& state = m_serverSettings;
    if (!state.open || state.saving || state.mfaRequest)
      return;
    if (raster.width <= 0
        || raster.height <= 0
        || raster.width > 4096
        || raster.height > 4096
        || std::uint64_t(raster.width) * raster.height > 8388608
        || raster.pixels.size() != std::size_t(raster.width) * raster.height * 4) {
      state.error = "This image could not be prepared. Choose an image no larger than 4096×4096 pixels.";
      m_window.invalidate();
      return;
    }
    if (state.imageCrop)
      m_images.erase(state.imageCrop->texture);
    ServerImageCrop crop;
    crop.field = field;
    crop.mime = mime;
    crop.animated = animated(mime, bytes);
    crop.bytes = std::move(bytes);
    crop.pixels = std::move(raster.pixels);
    crop.width = raster.width;
    crop.height = raster.height;
    crop.texture = "server-crop:" + state.guild + ':' + std::to_string(++state.serial);
    m_images[crop.texture].texture =
        m_window.context().textureManager().loadFromRgba(crop.pixels.data(), crop.width, crop.height);
    state.imageCrop = std::move(crop);
    state.error.clear();
    m_scrolling.stop(state.scroll);
    state.scroll = 0;
    m_window.invalidate();
  }
  bool ClientApplication::serverImageCropAction(const std::string& action) {
    if (!action.starts_with("crop-"))
      return false;
    auto& state = m_serverSettings;
    if (!state.imageCrop || state.saving || state.mfaRequest)
      return true;
    if (action == "crop-cancel") {
      m_images.erase(state.imageCrop->texture);
      state.imageCrop.reset();
      m_window.invalidate();
      return true;
    }
    if (action != "crop-apply" && action != "crop-original")
      return true;
    auto crop = std::move(*state.imageCrop);
    state.imageCrop.reset();
    auto width = crop.width, height = crop.height;
    auto pixels = crop.pixels;
    if (action == "crop-apply") {
      const auto bounds = cropBounds(crop);
      width = state.page == "roles" ? 128 : crop.field == "icon" || crop.field == "avatar" ? 512 : 960;
      height = static_cast<int>(std::round(width / aspect(crop.field)));
      pixels.resize(std::size_t(width) * height * 4);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const auto sx = std::clamp(bounds.x + (x + 0.5) * bounds.width / width - 0.5, 0.0, double(crop.width - 1));
          const auto sy = std::clamp(bounds.y + (y + 0.5) * bounds.height / height - 0.5, 0.0, double(crop.height - 1));
          const auto x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, crop.width - 1),
                     y1 = std::min(y0 + 1, crop.height - 1);
          const auto dx = sx - x0, dy = sy - y0;
          for (int c = 0; c < 4; ++c) {
            const auto a = crop.pixels[(std::size_t(y0) * crop.width + x0) * 4 + c] * (1 - dx)
                + crop.pixels[(std::size_t(y0) * crop.width + x1) * 4 + c] * dx;
            const auto b = crop.pixels[(std::size_t(y1) * crop.width + x0) * 4 + c] * (1 - dx)
                + crop.pixels[(std::size_t(y1) * crop.width + x1) * 4 + c] * dx;
            pixels[(std::size_t(y) * width + x) * 4 + c] = static_cast<std::uint8_t>(std::round(a * (1 - dy) + b * dy));
          }
        }
      std::string error;
      auto bytes = encodePng(pixels.data(), width, height, &error);
      if (bytes.empty()) {
        state.imageCrop = std::move(crop);
        state.error = "The cropped image could not be encoded. Try a different image.";
        m_window.invalidate();
        return true;
      }
      serverSettingsUpload(crop.field, "image/png", std::move(bytes));
    } else
      serverSettingsUpload(crop.field, crop.mime, std::move(crop.bytes));
    if (state.error.empty()) {
      const auto kind = crop.field == "icon"                        ? "icons"
          : crop.field == "custom_banner" || crop.field == "banner" ? "banners"
                                                                    : crop.field;
      m_images["server-upload:" + state.guild + ':' + kind].texture =
          m_window.context().textureManager().loadFromRgba(pixels.data(), width, height);
    }
    m_images.erase(crop.texture);
    m_window.invalidate();
    return true;
  }
  bool ClientApplication::drawServerImageCrop(ServerSettingsForm& form) {
    auto& state = m_serverSettings;
    if (!state.imageCrop)
      return false;
    auto& crop = *state.imageCrop;
    form.section("Adjust image");
    form.detail(
        "Crop and position",
        crop.animated ? "Choose a crop for a still image, or use the original file to preserve its animation."
                      : "Position the image and adjust its zoom before applying it to the server."
    );
    const auto width = std::min(form.width, 280.0F);
    const auto height = static_cast<float>(width / aspect(crop.field));
    const auto bounds = cropBounds(crop);
    const auto scale = width / bounds.width;
    auto frame = std::make_unique<Node>();
    frame->setPosition(0, form.y);
    frame->setSize(width, height);
    frame->setClipChildren(true);
    rectangle(*frame, 0, 0, width, height, background, Style::controlRadius);
    picture(
        *frame, -bounds.x * scale, -bounds.y * scale, crop.width * scale, crop.height * scale, crop.texture, 0, true,
        false, ImageFitMode::Stretch
    );
    form.node.addChild(std::move(frame));
    form.y += height + 12;
    const auto slider = [&](const std::string& name, const std::string& key, double value, double min, double max) {
      const bool stacked = form.width < 280;
      const auto row = form.row(name, "", stacked ? 0 : form.width - 114);
      const auto id = "server-crop-slider:" + key;
      std::unique_ptr<Node> saved;
      if (auto at = m_detachedInputs.find(id); at != m_detachedInputs.end()) {
        saved = std::move(at->second);
        m_detachedInputs.erase(at);
      }
      auto control =
          saved ? std::unique_ptr<Slider>(static_cast<Slider*>(saved.release())) : std::make_unique<Slider>();
      m_widgets[id] = control.get();
      control->setPosition(stacked ? 0 : 124, stacked ? row.first + row.second : row.first + (row.second - 26) / 2);
      control->setSize(stacked ? width : std::min(280.0F, form.width - 124), 26);
      control->setRange(min, max);
      control->setStep(0.01);
      control->setValue(value);
      control->setControlHeight(26);
      control->setThumbSize(12);
      control->setTrackHeight(3);
      control->setWheelAdjustEnabled(false);
      control->setOnValueChanged([this, key](double value) {
        if (auto& crop = m_serverSettings.imageCrop; crop) {
          if (key == "zoom")
            crop->zoom = value;
          else if (key == "x")
            crop->x = value;
          else
            crop->y = value;
          m_window.invalidate();
        }
      });
      form.node.addChild(std::move(control));
      if (stacked)
        form.y += Style::space2Xl;
    };
    slider("Zoom", "zoom", crop.zoom, 1, 4);
    slider("Horizontal position", "x", crop.x, 0, 1);
    slider("Vertical position", "y", crop.y, 0, 1);
    form.buttonControl(form.node, 0, form.y, 98, "Apply crop", "crop-apply", ButtonVariant::Primary, true);
    form.buttonControl(form.node, 110, form.y, 98, "Use original", "crop-original", ButtonVariant::Default, true);
    if (form.width < 300)
      form.y += (Style::spaceSection - Style::spaceXxs);
    form.buttonControl(
        form.node, form.width < 300 ? 0 : 220, form.y, 70, "Cancel", "crop-cancel", ButtonVariant::Default, true
    );
    form.y += Style::spaceSection;
    return true;
  }
} // namespace app
