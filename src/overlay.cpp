/*
 * overlay.cpp -- annotation layer drawn over stim2's scene (see overlay.h)
 *
 * Each item is a set of discs of one size and colour, in degrees.  Drawing
 * builds the discs' triangles on the CPU (a gaze marker and a short trail
 * are a few dozen discs) and issues one draw call per item with a small
 * program of its own, so it depends on nothing in the object list.
 */
#include <string>
#include <vector>
#include <cmath>
#include <cstring>

#include <glad/glad.h>

#include "overlay.h"

void log_message(const char *level, const char *fmt, ...);

namespace {

struct Item {
  std::string name;
  std::vector<float> pts;   /* x0 y0 x1 y1 ..., degrees */
  float size = 0.5f;        /* disc diameter, degrees */
  float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
  bool show = true;
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
    if (!it.show || it.pts.empty() || it.size <= 0.0f || it.rgba[3] <= 0.0f) continue;
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
