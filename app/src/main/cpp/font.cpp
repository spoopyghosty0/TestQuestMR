#include "font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include "log.h"
#include "stb_easy_font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

namespace {
// The em size of scale 1 in units: Roboto's cap height (0.711 em) is then stb_easy_font's 7 units, and a line is
// about as wide as it was with stb_easy_font, so the panel's columns still fit.
constexpr float kEm = 11.0f;
constexpr float kBaseline = 7.5f;  // units below the line's top

struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
    std::string name = "stb_easy_font";
};

Font &the_font() {
    static Font f;
    return f;
}

unsigned char printable(char c) {
    unsigned char u = (unsigned char)c;
    return u >= 32 && u < 127 ? u : '?';
}

int easy_width(unsigned char c) {
    char s[2] = {(char)c, 0};
    return stb_easy_font_width(s);
}
}  // namespace

namespace font {
bool load(const std::vector<std::string> &paths) {
    Font &f = the_font();
    for (const std::string &p : paths) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (data.size() < 1024) continue;
        int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
        f.data = std::move(data);
        if (offset >= 0 && stbtt_InitFont(&f.info, f.data.data(), offset) && stbtt_FindGlyphIndex(&f.info, 'A')) {
            f.ok = true;
            f.name = p;
            LOGI("font: %s", p.c_str());
            return true;
        }
        f.data.clear();
    }
    LOGW("font: no TrueType font found, using stb_easy_font");
    return false;
}

const std::string &name() { return the_font().name; }

float width(const std::string &s, float scale) {
    Font &f = the_font();
    if (!f.ok) return (float)stb_easy_font_width(const_cast<char *>(s.c_str())) * scale;
    float k = stbtt_ScaleForMappingEmToPixels(&f.info, kEm * scale);
    float w = 0;
    int prev = 0;
    for (char ch : s) {
        int c = printable(ch), adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, c, &adv, &lsb);
        if (prev) w += stbtt_GetCodepointKernAdvance(&f.info, prev, c) * k;
        w += adv * k;
        prev = c;
    }
    return w;
}
}  // namespace font

GlyphAtlas::GlyphAtlas(float texels_per_unit, bool mipmaps, int size)
    : tpu_(texels_per_unit), mipmaps_(mipmaps), size_(size), pad_(mipmaps ? 8 : 2) {
    pixels_.assign((size_t)size_ * size_, 0);
    shelf_x_ = shelf_y_ = pad_;
}

const GlyphAtlas::Glyph &GlyphAtlas::glyph(int px, unsigned char c) {
    int key = px << 8 | c;
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return it->second;
    Glyph &g = glyphs_[key];
    Font &f = the_font();
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float k = 0;   // TrueType: font units -> texels
    int easy = 0;  // stb_easy_font: texels per stroke unit
    if (f.ok) {
        k = stbtt_ScaleForMappingEmToPixels(&f.info, (float)px);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, c, &adv, &lsb);
        g.advance = adv * k;
        stbtt_GetCodepointBitmapBox(&f.info, c, k, k, &x0, &y0, &x1, &y1);
    } else {
        float per_unit = px / kEm;
        easy = std::max(1, (int)std::lround(per_unit));
        g.advance = easy_width(c) * per_unit;
        x0 = 0, y0 = -(int)kBaseline * easy, x1 = easy_width(c) * easy, y1 = 3 * easy;
    }
    g.ox = x0, g.oy = y0, g.w = x1 - x0, g.h = y1 - y0;
    if (g.w <= 0 || g.h <= 0) {  // space
        g.w = g.h = 0;
        g.ok = true;
        return g;
    }
    if (shelf_x_ + g.w + pad_ > size_) {
        shelf_x_ = pad_;
        shelf_y_ += shelf_h_ + pad_;
        shelf_h_ = 0;
    }
    if (shelf_y_ + g.h + pad_ > size_) {
        if (!full_logged_) LOGE("glyph atlas full (%d px)", size_);
        full_logged_ = true;
        return g;  // not drawn
    }
    g.x = shelf_x_, g.y = shelf_y_;
    shelf_x_ += g.w + pad_;
    shelf_h_ = std::max(shelf_h_, g.h);
    unsigned char *dst = &pixels_[(size_t)g.y * size_ + g.x];
    if (f.ok) {
        stbtt_MakeCodepointBitmap(&f.info, dst, g.w, g.h, size_, k, k, c);
    } else {
        static char buf[4096];
        char s[2] = {(char)c, 0};
        int quads = stb_easy_font_print(0, 0, s, nullptr, buf, sizeof(buf));
        for (int q = 0; q < quads; ++q) {
            const float *v = (const float *)(buf + q * 64);  // 4 vertices of 16 bytes: x, y, z, colour
            int qx0 = (int)v[0] * easy, qy0 = (int)v[1] * easy, qx1 = (int)v[8] * easy, qy1 = (int)v[9] * easy;
            for (int y = std::max(0, qy0); y < std::min(g.h, qy1); ++y)
                for (int x = std::max(0, qx0); x < std::min(g.w, qx1); ++x) dst[(size_t)y * size_ + x] = 255;
        }
    }
    g.ok = true;
    dirty_ = true;
    return g;
}

float GlyphAtlas::emit(std::vector<TexVertex> &out, const std::string &s, float x, float y, float scale,
                       const float color[4], const std::function<Vec3(float, float)> &map) {
    Font &f = the_font();
    int px = std::max(4, (int)std::lround(kEm * scale * tpu_));
    float kern_k = f.ok ? stbtt_ScaleForMappingEmToPixels(&f.info, (float)px) : 0;
    float pen = x * tpu_;
    float base = std::round((y + kBaseline * scale) * tpu_);
    const float inv = 1.0f / size_;
    int prev = 0;
    for (char ch : s) {
        unsigned char c = printable(ch);
        if (f.ok && prev) pen += stbtt_GetCodepointKernAdvance(&f.info, prev, c) * kern_k;
        prev = c;
        const Glyph &g = glyph(px, c);
        if (g.ok && g.w > 0) {
            float gx0 = std::round(pen) + g.ox, gy0 = base + g.oy, gx1 = gx0 + g.w, gy1 = gy0 + g.h;
            float u0 = g.x * inv, v0 = g.y * inv, u1 = (g.x + g.w) * inv, v1 = (g.y + g.h) * inv;
            Vec3 p00 = map(gx0 / tpu_, gy0 / tpu_), p10 = map(gx1 / tpu_, gy0 / tpu_), p11 = map(gx1 / tpu_, gy1 / tpu_),
                 p01 = map(gx0 / tpu_, gy1 / tpu_);
            auto vtx = [&](const Vec3 &p, float u, float v) {
                out.push_back({p.x, p.y, p.z, u, v, color[0], color[1], color[2], color[3]});
            };
            vtx(p00, u0, v0), vtx(p10, u1, v0), vtx(p11, u1, v1);
            vtx(p00, u0, v0), vtx(p11, u1, v1), vtx(p01, u0, v1);
        }
        pen += g.advance;
    }
    return pen / tpu_;
}

GLuint GlyphAtlas::texture() {
    if (!tex_) {
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        int levels = 1;
        if (mipmaps_)
            for (int s = size_; s > 1; s /= 2) ++levels;
        glTexStorage2D(GL_TEXTURE_2D, levels, GL_R8, size_, size_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps_ ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (mipmaps_) {
            // labels are seen at an angle too
            const char *ext = (const char *)glGetString(GL_EXTENSIONS);
            bool aniso = ext && std::strstr(ext, "GL_EXT_texture_filter_anisotropic");
            GLint n = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &n);
            for (GLint i = 0; i < n && !aniso; ++i) {
                const char *e = (const char *)glGetStringi(GL_EXTENSIONS, i);
                aniso = e && std::strcmp(e, "GL_EXT_texture_filter_anisotropic") == 0;
            }
            if (aniso) glTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, 4.0f);
            glGetError();  // GL_EXTENSIONS with glGetString is an error in some core contexts
        }
    }
    if (dirty_) {
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size_, size_, GL_RED, GL_UNSIGNED_BYTE, pixels_.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (mipmaps_) glGenerateMipmap(GL_TEXTURE_2D);
        dirty_ = false;
    }
    return tex_;
}

GlyphAtlas &panel_atlas() {
    static GlyphAtlas a(kPanelTexelsPerUnit, false);
    return a;
}

GlyphAtlas &label_atlas() {
    // labels: about 0.0016 m per unit up to 0.0035 m far away; 4 texels per unit stay sharp close up, the mipmaps
    // keep them calm further away
    static GlyphAtlas a(4.0f, true);
    return a;
}
