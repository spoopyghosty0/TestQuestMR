// Text: a TrueType font (the system's Roboto on Android) rasterised with stb_truetype into glyph atlases at the exact
// size each atlas draws it, so a glyph lands 1:1 on texels. Without a font file the stb_easy_font strokes are
// rasterised into the atlas instead.
//
// Sizes are in "units": the panel's layout pixels (the old stb_easy_font pixels). A line of scale 1 has its top at y,
// its baseline at y + 7 and the cap height of stb_easy_font; text_width() measures in units.
#pragma once

#include <GLES3/gl3.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "xmath.h"

struct TexVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

namespace font {
// Loads the first readable TrueType file of `paths`; false: none (the stb_easy_font fallback draws).
bool load(const std::vector<std::string> &paths);
const std::string &name();  // file used, or "stb_easy_font"
float width(const std::string &s, float scale = 1.0f);
}  // namespace font

// Glyphs of the font packed into one R8 texture, rasterised at texels_per_unit x scale.
class GlyphAtlas {
public:
    GlyphAtlas(float texels_per_unit, bool mipmaps, int size = 1024);
    // Appends `s` as textured quads; `map` turns unit coordinates into vertex positions. Glyph corners are snapped to
    // whole texels (pixel-aligned in a texture drawn at texels_per_unit). Returns the end x.
    float emit(std::vector<TexVertex> &out, const std::string &s, float x, float y, float scale, const float color[4],
               const std::function<Vec3(float, float)> &map);
    // The texture with every glyph emitted so far (uploads new ones; needs the GL context).
    GLuint texture();
    float texels_per_unit() const { return tpu_; }

private:
    struct Glyph {
        int x = 0, y = 0, w = 0, h = 0;  // in the atlas
        int ox = 0, oy = 0;              // bitmap offset from the pen position on the baseline (texels)
        float advance = 0;               // texels
        bool ok = false;
    };
    const Glyph &glyph(int px, unsigned char c);
    float tpu_;
    bool mipmaps_;
    int size_, pad_;
    int shelf_x_ = 0, shelf_y_ = 0, shelf_h_ = 0;
    std::vector<unsigned char> pixels_;
    std::map<int, Glyph> glyphs_;  // px << 8 | char
    GLuint tex_ = 0;
    bool dirty_ = true, full_logged_ = false;
};

// The panel texture's resolution: texels per panel unit.
constexpr float kPanelTexelsPerUnit = 2.0f;

// The panel's atlas (drawn into the panel texture at kPanelTexelsPerUnit) and the world labels' atlas (mipmapped).
GlyphAtlas &panel_atlas();
GlyphAtlas &label_atlas();
