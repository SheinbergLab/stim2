/*
 * passutil.h -- what stim2's post-pass modules share (gazewin, shaderpass):
 * building a full-screen shader program, reading a shader file's
 * sections, and saving the GL state around a pass.  Header only; include
 * <glad/glad.h> first.  See stim2's docs/postpass.md.
 */
#ifndef PASSUTIL_H
#define PASSUTIL_H

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>

namespace passutil {

#ifdef STIM2_USE_GLES
static const char *HEAD = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
static const char *HEAD = "#version 150\n";
#endif

/* a triangle covering the viewport, from gl_VertexID alone (no buffer) */
static const char *VS_BODY =
  "void main() {\n"
  "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
  "  gl_Position = vec4(p, 0.0, 1.0);\n"
  "}\n";

inline bool blank(const std::string &s)
{
  return s.find_first_not_of(" \t\r\n") == std::string::npos;
}

inline GLuint compile(GLenum type, const std::string &src, std::string &err)
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
inline std::string with_head(const std::string &body)
{
  size_t k = body.find_first_not_of(" \t\r\n");
  if (k != std::string::npos && body.compare(k, 8, "#version") == 0) return body;
  return std::string(HEAD) + body;
}

/* a program from a vertex and a fragment body; 0 and err if it fails */
inline GLuint link(const std::string &vs_body, const std::string &fs_body, std::string &err)
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

typedef std::map<std::string, std::vector<float>> Uniforms;

/* A shader source in the layout of stim2's shaders/ directory: "-- Name"
   lines start sections (Vertex, Fragment, Uniforms, and any other a module
   reads); text with no section marker is all Fragment.  Uniforms lines are
   "name v1 ?v2 v3 v4?", values optionally in double quotes. */
struct ShaderText {
  std::map<std::string, std::string> sections;
  Uniforms uniforms;
  const std::string &section(const std::string &name) const {
    static const std::string none;
    auto it = sections.find(name);
    return it == sections.end() ? none : it->second;
  }
};

inline void parse_shader(std::istream &in, ShaderText &out)
{
  std::string line, section = "Fragment";
  while (std::getline(in, line)) {
    if (line.compare(0, 2, "--") == 0) {
      std::string name = line.substr(2);
      size_t a = name.find_first_not_of(" \t");
      size_t b = name.find_last_not_of(" \t\r");
      section = a == std::string::npos ? "" : name.substr(a, b - a + 1);
      continue;
    }
    if (section == "Uniforms") {
      for (char &c : line) if (c == '"') c = ' ';
      std::istringstream ls(line);
      std::string name;
      if (!(ls >> name)) continue;
      std::vector<float> vals;
      float x;
      while (vals.size() < 4 && ls >> x) vals.push_back(x);
      if (!vals.empty()) out.uniforms[name] = vals;
    }
    else out.sections[section] += line + "\n";
  }
}

inline bool load_shader(const std::string &path, ShaderText &out, std::string &err)
{
  std::ifstream in(path);
  if (!in) { err = "cannot read " + path; return false; }
  parse_shader(in, out);
  if (blank(out.section("Fragment"))) { err = path + ": no fragment shader"; return false; }
  return true;
}

/* the float uniforms a program declares, of those given */
inline void apply_uniforms(GLuint prog, const Uniforms &u)
{
  for (auto &kv : u) {
    GLint loc = glGetUniformLocation(prog, kv.first.c_str());
    if (loc < 0) continue;
    const float *v = kv.second.data();
    switch (kv.second.size()) {
    case 1: glUniform1f(loc, v[0]); break;
    case 2: glUniform2f(loc, v[0], v[1]); break;
    case 3: glUniform3f(loc, v[0], v[1], v[2]); break;
    case 4: glUniform4f(loc, v[0], v[1], v[2], v[3]); break;
    }
  }
}

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

} /* namespace passutil */

#endif /* PASSUTIL_H */
