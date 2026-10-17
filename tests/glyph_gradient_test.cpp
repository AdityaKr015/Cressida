#include "render/programs/glyph_program.h"
#include "render/core/text_gradient.h"
#include "render/core/texture_handle.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
  void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
  struct Graphics {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint output = 0, mask = 0, fbo = 0;
    bool initialize() {
      display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
      if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
        return false;
      eglBindAPI(EGL_OPENGL_ES_API);
      const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
      EGLConfig config{};
      EGLint count = 0;
      if (!eglChooseConfig(display, attrs, &config, 1, &count) || !count)
        return false;
      const EGLint options[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
      context = eglCreateContext(display, config, EGL_NO_CONTEXT, options);
      if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return false;
      glGenTextures(1, &output);
      glBindTexture(GL_TEXTURE_2D, output);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;
      glGenTextures(1, &mask);
      glBindTexture(GL_TEXTURE_2D, mask);
      constexpr unsigned char white[] = {255, 255, 255, 255};
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glViewport(0, 0, 128, 32);
      glDisable(GL_BLEND);
      return true;
    }
    ~Graphics() {
      if (context != EGL_NO_CONTEXT) {
        glDeleteTextures(1, &output); glDeleteTextures(1, &mask); glDeleteFramebuffers(1, &fbo);
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
      }
      if (display != EGL_NO_DISPLAY)
        eglTerminate(display);
    }
  };
}
int main() {
  Graphics graphics;
  if (!graphics.initialize()) {
    std::cerr << "No headless EGL context for gradient readback\n";
    return 77;
  }
  try {
    GlyphProgram program;
    program.ensureInitialized();
    auto pixel = [](int x) {
      std::array<unsigned char, 4> rgba{};
      glReadPixels(x, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      return rgba;
    };
    TextGradient gradient{.colors = {rgbHex(0xFF0000), rgbHex(0x0000FF), rgbHex(0x00FF00)}};
    auto draw = [&](float opacity = 1) {
      program.drawGradient(TextureId(graphics.mask), 128, 32, 128, 32, 0, 0, 1, 1, opacity, gradient, 16, 96, Mat3::identity());
    };
    draw();
    require(pixel(16)[0] > 245 && pixel(16)[2] < 10, "Primary color aligns with logical text start after ink padding");
    require(pixel(111)[2] > 245 && pixel(111)[0] < 10, "Secondary color reaches logical text end");
    const auto center = pixel(64);
    require(center[0] > 110 && center[0] < 145 && center[2] > 110 && center[2] < 145, "Idle two-color gradient interpolates across the glyph mask");
    gradient.phase = .25F;
    draw();
    require(pixel(64)[2] > 245 && pixel(64)[0] < 10, "Hover phase moves the gradient without changing geometry");
    gradient.phase = 0;
    draw();
    require(pixel(64) == center, "Leaving hover restores the identical static gradient");
    gradient.count = 3;
    draw();
    require(pixel(111)[1] > 245 && pixel(111)[2] < 10, "Holographic gradient reaches its tertiary color");
    require(pixel(64)[2] > 245, "Three-color midpoint uses the secondary color");
    draw(.5F);
    require(pixel(64)[3] >= 126 && pixel(64)[3] <= 129 && pixel(64)[2] >= 124 && pixel(64)[2] <= 129,
            "Gradient output preserves premultiplied opacity");
    program.drawTinted(TextureId(graphics.mask), 128, 32, 128, 32, 0, 0, 1, 1, 1, rgbHex(0x00FF00));
    require(pixel(64) == std::array<unsigned char, 4>{0, 255, 0, 255}, "Gradient uniforms do not leak into ordinary text tint");
    program.draw(TextureId(graphics.mask), 128, 32, 128, 32, 0, 0, 1, 1, 1);
    require(pixel(64) == std::array<unsigned char, 4>{255, 255, 255, 255}, "RGBA emoji path stays unchanged after gradient draws");
    require(glGetError() == GL_NO_ERROR, "Gradient rendering leaves no GL errors");
    program.destroy();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
