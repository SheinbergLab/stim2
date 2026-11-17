/*
 * tlock.cpp -- frame-locked triggers from a CRS Display++ (or Bits#, or
 * Bits++): the T-Lock digital-output packet, drawn as one row of pixels
 * (see docs/tlock.md)
 *
 * A stimdll.  The monitor watches the video stream for an unlock key and
 * loads what follows it into its digital outputs (the DB25, and the
 * trigger-out BNC), which it then drives from its own frame timing: an
 * edge at a fixed time after the frame reaches the panel, with no software
 * between the two.  Against stim/frame's flip time it measures the video
 * link and the monitor's input stage; against a photodiode, the panel.
 *
 * It is a post pass (src/postpass.h) of priority 950, a display encoding
 * that comes last: the row must reach the monitor exactly as written.
 *
 * What the device does shapes the pass:
 *   - a packet acts on the frame AFTER the one it is drawn in;
 *   - the device plays its last packet again on every frame until another
 *     replaces it.  So while the pass is on it draws a packet on every
 *     frame it is given (the idle one when no pulse is armed), a static
 *     display is repainted until the idle packet has followed a pulse, and
 *     `tlock off` draws one last idle packet before the pass goes quiet.
 *
 * Toggle mode uses the same fact the other way round: each frame drawn
 * flips an output's level for the whole frame, so an edge on that output
 * is a new frame reaching the panel and a refresh stim2 missed is an edge
 * that does not come.
 *
 * State is plain statics: the render thread is the only one that reads or
 * writes it (Tcl commands and the draw run there).
 */
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>

#ifdef _WIN32
#include <windows.h>
#endif

#include <tcl.h>
#include <glad/glad.h>

#include "postpass.h"         /* stim2's seam for full-screen passes */
#include "tlock_packet.h"

#ifndef TCL_SIZE_MAX
typedef int Tcl_Size;
#endif

extern "C" {
void redraw(void);
}

namespace {

void log_message(const char *level, const char *fmt, ...)
{
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  stimLogMessage(level, buf);
}

/* ---- settings ----------------------------------------------------------- */

struct Settings {
  int row = 2;                /* from the top; Psychtoolbox's default row */
  int x = 0;                  /* first pixel: even */
  int nslots = tlock::MAX_SLOTS;
  int mask = 0xFFFF;          /* the output bits a packet may change */
  int command = 0;
  int idle = 0;               /* the output word outside a pulse */
} cfg;

/* ---- state -------------------------------------------------------------- */

enum { OFF = 0, ON = 1, CLOSING = 2 };   /* closing: one idle packet to draw */
int state = OFF;

struct Pulse {
  bool armed = false;
  int bits = 0, on_slot = 0, dur_slots = 0;
  int frames_left = 0;        /* frames still to carry it; -1 = until stopped */
  long long serial = 0;       /* which `tlock pulse` call */
} pulse;

/* toggle mode: these bits change level on every frame drawn */
int toggle_bits = 0;
bool toggle_high = false;     /* the level the last frame drawn carried */
long long n_toggles = 0;      /* frames drawn in toggle mode, from 1 */

/* what the last packet drawn was, which is what the device now plays */
enum { SENT_NONE = 0, SENT_IDLE = 1, SENT_PULSE = 2 };
int sent = SENT_NONE;
long long sent_serial = 0;
int sent_base = 0;            /* its output word outside a pulse */
bool settings_dirty = true;   /* the idle packet as drawn is out of date */

/* the frame just drawn, until its flip is known */
struct Drawn {
  bool pending = false;
  bool record = false;        /* a frame worth a stim/tlock record */
  int bits = 0, on_slot = 0, dur_slots = 0;
  long long serial = 0;
  int base = 0;               /* the output word outside the pulse */
  long long toggle_n = 0;     /* its count in toggle mode, else 0 */
} drawn;
long long recorded_serial = 0;  /* an until-stopped pulse is recorded once */

long long n_frames = 0, n_pulse_frames = 0, n_pulses = 0, n_nofit = 0;
double last_pulse_flip_s = -1.0;
long long last_pulse_flip_wall_us = -1;
bool warned_nofit = false;

/* ---- GL ----------------------------------------------------------------- */

GLuint tex = 0, fbo = 0;
int tex_w = 0;
std::vector<uint8_t> tex_bytes;         /* what the texture holds */

bool fits(int w, int h)
{
  return cfg.row >= 0 && cfg.row < h && cfg.x >= 0 &&
         cfg.x + tlock::packet_width(cfg.nslots) <= w;
}

/* the packet into the 1-row texture behind fbo; false if GL refuses */
bool upload(const std::vector<uint8_t> &px)
{
  int w = (int) (px.size() / 4);
  if (tex && tex_w == w && px == tex_bytes) return true;
  GLint o_tex, o_align;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &o_tex);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &o_align);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  bool ok = true;
  if (!tex || tex_w != w) {
    if (!tex) glGenTextures(1, &tex);
    if (!fbo) glGenFramebuffers(1, &fbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
      log_message("error", "tlock: packet target incomplete (0x%x)", st);
      tex_w = 0;
      ok = false;
    }
    else tex_w = w;
  }
  else {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  }
  glBindTexture(GL_TEXTURE_2D, o_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, o_align);
  if (ok) tex_bytes = px; else tex_bytes.clear();
  return ok;
}

/* The row onto the frame: a 1:1 blit, so every byte arrives as encoded (no
   shader, no blending, no filtering).  The scissor and an sRGB encode
   would both apply to a blit: off for it. */
void blit(const stim_postpass_frame_t *f)
{
  GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  if (scissor) glDisable(GL_SCISSOR_TEST);
#if defined(GL_FRAMEBUFFER_SRGB) && !defined(STIM2_USE_GLES)
  GLboolean srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
  if (srgb) glDisable(GL_FRAMEBUFFER_SRGB);
#endif
  GLboolean cmask[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  int y = f->h - 1 - cfg.row;           /* GL's rows count from the bottom */
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f->drawfb);
  glBlitFramebuffer(0, 0, tex_w, 1, cfg.x, y, cfg.x + tex_w, y + 1,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);

  glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
#if defined(GL_FRAMEBUFFER_SRGB) && !defined(STIM2_USE_GLES)
  if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
#endif
  if (scissor) glEnable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, f->drawfb);
  glReadBuffer(f->drawnbuf);
}

/* ---- the packets -------------------------------------------------------- */

/* the output word outside a pulse: the idle word, with the toggle bits at
   the given level while toggle mode is on */
int base_word(bool high)
{
  if (!toggle_bits) return cfg.idle;
  return high ? (cfg.idle | toggle_bits) : (cfg.idle & ~toggle_bits);
}

std::vector<uint8_t> idle_packet(int base)
{
  return tlock::encode(nullptr, cfg.nslots, (uint16_t) cfg.mask, (uint8_t) cfg.command,
                       (uint16_t) base);
}

/* the pulse's bits over the base word, for the pulse's slots */
std::vector<uint8_t> pulse_packet(int base)
{
  return tlock::encode_pulse(cfg.nslots, (uint16_t) (base | pulse.bits), pulse.on_slot, pulse.dur_slots,
                             (uint16_t) base, (uint16_t) cfg.mask, (uint8_t) cfg.command);
}

/* a pulse is waiting for a frame to carry it */
bool pulse_due() { return state == ON && pulse.armed && pulse.frames_left != 0; }

/* ---- the pass, as stim2 calls it (postpass.h) --------------------------- */

int pass_wanted(void *) { return state != OFF; }

int pass_stage(void *, const stim_postpass_frame_t *f)
{
  if (state == OFF || f->w <= 0 || f->h <= 0) return 0;
  if (!fits(f->w, f->h)) {
    n_nofit++;
    if (!warned_nofit) {
      warned_nofit = true;
      log_message("error", "tlock: a %d-pixel packet at x %d, row %d does not fit a %dx%d frame: not drawn",
                  tlock::packet_width(cfg.nslots), cfg.x, cfg.row, f->w, f->h);
    }
    if (state == CLOSING) state = OFF;
    return 0;
  }

  bool is_pulse = pulse_due();
  /* toggle mode: this frame carries the other level.  The last packet of
     a closing pass leaves every output at the idle word. */
  bool toggling = state == ON && toggle_bits != 0;
  bool high = toggling ? !toggle_high : false;
  int base = state == ON ? base_word(high) : cfg.idle;
  if (!upload(is_pulse ? pulse_packet(base) : idle_packet(base))) return 0;
  blit(f);
  n_frames++;
  toggle_high = high;
  sent_base = base;

  drawn.pending = true;
  drawn.record = toggling;      /* every toggle frame: its edge has a swap */
  drawn.bits = drawn.on_slot = drawn.dur_slots = 0;
  drawn.serial = 0;
  drawn.base = base;
  drawn.toggle_n = toggling ? ++n_toggles : 0;
  if (is_pulse) {
    n_pulse_frames++;
    /* every frame of a counted pulse; an until-stopped one only once */
    if (pulse.frames_left > 0 || recorded_serial != pulse.serial) drawn.record = true;
    recorded_serial = pulse.serial;
    drawn.bits = pulse.bits;
    drawn.on_slot = pulse.on_slot;
    drawn.dur_slots = pulse.dur_slots;
    drawn.serial = pulse.serial;
    if (pulse.frames_left > 0 && --pulse.frames_left == 0) pulse.armed = false;
    sent = SENT_PULSE;
    sent_serial = pulse.serial;
  }
  else {
    sent = SENT_IDLE;
    settings_dirty = false;
    if (state == CLOSING) state = OFF;  /* the device is left idle */
  }
  return 1;
}

/* A static display.  The device plays what it was last sent, so a repaint
   is due only while that is not what it should now be playing: a pulse not
   yet drawn, a counted pulse with frames to go (each refresh carries one),
   or the idle packet owed after a pulse or a changed setting.  Toggle mode
   asks for none: it marks the frames others cause. */
int pass_repaint_due(void *, double, double *wait_s)
{
  *wait_s = -1.0;
  if (state == OFF) return 0;
  if (state == CLOSING) return 1;
  if (pulse_due()) {
    if (pulse.frames_left > 0) return 1;
    return !(sent == SENT_PULSE && sent_serial == pulse.serial);
  }
  return sent != SENT_IDLE || settings_dirty;
}

/* the flip that showed a packet: when it was, and the frame's record for
   the data file (stim/tlock: after the swapcount stim2 puts first, the flip
   on the wall clock, the pulse's bits, start and length in 100 us slots
   and which `tlock pulse` call it was (all 0 on a frame with no pulse),
   the output word outside the pulse, and the frame's count in toggle
   mode) */
void pass_flip(void *, double flip_s, long long flip_wall_us)
{
  if (!drawn.pending) return;
  drawn.pending = false;
  if (!drawn.record) return;
  long long wall = flip_wall_us > 0 ? flip_wall_us : -1;
  if (drawn.serial) { last_pulse_flip_s = flip_s; last_pulse_flip_wall_us = wall; }
  long long v[7] = { wall, drawn.bits, drawn.on_slot, drawn.dur_slots, drawn.serial,
                     drawn.base, drawn.toggle_n };
  stimFrameRecord("stim/tlock", v, 7);
}

/* off, leaving the device idle: if it may be playing a pulse, one more
   frame carries the idle packet first */
void turn_off()
{
  pulse.armed = false;
  pulse.frames_left = 0;
  if (state == OFF) return;
  /* a pulse, or a toggle left high, would play on */
  if (sent == SENT_PULSE || (sent == SENT_IDLE && sent_base != cfg.idle)) { state = CLOSING; redraw(); }
  else state = OFF;
}

/* `postpass reset`: off and back to the defaults.  A pending last idle
   packet keeps the row and the size it was to be drawn with. */
void pass_reset(void *, Tcl_Interp *)
{
  turn_off();
  toggle_bits = 0;
  toggle_high = false;
  if (state == OFF) { cfg = Settings(); settings_dirty = true; }
  warned_nofit = false;
}

stim_postpass_t pass = {
  "tlock", 950.0f, nullptr,
  pass_wanted, pass_stage,
  nullptr,                      /* composite: stim2 redraws the scene */
  pass_repaint_due,
  nullptr,                      /* late_ms */
  pass_flip, pass_reset
};

/* ---- the Tcl command ---------------------------------------------------- */

const char *USAGE =
  "tlock on | off | "
  "set ?-row r? ?-x x? ?-slots n? ?-mask m? ?-command c? ?-idle word? | "
  "pulse bits ?-on ms? ?-dur ms? ?-frames n|always? | stop | toggle ?bits? | "
  "packet ?idle|pulse? | status | reset";

Tcl_Obj *settings_dict()
{
  Tcl_Obj *d = Tcl_NewDictObj();
  auto put = [&](const char *k, int v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), Tcl_NewIntObj(v)); };
  put("-row", cfg.row);
  put("-x", cfg.x);
  put("-slots", cfg.nslots);
  put("-mask", cfg.mask);
  put("-command", cfg.command);
  put("-idle", cfg.idle);
  return d;
}

int get_range(Tcl_Interp *interp, Tcl_Obj *o, const char *what, int lo, int hi, int *out)
{
  int v;
  if (Tcl_GetIntFromObj(interp, o, &v) != TCL_OK) return TCL_ERROR;
  if (v < lo || v > hi) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("tlock: %s is %d to %d", what, lo, hi));
    return TCL_ERROR;
  }
  *out = v;
  return TCL_OK;
}

int set_cmd(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  if ((objc - 2) % 2) {
    Tcl_WrongNumArgs(interp, 2, objv, "?-row r? ?-x x? ?-slots n? ?-mask m? ?-command c? ?-idle word?");
    return TCL_ERROR;
  }
  Settings s = cfg;
  for (int i = 2; i < objc; i += 2) {
    std::string opt = Tcl_GetString(objv[i]);
    Tcl_Obj *val = objv[i + 1];
    int rc;
    if (opt == "-row") rc = get_range(interp, val, "-row", 0, 65535, &s.row);
    else if (opt == "-x") {
      rc = get_range(interp, val, "-x", 0, 65535, &s.x);
      if (rc == TCL_OK && (s.x & 1)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("tlock: -x must be even (the device reads pixel pairs)", -1));
        rc = TCL_ERROR;
      }
    }
    else if (opt == "-slots") rc = get_range(interp, val, "-slots", 1, tlock::MAX_SLOTS, &s.nslots);
    else if (opt == "-mask") rc = get_range(interp, val, "-mask", 0, 0xFFFF, &s.mask);
    else if (opt == "-command") rc = get_range(interp, val, "-command", 0, 255, &s.command);
    else if (opt == "-idle") rc = get_range(interp, val, "-idle", 0, 0xFFFF, &s.idle);
    else {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("tlock set: unknown option \"%s\" (-row -x -slots -mask -command -idle)", opt.c_str()));
      rc = TCL_ERROR;
    }
    if (rc != TCL_OK) return TCL_ERROR;
  }
  if (objc > 2) {
    if (pulse.armed && pulse.on_slot >= s.nslots) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj("tlock: the armed pulse starts past -slots", -1));
      return TCL_ERROR;
    }
    cfg = s;
    settings_dirty = true;
    warned_nofit = false;
  }
  Tcl_SetObjResult(interp, settings_dict());
  return TCL_OK;
}

/* pulse BITS ?-on MS? ?-dur MS? ?-frames N|always?: the next N frames drawn
   carry BITS high from MS into the frame, for MS.  Returns the pulse's
   serial, as its stim/tlock records have it. */
int pulse_cmd(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  if (objc < 3 || (objc - 3) % 2) {
    Tcl_WrongNumArgs(interp, 2, objv, "bits ?-on ms? ?-dur ms? ?-frames n|always?");
    return TCL_ERROR;
  }
  int bits, frames = 1;
  double on_ms = 0.0, dur_ms = 1.0;
  if (get_range(interp, objv[2], "bits", 0, 0xFFFF, &bits) != TCL_OK) return TCL_ERROR;
  for (int i = 3; i < objc; i += 2) {
    std::string opt = Tcl_GetString(objv[i]);
    Tcl_Obj *val = objv[i + 1];
    if (opt == "-on") {
      if (Tcl_GetDoubleFromObj(interp, val, &on_ms) != TCL_OK) return TCL_ERROR;
    }
    else if (opt == "-dur") {
      if (Tcl_GetDoubleFromObj(interp, val, &dur_ms) != TCL_OK) return TCL_ERROR;
    }
    else if (opt == "-frames") {
      if (std::string(Tcl_GetString(val)) == "always") frames = -1;
      else if (get_range(interp, val, "-frames", 1, 1000000, &frames) != TCL_OK) return TCL_ERROR;
    }
    else {
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("tlock pulse: unknown option \"%s\" (-on -dur -frames)", opt.c_str()));
      return TCL_ERROR;
    }
  }
  if (!(on_ms >= 0.0) || !(dur_ms > 0.0) || on_ms > 1000.0 || dur_ms > 1000.0) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("tlock pulse: -on is 0 or more and -dur more than 0, in ms", -1));
    return TCL_ERROR;
  }
  int on_slot = (int) std::lround(on_ms * tlock::SLOTS_PER_MS);
  int dur_slots = (int) std::lround(dur_ms * tlock::SLOTS_PER_MS);
  if (dur_slots < 1) dur_slots = 1;
  if (on_slot >= cfg.nslots) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("tlock pulse: -on %g ms is past the packet's %d slots of 0.1 ms", on_ms, cfg.nslots));
    return TCL_ERROR;
  }
  pulse.armed = true;
  pulse.bits = bits;
  pulse.on_slot = on_slot;
  pulse.dur_slots = dur_slots;
  pulse.frames_left = frames;
  pulse.serial = ++n_pulses;
  Tcl_SetObjResult(interp, Tcl_NewWideIntObj(pulse.serial));
  return TCL_OK;
}

Tcl_Obj *status_dict()
{
  Tcl_Obj *d = Tcl_NewDictObj();
  auto put = [&](const char *k, Tcl_Obj *v) { Tcl_DictObjPut(nullptr, d, Tcl_NewStringObj(k, -1), v); };
  put("on", Tcl_NewIntObj(state == ON ? 1 : 0));
  put("state", Tcl_NewStringObj(state == ON ? "on" : state == CLOSING ? "closing" : "off", -1));
  put("settings", settings_dict());
  put("width", Tcl_NewIntObj(tlock::packet_width(cfg.nslots)));
  put("armed", Tcl_NewIntObj(pulse.armed ? 1 : 0));
  put("frames_left", Tcl_NewIntObj(pulse.armed ? pulse.frames_left : 0));
  put("sent", Tcl_NewStringObj(sent == SENT_PULSE ? "pulse" : sent == SENT_IDLE ? "idle" : "none", -1));
  put("toggle", Tcl_NewIntObj(toggle_bits));
  put("toggle_high", Tcl_NewIntObj(toggle_high ? 1 : 0));
  put("toggles", Tcl_NewWideIntObj(n_toggles));
  put("pulses", Tcl_NewWideIntObj(n_pulses));
  put("frames", Tcl_NewWideIntObj(n_frames));
  put("pulse_frames", Tcl_NewWideIntObj(n_pulse_frames));
  put("not_drawn", Tcl_NewWideIntObj(n_nofit));
  put("last_pulse_flip_s", Tcl_NewDoubleObj(last_pulse_flip_s));
  put("last_pulse_flip_wall_us", Tcl_NewWideIntObj(last_pulse_flip_wall_us));
  return d;
}

int tlockCmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  std::string sub = objc > 1 ? Tcl_GetString(objv[1]) : "status";

  if (sub == "status" && objc <= 2) {
    Tcl_SetObjResult(interp, status_dict());
    return TCL_OK;
  }
  if (sub == "on" && objc == 2) {
    if (state != ON) {
      state = ON;
      sent = SENT_NONE;         /* the device's state is not known: say it */
      settings_dirty = true;
      warned_nofit = false;
      redraw();
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
    return TCL_OK;
  }
  if (sub == "off" && objc == 2) {
    turn_off();
    Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
    return TCL_OK;
  }
  if (sub == "set") return set_cmd(interp, objc, objv);
  if (sub == "pulse") return pulse_cmd(interp, objc, objv);
  if (sub == "stop" && objc == 2) {
    pulse.armed = false;
    pulse.frames_left = 0;
    return TCL_OK;
  }
  /* toggle ?BITS?: these outputs change level on every frame drawn (0:
     off, and the next frame puts them back at the idle word) */
  if (sub == "toggle" && objc <= 3) {
    if (objc == 3) {
      int bits;
      if (get_range(interp, objv[2], "toggle bits", 0, 0xFFFF, &bits) != TCL_OK) return TCL_ERROR;
      if (bits != toggle_bits) {
        toggle_bits = bits;
        toggle_high = false;
        settings_dirty = true;  /* a static display: one frame says so */
      }
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(toggle_bits));
    return TCL_OK;
  }
  if (sub == "reset" && objc == 2) {
    pass_reset(nullptr, interp);
    return TCL_OK;
  }
  /* packet ?idle|pulse?: the row as it would be drawn, R G B per pixel
     (default: what the next frame would carry) */
  if (sub == "packet" && objc <= 3) {
    std::string which = objc == 3 ? Tcl_GetString(objv[2]) : (pulse_due() ? "pulse" : "idle");
    std::vector<uint8_t> px;
    /* as the next frame would carry it: in toggle mode, the other level */
    int base = state == ON && toggle_bits ? base_word(!toggle_high) : base_word(false);
    if (which == "idle") px = idle_packet(base);
    else if (which == "pulse") {
      if (!pulse.armed) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("tlock packet: no pulse is armed", -1));
        return TCL_ERROR;
      }
      px = pulse_packet(base);
    }
    else {
      Tcl_SetObjResult(interp, Tcl_NewStringObj("tlock packet ?idle|pulse?", -1));
      return TCL_ERROR;
    }
    Tcl_Obj *l = Tcl_NewListObj(0, nullptr);
    for (size_t i = 0; i < px.size(); i += 4)
      for (int c = 0; c < 3; c++)
        Tcl_ListObjAppendElement(nullptr, l, Tcl_NewIntObj(px[i + c]));
    Tcl_SetObjResult(interp, l);
    return TCL_OK;
  }

  Tcl_SetObjResult(interp, Tcl_ObjPrintf("usage: %s", USAGE));
  return TCL_ERROR;
}

} /* namespace */

#ifdef _WIN32
extern "C" __declspec(dllexport) int Tlock_Init(Tcl_Interp *interp)
#else
extern "C" int Tlock_Init(Tcl_Interp *interp)
#endif
{
  if (
#ifdef USE_TCL_STUBS
      Tcl_InitStubs(interp, "8.5-", 0)
#else
      Tcl_PkgRequire(interp, "Tcl", "8.5-", 0)
#endif
      == NULL) {
    return TCL_ERROR;
  }
  gladLoadGL();
  stimPostpassRegister(&pass);
  Tcl_CreateObjCommand(interp, "tlock", tlockCmd, nullptr, nullptr);
  return TCL_OK;
}
