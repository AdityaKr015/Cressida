#include "media/animation.h"
#include "render/backend/gles_texture_manager.h"
#include "render/programs/image_program.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  struct Graphics {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint framebuffer = 0;
    bool initialize() {
      display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
      if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
        return false;
      eglBindAPI(EGL_OPENGL_ES_API);
      const EGLint attributes[] = {
          EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
      };
      EGLConfig config{};
      EGLint count = 0;
      if (!eglChooseConfig(display, attributes, &config, 1, &count) || !count)
        return false;
      const EGLint options[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
      context = eglCreateContext(display, config, EGL_NO_CONTEXT, options);
      if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return false;
      glGenFramebuffers(1, &framebuffer);
      return true;
    }
    ~Graphics() {
      if (context != EGL_NO_CONTEXT) {
        glDeleteFramebuffers(1, &framebuffer);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
      }
      if (display != EGL_NO_DISPLAY)
        eglTerminate(display);
    }
  };
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
} // namespace

int main() {
  Graphics graphics;
  if (!graphics.initialize()) {
    std::cerr << "No headless EGL context for animation texture readback\n";
    return 77;
  }
  try {
    media::Animation animation;
    animation.width = 337;
    animation.height = 600;
    std::vector<std::vector<std::uint8_t>> expected;
    for (int phase = 0; phase < 2; ++phase) {
      media::Frame frame;
      for (int y = 0; y < animation.height; ++y)
        for (int x = 0; x < animation.width; ++x) {
          frame.rgba.push_back((x + phase) % 2 ? 255 : 0);
          frame.rgba.push_back((y + phase) % 2 ? 255 : 0);
          frame.rgba.push_back(phase ? 255 : 0);
          frame.rgba.push_back(x % 3 ? 255 : 127);
        }
      expected.push_back(frame.rgba);
      frame.pack();
      require(frame.rgba.empty() && !frame.packed.empty(), "Readback must exercise losslessly packed frames");
      animation.frames.push_back(std::move(frame));
    }
    GlesTextureManager textures;
    std::vector<std::uint8_t> canvas, readback(expected.front().size());
    auto texture = textures.loadFromRgba(animation.pixels(0, canvas).data(), animation.width, animation.height);
    require(texture.valid(), "Upload the first packed animation frame at native resolution");
    // Pause/reset, advancing and wrapping reuse the same native texture.
    for (std::size_t index : {0, 1, 0, 1}) {
      const auto pixels = animation.pixels(index, canvas);
      require(
          textures.updateSubImage(
              texture, pixels.data(), 0, 0, animation.width, animation.height, TextureDataFormat::Rgba
          ),
          "Upload decoded animation pixels without resizing"
      );
      glBindFramebuffer(GL_FRAMEBUFFER, graphics.framebuffer);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id.value(), 0);
      require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Read the animation texture");
      glReadPixels(0, 0, animation.width, animation.height, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
      require(glGetError() == GL_NO_ERROR, "Animation texture readback succeeds");
      require(readback == expected[index], "GPU uploads preserve individual pixels, alpha and portrait row boundaries");
    }
    constexpr int sourceSize = 64, outputSize = 8;
    std::vector<std::uint8_t> art(sourceSize * sourceSize * 4);
    for (int y = 0; y < sourceSize; ++y)
      for (int x = 0; x < sourceSize; ++x) {
        const std::array<std::uint8_t, 4> color =
            x % 2 ? std::array<std::uint8_t, 4>{192, 160, 224, 255} : std::array<std::uint8_t, 4>{64, 96, 160, 255};
        std::ranges::copy(color, art.begin() + (y * sourceSize + x) * 4);
      }
    const auto detail = textures.loadFromRgba(art.data(), sourceSize, sourceSize, true);
    const auto target = textures.createEmpty(outputSize, outputSize, TextureDataFormat::Rgba);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.id.value(), 0);
    require(
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Cosmetic sampling target is complete"
    );
    glViewport(0, 0, outputSize, outputSize);
    glDisable(GL_BLEND);
    ImageProgram image;
    image.ensureInitialized();
    image.draw(detail.id, outputSize, outputSize, outputSize, outputSize, {1, 1, 1, 1}, false, false, 1);
    std::array<std::uint8_t, 4> color{};
    glReadPixels(outputSize / 2, outputSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
    require(
        std::abs(int(color[0]) - 128) <= 2
            && std::abs(int(color[1]) - 128) <= 2
            && std::abs(int(color[2]) - 192) <= 2
            && color[3] == 255,
        "Downscaled cosmetic details preserve their average RGB color instead of aliasing to bright or dark stripes"
    );
    require(glGetError() == GL_NO_ERROR, "Cosmetic image sampling leaves no graphics errors");
    constexpr int maskSize = 32;
    const auto maskTarget = textures.createEmpty(maskSize, maskSize, TextureDataFormat::Rgba);
    const std::array<std::uint8_t, 4> solid{96, 112, 160, 255};
    const auto solidTexture = textures.loadFromRgba(solid.data(), 1, 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTarget.id.value(), 0);
    require(
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Rounded profile mask target is complete"
    );
    glViewport(0, 0, maskSize, maskSize);
    const auto pixel = [&](int x, int y) {
      std::array<std::uint8_t, 4> value{};
      glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value.data());
      return value;
    };
    for (const auto offset : {std::array<float, 2>{-8, -8}, std::array<float, 2>{-4, -12}}) {
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      const ImageClip clip{4 - offset[0], 4 - offset[1], 24, 24, 8, true};
      image.draw(
          solidTexture.id, maskSize, maskSize, 64, 64, {1, 1, 1, 1}, false, false, 1, 0, {0, 0, 0, 0}, 0, 0, 1, 1,
          Mat3::translation(offset[0], offset[1]), {}, clip
      );
      require(
          pixel(4, 4)[3] == 0 && pixel(27, 27)[3] == 0 && pixel(0, 16)[3] == 0,
          "Oversized and offset effect layers cannot paint outside the shared rounded profile area"
      );
      require(pixel(16, 16) == solid, "Rounded masks preserve the effect's interior colors and opacity");
    }
    image.draw(solidTexture.id, maskSize, maskSize, maskSize, maskSize, {1, 1, 1, 1}, false, false, 1);
    require(pixel(4, 4) == solid, "Profile masks do not leak into later ordinary image draws");
    require(glGetError() == GL_NO_ERROR, "Rounded effect masks leave no graphics errors");
    image.destroy();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
