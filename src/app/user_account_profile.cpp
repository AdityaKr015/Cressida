#include "app/client_application.h"
#include "app/theme.h"
#include "app/user_settings_form.h"
#include "render/core/image_encoder.h"
#include "render/core/texture_manager.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace app {
  namespace {
    double ratio(const std::string& field) { return field == "banner" ? 2.5 : 1; }
    struct CropBounds {
      double x, y, width, height;
    };
    CropBounds bounds(const UserProfileCrop& crop) {
      const auto width = std::min(double(crop.width), crop.height * ratio(crop.field)) / crop.zoom;
      const auto height = width / ratio(crop.field);
      return {(crop.width - width) * crop.x, (crop.height - height) * crop.y, width, height};
    }
    std::string base64(std::span<const std::uint8_t> bytes) {
      constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string output;
      output.reserve((bytes.size() + 2) / 3 * 4);
      for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto value = (std::uint32_t(bytes[i]) << 16)
            | (i + 1 < bytes.size() ? std::uint32_t(bytes[i + 1]) << 8 : 0)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        output += alphabet[value >> 18];
        output += alphabet[(value >> 12) & 63];
        output += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        output += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
      }
      return output;
    }
  } // namespace
  void ClientApplication::beginUserProfileUpload(const std::string& field) {
    auto& state = m_userAccount;
    if (!m_settingsOpen
        || m_settingsPage != "profile"
        || state.busy
        || !userAccountProfileReady(state)
        || (field != "avatar" && field != "banner"))
      return;
    if ((field == "banner" || !state.guild.empty()) && m_account.premiumType.value_or(0) != 2) {
      state.error = "This profile customization requires Discord Nitro.";
      m_window.invalidate();
      return;
    }
    if (m_demo) {
      state.error = "The desktop file chooser is unavailable in this offline preview.";
      m_window.invalidate();
      return;
    }
    state.uploadField = field;
    state.uploadGeneration = state.generation;
    state.uploadRequest = (std::uint64_t{1} << 63) | ++state.serial;
    m_filePicker.open(
        state.uploadRequest, field == "avatar" ? "Choose a profile avatar" : "Choose a profile banner",
        {"image/png", "image/jpeg", "image/gif", "image/webp"}, 10 * 1024 * 1024
    );
  }
  bool ClientApplication::userAccountPickedFile(PickedFile& result) {
    if (!(result.request & (std::uint64_t{1} << 63)))
      return false;
    auto& state = m_userAccount;
    if (result.request != state.uploadRequest
        || state.uploadGeneration != state.generation
        || !m_settingsOpen
        || m_settingsPage != "profile")
      return true;
    state.uploadRequest = 0;
    if (result.cancelled)
      return true;
    if (!result.error.empty()) {
      state.error = result.error;
      m_window.invalidate();
      return true;
    }
    if (!result.raster
        || result.raster->width <= 0
        || result.raster->height <= 0
        || result.raster->width > 4096
        || result.raster->height > 4096
        || std::uint64_t(result.raster->width) * result.raster->height > 8388608) {
      state.error = "Choose an image no larger than 4096×4096 pixels.";
      m_window.invalidate();
      return true;
    }
    const auto has = [&](std::string_view marker) {
      return std::search(result.bytes.begin(), result.bytes.end(), marker.begin(), marker.end()) != result.bytes.end();
    };
    const bool animated = result.mime == "image/gif"
        || (result.mime == "image/png" && has("acTL"))
        || (result.mime == "image/webp" && has("ANIM"));
    if (animated && m_account.premiumType.value_or(0) != 2) {
      state.error = "Animated profile images require Discord Nitro.";
      m_window.invalidate();
      return true;
    }
    if (state.crop)
      m_images.erase(state.crop->texture);
    UserProfileCrop crop;
    crop.field = state.uploadField;
    crop.mime = result.mime;
    crop.bytes = std::move(result.bytes);
    crop.pixels = std::move(result.raster->pixels);
    crop.width = result.raster->width;
    crop.height = result.raster->height;
    crop.animated = animated;
    crop.texture = "user-profile-upload:crop:" + std::to_string(++state.serial);
    m_images[crop.texture].texture =
        m_window.context().textureManager().loadFromRgba(crop.pixels.data(), crop.width, crop.height);
    state.crop = std::move(crop);
    state.error.clear();
    m_window.invalidate();
    return true;
  }
  bool ClientApplication::userProfileCropAction(const std::string& action) {
    auto& state = m_userAccount;
    if (!action.starts_with("crop-") || !state.crop || state.busy)
      return false;
    if (action == "crop-cancel") {
      m_images.erase(state.crop->texture);
      state.crop.reset();
      m_window.invalidate();
      return true;
    }
    if (action != "crop-original" && action != "crop-apply")
      return true;
    auto& crop = *state.crop;
    auto width = crop.width, height = crop.height;
    auto pixels = crop.pixels;
    auto bytes = crop.bytes;
    auto mime = crop.mime;
    if (action == "crop-apply") {
      width = crop.field == "avatar" ? 512 : 960;
      height = static_cast<int>(std::round(width / ratio(crop.field)));
      pixels.resize(std::size_t(width) * height * 4);
      const auto area = bounds(crop);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const auto sx = std::clamp(area.x + (x + .5) * area.width / width - .5, 0.0, double(crop.width - 1));
          const auto sy = std::clamp(area.y + (y + .5) * area.height / height - .5, 0.0, double(crop.height - 1));
          const int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, crop.width - 1),
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
      bytes = encodePng(pixels.data(), width, height);
      mime = "image/png";
    }
    if (bytes.empty() || bytes.size() > 10 * 1024 * 1024) {
      state.error = "The image could not be prepared within the 10 MiB upload limit.";
      m_window.invalidate();
      return true;
    }
    state.draft[crop.field] = "data:" + mime + ";base64," + base64(bytes);
    m_images["user-profile-upload:" + crop.field].texture =
        m_window.context().textureManager().loadFromRgba(pixels.data(), width, height);
    m_images.erase(crop.texture);
    state.crop.reset();
    m_profileDirty = true;
    state.error.clear();
    m_window.invalidate();
    return true;
  }
  bool ClientApplication::drawUserProfileCrop(UserSettingsForm& form) {
    auto& state = m_userAccount;
    if (!state.crop)
      return false;
    auto& crop = *state.crop;
    form.section("Adjust profile image");
    form.row(
        "Crop and position",
        crop.animated ? "Apply a still crop, or keep the original file to preserve its animation."
                      : "Adjust zoom and position, then apply this image to your unsaved profile."
    );
    const auto area = bounds(crop);
    const float width = std::min(form.width, 280.0F), height = width / ratio(crop.field), scale = width / area.width;
    auto image = std::make_unique<Node>();
    image->setPosition(0, form.y);
    image->setSize(width, height);
    image->setClipChildren(true);
    rectangle(*image, 0, 0, width, height, theme::background, 5);
    picture(
        *image, -area.x * scale, -area.y * scale, crop.width * scale, crop.height * scale, crop.texture, 0, true, false,
        ImageFitMode::Stretch
    );
    form.node.addChild(std::move(image));
    form.y += height + 12;
    for (const auto& [key, title, value, minimum, maximum] :
         {std::tuple{"zoom", "Zoom", crop.zoom, 1.0, 4.0}, std::tuple{"x", "Horizontal position", crop.x, 0.0, 1.0},
          std::tuple{"y", "Vertical position", crop.y, 0.0, 1.0}}) {
      form.slider(
          "user-account:crop:" + std::string(key), title, "", value, minimum, maximum, .01,
          [this, key = std::string(key)](double value) {
            if (!m_userAccount.crop)
              return;
            auto& crop = *m_userAccount.crop;
            if (key == "zoom")
              crop.zoom = value;
            else if (key == "x")
              crop.x = value;
            else
              crop.y = value;
            m_window.invalidate();
          }
      );
    }
    form.button("Apply crop", "crop-apply", ButtonVariant::Primary, 116);
    form.button("Use original", "crop-original", ButtonVariant::Default, 116);
    form.button("Cancel", "crop-cancel", ButtonVariant::Default, 80);
    return true;
  }
} // namespace app
