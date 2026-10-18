#include "gfx.h"

#include <EGL/eglext.h>

#include "log.h"

bool Gl::init() {
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
        LOGE("eglInitialize failed");
        return false;
    }
    const EGLint attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                              EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLES, 0,
                              EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                              EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(display, attribs, &config, 1, &count) || count == 0) {
        LOGE("eglChooseConfig found no config");
        return false;
    }
    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    const EGLint surf_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, surf_attribs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        LOGE("EGL context setup failed: 0x%x", eglGetError());
        return false;
    }
    LOGI("GL: %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    return true;
}

void Gl::shutdown() {
    if (display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    eglTerminate(display);
    display = EGL_NO_DISPLAY;
}

void Mesh::upload(const std::vector<float> &interleaved, const std::vector<uint32_t> &indices) {
    release();
    if (indices.empty()) return;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    count = (GLsizei)indices.size();
    glBindVertexArray(0);
}

void Mesh::draw() const {
    if (!count) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

void Mesh::release() {
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = ibo = 0;
    count = 0;
}

static void flat_attribs() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void *)(3 * sizeof(float)));
}

void FlatBuffer::upload(const std::vector<FlatVertex> &v) {
    release();
    if (v.empty()) return;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(FlatVertex), v.data(), GL_STATIC_DRAW);
    flat_attribs();
    glBindVertexArray(0);
    count = (GLsizei)v.size();
}

void FlatBuffer::release() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = 0;
    count = 0;
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_second");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
    }
    return p;
}

static const char *kLitVs = R"(#version 300 es
in vec3 a_pos;
in vec3 a_second;
uniform mat4 u_mvp;
uniform mat4 u_model;
out vec3 v_normal;
void main() {
    v_normal = mat3(u_model) * a_second;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kLitFs = R"(#version 300 es
precision mediump float;
in vec3 v_normal;
uniform vec3 u_color;
uniform float u_alpha;
uniform float u_normal_color;
out vec4 o_color;
void main() {
    vec3 n = normalize(v_normal);
    float key = abs(dot(n, normalize(vec3(0.3, 0.8, 0.5))));
    float fill = abs(dot(n, normalize(vec3(-0.6, -0.2, 0.3)))) * 0.3;
    // normal colour, muted: walls facing different ways, floor and table tops read apart without shouting
    vec3 by_normal = mix(vec3(0.42, 0.46, 0.52), abs(n.zxy) * vec3(0.55, 0.75, 0.9) + vec3(0.2, 0.2, 0.25), 0.55);
    vec3 base = mix(u_color, by_normal, u_normal_color);
    o_color = vec4(base * (0.45 + 0.6 * key + fill), u_alpha);
}
)";

static const char *kFlatVs = R"(#version 300 es
in vec3 a_pos;
in vec4 a_second;
uniform mat4 u_mvp;
uniform float u_alpha;
out vec4 v_color;
void main() {
    v_color = vec4(a_second.rgb, a_second.a * u_alpha);
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kFlatFs = R"(#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 o_color;
void main() { o_color = v_color; }
)";

static const char *kTexVs = R"(#version 300 es
in vec3 a_pos;
in vec2 a_uv;
in vec4 a_color;
uniform mat4 u_mvp;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char *kTexFs = R"(#version 300 es
precision mediump float;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_tex;
uniform float u_image;
out vec4 o_color;
void main() {
    vec4 t = texture(u_tex, v_uv);
    // glyphs: coverage in R; image: premultiplied RGBA, scaled by the vertex alpha
    o_color = u_image > 0.5 ? t * v_color.a : vec4(v_color.rgb, v_color.a * t.r);
}
)";

static void make_sphere(Mesh &mesh) {
    std::vector<float> v;
    std::vector<uint32_t> idx;
    const int nu = 16, nv = 10;
    for (int j = 0; j <= nv; ++j) {
        float ph = kPi * ((float)j / nv - 0.5f);
        for (int i = 0; i <= nu; ++i) {
            float th = 2 * kPi * i / nu;
            float x = std::cos(ph) * std::cos(th), y = std::sin(ph), z = std::cos(ph) * std::sin(th);
            v.insert(v.end(), {x, y, z, x, y, z});
        }
    }
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            uint32_t a = j * (nu + 1) + i, b = a + 1, c = a + nu + 1, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    mesh.upload(v, idx);
}

bool Renderer::init() {
    lit_ = program(kLitVs, kLitFs);
    lit_mvp_ = glGetUniformLocation(lit_, "u_mvp");
    lit_model_ = glGetUniformLocation(lit_, "u_model");
    lit_color_ = glGetUniformLocation(lit_, "u_color");
    lit_alpha_ = glGetUniformLocation(lit_, "u_alpha");
    lit_normal_color_ = glGetUniformLocation(lit_, "u_normal_color");
    flat_ = program(kFlatVs, kFlatFs);
    flat_mvp_ = glGetUniformLocation(flat_, "u_mvp");
    flat_alpha_ = glGetUniformLocation(flat_, "u_alpha");
    {
        GLuint p = glCreateProgram();
        glAttachShader(p, compile(GL_VERTEX_SHADER, kTexVs));
        glAttachShader(p, compile(GL_FRAGMENT_SHADER, kTexFs));
        glBindAttribLocation(p, 0, "a_pos");
        glBindAttribLocation(p, 1, "a_uv");
        glBindAttribLocation(p, 2, "a_color");
        glLinkProgram(p);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) LOGE("text program link failed");
        tex_ = p;
        tex_mvp_ = glGetUniformLocation(p, "u_mvp");
        tex_image_ = glGetUniformLocation(p, "u_image");
        glUseProgram(p);
        glUniform1i(glGetUniformLocation(p, "u_tex"), 0);
        glGenVertexArrays(1, &tex_vao_);
        glBindVertexArray(tex_vao_);
        glGenBuffers(1, &tex_vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, tex_vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TexVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TexVertex), (void *)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TexVertex), (void *)(5 * sizeof(float)));
        glBindVertexArray(0);
    }
    glGenVertexArrays(1, &flat_vao_);
    glBindVertexArray(flat_vao_);
    glGenBuffers(1, &flat_vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, flat_vbo_);
    flat_attribs();
    glBindVertexArray(0);
    make_sphere(sphere);
    return lit_ && flat_ && tex_;
}

void Renderer::begin_eye(const Mat4 &view_proj) { view_proj_ = view_proj; }

void Renderer::draw_mesh(const Mesh &mesh, const Mat4 &model, const Vec3 &color, float alpha, bool normal_color) {
    glUseProgram(lit_);
    Mat4 mvp = view_proj_ * model;
    glUniformMatrix4fv(lit_mvp_, 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(lit_model_, 1, GL_FALSE, model.m);
    glUniform3f(lit_color_, color.x, color.y, color.z);
    glUniform1f(lit_alpha_, alpha);
    glUniform1f(lit_normal_color_, normal_color ? 1.0f : 0.0f);
    mesh.draw();
}

void Renderer::flat(const std::vector<FlatVertex> &v, GLenum mode) {
    if (v.empty()) return;
    glUseProgram(flat_);
    glUniformMatrix4fv(flat_mvp_, 1, GL_FALSE, view_proj_.m);
    glUniform1f(flat_alpha_, 1.0f);
    glBindVertexArray(flat_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, flat_vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(FlatVertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)v.size());
}

void Renderer::draw_flat(const std::vector<FlatVertex> &triangles) { flat(triangles, GL_TRIANGLES); }
void Renderer::draw_lines(const std::vector<FlatVertex> &lines) { flat(lines, GL_LINES); }

void Renderer::draw_buffer(const FlatBuffer &buffer, GLenum mode, const Mat4 &model, float alpha_scale) {
    if (buffer.empty()) return;
    glUseProgram(flat_);
    Mat4 mvp = view_proj_ * model;
    glUniformMatrix4fv(flat_mvp_, 1, GL_FALSE, mvp.m);
    glUniform1f(flat_alpha_, alpha_scale);
    glBindVertexArray(buffer.vao);
    glDrawArrays(mode, 0, buffer.count);
}

void Renderer::textured(const std::vector<TexVertex> &v, GLuint texture, bool image) {
    if (v.empty()) return;
    glUseProgram(tex_);
    glUniformMatrix4fv(tex_mvp_, 1, GL_FALSE, view_proj_.m);
    glUniform1f(tex_image_, image ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(tex_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, tex_vbo_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(TexVertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
}

void Renderer::draw_text(const std::vector<TexVertex> &quads, GLuint atlas) { textured(quads, atlas, false); }

void Renderer::draw_image(const std::vector<TexVertex> &quads, GLuint texture) {
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    textured(quads, texture, true);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void EyeTarget::create(const std::vector<GLuint> &color_textures, int w, int h, bool depth_buffer) {
    width = w;
    height = h;
    if (depth_buffer) {
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    }
    fbos.resize(color_textures.size());
    glGenFramebuffers((GLsizei)fbos.size(), fbos.data());
    for (size_t i = 0; i < fbos.size(); ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_textures[i], 0);
        if (depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) LOGE("framebuffer incomplete: 0x%x", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void EyeTarget::release() {
    if (!fbos.empty()) glDeleteFramebuffers((GLsizei)fbos.size(), fbos.data());
    fbos.clear();
    if (depth) glDeleteRenderbuffers(1, &depth);
    depth = 0;
}
