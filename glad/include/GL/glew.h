#ifndef GLEW_STUB
#define GLEW_STUB
#include "../glad/gl.h"
#define GLEW_EXT_texture_filter_anisotropic GLAD_GL_EXT_texture_filter_anisotropic
#define GLEW_ARB_compatibility GLAD_GL_ARB_compatibility
#define GLEW_EXT_texture_compression_s3tc GLAD_GL_EXT_texture_compression_s3tc
#define GLEW_ARB_framebuffer_object GLAD_GL_ARB_framebuffer_object
#define GLEW_EXT_framebuffer_object GLAD_GL_EXT_framebuffer_object
#define GLEW_KHR_debug GLAD_GL_KHR_debug
#define GLEW_OK 0
static unsigned char glewExperimental __attribute__((unused));
static inline unsigned int __attribute__((unused)) glewInit() { return gladLoaderLoadGL() == 0; }
static inline const char* __attribute__((unused)) glewGetErrorString(unsigned int) { return "glad loader failed"; }
#endif
