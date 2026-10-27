#pragma once

#include <windows.h>
#include <GL/gl.h>

#include <cstddef>
#include <cstdint>

// Only the OpenGL 3.3 entry points used by this backend. No SDK or loader
// shared with another plugin; all pointers belong to the current WGL context.
namespace gl {

constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kArrayBufferBinding = 0x8894;
constexpr GLenum kElementArrayBuffer = 0x8893;
constexpr GLenum kStreamDraw = 0x88E0;
constexpr GLenum kVertexArrayBinding = 0x85B5;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kCurrentProgram = 0x8B8D;
constexpr GLenum kActiveTexture = 0x84E0;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kSamplerBinding = 0x8919;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kReadFramebufferBinding = 0x8CAA;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kBlendSrcRgb = 0x80C9;
constexpr GLenum kBlendDstRgb = 0x80C8;
constexpr GLenum kBlendSrcAlpha = 0x80CB;
constexpr GLenum kBlendDstAlpha = 0x80CA;
constexpr GLenum kBlendEquationRgb = 0x8009;
constexpr GLenum kBlendEquationAlpha = 0x883D;
constexpr GLenum kFuncAdd = 0x8006;
constexpr GLenum kFramebufferSrgb = 0x8DB9;
constexpr GLenum kRasterizerDiscard = 0x8C89;
constexpr GLenum kPixelUnpackBuffer = 0x88EC;
constexpr GLenum kPixelUnpackBufferBinding = 0x88EF;
constexpr GLenum kDepthComponent24 = 0x81A6;

#define ABCUI_GL_FUNCTIONS(X)                                                  \
  X(void, ActiveTexture, (GLenum))                                             \
  X(void, GenVertexArrays, (GLsizei, GLuint *))                                \
  X(void, BindVertexArray, (GLuint))                                           \
  X(void, DeleteVertexArrays, (GLsizei, const GLuint *))                       \
  X(void, GenBuffers, (GLsizei, GLuint *))                                     \
  X(void, BindBuffer, (GLenum, GLuint))                                        \
  X(void, BufferData, (GLenum, std::ptrdiff_t, const void *, GLenum))          \
  X(void, DeleteBuffers, (GLsizei, const GLuint *))                            \
  X(void, EnableVertexAttribArray, (GLuint))                                   \
  X(void, DisableVertexAttribArray, (GLuint))                                  \
  X(void, VertexAttribPointer,                                                 \
    (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))                 \
  X(GLuint, CreateShader, (GLenum))                                            \
  X(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
  X(void, CompileShader, (GLuint))                                             \
  X(void, GetShaderiv, (GLuint, GLenum, GLint *))                              \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *))              \
  X(void, DeleteShader, (GLuint))                                              \
  X(GLuint, CreateProgram, ())                                                 \
  X(void, AttachShader, (GLuint, GLuint))                                      \
  X(void, LinkProgram, (GLuint))                                               \
  X(void, GetProgramiv, (GLuint, GLenum, GLint *))                             \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *))             \
  X(void, DeleteProgram, (GLuint))                                             \
  X(void, UseProgram, (GLuint))                                                \
  X(GLint, GetUniformLocation, (GLuint, const char *))                         \
  X(void, Uniform1i, (GLint, GLint))                                           \
  X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                \
  X(void, GenFramebuffers, (GLsizei, GLuint *))                                \
  X(void, BindFramebuffer, (GLenum, GLuint))                                   \
  X(void, DeleteFramebuffers, (GLsizei, const GLuint *))                       \
  X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))       \
  X(GLenum, CheckFramebufferStatus, (GLenum))                                  \
  X(void, BlitFramebuffer,                                                     \
    (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,       \
     GLenum))                                                                  \
  X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                 \
  X(void, BlendEquationSeparate, (GLenum, GLenum))                             \
  X(void, GenSamplers, (GLsizei, GLuint *))                                    \
  X(void, BindSampler, (GLuint, GLuint))                                       \
  X(void, SamplerParameteri, (GLuint, GLenum, GLint))                          \
  X(void, DeleteSamplers, (GLsizei, const GLuint *))

struct Api {
#define ABCUI_GL_DECLARE(result, name, args)                                   \
  using name##Fn = result(APIENTRY *) args;                                    \
  name##Fn name = nullptr;
  ABCUI_GL_FUNCTIONS(ABCUI_GL_DECLARE)
#undef ABCUI_GL_DECLARE

  const char *load() {
#define ABCUI_GL_LOAD(result, name, args)                                      \
  {                                                                            \
    const auto address = wglGetProcAddress("gl" #name);                        \
    const auto value = reinterpret_cast<std::intptr_t>(address);               \
    if (!address || value == 1 || value == 2 || value == 3 || value == -1)     \
      return "gl" #name;                                                       \
    name = reinterpret_cast<name##Fn>(address);                                \
  }
    ABCUI_GL_FUNCTIONS(ABCUI_GL_LOAD)
#undef ABCUI_GL_LOAD
    return nullptr;
  }
};
#undef ABCUI_GL_FUNCTIONS

} // namespace gl
