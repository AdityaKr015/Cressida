#include "media/animation.h"
#include "render/backend/gles_texture_manager.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
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
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
