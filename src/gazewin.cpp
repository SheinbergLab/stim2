/*
 * gazewin.cpp -- gaze-contingent pass over stim2's finished scene
 * (see gazewin.h and docs/gazewin.md)
 *
 * State is plain statics: the render thread is the only one that reads or
 * writes it (Tcl commands, processDSCommands and the draw all run there).
 */
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>       /* glfwGetTime: the draw's clock */

#include "gazewin.h"

void log_message(const char *level, const char *fmt, ...);   /* stim2.cpp, C++ */

extern "C" {
void redraw(void);
extern float BackgroundColor[];
}

namespace {

/* ---- settings ---------------------------------------------------------- */

enum { MODE_WINDOW = 0, MODE_SCOTOMA = 1 };
enum { LOST_MASK = 0, LOST_SHOW = 1, LOST_FREEZE = 2 };

struct Settings {
  int mode = MODE_WINDOW;
  float radius = 5.0f;            /* deg */
  float edge = 0.5f;              /* deg, width of the soft edge; 0 = hard */
  bool color_bg = true;           /* the mask is the background colour */
  float color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  float blur = 0.0f;              /* max mip level in the treated region */
  float falloff = 0.0f;           /* deg of blur per deg into the treated
                                     region; 0 = uniform `blur` */
  int lost = LOST_MASK;
  float hold_ms = 100.0f;         /* a sample older than this is lost; 0 never */
  double late_ms = -1.0;          /* repaint budget; -1 = follow stimLateDraw */
};

Settings S;
bool on = false;
std::string source;               /* "" = none */
bool subscribed = false;          /* we added the dserv match */

/* ---- the gaze ---------------------------------------------------------- */

struct Sample {
  bool have = false;
  bool valid = false;             /* finite */
  float x = 0.0f, y = 0.0f;       /* deg */
  unsigned long long ts_us = 0;   /* dserv's stamp; 0 = none (a point) */
  double arrival_s = 0.0;         /* glfwGetTime() when it was taken */
  unsigned long long seq = 0;
};

Sample gaze;
bool point_on = false;            /* `gazewin point`: fixed, never stale */
unsigned long long seq_counter = 0;
long long samples = 0, invalid_samples = 0;

/* what the last drawn pass showed, to know when a repaint is due */
unsigned long long drawn_seq = ~0ull;
int drawn_lost = -1;
bool settings_dirty = true;

/* ---- statistics -------------------------------------------------------- */

const int RING = 1024;
struct Ring {
  float v[RING];
  int n = 0, i = 0;
  void add(float x) { v[i] = x; i = (i + 1) % RING; if (n < RING) n++; }
  void reset() { n = i = 0; }
};
Ring arrival_to_flip;             /* ms: sample taken here -> its flip */
Ring sample_to_flip;              /* ms: dserv stamp -> flip (wall clocks) */
long long stages = 0, composites = 0;

/* the sample a pass drew, until its flip is known */
bool flip_pending = false;
Sample pass_sample;
bool pass_lost = true;

/* ---- GL ---------------------------------------------------------------- */

GLuint prog = 0, vao = 0, fbo = 0, tex = 0;
GLuint dprog = 0, pfbo = 0;       /* the pyramid: downsample program, level target */
int tex_w = 0, tex_h = 0;
int tex_levels = 0;               /* allocated mip levels, 0 included */
bool levels_ok = false;           /* levels 1.. hold the saved scene, as now set */
std::string ds_body;              /* a shader file's "-- Downsample"; "" = built in */
bool tex_mips = false;            /* the min filter wants mipmaps */
bool scene_valid = false;
std::string shader_path;          /* "" = built in */
std::string shader_error;

/* user uniforms: from a shader file's "-- Uniforms" section and `gazewin
   uniform`; 1-4 floats each */
std::map<std::string, std::vector<float>> uniforms;

struct Loc {
  GLint scene = -1, resolution = -1, halfdeg = -1, gaze = -1, valid = -1,
    age_ms = -1, time = -1, radius = -1, edge = -1, mode = -1,
    maskcolor = -1, blur = -1, falloff = -1, lostmode = -1;
} L;
struct DLoc { GLint src = -1, dstsize = -1; } D;

#ifdef STIM2_USE_GLES
const char *HEAD = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
const char *HEAD = "#version 150\n";
#endif

/* a triangle covering the viewport, from gl_VertexID alone (no buffer) */
const char *VS_BODY =
  "void main() {\n"
  "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
  "  gl_Position = vec4(p, 0.0, 1.0);\n"
  "}\n";

/* The built-in pass.  w is how much of the treatment a pixel gets: 0 is
   the scene untouched (texelFetch, so exactly the scene's pixel), 1 the
   full treatment -- the scene blurred to `blur` mip levels, then mixed
   toward maskcolor by its alpha.  So maskcolor alpha 1 hides, alpha 0.5
   halves the contrast toward the mask colour, alpha 0 with blur > 0 only
   blurs.

   falloff > 0 grades the blur: it is about falloff * (distance into the
   treated region) degrees wide, so it starts at nothing at the window's
   edge and widens linearly with eccentricity (as acuity falls), as a
   level log2(width in pixels), capped at `blur`.  While gaze is lost there
   is no eccentricity and the whole screen gets `blur`.

   The levels are a Gaussian pyramid (DS_BODY below).  A level is read
   with a cubic B-spline (four bilinear reads, Sigg & Hadwiger's trick),
   two levels mixed for a fractional one.  A setting's spread is sd
   0.5 * 2^lod pixels (what the first build's bilinear reads of box mips
   gave).  Level k read this way spreads sd 0.87 * 2^k, so the level is
   lowered by log2(0.87/0.5) = 0.79. */
const char *FS_BODY =
  "uniform sampler2D scene;\n"
  "uniform vec2 resolution;\n"
  "uniform vec2 halfdeg;\n"
  "uniform vec2 gaze;\n"
  "uniform int valid;\n"
  "uniform int lostmode;\n"
  "uniform int mode;\n"
  "uniform float radius;\n"
  "uniform float edge;\n"
  "uniform float blur;\n"
  "uniform float falloff;\n"
  "uniform vec4 maskcolor;\n"
  "out vec4 frag;\n"
  "vec4 bspline(float lev, vec2 uv) {\n"
  "  vec2 size = vec2(textureSize(scene, int(lev)));\n"
  "  vec2 p = uv * size - 0.5;\n"
  "  vec2 i = floor(p);\n"
  "  vec2 f = p - i;\n"
  "  vec2 f2 = f * f;\n"
  "  vec2 f3 = f2 * f;\n"
  "  vec2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;\n"
  "  vec2 w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;\n"
  "  vec2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;\n"
  "  vec2 w3 = f3 / 6.0;\n"
  "  vec2 g0 = w0 + w1;\n"
  "  vec2 g1 = w2 + w3;\n"
  "  vec2 c0 = (i - 0.5 + w1 / g0) / size;\n"
  "  vec2 c1 = (i + 1.5 + w3 / g1) / size;\n"
  "  return g0.y * (g0.x * textureLod(scene, c0, lev) + g1.x * textureLod(scene, vec2(c1.x, c0.y), lev))\n"
  "       + g1.y * (g0.x * textureLod(scene, vec2(c0.x, c1.y), lev) + g1.x * textureLod(scene, c1, lev));\n"
  "}\n"
  "void main() {\n"
  "  vec4 s = texelFetch(scene, ivec2(gl_FragCoord.xy), 0);\n"
  "  float w;\n"
  "  float lod = blur;\n"
  "  if (valid == 0) {\n"
  "    w = lostmode == 1 ? 0.0 : 1.0;\n"
  "  } else {\n"
  "    vec2 deg = (gl_FragCoord.xy / resolution * 2.0 - 1.0) * halfdeg;\n"
  "    float d = distance(deg, gaze);\n"
  "    float inside = edge > 0.0\n"
  "      ? 1.0 - smoothstep(radius - 0.5 * edge, radius + 0.5 * edge, d)\n"
  "      : (d <= radius ? 1.0 : 0.0);\n"
  "    w = mode == 0 ? 1.0 - inside : inside;\n"
  "    if (falloff > 0.0) {\n"
  "      float into = mode == 0 ? d - radius : radius - d;\n"
  "      float px = falloff * into * resolution.x / (2.0 * halfdeg.x);\n"
  "      lod = clamp(log2(max(px, 1.0)), 0.0, blur);\n"
  "    }\n"
  "  }\n"
  "  if (w <= 0.0) { frag = s; return; }\n"
  "  vec4 t = s;\n"
  "  if (blur > 0.0) {\n"
  "    float top = floor(log2(max(resolution.x, resolution.y)));\n"
  "    float l = min(max(lod * w - 0.79, 0.0), top);\n"
  "    float l0 = floor(l);\n"
  "    vec2 uv = gl_FragCoord.xy / resolution;\n"
  "    t = bspline(l0, uv);\n"
  "    if (l > l0 && l0 < top) t = mix(t, bspline(l0 + 1.0, uv), l - l0);\n"
  "  }\n"
  "  t.rgb = mix(t.rgb, maskcolor.rgb, maskcolor.a);\n"
  "  frag = vec4(mix(s.rgb, t.rgb, w), s.a);\n"
  "}\n";

/* One level of the Gaussian pyramid from the one below it (the only level
   `src` exposes while this runs): a 6x6 binomial, [1 5 10 10 5 1]/32 each
   way, then every other pixel.  Each pair of taps is one bilinear read, so
   9 reads per output texel.  The 2x2 box that glGenerateMipmap uses passes
   71% of the contrast at the new level's Nyquist frequency, so fine detail
   (a thin plank) aliases into blobs that depend on where it falls on the
   grid; this passes 18%.  Variance 1.25 texels^2 per level. */
const char *DS_BODY =
  "uniform sampler2D src;\n"
  "uniform vec2 dstsize;\n"
  "out vec4 frag;\n"
  "void main() {\n"
  "  vec2 ts = 1.0 / vec2(textureSize(src, 0));\n"
  "  vec2 uv = gl_FragCoord.xy / dstsize;\n"
  "  const float o = 5.0 / 3.0;\n"
  "  vec3 wt = vec3(0.1875, 0.625, 0.1875);\n"
  "  vec4 c = vec4(0.0);\n"
  "  for (int j = 0; j < 3; j++)\n"
  "    for (int i = 0; i < 3; i++)\n"
  "      c += wt[i] * wt[j] * textureLod(src, uv + vec2(float(i - 1), float(j - 1)) * o * ts, 0.0);\n"
  "  frag = c;\n"
  "}\n";

GLuint compile(GLenum type, const std::string &src, std::string &err)
{
  GLuint s = glCreateShader(type);
  const char *p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    err = std::string(type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + log;
    glDeleteShader(s);
    return 0;
  }
  return s;
}

/* a source without its own #version gets the platform's header */
std::string with_head(const std::string &body)
{
  size_t k = body.find_first_not_of(" \t\r\n");
  if (k != std::string::npos && body.compare(k, 8, "#version") == 0) return body;
  return std::string(HEAD) + body;
}

GLuint link(const std::string &vs_body, const std::string &fs_body, std::string &err)
{
  GLuint vs = compile(GL_VERTEX_SHADER, with_head(vs_body), err);
  if (!vs) return 0;
  GLuint fs = compile(GL_FRAGMENT_SHADER, with_head(fs_body), err);
  if (!fs) { glDeleteShader(vs); return 0; }
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(p, sizeof(log), nullptr, log);
    err = std::string("link: ") + log;
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

/* The pyramid's downsample program, `body` or the built-in one. */
GLuint link_downsample(const std::string &body, std::string &err)
{
  GLuint p = link(VS_BODY, body.empty() ? DS_BODY : body, err);
  if (!p) err = "downsample: " + err;
  return p;
}

/* The downsample program for `body`, replacing the one in use; on an
   error that one stays. */
void adopt_downsample(GLuint p, const std::string &body)
{
  if (dprog) glDeleteProgram(dprog);
  dprog = p;
  ds_body = body;
  levels_ok = false;
  D.src = glGetUniformLocation(dprog, "src");
  D.dstsize = glGetUniformLocation(dprog, "dstsize");
}

bool set_downsample(const std::string &body, std::string &err)
{
  GLuint p = link_downsample(body, err);
  if (!p) return false;
  adopt_downsample(p, body);
  return true;
}

bool ensure_downsample(std::string &err)
{
  return dprog || set_downsample(ds_body, err);
}

/* Build a program; on success it replaces the current one. */
bool build(const std::string &vs_body, const std::string &fs_body, std::string &err)
{
  GLuint p = link(vs_body, fs_body, err);
  if (!p) return false;
  if (prog) glDeleteProgram(prog);
  prog = p;
  L.scene = glGetUniformLocation(prog, "scene");
  L.resolution = glGetUniformLocation(prog, "resolution");
  L.halfdeg = glGetUniformLocation(prog, "halfdeg");
  L.gaze = glGetUniformLocation(prog, "gaze");
  L.valid = glGetUniformLocation(prog, "valid");
  L.age_ms = glGetUniformLocation(prog, "age_ms");
  L.time = glGetUniformLocation(prog, "time");
  L.radius = glGetUniformLocation(prog, "radius");
  L.edge = glGetUniformLocation(prog, "edge");
  L.mode = glGetUniformLocation(prog, "mode");
  L.maskcolor = glGetUniformLocation(prog, "maskcolor");
  L.blur = glGetUniformLocation(prog, "blur");
  L.falloff = glGetUniformLocation(prog, "falloff");
  L.lostmode = glGetUniformLocation(prog, "lostmode");
  if (!vao) glGenVertexArrays(1, &vao);
  return true;
}

/* A shader file: "-- Vertex", "-- Fragment" and "-- Uniforms" sections (the
   layout of stim2's shaders/ directory), and "-- Downsample", the
   pyramid's filter; a file with no section markers is all fragment.
   Uniform lines are "name v1 ?v2 v3 v4?", values optionally in double
   quotes. */
bool load_file(const std::string &path, std::string &vs, std::string &fs, std::string &ds,
               std::map<std::string, std::vector<float>> &unis, std::string &err)
{
  std::ifstream in(path);
  if (!in) { err = "cannot read " + path; return false; }
  std::string line, section = "Fragment";
  bool any_marker = false;
  std::string v, f, dsrc;
  while (std::getline(in, line)) {
    if (line.compare(0, 2, "--") == 0) {
      std::string name = line.substr(2);
      size_t a = name.find_first_not_of(" \t");
      size_t b = name.find_last_not_of(" \t\r");
      section = a == std::string::npos ? "" : name.substr(a, b - a + 1);
      any_marker = true;
      continue;
    }
    if (section == "Vertex") v += line + "\n";
    else if (section == "Fragment") f += line + "\n";
    else if (section == "Downsample") dsrc += line + "\n";
    else if (section == "Uniforms") {
      for (char &c : line) if (c == '"') c = ' ';
      std::istringstream ls(line);
      std::string name;
      if (!(ls >> name)) continue;
      std::vector<float> vals;
      float x;
      while (vals.size() < 4 && ls >> x) vals.push_back(x);
      if (!vals.empty()) unis[name] = vals;
    }
  }
  (void) any_marker;
  if (f.find_first_not_of(" \t\r\n") == std::string::npos) {
    err = path + ": no fragment shader";
    return false;
  }
  vs = v.find_first_not_of(" \t\r\n") == std::string::npos ? VS_BODY : v;
  fs = f;
  ds = dsrc.find_first_not_of(" \t\r\n") == std::string::npos ? "" : dsrc;
  return true;
}

bool ensure_program(std::string &err)
{
  if (prog) return true;
  if (shader_path.empty()) return build(VS_BODY, FS_BODY, err);
  std::string vs, fs, ds;
  std::map<std::string, std::vector<float>> unis;
  if (!load_file(shader_path, vs, fs, ds, unis, err)) return false;
  if (!set_downsample(ds, err) || !build(vs, fs, err)) return false;
  for (auto &u : unis) if (!uniforms.count(u.first)) uniforms[u.first] = u.second;
  return true;
}

bool ensure_target(int w, int h, std::string &err)
{
  bool want_mips = S.blur > 0.0f;
  if (tex && tex_w == w && tex_h == h) {
    if (want_mips != tex_mips) {
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      want_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
      tex_mips = want_mips;
    }
    return true;
  }
  if (!tex) glGenTextures(1, &tex);
  if (!fbo) glGenFramebuffers(1, &fbo);
  glBindTexture(GL_TEXTURE_2D, tex);
  /* every level down to 1x1, for make_levels to fill */
  tex_levels = 0;
  levels_ok = false;
  for (int lw = w, lh = h; ; lw = std::max(1, lw / 2), lh = std::max(1, lh / 2)) {
    glTexImage2D(GL_TEXTURE_2D, tex_levels++, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (lw == 1 && lh == 1) break;
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, tex_levels - 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  want_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  tex_mips = want_mips;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  GLenum st = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE) {
    char b[64];
    snprintf(b, sizeof(b), "scene target incomplete (0x%x)", st);
    err = b;
    tex_w = tex_h = 0;
    return false;
  }
  tex_w = w; tex_h = h;
  return true;
}

/* ---- the gaze as the pass sees it -------------------------------------- */

bool is_lost(double now_s)
{
  if (point_on) return false;
  if (!gaze.have || !gaze.valid) return true;
  if (S.lost == LOST_FREEZE) return false;
  if (S.hold_ms > 0.0f && (now_s - gaze.arrival_s) * 1000.0 > S.hold_ms) return true;
  return false;
}

/* ---- the pass ---------------------------------------------------------- */

/* The GL state a pass changes, put back as it was when this goes out of
   scope (the framebuffer bindings are the caller's).  Texture unit 0 is
   left active, with its binding saved. */
struct GLSave {
  GLint prog, vao, vp[4], tex, active;
  GLboolean blend, depth, scissor, cull, stencil;
  GLSave() {
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    blend = glIsEnabled(GL_BLEND);
    depth = glIsEnabled(GL_DEPTH_TEST);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    cull = glIsEnabled(GL_CULL_FACE);
    stencil = glIsEnabled(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
  }
  ~GLSave() {
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(active);
    glBindVertexArray(vao);
    glUseProgram(prog);
    if (blend) glEnable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (stencil) glEnable(GL_STENCIL_TEST);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
  }
};

/* Fill the blur's levels from level 0: each drawn from the one below with
   the downsample program.  While it runs, BASE_LEVEL = MAX_LEVEL = k-1,
   so the texture it samples never includes level k, the one it writes. */
void make_levels(int w, int h)
{
  GLSave save;
  glBindTexture(GL_TEXTURE_2D, tex);
  std::string err;
  if (!vao || !ensure_downsample(err)) {
    /* only if the built-in filter does not build on this driver (a file's
       is checked when it loads): the driver's box mips, not garbage */
    static bool told = false;
    if (!told) { log_message("error", "gazewin: %s; box mips instead", err.c_str()); told = true; }
    glGenerateMipmap(GL_TEXTURE_2D);
    return;
  }
  if (!pfbo) glGenFramebuffers(1, &pfbo);
  GLint o_draw;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &o_draw);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pfbo);
  glUseProgram(dprog);
  glBindVertexArray(vao);
  if (D.src >= 0) glUniform1i(D.src, 0);
  int lw = w, lh = h;
  for (int k = 1; k < tex_levels; k++) {
    lw = std::max(1, lw / 2);
    lh = std::max(1, lh / 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, k - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, k - 1);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, k);
    glViewport(0, 0, lw, lh);
    if (D.dstsize >= 0) glUniform2f(D.dstsize, (float) lw, (float) lh);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, tex_levels - 1);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, o_draw);
}

void draw_pass(int w, int h, float halfx, float halfy, GLuint drawfb, double now_s)
{
  GLSave save;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawfb);
  glViewport(0, 0, w, h);

  bool lost = is_lost(now_s);
  const Sample &g = gaze;
  float gx = point_on || g.have ? g.x : 0.0f;
  float gy = point_on || g.have ? g.y : 0.0f;

  glUseProgram(prog);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (L.scene >= 0) glUniform1i(L.scene, 0);
  if (L.resolution >= 0) glUniform2f(L.resolution, (float) w, (float) h);
  if (L.halfdeg >= 0) glUniform2f(L.halfdeg, halfx, halfy);
  if (L.gaze >= 0) glUniform2f(L.gaze, gx, gy);
  if (L.valid >= 0) glUniform1i(L.valid, lost ? 0 : 1);
  if (L.age_ms >= 0)
    glUniform1f(L.age_ms, point_on ? 0.0f : g.have ? (float) ((now_s - g.arrival_s) * 1000.0) : 1e9f);
  if (L.time >= 0) glUniform1f(L.time, (float) now_s);
  if (L.radius >= 0) glUniform1f(L.radius, S.radius);
  if (L.edge >= 0) glUniform1f(L.edge, S.edge);
  if (L.mode >= 0) glUniform1i(L.mode, S.mode);
  if (L.blur >= 0) glUniform1f(L.blur, S.blur);
  if (L.falloff >= 0) glUniform1f(L.falloff, S.falloff);
  if (L.lostmode >= 0) glUniform1i(L.lostmode, S.lost == LOST_SHOW ? 1 : 0);
  if (L.maskcolor >= 0) {
    if (S.color_bg)   /* the background, at the alpha given with it */
      glUniform4f(L.maskcolor, BackgroundColor[0], BackgroundColor[1], BackgroundColor[2], S.color[3]);
    else
      glUniform4fv(L.maskcolor, 1, S.color);
  }
  for (auto &u : uniforms) {
    GLint loc = glGetUniformLocation(prog, u.first.c_str());
    if (loc < 0) continue;
    const float *v = u.second.data();
    switch (u.second.size()) {
    case 1: glUniform1f(loc, v[0]); break;
    case 2: glUniform2f(loc, v[0], v[1]); break;
    case 3: glUniform3f(loc, v[0], v[1], v[2]); break;
    case 4: glUniform4f(loc, v[0], v[1], v[2], v[3]); break;
    }
  }

  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  /* what this pass showed: the repaint test and the latency record */
  drawn_seq = gaze.seq;
  drawn_lost = lost ? 1 : 0;
  settings_dirty = false;
  pass_sample = gaze;
  pass_lost = lost || point_on;   /* a point has no latency to record */
  flip_pending = true;
}

/* ---- sample decoding --------------------------------------------------- */

int b64val(unsigned char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

size_t b64decode(const char *s, size_t n, unsigned char *out, size_t outmax)
{
  size_t o = 0;
  unsigned int acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; i++) {
    int v = b64val((unsigned char) s[i]);
    if (v < 0) continue;          /* padding, whitespace */
    acc = (acc << 6) | (unsigned) v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o < outmax) out[o++] = (unsigned char) ((acc >> bits) & 0xff);
    }
  }
  return o;
}

/* dserv's datatypes (dserv src/Datapoint.h) */
enum { DS_STRING = 1, DS_FLOAT = 2, DS_DOUBLE = 3 };

/* Two coordinates from a value: plain numbers ("x y", any type -- what a
   replay or a test may hand over), else base64 floats or doubles by type.
   Returns false if the value holds neither. */
bool decode_xy(int type, const char *val, size_t n, double &x, double &y)
{
  std::string v(val, n);
  {
    char *end = nullptr;
    const char *p = v.c_str();
    double a = strtod(p, &end);
    if (end != p) {
      const char *q = end;
      double b = strtod(q, &end);
      if (end != q) { x = a; y = b; return true; }
    }
  }
  unsigned char buf[16];
  size_t got = b64decode(v.data(), v.size(), buf, sizeof(buf));
  if (type == DS_FLOAT && got >= 8) {
    float f[2];
    memcpy(f, buf, 8);
    x = f[0]; y = f[1];
    return true;
  }
  if (type == DS_DOUBLE && got >= 16) {
    double d[2];
    memcpy(d, buf, 16);
    x = d[0]; y = d[1];
    return true;
  }
  return false;
}

void take_sample(int type, unsigned long long ts, const char *val, size_t n, double now_s)
{
  double x, y;
  if (!decode_xy(type, val, n, x, y)) { invalid_samples++; return; }
  samples++;
  gaze.have = true;
  gaze.valid = std::isfinite(x) && std::isfinite(y);
  if (!gaze.valid) invalid_samples++;
  gaze.x = (float) x;
  gaze.y = (float) y;
  gaze.ts_us = ts;
  gaze.arrival_s = now_s;
  gaze.seq = ++seq_counter;
}

/* ---- statistics ---------------------------------------------------------- */

Tcl_Obj *ring_stats(Ring &r)
{
  Tcl_Obj *d = Tcl_NewDictObj();
  Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("n", -1), Tcl_NewIntObj(r.n));
  if (r.n) {
    std::vector<float> v(r.v, r.v + r.n);
    std::sort(v.begin(), v.end());
    auto q = [&](double p) { return v[std::min((size_t) (p * (v.size() - 1) + 0.5), v.size() - 1)]; };
    Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("p50", -1), Tcl_NewDoubleObj(q(0.5)));
    Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("p95", -1), Tcl_NewDoubleObj(q(0.95)));
    Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj("max", -1), Tcl_NewDoubleObj(v.back()));
  }
  return d;
}

} /* namespace */

/* ======================================================================== */

extern "C" {

int gazewinWanted(void) { return on && prog; }
int gazewinSourceSet(void) { return !source.empty(); }
double gazewinLateMs(void) { return S.late_ms; }
int gazewinFlipPending(void) { return flip_pending; }

void gazewinMaybeSample(const char *name, const char *line, double now_s)
{
  if (source.empty() || source != name || point_on) return;
  /* "name type ts len {value}" */
  int type = 0, len = 0;
  unsigned long long ts = 0;
  const char *p = strchr(line, ' ');
  if (!p) return;
  if (sscanf(p, " %d %llu %d", &type, &ts, &len) != 3) return;
  const char *a = strchr(p, '{');
  const char *b = a ? strrchr(a, '}') : nullptr;
  if (!a || !b) return;
  take_sample(type, ts, a + 1, (size_t) (b - a - 1), now_s);
}

int gazewinStage(int w, int h, float halfx, float halfy,
                 unsigned drawfb, unsigned drawnbuf, double now_s)
{
  if (!on || !prog || w <= 0 || h <= 0) return 0;
  std::string err;
  if (!ensure_target(w, h, err)) {
    log_message("error", "gazewin: %s", err.c_str());
    scene_valid = false;
    return 0;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, drawfb);
  glReadBuffer(drawnbuf);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
  glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  if (tex_mips) make_levels(w, h);
  levels_ok = tex_mips;
  scene_valid = true;
  draw_pass(w, h, halfx, halfy, drawfb, now_s);
  glBindFramebuffer(GL_FRAMEBUFFER, drawfb);
  glReadBuffer(drawnbuf);
  stages++;
  return 1;
}

int gazewinComposite(int w, int h, float halfx, float halfy,
                     unsigned drawfb, double now_s)
{
  if (!on || !prog || !scene_valid || w != tex_w || h != tex_h) return 0;
  std::string err;
  if (!ensure_target(w, h, err)) return 0;   /* a -blur change: the filter */
  if (tex_mips && !levels_ok) {
    /* -blur turned on, or the filter changed, since the scene
       was saved */
    make_levels(w, h);
    levels_ok = true;
  }
  draw_pass(w, h, halfx, halfy, drawfb, now_s);
  glBindFramebuffer(GL_FRAMEBUFFER, drawfb);
  composites++;
  return 1;
}

int gazewinRepaintDue(double now_s, double *wait_s)
{
  *wait_s = -1.0;
  if (!on || !prog) return 0;
  if (settings_dirty) return 1;
  bool lost = is_lost(now_s);
  if ((lost ? 1 : 0) != drawn_lost) return 1;
  unsigned long long cur = gaze.seq;
  if (!lost && cur != drawn_seq) return 1;
  /* nothing new: when will the current sample go stale? */
  if (!lost && !point_on && gaze.have && S.lost != LOST_FREEZE && S.hold_ms > 0.0f) {
    double t = gaze.arrival_s + S.hold_ms / 1000.0 - now_s;
    *wait_s = t > 0.0 ? t : 0.0;
  }
  return 0;
}

void gazewinNoteFlip(double flip_s, long long flip_wall_us)
{
  if (!flip_pending) return;
  flip_pending = false;
  if (pass_lost || !pass_sample.have) return;
  arrival_to_flip.add((float) ((flip_s - pass_sample.arrival_s) * 1000.0));
  if (flip_wall_us > 0 && pass_sample.ts_us > 0)
    sample_to_flip.add((float) ((double) (flip_wall_us - (long long) pass_sample.ts_us) / 1000.0));
}

} /* extern "C" */

/* ======================================================================== */
/* the Tcl command                                                          */

namespace {

double tclNow(void) { return glfwGetTime(); }

void reset_gl_program(void)
{
  if (prog) { glDeleteProgram(prog); prog = 0; }
  if (dprog) { glDeleteProgram(dprog); dprog = 0; }
  ds_body.clear();
  levels_ok = false;
}

Tcl_Obj *settings_dict(void)
{
  Tcl_Obj *d = Tcl_NewDictObj();
  auto put = [&](const char *k, Tcl_Obj *v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), v); };
  put("mode", Tcl_NewStringObj(S.mode == MODE_WINDOW ? "window" : "scotoma", -1));
  put("radius", Tcl_NewDoubleObj(S.radius));
  put("edge", Tcl_NewDoubleObj(S.edge));
  if (S.color_bg) {
    if (S.color[3] == 1.0f) put("color", Tcl_NewStringObj("background", -1));
    else {
      Tcl_Obj *c = Tcl_NewListObj(0, nullptr);
      Tcl_ListObjAppendElement(nullptr, c, Tcl_NewStringObj("background", -1));
      Tcl_ListObjAppendElement(nullptr, c, Tcl_NewDoubleObj(S.color[3]));
      put("color", c);
    }
  }
  else {
    Tcl_Obj *c = Tcl_NewListObj(0, nullptr);
    for (int i = 0; i < 4; i++) Tcl_ListObjAppendElement(nullptr, c, Tcl_NewDoubleObj(S.color[i]));
    put("color", c);
  }
  put("blur", Tcl_NewDoubleObj(S.blur));
  put("falloff", Tcl_NewDoubleObj(S.falloff));
  put("lost", Tcl_NewStringObj(S.lost == LOST_MASK ? "mask" : S.lost == LOST_SHOW ? "show" : "freeze", -1));
  put("hold", Tcl_NewDoubleObj(S.hold_ms));
  put("late", Tcl_NewDoubleObj(S.late_ms));
  return d;
}

Tcl_Obj *status_dict(void)
{
  double now = tclNow();
  Tcl_Obj *d = settings_dict();
  auto put = [&](const char *k, Tcl_Obj *v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), v); };
  put("on", Tcl_NewIntObj(on ? 1 : 0));
  put("source", Tcl_NewStringObj(source.c_str(), -1));
  put("subscribed", Tcl_NewIntObj(subscribed ? 1 : 0));
  put("shader", Tcl_NewStringObj(shader_path.empty() ? "default" : shader_path.c_str(), -1));
  if (point_on) {
    Tcl_Obj *p = Tcl_NewListObj(0, nullptr);
    Tcl_ListObjAppendElement(nullptr, p, Tcl_NewDoubleObj(gaze.x));
    Tcl_ListObjAppendElement(nullptr, p, Tcl_NewDoubleObj(gaze.y));
    put("point", p);
  } else put("point", Tcl_NewStringObj("off", -1));
  put("samples", Tcl_NewWideIntObj(samples));
  put("invalid", Tcl_NewWideIntObj(invalid_samples));
  Tcl_Obj *g = Tcl_NewListObj(0, nullptr);
  if (gaze.have) {
    Tcl_ListObjAppendElement(nullptr, g, Tcl_NewDoubleObj(gaze.x));
    Tcl_ListObjAppendElement(nullptr, g, Tcl_NewDoubleObj(gaze.y));
  }
  put("gaze", g);
  put("age_ms", Tcl_NewDoubleObj(gaze.have ? (now - gaze.arrival_s) * 1000.0 : -1.0));
  put("gaze_lost", Tcl_NewIntObj(is_lost(now) ? 1 : 0));   /* "lost" is the policy */
  put("stages", Tcl_NewWideIntObj(stages));
  put("repaints", Tcl_NewWideIntObj(composites));
  put("arrival_to_flip_ms", ring_stats(arrival_to_flip));
  put("sample_to_flip_ms", ring_stats(sample_to_flip));
  return d;
}

int set_source(Tcl_Interp *interp, const std::string &name)
{
  if (name == source) return TCL_OK;
  /* drop our dserv match, unless a script bound the same name itself */
  if (subscribed && !source.empty()) {
    Tcl_Obj *cmd = Tcl_NewListObj(0, nullptr);
    Tcl_IncrRefCount(cmd);
    Tcl_ListObjAppendElement(nullptr, cmd, Tcl_NewStringObj("::gazewin_unsubscribe", -1));
    Tcl_ListObjAppendElement(nullptr, cmd, Tcl_NewStringObj(source.c_str(), -1));
    Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(cmd);
  }
  subscribed = false;
  source = name;
  gaze = Sample();
  if (!source.empty()) {
    Tcl_Obj *cmd = Tcl_NewListObj(0, nullptr);
    Tcl_IncrRefCount(cmd);
    Tcl_ListObjAppendElement(nullptr, cmd, Tcl_NewStringObj("::gazewin_subscribe", -1));
    Tcl_ListObjAppendElement(nullptr, cmd, Tcl_NewStringObj(source.c_str(), -1));
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) == TCL_OK) {
      int ok = 0;
      Tcl_GetBooleanFromObj(nullptr, Tcl_GetObjResult(interp), &ok);
      subscribed = ok != 0;
    }
    Tcl_DecrRefCount(cmd);
  }
  Tcl_ResetResult(interp);
  settings_dirty = true;
  return TCL_OK;
}

/* The dserv subscription, in Tcl so it uses the same qpcs calls and
   ::dservhost as stim::bind.  No dservhost (a replay, a desk test): not
   subscribed, and samples come by `gazewin datapoint`. */
const char *SUBSCRIBE_PROCS =
  "proc ::gazewin_subscribe {name} {\n"
  "  if {![info exists ::dservhost] || ![llength [info commands qpcs::dsStimAddMatch]]} { return 0 }\n"
  "  if {[catch {qpcs::dsStimAddMatch $::dservhost $name}]} { return 0 }\n"
  "  return 1\n"
  "}\n"
  "proc ::gazewin_unsubscribe {name} {\n"
  "  if {[info exists ::dsCmds($name)]} return\n"
  "  if {![info exists ::dservhost]} return\n"
  "  catch {qpcs::dsStimRemoveMatch $::dservhost $name}\n"
  "}\n";

void reset_all(Tcl_Interp *interp)
{
  bool was_on = on;
  set_source(interp, "");
  on = false;
  point_on = false;
  gaze = Sample();
  S = Settings();
  uniforms.clear();
  if (!shader_path.empty()) { shader_path.clear(); reset_gl_program(); }
  scene_valid = false;
  flip_pending = false;
  settings_dirty = true;
  if (was_on) redraw();
}

const char *USAGE =
  "gazewin ?on|off|status|reset|source ?name?|set ?options?|point x y|point off|"
  "uniform name ?v ...?|shader ?path|default?|datapoint name type ts len value|stats_reset?";

int gazewinCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  if (objc < 2) { Tcl_SetObjResult(interp, status_dict()); return TCL_OK; }
  std::string sub = Tcl_GetString(objv[1]);

  if (sub == "status") { Tcl_SetObjResult(interp, status_dict()); return TCL_OK; }

  if (sub == "on") {
    std::string err;
    if (!ensure_program(err)) {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin: shader: %s", err.c_str()));
      return TCL_ERROR;
    }
    if (!on) { on = true; settings_dirty = true; redraw(); }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
    return TCL_OK;
  }
  if (sub == "off") {
    if (on) { on = false; scene_valid = false; flip_pending = false; redraw(); }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
    return TCL_OK;
  }
  if (sub == "reset") { reset_all(interp); return TCL_OK; }

  if (sub == "source") {
    if (objc == 2) { Tcl_SetObjResult(interp, Tcl_NewStringObj(source.c_str(), -1)); return TCL_OK; }
    if (objc != 3) { Tcl_WrongNumArgs(interp, 2, objv, "?name?"); return TCL_ERROR; }
    set_source(interp, Tcl_GetString(objv[2]));
    Tcl_SetObjResult(interp, Tcl_NewIntObj(subscribed ? 1 : 0));
    return TCL_OK;
  }

  if (sub == "point") {
    if (objc == 3 && std::string(Tcl_GetString(objv[2])) == "off") {
      if (point_on) { point_on = false; gaze = Sample(); settings_dirty = true; }
      return TCL_OK;
    }
    double x, y;
    if (objc != 4 || Tcl_GetDoubleFromObj(interp, objv[2], &x) != TCL_OK ||
        Tcl_GetDoubleFromObj(interp, objv[3], &y) != TCL_OK) {
      Tcl_ResetResult(interp);
      Tcl_WrongNumArgs(interp, 2, objv, "x y | off");
      return TCL_ERROR;
    }
    point_on = true;
    gaze.have = gaze.valid = true;
    gaze.x = (float) x; gaze.y = (float) y;
    gaze.ts_us = 0;
    gaze.arrival_s = tclNow();
    gaze.seq = ++seq_counter;
    settings_dirty = true;
    return TCL_OK;
  }

  if (sub == "datapoint") {
    /* name type ts len value: the line as stim::inject has it */
    if (objc != 7) { Tcl_WrongNumArgs(interp, 2, objv, "name type ts len value"); return TCL_ERROR; }
    if (source.empty() || source != Tcl_GetString(objv[2]) || point_on) {
      Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
      return TCL_OK;
    }
    int type;
    Tcl_WideInt ts;
    if (Tcl_GetIntFromObj(interp, objv[3], &type) != TCL_OK ||
        Tcl_GetWideIntFromObj(interp, objv[4], &ts) != TCL_OK) return TCL_ERROR;
    Tcl_Size n;
    const char *v = Tcl_GetStringFromObj(objv[6], &n);
    long long before = samples;
    take_sample(type, (unsigned long long) ts, v, (size_t) n, tclNow());
    Tcl_SetObjResult(interp, Tcl_NewIntObj(samples > before ? 1 : 0));
    return TCL_OK;
  }

  if (sub == "set") {
    if (objc == 2) { Tcl_SetObjResult(interp, settings_dict()); return TCL_OK; }
    if ((objc - 2) % 2) {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin set: option \"%s\" needs a value", Tcl_GetString(objv[objc - 1])));
      return TCL_ERROR;
    }
    Settings n = S;
    for (int i = 2; i < objc; i += 2) {
      std::string opt = Tcl_GetString(objv[i]);
      Tcl_Obj *val = objv[i + 1];
      double d;
      if (opt == "-mode") {
        std::string m = Tcl_GetString(val);
        if (m == "window") n.mode = MODE_WINDOW;
        else if (m == "scotoma") n.mode = MODE_SCOTOMA;
        else { Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin set: -mode must be window or scotoma, not \"%s\"", m.c_str())); return TCL_ERROR; }
      }
      else if (opt == "-lost") {
        std::string m = Tcl_GetString(val);
        if (m == "mask") n.lost = LOST_MASK;
        else if (m == "show") n.lost = LOST_SHOW;
        else if (m == "freeze") n.lost = LOST_FREEZE;
        else { Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin set: -lost must be mask, show or freeze, not \"%s\"", m.c_str())); return TCL_ERROR; }
      }
      else if (opt == "-color") {
        Tcl_Size lc; Tcl_Obj **le;
        if (Tcl_ListObjGetElements(interp, val, &lc, &le) != TCL_OK) return TCL_ERROR;
        /* background ?alpha?: stim2's background colour, followed live */
        if (lc >= 1 && lc <= 2 && std::string(Tcl_GetString(le[0])) == "background") {
          double a = 1.0;
          if (lc == 2 && Tcl_GetDoubleFromObj(interp, le[1], &a) != TCL_OK) return TCL_ERROR;
          if (a < 0.0 || a > 1.0) { Tcl_SetObjResult(interp, Tcl_NewStringObj("gazewin set: -color background alpha must be 0..1", -1)); return TCL_ERROR; }
          n.color_bg = true;
          n.color[3] = (float) a;
          continue;
        }
        if (lc != 3 && lc != 4) { Tcl_SetObjResult(interp, Tcl_NewStringObj("gazewin set: -color is {background ?a?} or {r g b ?a?}", -1)); return TCL_ERROR; }
        float c[4] = { 0, 0, 0, 1 };
        for (int k = 0; k < lc; k++) {
          if (Tcl_GetDoubleFromObj(interp, le[k], &d) != TCL_OK) return TCL_ERROR;
          c[k] = (float) d;
        }
        memcpy(n.color, c, sizeof(c));
        n.color_bg = false;
      }
      else if (opt == "-radius" || opt == "-edge" || opt == "-blur" || opt == "-falloff" ||
               opt == "-hold" || opt == "-late") {
        if (Tcl_GetDoubleFromObj(interp, val, &d) != TCL_OK) return TCL_ERROR;
        bool neg_ok = opt == "-late";
        if (d < 0.0 && !(neg_ok && d == -1.0)) {
          Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin set: %s must be >= 0%s", opt.c_str(), neg_ok ? " (or -1)" : ""));
          return TCL_ERROR;
        }
        if (opt == "-radius") n.radius = (float) d;
        else if (opt == "-edge") n.edge = (float) d;
        else if (opt == "-blur") n.blur = (float) d;
        else if (opt == "-falloff") n.falloff = (float) d;
        else if (opt == "-hold") n.hold_ms = (float) d;
        else n.late_ms = d;
      }
      else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin set: unknown option \"%s\" (-mode -radius -edge -color -blur -falloff -lost -hold -late)", opt.c_str()));
        return TCL_ERROR;
      }
    }
    S = n;
    settings_dirty = true;
    Tcl_SetObjResult(interp, settings_dict());
    return TCL_OK;
  }

  if (sub == "uniform") {
    if (objc < 3) { Tcl_WrongNumArgs(interp, 2, objv, "name ?v1 ?v2 v3 v4??"); return TCL_ERROR; }
    std::string name = Tcl_GetString(objv[2]);
    if (objc == 3) {
      auto it = uniforms.find(name);
      Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
      if (it != uniforms.end())
        for (float f : it->second) Tcl_ListObjAppendElement(nullptr, l, Tcl_NewDoubleObj(f));
      Tcl_SetObjResult(interp, l);
      return TCL_OK;
    }
    /* values as separate words or one list */
    std::vector<float> vals;
    for (int i = 3; i < objc; i++) {
      Tcl_Size lc; Tcl_Obj **le;
      if (Tcl_ListObjGetElements(interp, objv[i], &lc, &le) != TCL_OK) return TCL_ERROR;
      for (int k = 0; k < lc; k++) {
        double d;
        if (Tcl_GetDoubleFromObj(interp, le[k], &d) != TCL_OK) return TCL_ERROR;
        vals.push_back((float) d);
      }
    }
    if (vals.empty() || vals.size() > 4) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj("gazewin uniform: 1 to 4 values", -1));
      return TCL_ERROR;
    }
    uniforms[name] = vals;
    settings_dirty = true;
    return TCL_OK;
  }

  if (sub == "shader") {
    if (objc == 2) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj(shader_path.empty() ? "default" : shader_path.c_str(), -1));
      return TCL_OK;
    }
    if (objc != 3) { Tcl_WrongNumArgs(interp, 2, objv, "?path|default?"); return TCL_ERROR; }
    std::string path = Tcl_GetString(objv[2]);
    std::string err;
    if (path == "default" || path.empty()) {
      if (!build(VS_BODY, FS_BODY, err) || !set_downsample("", err)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin: shader: %s", err.c_str()));
        return TCL_ERROR;
      }
      shader_path.clear();
      uniforms.clear();
    }
    else {
      std::string vs, fs, ds;
      std::map<std::string, std::vector<float>> unis;
      GLuint dp = 0;
      bool ok = load_file(path, vs, fs, ds, unis, err) && (dp = link_downsample(ds, err)) != 0;
      if (ok && !build(vs, fs, err)) { glDeleteProgram(dp); ok = false; }
      if (!ok) {
        /* the programs in use stay */
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("gazewin: shader: %s", err.c_str()));
        return TCL_ERROR;
      }
      adopt_downsample(dp, ds);
      shader_path = path;
      uniforms = unis;
    }
    settings_dirty = true;
    if (on) redraw();
    Tcl_SetObjResult(interp, Tcl_NewStringObj(shader_path.empty() ? "default" : shader_path.c_str(), -1));
    return TCL_OK;
  }

  if (sub == "stats_reset") {
    arrival_to_flip.reset();
    sample_to_flip.reset();
    stages = composites = 0;
    samples = invalid_samples = 0;
    return TCL_OK;
  }

  Tcl_SetObjResult(interp, Tcl_ObjPrintf("usage: %s", USAGE));
  return TCL_ERROR;
}

} /* namespace */

extern "C" int Gazewin_TclInit(Tcl_Interp *interp)
{
  Tcl_CreateObjCommand(interp, "gazewin", gazewinCmd, nullptr, nullptr);
  return Tcl_Eval(interp, SUBSCRIBE_PROCS);
}
