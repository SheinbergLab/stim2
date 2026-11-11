/*
 * overlay.cpp -- annotation layer drawn over stim2's scene (see overlay.h)
 *
 * Each item is a set of discs of one size and colour, in degrees, and may
 * carry a text label.  Drawing builds the discs' triangles on the CPU (a
 * gaze marker and a short trail are a few dozen discs) and issues one draw
 * call per item with a small program of its own, so it depends on nothing
 * in the object list.  Text is drawn from a glyph atlas made here with
 * stb_truetype (all of it static to this file, so it cannot meet the text
 * module's own copy), one atlas per font and pixel size, rasterized at the
 * size it is shown so it is sharp in a movie as on the screen.
 */
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <glad/glad.h>

#include "overlay.h"

#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "imstb_rectpack.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

void log_message(const char *level, const char *fmt, ...);

namespace {

struct Item {
  std::string name;
  std::vector<float> pts;   /* x0 y0 x1 y1 ..., degrees */
  float size = 0.5f;        /* disc diameter, degrees */
  float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
  bool show = true;
  /* line: the points are a polyline, `size` degrees wide, drawn once per
     pixel (where it crosses itself it does not darken); fade: its alpha
     falls from the first point (full) to the last (0) -- a trail given
     newest first */
  bool line = false;
  bool fade = false;
  /* a text label: drawn when text is not empty, `size` degrees a line */
  std::string text;
  float pos[2] = { 0.0f, 0.0f };   /* the anchor point, degrees */
  std::string anchor = "c";        /* which part of the text is at pos */
  float bg[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   /* a box behind it (a > 0) */
  std::string font;                /* "" = the default font */
};

std::vector<Item> items;    /* in draw order: first added, drawn first */

GLuint prog = 0, vao = 0, vbo = 0;
GLint u_half = -1, u_color = -1;
bool gl_failed = false;
std::vector<float> verts;

const int SEGMENTS = 24;

Item *find(const char *name)
{
  for (auto &it : items) if (it.name == name) return &it;
  return nullptr;
}

#ifdef STIM2_USE_GLES
const char *VS_HEAD = "#version 300 es\n";
const char *FS_HEAD = "#version 300 es\nprecision mediump float;\n";
#else
const char *VS_HEAD = "#version 150\n";
const char *FS_HEAD = "#version 150\n";
#endif

const char *VS_BODY =
  "in vec2 pos;\n"
  "uniform vec2 halfdeg;\n"
  "void main() { gl_Position = vec4(pos / halfdeg, 0.0, 1.0); }\n";
const char *FS_BODY =
  "uniform vec4 color;\n"
  "out vec4 frag;\n"
  "void main() { frag = color; }\n";

GLuint compile(GLenum type, const char *head, const char *body)
{
  GLuint s = glCreateShader(type);
  const char *src[2] = { head, body };
  glShaderSource(s, 2, src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    log_message("error", "overlay: shader: %s", log);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

bool ensure_gl(void)
{
  if (prog) return true;
  if (gl_failed) return false;
  GLuint vs = compile(GL_VERTEX_SHADER, VS_HEAD, VS_BODY);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FS_HEAD, FS_BODY);
  if (!vs || !fs) { gl_failed = true; return false; }
  prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glBindAttribLocation(prog, 0, "pos");
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    log_message("error", "overlay: program did not link");
    glDeleteProgram(prog);
    prog = 0;
    gl_failed = true;
    return false;
  }
  u_half = glGetUniformLocation(prog, "halfdeg");
  u_color = glGetUniformLocation(prog, "color");
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  return true;
}

/* ---------------------------------------------------------------------
 * lines: a triangle strip in degrees with an alpha per vertex
 * --------------------------------------------------------------------- */
const char *LVS_BODY =
  "in vec2 pos;\n"
  "in float a;\n"
  "uniform vec2 halfdeg;\n"
  "out float v_a;\n"
  "void main() { v_a = a; gl_Position = vec4(pos / halfdeg, 0.0, 1.0); }\n";
const char *LFS_BODY =
  "in float v_a;\n"
  "uniform vec4 color;\n"
  "out vec4 frag;\n"
  "void main() { frag = vec4(color.rgb, color.a * v_a); }\n";

GLuint lprog = 0, lvao = 0, lvbo = 0;
GLint lu_half = -1, lu_color = -1;
bool lgl_failed = false;
std::vector<float> lverts;

bool ensure_line_gl(void)
{
  if (lprog) return true;
  if (lgl_failed) return false;
  GLuint vs = compile(GL_VERTEX_SHADER, VS_HEAD, LVS_BODY);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FS_HEAD, LFS_BODY);
  if (!vs || !fs) { lgl_failed = true; return false; }
  lprog = glCreateProgram();
  glAttachShader(lprog, vs);
  glAttachShader(lprog, fs);
  glBindAttribLocation(lprog, 0, "pos");
  glBindAttribLocation(lprog, 1, "a");
  glLinkProgram(lprog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(lprog, GL_LINK_STATUS, &ok);
  if (!ok) {
    log_message("error", "overlay: line program did not link");
    glDeleteProgram(lprog);
    lprog = 0;
    lgl_failed = true;
    return false;
  }
  lu_half = glGetUniformLocation(lprog, "halfdeg");
  lu_color = glGetUniformLocation(lprog, "color");
  glGenVertexArrays(1, &lvao);
  glGenBuffers(1, &lvbo);
  return true;
}

/* the strip for item it's points: two vertices a point, on either side
   of it along the joint's miter (capped at twice the half width, so a
   sharp turn does not spike), each {x y alpha}.  Returns the vertices. */
int build_line(const Item &it)
{
  std::vector<float> p;
  size_t m = it.pts.size() / 2;
  for (size_t i = 0; i < m; i++) {      /* repeated points have no direction */
    float x = it.pts[2 * i], y = it.pts[2 * i + 1];
    if (!p.empty() && fabsf(x - p[p.size() - 2]) < 1e-5f && fabsf(y - p.back()) < 1e-5f) continue;
    p.push_back(x); p.push_back(y);
  }
  int n = (int) p.size() / 2;
  lverts.clear();
  if (n < 2) return 0;
  float hw = it.size / 2.0f;
  auto dir = [&](int a, int b, float &dx, float &dy) {
    dx = p[2 * b] - p[2 * a]; dy = p[2 * b + 1] - p[2 * a + 1];
    float l = sqrtf(dx * dx + dy * dy);
    dx /= l; dy /= l;
  };
  for (int i = 0; i < n; i++) {
    float nx, ny;                       /* the offset direction */
    float len = hw;
    if (i == 0 || i == n - 1) {
      float dx, dy;
      if (i == 0) dir(0, 1, dx, dy); else dir(n - 2, n - 1, dx, dy);
      nx = -dy; ny = dx;
    } else {
      float ax, ay, bx, by;
      dir(i - 1, i, ax, ay); dir(i, i + 1, bx, by);
      float mx = -ay - by, my = ax + bx;    /* the two normals' sum */
      float ml = sqrtf(mx * mx + my * my);
      if (ml < 1e-4f) { nx = -by; ny = bx; }   /* a reversal */
      else {
        nx = mx / ml; ny = my / ml;
        float c = nx * -by + ny * bx;           /* cos of half the turn */
        len = c > 0.5f ? hw / c : 2.0f * hw;
      }
    }
    float a = it.fade ? 1.0f - (float) i / (float) (n - 1) : 1.0f;
    float x = p[2 * i], y = p[2 * i + 1];
    float v[6] = { x + nx * len, y + ny * len, a, x - nx * len, y - ny * len, a };
    lverts.insert(lverts.end(), v, v + 6);
  }
  return 2 * n;
}

/* ---------------------------------------------------------------------
 * text: textured quads in window pixels (y down), alpha from the atlas,
 * or solid (the box behind a label)
 * --------------------------------------------------------------------- */
const char *TVS_BODY =
  "in vec2 pos;\n"
  "in vec2 uv;\n"
  "uniform vec2 px;\n"
  "out vec2 v_uv;\n"
  "void main() {\n"
  "  v_uv = uv;\n"
  "  gl_Position = vec4(pos.x / px.x * 2.0 - 1.0, 1.0 - pos.y / px.y * 2.0, 0.0, 1.0);\n"
  "}\n";
const char *TFS_BODY =
  "in vec2 v_uv;\n"
  "uniform sampler2D tex;\n"
  "uniform vec4 color;\n"
  "uniform float solid;\n"
  "out vec4 frag;\n"
  "void main() {\n"
  "  float a = solid > 0.5 ? 1.0 : texture(tex, v_uv).r;\n"
  "  frag = vec4(color.rgb, color.a * a);\n"
  "}\n";

GLuint tprog = 0, tvao = 0, tvbo = 0;
GLint tu_px = -1, tu_color = -1, tu_solid = -1, tu_tex = -1;
bool tgl_failed = false;
std::vector<float> tverts;

bool ensure_text_gl(void)
{
  if (tprog) return true;
  if (tgl_failed) return false;
  GLuint vs = compile(GL_VERTEX_SHADER, VS_HEAD, TVS_BODY);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FS_HEAD, TFS_BODY);
  if (!vs || !fs) { tgl_failed = true; return false; }
  tprog = glCreateProgram();
  glAttachShader(tprog, vs);
  glAttachShader(tprog, fs);
  glBindAttribLocation(tprog, 0, "pos");
  glBindAttribLocation(tprog, 1, "uv");
  glLinkProgram(tprog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(tprog, GL_LINK_STATUS, &ok);
  if (!ok) {
    log_message("error", "overlay: text program did not link");
    glDeleteProgram(tprog);
    tprog = 0;
    tgl_failed = true;
    return false;
  }
  tu_px = glGetUniformLocation(tprog, "px");
  tu_color = glGetUniformLocation(tprog, "color");
  tu_solid = glGetUniformLocation(tprog, "solid");
  tu_tex = glGetUniformLocation(tprog, "tex");
  glGenVertexArrays(1, &tvao);
  glGenBuffers(1, &tvbo);
  return true;
}

/* where fonts are: $STIM2_FONT_PATH, the app bundle's Resources/fonts,
   /usr/local/stim2/fonts, ./fonts -- as the text module looks */
std::string font_dir(void)
{
  static std::string dir;
  if (!dir.empty()) return dir;
  const char *env = getenv("STIM2_FONT_PATH");
  if (env && *env) return dir = env;
  struct stat st;
#ifdef __APPLE__
  CFBundleRef b = CFBundleGetMainBundle();
  if (b) {
    CFURLRef u = CFBundleCopyResourcesDirectoryURL(b);
    if (u) {
      char buf[1024];
      if (CFURLGetFileSystemRepresentation(u, TRUE, (UInt8 *) buf, sizeof(buf))) {
        std::string d = std::string(buf) + "/fonts";
        if (stat(d.c_str(), &st) == 0) dir = d;
      }
      CFRelease(u);
    }
  }
  if (!dir.empty()) return dir;
#endif
#ifdef _WIN32
  if (stat("C:/stim2/fonts", &st) == 0) return dir = "C:/stim2/fonts";
#endif
  if (stat("/usr/local/stim2/fonts", &st) == 0) return dir = "/usr/local/stim2/fonts";
  return dir = "./fonts";
}

const char *DEFAULT_FONT = "NotoSans-Regular.ttf";

std::string font_path(const std::string &name)
{
  std::string n = name.empty() ? DEFAULT_FONT : name;
  if (n[0] == '/' || (n.size() > 1 && n[1] == ':')) return n;
  return font_dir() + "/" + n;
}

struct Font {
  std::vector<unsigned char> data;
  stbtt_fontinfo info;
  bool ok = false;
};
std::map<std::string, Font> fonts;   /* by path */

Font *get_font(const std::string &path)
{
  auto f = fonts.find(path);
  if (f != fonts.end()) return f->second.ok ? &f->second : nullptr;
  Font &F = fonts[path];
  FILE *fp = fopen(path.c_str(), "rb");
  if (!fp) { log_message("error", "overlay: cannot open font %s", path.c_str()); return nullptr; }
  fseek(fp, 0, SEEK_END);
  long n = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  F.data.resize(n > 0 ? n : 0);
  bool got = n > 0 && fread(F.data.data(), 1, n, fp) == (size_t) n;
  fclose(fp);
  if (!got || !stbtt_InitFont(&F.info, F.data.data(), stbtt_GetFontOffsetForIndex(F.data.data(), 0))) {
    log_message("error", "overlay: %s is not a font stb_truetype reads", path.c_str());
    return nullptr;
  }
  F.ok = true;
  return &F;
}

/* the characters an atlas holds: Latin-1, Greek, and the punctuation,
   arrows and math signs a caption is likely to want */
struct Range { int first, count; };
const Range RANGES[] = {
  { 0x20, 0x5f },      /* ASCII */
  { 0xa0, 0x60 },      /* Latin-1: degree, micro, plus-minus, times */
  { 0x391, 0x39 },     /* Greek */
  { 0x2010, 0x17 },    /* dashes, quotes, bullet, ellipsis */
  { 0x2190, 0x04 },    /* arrows */
  { 0x2212, 0x01 },    /* minus */
  { 0x2248, 0x01 },    /* almost equal */
  { 0x2260, 0x06 },    /* not equal, <= >= */
};
const int NRANGES = sizeof(RANGES) / sizeof(RANGES[0]);

struct Atlas {
  GLuint tex = 0;
  int w = 0, h = 0, px = 0;
  float ascent = 0, descent = 0, gap = 0;   /* pixels */
  std::vector<stbtt_packedchar> chars[NRANGES];
  bool ok = false;
};
std::map<std::pair<std::string, int>, Atlas> atlases;   /* by font path, px */

Atlas *get_atlas(const std::string &path, int px)
{
  auto key = std::make_pair(path, px);
  auto a = atlases.find(key);
  if (a != atlases.end()) return a->second.ok ? &a->second : nullptr;
  /* a few sizes at a time is what a caption or two needs: past that,
     start over rather than grow without bound */
  if (atlases.size() >= 12) {
    for (auto &e : atlases) if (e.second.tex) glDeleteTextures(1, &e.second.tex);
    atlases.clear();
  }
  Atlas &A = atlases[key];
  Font *F = get_font(path);
  if (!F) return nullptr;
  A.px = px;
  stbtt_pack_range pr[NRANGES];
  for (int i = 0; i < NRANGES; i++) {
    A.chars[i].resize(RANGES[i].count);
    memset(&pr[i], 0, sizeof(pr[i]));
    pr[i].font_size = (float) px;
    pr[i].first_unicode_codepoint_in_range = RANGES[i].first;
    pr[i].num_chars = RANGES[i].count;
    pr[i].chardata_for_range = A.chars[i].data();
  }
  std::vector<unsigned char> pixels;
  bool packed = false;
  for (int side = 256; side <= 4096 && !packed; side *= 2) {
    pixels.assign((size_t) side * side, 0);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, pixels.data(), side, side, 0, 1, nullptr)) break;
    stbtt_PackSetOversampling(&pc, 1, 1);
    packed = stbtt_PackFontRanges(&pc, F->data.data(), 0, pr, NRANGES) != 0;
    stbtt_PackEnd(&pc);
    A.w = A.h = side;
  }
  if (!packed) {
    log_message("error", "overlay: %d px glyphs of %s do not fit an atlas", px, path.c_str());
    return nullptr;
  }
  int asc, desc, gap;
  stbtt_GetFontVMetrics(&F->info, &asc, &desc, &gap);
  float sc = stbtt_ScaleForPixelHeight(&F->info, (float) px);
  A.ascent = asc * sc; A.descent = desc * sc; A.gap = gap * sc;

  GLint o_tex, o_align;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &o_tex);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &o_align);
  glGenTextures(1, &A.tex);
  glBindTexture(GL_TEXTURE_2D, A.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, A.w, A.h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, o_align);
  glBindTexture(GL_TEXTURE_2D, o_tex);
  A.ok = true;
  return &A;
}

/* the next code point of UTF-8 s at i (advancing i); bad bytes are '?' */
int utf8_next(const std::string &s, size_t &i)
{
  unsigned char c = s[i++];
  if (c < 0x80) return c;
  int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
  if (n < 0) return '?';
  int cp = c & (0x3f >> n);
  for (int k = 0; k < n; k++) {
    if (i >= s.size() || (s[i] & 0xc0) != 0x80) return '?';
    cp = (cp << 6) | (s[i++] & 0x3f);
  }
  return cp;
}

/* where code point cp is in the atlas: {range, index}, '?' when absent */
void glyph_of(int cp, int &r, int &k)
{
  for (r = 0; r < NRANGES; r++) {
    k = cp - RANGES[r].first;
    if (k >= 0 && k < RANGES[r].count) return;
  }
  r = 0; k = '?' - RANGES[0].first;
}

float line_width(Atlas *A, const std::string &line)
{
  float x = 0, y = 0;
  stbtt_aligned_quad q;
  for (size_t i = 0; i < line.size();) {
    int r, k;
    glyph_of(utf8_next(line, i), r, k);
    stbtt_GetPackedQuad(A->chars[r].data(), A->w, A->h, k, &x, &y, &q, 1);
  }
  return x;
}

void push_quad(std::vector<float> &v, float x0, float y0, float x1, float y1,
               float s0, float t0, float s1, float t1)
{
  float q[24] = { x0, y0, s0, t0,  x1, y0, s1, t0,  x1, y1, s1, t1,
                  x0, y0, s0, t0,  x1, y1, s1, t1,  x0, y1, s0, t1 };
  v.insert(v.end(), q, q + 24);
}

/* draw one item's label: called with the text program in use, its VAO
   and buffer bound and px set */
void draw_text(Item &it, int w, int h, float halfx, float halfy)
{
  int px = (int) lroundf(it.size * h / (2.0f * halfy));
  if (px < 4) return;
  if (px > 400) px = 400;
  Atlas *A = get_atlas(font_path(it.font), px);
  if (!A) return;

  std::vector<std::string> lines;
  size_t s = 0;
  for (;;) {
    size_t e = it.text.find('\n', s);
    lines.push_back(it.text.substr(s, e == std::string::npos ? std::string::npos : e - s));
    if (e == std::string::npos) break;
    s = e + 1;
  }
  float lh = A->ascent - A->descent + A->gap;
  float bw = 0;
  std::vector<float> lw;
  for (auto &l : lines) { lw.push_back(line_width(A, l)); if (lw.back() > bw) bw = lw.back(); }
  float bh = lh * lines.size() - A->gap;

  /* the anchor point in pixels, y down, and the block's corner from it */
  float ax = (0.5f + it.pos[0] / (2.0f * halfx)) * w;
  float ay = (0.5f - it.pos[1] / (2.0f * halfy)) * h;
  const std::string &an = it.anchor;
  bool W = an.find('w') != std::string::npos, E = an.find('e') != std::string::npos;
  bool N = an.find('n') != std::string::npos, S = an.find('s') != std::string::npos;
  float bx0 = W ? ax : E ? ax - bw : ax - bw / 2;
  float by0 = N ? ay : S ? ay - bh : ay - bh / 2;
  bx0 = floorf(bx0 + 0.5f); by0 = floorf(by0 + 0.5f);

  if (it.bg[3] > 0.0f) {
    float pad = 0.3f * px;
    tverts.clear();
    push_quad(tverts, bx0 - pad, by0 - pad, bx0 + bw + pad, by0 + bh + pad, 0, 0, 0, 0);
    glBufferData(GL_ARRAY_BUFFER, tverts.size() * sizeof(float), tverts.data(), GL_STREAM_DRAW);
    glUniform1f(tu_solid, 1.0f);
    glUniform4fv(tu_color, 1, it.bg);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  tverts.clear();
  for (size_t li = 0; li < lines.size(); li++) {
    float x = W ? bx0 : E ? bx0 + bw - lw[li] : bx0 + (bw - lw[li]) / 2;
    x = floorf(x + 0.5f);
    float y = by0 + li * lh + A->ascent;
    y = floorf(y + 0.5f);
    stbtt_aligned_quad q;
    const std::string &l = lines[li];
    for (size_t i = 0; i < l.size();) {
      int r, k;
      glyph_of(utf8_next(l, i), r, k);
      stbtt_GetPackedQuad(A->chars[r].data(), A->w, A->h, k, &x, &y, &q, 1);
      if (q.x1 > q.x0) push_quad(tverts, q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1);
    }
  }
  if (tverts.empty()) return;
  glBindTexture(GL_TEXTURE_2D, A->tex);
  glBufferData(GL_ARRAY_BUFFER, tverts.size() * sizeof(float), tverts.data(), GL_STREAM_DRAW);
  glUniform1f(tu_solid, 0.0f);
  glUniform4fv(tu_color, 1, it.rgba);
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei) (tverts.size() / 4));
}

} /* namespace */

extern "C" {

void overlaySet(const char *name, const float *pts, int n, float size,
                const float *rgba, int show)
{
  Item *it = find(name);
  if (!it) {
    items.emplace_back();
    it = &items.back();
    it->name = name;
  }
  if (pts && n >= 0) it->pts.assign(pts, pts + 2 * n);
  else if (n == 0) it->pts.clear();
  if (size >= 0.0f) it->size = size;
  if (rgba) memcpy(it->rgba, rgba, sizeof(it->rgba));
  if (show >= 0) it->show = show != 0;
}

void overlaySetStyle(const char *name, int line, int fade)
{
  Item *it = find(name);
  if (!it) {
    items.emplace_back();
    it = &items.back();
    it->name = name;
  }
  if (line >= 0) it->line = line != 0;
  if (fade >= 0) it->fade = fade != 0;
}

int overlayGetStyle(const char *name, int *line, int *fade)
{
  Item *it = find(name);
  if (!it) return 0;
  if (line) *line = it->line;
  if (fade) *fade = it->fade;
  return 1;
}

void overlaySetText(const char *name, const char *text, const float *pos,
                    const char *anchor, const float *bg, const char *font)
{
  Item *it = find(name);
  if (!it) {
    items.emplace_back();
    it = &items.back();
    it->name = name;
  }
  if (text) it->text = text;
  if (pos) { it->pos[0] = pos[0]; it->pos[1] = pos[1]; }
  if (anchor) it->anchor = anchor;
  if (bg) memcpy(it->bg, bg, sizeof(it->bg));
  if (font) it->font = font;
}

int overlayGetText(const char *name, const char **text, float pos[2],
                   const char **anchor, float bg[4], const char **font)
{
  Item *it = find(name);
  if (!it) return 0;
  if (text) *text = it->text.c_str();
  if (pos) { pos[0] = it->pos[0]; pos[1] = it->pos[1]; }
  if (anchor) *anchor = it->anchor.c_str();
  if (bg) memcpy(bg, it->bg, sizeof(it->bg));
  if (font) *font = it->font.c_str();
  return 1;
}

int overlayDelete(const char *name)
{
  for (size_t i = 0; i < items.size(); i++)
    if (items[i].name == name) { items.erase(items.begin() + i); return 1; }
  return 0;
}

void overlayClear(void) { items.clear(); }
int overlayCount(void) { return (int) items.size(); }

const char *overlayName(int i)
{
  return i >= 0 && i < (int) items.size() ? items[i].name.c_str() : nullptr;
}

int overlayGet(const char *name, int *npts, float *size, float rgba[4], int *show)
{
  Item *it = find(name);
  if (!it) return 0;
  if (npts) *npts = (int) it->pts.size() / 2;
  if (size) *size = it->size;
  if (rgba) memcpy(rgba, it->rgba, sizeof(it->rgba));
  if (show) *show = it->show;
  return 1;
}

int overlayDraw(int w, int h, float halfx, float halfy)
{
  if (items.empty() || halfx <= 0.0f || halfy <= 0.0f) return 0;
  if (!ensure_gl()) return 0;

  /* what the scene's code may have left set, put back afterwards */
  GLint o_prog, o_vao, o_buf, o_vp[4], o_src_rgb, o_dst_rgb, o_src_a, o_dst_a;
  glGetIntegerv(GL_CURRENT_PROGRAM, &o_prog);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &o_vao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &o_buf);
  glGetIntegerv(GL_VIEWPORT, o_vp);
  glGetIntegerv(GL_BLEND_SRC_RGB, &o_src_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &o_dst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &o_src_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &o_dst_a);
  GLint o_tex, o_active;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &o_active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &o_tex);
  GLboolean o_blend = glIsEnabled(GL_BLEND);
  GLboolean o_depth = glIsEnabled(GL_DEPTH_TEST);
  GLboolean o_scissor = glIsEnabled(GL_SCISSOR_TEST);
  GLboolean o_cull = glIsEnabled(GL_CULL_FACE);

  glViewport(0, 0, w, h);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(prog);
  glUniform2f(u_half, halfx, halfy);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *) 0);

  int discs = 0;
  for (auto &it : items) {
    if (!it.show || it.pts.empty() || it.size <= 0.0f || it.rgba[3] <= 0.0f || it.line) continue;
    size_t n = it.pts.size() / 2;
    float r = it.size / 2.0f;
    verts.resize(n * SEGMENTS * 6);
    float *v = verts.data();
    for (size_t i = 0; i < n; i++) {
      float cx = it.pts[2 * i], cy = it.pts[2 * i + 1];
      for (int k = 0; k < SEGMENTS; k++) {
        float a0 = 2.0f * (float) M_PI * k / SEGMENTS;
        float a1 = 2.0f * (float) M_PI * (k + 1) / SEGMENTS;
        *v++ = cx; *v++ = cy;
        *v++ = cx + r * cosf(a0); *v++ = cy + r * sinf(a0);
        *v++ = cx + r * cosf(a1); *v++ = cy + r * sinf(a1);
      }
    }
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(),
                 GL_STREAM_DRAW);
    glUniform4fv(u_color, 1, it.rgba);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei) (n * SEGMENTS * 3));
    discs += (int) n;
  }
  glDisableVertexAttribArray(0);

  /* lines: each drawn once per pixel, with the stencil (cleared for each,
     so lines do not hide one another; the scene is finished and nothing
     after the overlay reads it).  A framebuffer without a stencil draws
     them all the same, darker where they cross. */
  bool any_line = false;
  for (auto &it : items) if (it.show && it.line && it.pts.size() >= 4 && it.size > 0.0f) { any_line = true; break; }
  if (any_line && ensure_line_gl()) {
    GLint o_sfunc, o_sref, o_smask, o_sfail, o_szfail, o_szpass, o_swmask, o_sclear;
    GLboolean o_stencil = glIsEnabled(GL_STENCIL_TEST);
    glGetIntegerv(GL_STENCIL_FUNC, &o_sfunc);
    glGetIntegerv(GL_STENCIL_REF, &o_sref);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &o_smask);
    glGetIntegerv(GL_STENCIL_FAIL, &o_sfail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &o_szfail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &o_szpass);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &o_swmask);
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &o_sclear);
    glUseProgram(lprog);
    glUniform2f(lu_half, halfx, halfy);
    glBindVertexArray(lvao);
    glBindBuffer(GL_ARRAY_BUFFER, lvbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *) 0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *) (2 * sizeof(float)));
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glClearStencil(0);
    glStencilFunc(GL_EQUAL, 0, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    for (auto &it : items) {
      if (!it.show || !it.line || it.size <= 0.0f || it.rgba[3] <= 0.0f) continue;
      int nv = build_line(it);
      if (!nv) continue;
      glClear(GL_STENCIL_BUFFER_BIT);
      glBufferData(GL_ARRAY_BUFFER, lverts.size() * sizeof(float), lverts.data(), GL_STREAM_DRAW);
      glUniform4fv(lu_color, 1, it.rgba);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, nv);
    }
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glStencilFunc(o_sfunc, o_sref, o_smask);
    glStencilOp(o_sfail, o_szfail, o_szpass);
    glStencilMask(o_swmask);
    glClearStencil(o_sclear);
    if (!o_stencil) glDisable(GL_STENCIL_TEST);
  }

  /* the labels, over every item's discs */
  bool any_text = false;
  for (auto &it : items) if (it.show && !it.text.empty() && it.size > 0.0f) { any_text = true; break; }
  if (any_text && ensure_text_gl()) {
    glUseProgram(tprog);
    glUniform2f(tu_px, (float) w, (float) h);
    glUniform1i(tu_tex, 0);
    glBindVertexArray(tvao);
    glBindBuffer(GL_ARRAY_BUFFER, tvbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *) 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *) (2 * sizeof(float)));
    for (auto &it : items)
      if (it.show && !it.text.empty() && it.size > 0.0f) draw_text(it, w, h, halfx, halfy);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
  }

  glBindTexture(GL_TEXTURE_2D, o_tex);
  glActiveTexture(o_active);
  glBindBuffer(GL_ARRAY_BUFFER, o_buf);
  glBindVertexArray(o_vao);
  glUseProgram(o_prog);
  glBlendFuncSeparate(o_src_rgb, o_dst_rgb, o_src_a, o_dst_a);
  if (!o_blend) glDisable(GL_BLEND);
  if (o_depth) glEnable(GL_DEPTH_TEST);
  if (o_scissor) glEnable(GL_SCISSOR_TEST);
  if (o_cull) glEnable(GL_CULL_FACE);
  glViewport(o_vp[0], o_vp[1], o_vp[2], o_vp[3]);
  return discs;
}

} /* extern "C" */
