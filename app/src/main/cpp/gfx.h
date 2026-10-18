// EGL context, shaders and meshes (GLES 3).
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <vector>

#include "xmath.h"

struct Gl {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    bool init();
    void shutdown();
};

// A lit triangle mesh: position and normal interleaved (6 floats per vertex).
struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
    void upload(const std::vector<float> &interleaved, const std::vector<uint32_t> &indices);
    void draw() const;
    void release();
    bool empty() const { return count == 0; }
};

// A flat-coloured vertex for panels, text, lines and translucent fills.
struct FlatVertex {
    float x, y, z;
    float r, g, b, a;
};

// Flat-coloured geometry kept on the GPU (big wireframes that don't change every frame).
struct FlatBuffer {
    GLuint vao = 0, vbo = 0;
    GLsizei count = 0;
    void upload(const std::vector<FlatVertex> &v);
    void release();
    bool empty() const { return count == 0; }
};

class Renderer {
public:
    bool init();
    void begin_eye(const Mat4 &view_proj);
    // Lit mesh in linear RGB. normal_color: colour from the surface normal instead (|n| -> RGB), so walls, floor and
    // furniture tops read apart even without lighting.
    void draw_mesh(const Mesh &mesh, const Mat4 &model, const Vec3 &color, float alpha, bool normal_color = false);
    void draw_flat(const std::vector<FlatVertex> &triangles);
    void draw_lines(const std::vector<FlatVertex> &lines);
    void draw_buffer(const FlatBuffer &buffer, GLenum mode, const Mat4 &model, float alpha_scale = 1.0f);
    Mesh sphere;  // unit sphere for markers

private:
    GLuint lit_ = 0, flat_ = 0;
    GLint lit_mvp_ = -1, lit_model_ = -1, lit_color_ = -1, lit_alpha_ = -1, lit_normal_color_ = -1, flat_mvp_ = -1,
          flat_alpha_ = -1;
    GLuint flat_vao_ = 0, flat_vbo_ = 0;
    Mat4 view_proj_ = Mat4::identity();
    void flat(const std::vector<FlatVertex> &v, GLenum mode);
};

// Framebuffers for one swapchain (one per image) with a shared depth buffer.
struct EyeTarget {
    std::vector<GLuint> fbos;
    GLuint depth = 0;
    int width = 0, height = 0;
    void create(const std::vector<GLuint> &color_textures, int w, int h);
    void release();
};
