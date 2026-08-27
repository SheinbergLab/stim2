/*
 * swapsync.cpp
 *  See swapsync.h.  One strategy is selected at startup and reported (with
 *  -v); everything degrades to plain glFinish() when the better mechanisms
 *  are unavailable, which is exactly the historical behavior.
 */

#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "swapsync.h"

/* stim2's global logger (stim2.cpp; C++ linkage) */
void log_message(const char *level, const char *fmt, ...);

typedef enum {
  STRAT_GLFINISH = 0,
  STRAT_OML,
  STRAT_WL_PRESENTATION,
} swapsync_strategy_t;

static struct {
  GLFWwindow *window;
  swapsync_strategy_t strategy;
  int verbose;
  double frame_period;          /* seconds, from the video mode */

  double last_flip_glfw;        /* glfwGetTime() domain, 0 = none */
  long long last_flip_wall_us;  /* CLOCK_REALTIME us, -1 = none */
  double last_refresh_ms;
  long long last_counter;       /* MSC / presentation seq, -1 = none */
  long missed;
  long discarded;
  long timeouts;
  double last_return;           /* glfwGetTime() when afterSwap last returned */
} ss = { NULL, STRAT_GLFINISH, 0, 1.0 / 60.0, 0.0, -1, 0.0, -1, 0, 0, 0, 0.0 };

/* Update flip bookkeeping shared by the OML and Wayland paths.  A counter
   jump only counts as missed frames when the previous swap was recent enough
   that the display could not simply have been idle between them. */
static void record_flip(double flip_glfw, long long counter, double refresh_ms)
{
  int continuous = ss.last_return > 0.0 &&
    (glfwGetTime() - ss.last_return) < 1.75 * ss.frame_period;
  if (continuous && counter >= 0 && ss.last_counter >= 0 &&
      counter > ss.last_counter + 1)
    ss.missed += (long) (counter - ss.last_counter - 1);
  if (counter >= 0) ss.last_counter = counter;
  if (flip_glfw > 0.0) {
    ss.last_flip_glfw = flip_glfw;
    /* Place the flip on the wall clock: sample (wall, glfw) together NOW --
       the flip was at most a frame ago, so slew-induced offset drift since
       then is microseconds at worst -- and project back.  glfwGetTime() and
       the flip source share CLOCK_MONOTONIC on Linux, so the difference is
       exact. */
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
      double wall_now_us = ts.tv_sec * 1e6 + ts.tv_nsec * 1e-3;
      ss.last_flip_wall_us = (long long)
        (wall_now_us - (glfwGetTime() - flip_glfw) * 1e6);
    }
  }
  if (refresh_ms > 0.0) ss.last_refresh_ms = refresh_ms;
}

/* ------------------------------------------------------------------ */
/* X11 / GLX_OML_sync_control                                          */
/* ------------------------------------------------------------------ */
#if defined(__linux__) && defined(GLFW_PLATFORM_X11)
#define SWAPSYNC_TRY_OML 1

#include <time.h>
#include <dlfcn.h>

/* GLFW's native-access functions exist only for the backends GLFW was
   compiled with, so referencing them directly would make stim2 unlinkable
   against e.g. a Wayland-only libglfw3.a.  stim2 links with -rdynamic, so
   look them up in our own image instead; type-erased signatures keep this
   file free of Xlib.h/GL/glx.h (GLXWindow is an XID). */
typedef void *(*PFN_GetX11Display)(void);
typedef unsigned long (*PFN_GetGLXWindow)(GLFWwindow *window);

typedef int (*PFN_WaitForSbcOML)(void *dpy, unsigned long drawable,
                                 int64_t target_sbc,
                                 int64_t *ust, int64_t *msc, int64_t *sbc);
typedef int (*PFN_GetSyncValuesOML)(void *dpy, unsigned long drawable,
                                    int64_t *ust, int64_t *msc, int64_t *sbc);

static struct {
  void *dpy;
  unsigned long drawable;
  PFN_WaitForSbcOML wait_for_sbc;
  double mono_to_glfw;          /* glfwGetTime() - CLOCK_MONOTONIC seconds */
  int ust_reliable;             /* UST verified to be CLOCK_MONOTONIC us */
} oml;

static double mono_now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int oml_init(void)
{
  if (glfwGetPlatform() != GLFW_PLATFORM_X11) return 0;
  /* GLES contexts on X11 may be EGL-backed; OML rides GLX only */
  if (glfwGetWindowAttrib(ss.window, GLFW_CONTEXT_CREATION_API) !=
      GLFW_NATIVE_CONTEXT_API) return 0;

  PFN_GetX11Display get_display =
    (PFN_GetX11Display) dlsym(RTLD_DEFAULT, "glfwGetX11Display");
  PFN_GetGLXWindow get_glxwin =
    (PFN_GetGLXWindow) dlsym(RTLD_DEFAULT, "glfwGetGLXWindow");
  if (!get_display || !get_glxwin) return 0;   /* GLFW built without GLX */

  oml.dpy = get_display();
  oml.drawable = get_glxwin(ss.window);
  if (!oml.dpy || !oml.drawable) return 0;

  oml.wait_for_sbc =
    (PFN_WaitForSbcOML) glfwGetProcAddress("glXWaitForSbcOML");
  PFN_GetSyncValuesOML get_sync =
    (PFN_GetSyncValuesOML) glfwGetProcAddress("glXGetSyncValuesOML");
  if (!oml.wait_for_sbc || !get_sync) return 0;

  /* Functional probe: Mesa and NVIDIA both export these entry points even
     when the extension is unsupported, returning False instead */
  int64_t ust = 0, msc = 0, sbc = 0;
  if (!get_sync(oml.dpy, oml.drawable, &ust, &msc, &sbc)) return 0;

  oml.mono_to_glfw = glfwGetTime() - mono_now();
  oml.ust_reliable =
    fabs(ust * 1e-6 + oml.mono_to_glfw - glfwGetTime()) < 0.5;
  if (!oml.ust_reliable)
    log_message("warn", "swapsync: OML UST is not CLOCK_MONOTONIC; "
                "flip timestamps will use wait-return time");
  return 1;
}

static void oml_wait(void)
{
  int64_t ust = 0, msc = 0, sbc = 0;
  /* target_sbc = 0: block until every queued swap for this drawable has
     completed, i.e. until our flip is on glass */
  if (oml.wait_for_sbc(oml.dpy, oml.drawable, 0, &ust, &msc, &sbc)) {
    double flip = oml.ust_reliable ? ust * 1e-6 + oml.mono_to_glfw
                                   : glfwGetTime();
    record_flip(flip, msc, 0.0);
  }
  else {
    glFinish();                 /* server hiccup: at least drain the GPU */
  }
}
#endif /* SWAPSYNC_TRY_OML */

/* ------------------------------------------------------------------ */
/* Wayland / wp_presentation                                           */
/* ------------------------------------------------------------------ */
#if defined(STIM2_HAVE_WAYLAND_PRESENTATION) && defined(GLFW_PLATFORM_WAYLAND)
#define SWAPSYNC_TRY_WL 1

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <dlfcn.h>
#include <wayland-client.h>
#include "presentation-time-client-protocol.h"

/* Same dlsym story as the X11 branch: don't hard-link native access that
   only exists when GLFW was compiled with the Wayland backend. */
typedef struct wl_display *(*PFN_GetWaylandDisplay)(void);
typedef struct wl_surface *(*PFN_GetWaylandWindow)(GLFWwindow *window);

static struct {
  struct wl_display *display;
  struct wl_surface *surface;
  struct wl_event_queue *queue;   /* private queue; GLFW keeps its own */
  struct wl_proxy *display_wrap;
  struct wp_presentation *presentation;
  uint32_t clk_id;
  int have_clk;
  double clk_to_glfw;             /* glfwGetTime() - clk_id seconds */

  struct wp_presentation_feedback *pending;
  struct {
    int done, discarded;
    uint64_t seq;
    double when;                  /* clk_id domain, seconds */
    double refresh_ms;
  } frame;
} wl;

static double wl_clock_now(clockid_t id)
{
  struct timespec ts;
  clock_gettime(id, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void fb_sync_output(void *, struct wp_presentation_feedback *,
                           struct wl_output *) {}

static void fb_presented(void *, struct wp_presentation_feedback *,
                         uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec,
                         uint32_t refresh, uint32_t seq_hi, uint32_t seq_lo,
                         uint32_t /* flags */)
{
  wl.frame.when = ((double) (((uint64_t) sec_hi << 32) | sec_lo)) + nsec * 1e-9;
  wl.frame.refresh_ms = refresh * 1e-6;
  wl.frame.seq = ((uint64_t) seq_hi << 32) | seq_lo;
  wl.frame.done = 1;
}

static void fb_discarded(void *, struct wp_presentation_feedback *)
{
  wl.frame.discarded = 1;
}

static const struct wp_presentation_feedback_listener fb_listener = {
  fb_sync_output, fb_presented, fb_discarded
};

static void pres_clock_id(void *, struct wp_presentation *, uint32_t clk)
{
  wl.clk_id = clk;
  wl.have_clk = 1;
}

static const struct wp_presentation_listener pres_listener = { pres_clock_id };

static void registry_global(void *, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
  if (!wl.presentation &&
      strcmp(interface, wp_presentation_interface.name) == 0) {
    wl.presentation = (struct wp_presentation *)
      wl_registry_bind(registry, name, &wp_presentation_interface,
                       version < 1 ? version : 1);
    wp_presentation_add_listener(wl.presentation, &pres_listener, NULL);
  }
}

static void registry_global_remove(void *, struct wl_registry *, uint32_t) {}

static const struct wl_registry_listener registry_listener = {
  registry_global, registry_global_remove
};

static int wl_init(void)
{
  if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) return 0;

  PFN_GetWaylandDisplay get_display =
    (PFN_GetWaylandDisplay) dlsym(RTLD_DEFAULT, "glfwGetWaylandDisplay");
  PFN_GetWaylandWindow get_surface =
    (PFN_GetWaylandWindow) dlsym(RTLD_DEFAULT, "glfwGetWaylandWindow");
  if (!get_display || !get_surface) return 0;  /* GLFW built without Wayland */

  wl.display = get_display();
  wl.surface = get_surface(ss.window);
  if (!wl.display || !wl.surface) return 0;

  wl.queue = wl_display_create_queue(wl.display);
  if (!wl.queue) return 0;

  /* Wrapper proxy so the registry (and everything bound through it) events
     into our queue instead of GLFW's */
  wl.display_wrap = (struct wl_proxy *) wl_proxy_create_wrapper(wl.display);
  if (!wl.display_wrap) return 0;
  wl_proxy_set_queue(wl.display_wrap, wl.queue);

  struct wl_registry *registry =
    wl_display_get_registry((struct wl_display *) wl.display_wrap);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip_queue(wl.display, wl.queue);   /* globals */
  wl_display_roundtrip_queue(wl.display, wl.queue);   /* clock_id */
  wl_registry_destroy(registry);

  if (!wl.presentation) return 0;
  if (!wl.have_clk) wl.clk_id = CLOCK_MONOTONIC;
  wl.clk_to_glfw = glfwGetTime() - wl_clock_now((clockid_t) wl.clk_id);
  return 1;
}

static void wl_before_swap(void)
{
  if (wl.pending) {               /* abandoned by an earlier timeout */
    wp_presentation_feedback_destroy(wl.pending);
    wl.pending = NULL;
  }
  memset(&wl.frame, 0, sizeof(wl.frame));
  /* Must precede eglSwapBuffers so the feedback rides the same commit */
  wl.pending = wp_presentation_feedback(wl.presentation, wl.surface);
  if (wl.pending)
    wp_presentation_feedback_add_listener(wl.pending, &fb_listener, NULL);
}

static void wl_wait(void)
{
  if (!wl.pending) { glFinish(); return; }

  glFinish();   /* rendering done; now wait for the compositor's flip */

  double deadline = glfwGetTime() + 4.0 * ss.frame_period + 0.05;
  int failed = 0;
  while (!wl.frame.done && !wl.frame.discarded && !failed) {
    while (wl_display_prepare_read_queue(wl.display, wl.queue) != 0)
      wl_display_dispatch_queue_pending(wl.display, wl.queue);
    wl_display_flush(wl.display);

    double remaining = deadline - glfwGetTime();
    if (remaining <= 0.0) {
      wl_display_cancel_read(wl.display);
      ss.timeouts++;
      if (ss.timeouts == 1)
        log_message("warn", "swapsync: presentation feedback timed out; "
                    "is the surface occluded?");
      break;
    }
    struct pollfd pfd = { wl_display_get_fd(wl.display), POLLIN, 0 };
    int pr = poll(&pfd, 1, (int) (remaining * 1000.0) + 1);
    if (pr > 0) {
      if (wl_display_read_events(wl.display) < 0) failed = 1;
    }
    else {
      wl_display_cancel_read(wl.display);
      if (pr < 0 && errno != EINTR) failed = 1;
      continue;
    }
    wl_display_dispatch_queue_pending(wl.display, wl.queue);
  }

  if (wl.frame.done) {
    record_flip(wl.frame.when + wl.clk_to_glfw, (long long) wl.frame.seq,
                wl.frame.refresh_ms);
    wp_presentation_feedback_destroy(wl.pending);
    wl.pending = NULL;
  }
  else if (wl.frame.discarded) {
    ss.discarded++;
    if (ss.discarded == 1)
      log_message("warn", "swapsync: compositor discarded a frame "
                  "(never presented)");
    wp_presentation_feedback_destroy(wl.pending);
    wl.pending = NULL;
  }
  /* on timeout wl.pending is left for wl_before_swap() to destroy */

  if (failed) {
    log_message("warn", "swapsync: wayland connection error; "
                "falling back to glFinish");
    ss.strategy = STRAT_GLFINISH;
  }
}
#endif /* SWAPSYNC_TRY_WL */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void swapsyncInit(GLFWwindow *window, int refresh_rate_hz, int verbose)
{
  ss.window = window;
  ss.verbose = verbose;
  ss.strategy = STRAT_GLFINISH;
  if (refresh_rate_hz > 0) ss.frame_period = 1.0 / refresh_rate_hz;

#if defined(SWAPSYNC_TRY_OML)
  if (oml_init()) ss.strategy = STRAT_OML;
#endif
#if defined(SWAPSYNC_TRY_WL)
  if (ss.strategy == STRAT_GLFINISH && wl_init())
    ss.strategy = STRAT_WL_PRESENTATION;
#endif

  if (verbose)
    log_message("info", "swapsync: strategy '%s' (%d Hz)",
                swapsyncStrategyName(), refresh_rate_hz);
  if (ss.strategy == STRAT_GLFINISH)
    log_message("info", "swapsync: no presentation feedback available; "
                "swap acks rely on glFinish semantics");
}

void swapsyncBeforeSwap(void)
{
#if defined(SWAPSYNC_TRY_WL)
  if (ss.strategy == STRAT_WL_PRESENTATION) wl_before_swap();
#endif
}

void swapsyncAfterSwap(void)
{
  switch (ss.strategy) {
#if defined(SWAPSYNC_TRY_OML)
  case STRAT_OML:
    oml_wait();
    break;
#endif
#if defined(SWAPSYNC_TRY_WL)
  case STRAT_WL_PRESENTATION:
    wl_wait();
    break;
#endif
  default:
    glFinish();
    break;
  }
  ss.last_return = glfwGetTime();
}

const char *swapsyncStrategyName(void)
{
  switch (ss.strategy) {
  case STRAT_OML:             return "oml";
  case STRAT_WL_PRESENTATION: return "presentation";
  default:                    return "glfinish";
  }
}

double swapsyncLastFlipTime(void)     { return ss.last_flip_glfw; }
long long swapsyncLastFlipWallUs(void) { return ss.last_flip_wall_us; }
double swapsyncLastRefreshMs(void)    { return ss.last_refresh_ms; }
long long swapsyncLastCounter(void)   { return ss.last_counter; }
long swapsyncMissedFrames(void)       { return ss.missed; }
long swapsyncDiscardedFrames(void)    { return ss.discarded; }
long swapsyncTimeouts(void)           { return ss.timeouts; }
