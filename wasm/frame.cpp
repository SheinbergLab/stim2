/*
 * frame.cpp
 *   The stim2 refresh, without the window, the sockets, or imgui.
 *   One call is one display frame: advance a dynamic group, run the
 *   pre-scripts (which is where object animations update), draw, then
 *   run the post-scripts and the frame scripts. stimTimeSet and
 *   stimPlayback own StimTimeF the same way they do in stim2.cpp.
 */

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <tcl.h>
#include "prmutil.h"
#include "stim2.h"
#include "animate.h"
#include "rawapi.h"
#include "swapsync.h"
#include "timer.h"

extern "C" int stim_redraw(void);
extern "C" int stim_ndraws(void);
extern "C" int kickAnimation(void);

void log_message(const char *level, const char *fmt, ...) {
    std::va_list ap;
    std::fprintf(stderr, "[%s] ", level);
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

static int current_eye = 0;
static int stim_playback = 0;
static int wants_frame = 0;
static double wall_ms = 0;

static int dummy_int = 1;
static float pix_per_deg_x = 0;
static float pix_per_deg_y = 0;
static float half_screen_deg_x = 20.f;
static float half_screen_deg_y = 12.f;
static float half_screen_deg_z = 1000.f;
static int screen_width = 0;
static int screen_height = 0;
static float screen_width_cm = 0;
static float screen_height_cm = 0;
static float distance_to_monitor = 40.f;
static float x_scale = 0;
static float y_scale = 0;
static int win_width = 0;
static int win_height = 0;
static int refresh_rate = 60;
static float frame_duration = 1000.f / 60.f;

extern "C" PARAM_ENTRY ScreenParamTable[] = {
    { (char *)"PixPerDegreeX", &pix_per_deg_x, &dummy_int, PU_FLOAT },
    { (char *)"PixPerDegreeY", &pix_per_deg_y, &dummy_int, PU_FLOAT },
    { (char *)"HalfScreenDegreeX", &half_screen_deg_x, &dummy_int, PU_FLOAT },
    { (char *)"HalfScreenDegreeY", &half_screen_deg_y, &dummy_int, PU_FLOAT },
    { (char *)"ScreenWidth", &screen_width, &dummy_int, PU_INT },
    { (char *)"ScreenHeight", &screen_height, &dummy_int, PU_INT },
    { (char *)"ScreenWidthCm", &screen_width_cm, &dummy_int, PU_FLOAT },
    { (char *)"ScreenHeightCm", &screen_height_cm, &dummy_int, PU_FLOAT },
    { (char *)"DistanceToMonitor", &distance_to_monitor, &dummy_int, PU_FLOAT },
    { (char *)"ScaleX", &x_scale, &dummy_int, PU_FLOAT },
    { (char *)"ScaleY", &y_scale, &dummy_int, PU_FLOAT },
    { (char *)"WinWidth", &win_width, &dummy_int, PU_INT },
    { (char *)"WinHeight", &win_height, &dummy_int, PU_INT },
    { (char *)"RefreshRate", &refresh_rate, &dummy_int, PU_INT },
    { (char *)"FrameDuration", &frame_duration, &dummy_int, PU_FLOAT },
    { (char *)"", nullptr, nullptr, PU_NULL }
};

extern "C" double StimStart = 0;
extern "C" unsigned int StimTime = 0;
extern "C" unsigned int StimTicks = 0;
extern "C" unsigned int StimVRetraceCount = 0;
extern "C" unsigned int StimDeltaTime = 0;
extern "C" double StimTimeF = 0;
extern "C" double StimTicksF = 0;
extern "C" double StimDeltaTimeF = 0;

static Tcl_Interp *our_interp = nullptr;

extern "C" void stim_bind_interp(Tcl_Interp *interp) { our_interp = interp; }

extern "C" void stim_set_eye(int eye) { current_eye = eye; }
extern "C" int getCurrentEye(void) { return current_eye; }

extern "C" char *sendTclCommand(char *command) {
    if (!our_interp || !command) return nullptr;
    if (Tcl_Eval(our_interp, command) == TCL_ERROR) {
        std::fprintf(stderr, "%s:\n%s\n", command, Tcl_GetStringResult(our_interp));
    }
    return nullptr;
}

extern "C" void executeScripts(char **scripts, int *actives, int n) {
    for (int i = 0; i < n; i++) {
        if (actives[i] && scripts[i]) sendTclCommand(scripts[i]);
    }
}

extern "C" void executeObjFrameScripts(GR_OBJ *o, int phase) {
    if (!o) return;
    switch (phase) {
    case STIM_PRE_SCRIPT:
        animateUpdateObj(o, StimTicksF, StimDeltaTimeF);
        executeScripts(GR_PRE_SCRIPTS(o), GR_PRE_SCRIPT_ACTIVES(o), GR_N_PRE_SCRIPTS(o));
        break;
    case STIM_POST_SCRIPT:
        executeScripts(GR_POST_SCRIPTS(o), GR_POST_SCRIPT_ACTIVES(o), GR_N_POST_SCRIPTS(o));
        break;
    case STIM_POSTFRAME_SCRIPT:
        executeScripts(GR_POSTFRAME_SCRIPTS(o), GR_POSTFRAME_SCRIPT_ACTIVES(o),
                       GR_N_POSTFRAME_SCRIPTS(o));
        break;
    case STIM_THISFRAME_SCRIPT: {
        char *once[MAXSCRIPTS];
        int n = 0;
        for (int j = 0; j < GR_N_THISFRAME_SCRIPTS(o); j++) once[n++] = GR_THISFRAME_SCRIPT(o, j);
        GR_N_THISFRAME_SCRIPTS(o) = 0;
        for (int j = 0; j < n; j++) {
            sendTclCommand(once[j]);
            std::free(once[j]);
        }
        break;
    }
    }
    if (GR_FRAMESCRIPTFUNCP(o)) GR_FRAMESCRIPTFUNC(o)(o, phase);
}

static void execute_group(OBJ_GROUP *g, int phase) {
    if (!g) return;
    for (int i = 0; i < OG_NOBJS(g); i++) {
        GR_OBJ *o = OL_OBJ(OBJList, OG_OBJID(g, i));
        if (o) executeObjFrameScripts(o, phase);
    }
}

extern "C" void resetStimTime(void) {
    StimStart = wall_ms / 1000.0;
    StimTime = 0;
    StimTimeF = 0.0;
}

static void advance_clock(void) {
    double prev_ticks = StimTicksF;
    unsigned int prev_ticks_i = StimTicks;
    wall_ms += (double)frame_duration;
    StimTicksF = wall_ms;
    StimDeltaTimeF = StimTicksF - prev_ticks;
    StimTicks = (unsigned int)StimTicksF;
    StimDeltaTime = StimTicks - prev_ticks_i;
    StimVRetraceCount++;
    if (!stim_playback) {
        StimTimeF += (double)frame_duration;
        StimTime = (unsigned int)StimTimeF;
    }
}

extern "C" int startAnimation(void) {
    int old = OBJList ? OL_DYNAMIC(OBJList) : 0;
    if (OBJList) OL_DYNAMIC(OBJList) = 1;
    wants_frame = 1;
    return old;
}

extern "C" int stopAnimation(void) {
    int old = OBJList ? OL_DYNAMIC(OBJList) : 0;
    if (OBJList) OL_DYNAMIC(OBJList) = OL_DYNAMIC_STORED(OBJList) = 0;
    return old;
}

extern "C" int toggleAnimation(void) {
    if (OBJList && OL_DYNAMIC(OBJList)) stopAnimation();
    else {
        startAnimation();
        if (OBJList) OL_DYNAMIC_STORED(OBJList) = 1;
    }
    return OBJList ? !OL_DYNAMIC(OBJList) : 1;
}

extern "C" int kickAnimation(void) {
    int previous = wants_frame;
    wants_frame = 1;
    return previous;
}

extern "C" void redraw(void) { wants_frame = 1; }

extern "C" int setWakeUp(int ms) {
    NextFrameTime = (int)StimTime + ms;
    return NextFrameTime;
}

extern "C" int getStimTime(void) { return (int)StimTime; }
extern "C" double getStimTimeF(void) { return StimTimeF; }
extern "C" double getStimTicksF(void) { return StimTicksF; }
extern "C" double getFrameDuration(void) { return frame_duration; }

extern "C" int getScreenInfo(int *sw, int *sh, int *ww, int *wh, float *freq) {
    if (sw) *sw = screen_width;
    if (sh) *sh = screen_height;
    if (ww) *ww = win_width;
    if (wh) *wh = win_height;
    if (freq) *freq = (float)refresh_rate;
    return 1;
}

extern "C" void stim_screen_params(float halfx, float halfy, int width, int height, float refresh) {
    if (halfx > 0.f) half_screen_deg_x = halfx;
    if (halfy > 0.f) half_screen_deg_y = halfy;
    if (width > 0) {
        screen_width = width;
        win_width = width;
    }
    if (height > 0) {
        screen_height = height;
        win_height = height;
    }
    if (refresh > 0.f) {
        refresh_rate = (int)(refresh + 0.5f);
        if (refresh_rate < 1) refresh_rate = 1;
        frame_duration = 1000.f / (float)refresh_rate;
    }
    if (half_screen_deg_x > 0.f) pix_per_deg_x = (float)(screen_width / 2) / half_screen_deg_x;
    if (half_screen_deg_y > 0.f) pix_per_deg_y = (float)(screen_height / 2) / half_screen_deg_y;
    (void)half_screen_deg_z;
    swapsyncInit(nullptr, refresh_rate, 0);
}

/* stim2's 1 ms timer. It does not own the clock: the page advances
   StimTime one frame at a time. The timer fires wakeup deadlines. */
static Timer *app_timer = nullptr;
static int timer_ticks = 0;

static void timer_tick() {
    timer_ticks++;
    if (!stim_playback && NextFrameTime >= 0 && (int)StimTime >= NextFrameTime) {
        NextFrameTime = -1;
        kickAnimation();
    }
}

extern "C" void stim_timer_start(void) {
    if (app_timer) return;
    app_timer = new Timer();
    app_timer->create(0, 1, timer_tick);
}

extern "C" int stim_timer_ticks(void) { return timer_ticks; }

static int read_pixels(int x, int y, int w, int h, unsigned char *dst, int flip) {
    if (w <= 0 || h <= 0) return -1;
    size_t rowbytes = (size_t)4 * (size_t)w;
    size_t nbytes = rowbytes * (size_t)h;
    unsigned char *scratch = (unsigned char *)std::malloc(nbytes);
    if (!scratch) return -1;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, scratch);
    if (flip) {
        for (int i = 0; i < h; i++) {
            std::memcpy(dst + (size_t)i * rowbytes,
                        scratch + (size_t)(h - 1 - i) * rowbytes, rowbytes);
        }
    } else {
        std::memcpy(dst, scratch, nbytes);
    }
    std::free(scratch);
    return 0;
}

/* dumpRaw: 20-byte raw header, then rows top-down, matching dumpWindowAsRaw. */
extern "C" int stim_dump_raw(void) {
    int x = DumpInfo.x;
    int y = DumpInfo.y;
    int w = DumpInfo.w ? DumpInfo.w : win_width;
    int h = DumpInfo.h ? DumpInfo.h : win_height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w <= 0 || h <= 0 || x >= win_width || y >= win_height) return 0;
    if (x + w > win_width) w = win_width - x;
    if (y + h > win_height) h = win_height - y;
    unsigned char *pixels = (unsigned char *)std::calloc((size_t)w * (size_t)h, 4);
    if (!pixels) return 0;
    FILE *fp = std::fopen(DumpInfo.filename, "wb");
    if (!fp) {
        std::free(pixels);
        return 0;
    }
    glFinish();
    if (read_pixels(x, y, w, h, pixels, 0) != 0) {
        std::fclose(fp);
        std::free(pixels);
        return 0;
    }
    int ok = raw_writeHeader(w, h, 4, fp);
    size_t rowbytes = (size_t)4 * (size_t)w;
    for (int i = 0; ok && i < h; i++) {
        unsigned char *row = pixels + (size_t)(h - 1 - i) * rowbytes;
        if (std::fwrite(row, 4, (size_t)w, fp) != (size_t)w) ok = 0;
    }
    std::fclose(fp);
    std::free(pixels);
    return ok;
}

/* Playback capture: the frame just drawn, no header, nrep copies.
   Refuses unless stimPlayback is on, same as stim2. */
extern "C" int stimCaptureFrame(const char *filename, int nrep) {
    if (!stim_playback || !filename || nrep < 0) return -1;
    if (nrep == 0) return 0;
    if (win_width <= 0 || win_height <= 0) return -1;
    size_t nbytes = (size_t)4 * (size_t)win_width * (size_t)win_height;
    unsigned char *pix = (unsigned char *)std::malloc(nbytes);
    if (!pix) return -1;
    glFinish();
    int rc = read_pixels(0, 0, win_width, win_height, pix, 1);
    if (rc == 0) {
        FILE *fp = std::fopen(filename, "wb");
        if (!fp) rc = -1;
        else {
            for (int r = 0; r < nrep; r++) {
                if (std::fwrite(pix, 1, nbytes, fp) != nbytes) rc = -1;
            }
            if (std::fclose(fp) != 0) rc = -1;
        }
    }
    std::free(pix);
    return rc;
}

/* afterFrames, frameGrab, and swapTag. stim2.cpp runs these on the
   swap that just completed. A grab is read after the draw and before
   the swap, then delivered with the afterFrames scripts. */
struct FrameAfter {
    int at;
    std::string script;
};
struct FrameGrab {
    std::string script;
    int x, y, w, h;
    int keep_alpha;
    bool captured;
    int gw, gh;
    std::vector<unsigned char> pixels;
};
static std::vector<FrameAfter> frame_afters;
static std::vector<FrameGrab> frame_grabs;
static std::vector<std::string> swap_tags;
static std::string swap_tag_callback;

static int frame_work_pending(void) {
    return !frame_grabs.empty() || !frame_afters.empty();
}

static void capture_grabs(void) {
    for (auto &g : frame_grabs) {
        if (g.captured) continue;
        int w = g.w > 0 ? g.w : win_width;
        int h = g.h > 0 ? g.h : win_height;
        int x = g.x, y = g.y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > win_width) w = win_width - x;
        if (y + h > win_height) h = win_height - y;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        g.pixels.assign((size_t)w * (size_t)h * 4, 0);
        if (w && h) {
            int gy = win_height - y - h;
            if (read_pixels(x, gy, w, h, g.pixels.data(), 1) != 0)
                g.pixels.assign((size_t)w * (size_t)h * 4, 0);
        }
        if (!g.keep_alpha)
            for (size_t i = 3; i < g.pixels.size(); i += 4) g.pixels[i] = 255;
        g.gw = w;
        g.gh = h;
        g.captured = true;
    }
}

static void deliver_swap_tags(void) {
    if (swap_tags.empty() || !our_interp) return;
    std::vector<std::string> tags = std::move(swap_tags);
    swap_tags.clear();
    double flip = swapsyncLastFlipTime();
    double flip_ms = flip > 0.0 ? 1000.0 * (flip - StimStart) : -1.0;
    for (const auto &payload : tags) {
        Tcl_Obj *items[6];
        int n = 0;
        if (!swap_tag_callback.empty())
            items[n++] = Tcl_NewStringObj(swap_tag_callback.c_str(), -1);
        items[n++] = Tcl_NewStringObj(payload.c_str(), -1);
        items[n++] = Tcl_NewWideIntObj((Tcl_WideInt)swapsyncLastFlipWallUs());
        items[n++] = Tcl_NewWideIntObj((Tcl_WideInt)swapsyncLastCounter());
        items[n++] = Tcl_NewIntObj(SwapCount);
        items[n++] = Tcl_NewDoubleObj(flip_ms);
        Tcl_Obj *cmd = Tcl_NewListObj(n, items);
        Tcl_IncrRefCount(cmd);
        if (!swap_tag_callback.empty()) {
            if (Tcl_EvalObjEx(our_interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
                log_message("error", "swapTag callback: %s", Tcl_GetStringResult(our_interp));
                Tcl_ResetResult(our_interp);
            }
        } else {
            Tcl_SetVar2Ex(our_interp, "swapTagLast", nullptr, cmd, TCL_GLOBAL_ONLY);
        }
        Tcl_DecrRefCount(cmd);
    }
}

static void deliver_frame_work(void) {
    if (!our_interp) return;
    if (frame_grabs.empty() && frame_afters.empty()) return;

    std::vector<FrameGrab> grabs;
    std::vector<FrameGrab> waiting;
    for (auto &g : frame_grabs) (g.captured ? grabs : waiting).push_back(std::move(g));
    frame_grabs = std::move(waiting);

    for (auto &g : grabs) {
        Tcl_Obj *cmd = Tcl_NewStringObj(g.script.c_str(), -1);
        Tcl_IncrRefCount(cmd);
        Tcl_ListObjAppendElement(our_interp, cmd, Tcl_NewIntObj(g.gw));
        Tcl_ListObjAppendElement(our_interp, cmd, Tcl_NewIntObj(g.gh));
        Tcl_ListObjAppendElement(our_interp, cmd,
            Tcl_NewByteArrayObj(g.pixels.data(), (Tcl_Size)g.pixels.size()));
        Tcl_ListObjAppendElement(our_interp, cmd, Tcl_NewIntObj(SwapCount));
        if (Tcl_EvalObjEx(our_interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
            log_message("error", "frameGrab: %s", Tcl_GetStringResult(our_interp));
            Tcl_ResetResult(our_interp);
        }
        Tcl_DecrRefCount(cmd);
    }

    std::vector<FrameAfter> due;
    std::vector<FrameAfter> later;
    for (auto &a : frame_afters) (a.at <= SwapCount ? due : later).push_back(std::move(a));
    frame_afters = std::move(later);
    for (auto &a : due) {
        if (Tcl_EvalEx(our_interp, a.script.c_str(), -1, TCL_EVAL_GLOBAL) != TCL_OK) {
            log_message("error", "afterFrames: %s", Tcl_GetStringResult(our_interp));
            Tcl_ResetResult(our_interp);
        }
    }
    if (frame_work_pending()) wants_frame = 1;
}

extern "C" void swapTagAdd(const char *payload) {
    swap_tags.push_back(payload ? payload : "");
}
extern "C" void swapTagCallbackSet(const char *proc) {
    swap_tag_callback = proc ? proc : "";
}
extern "C" const char *swapTagCallbackGet(void) {
    return swap_tag_callback.c_str();
}
extern "C" void frameAfterAdd(int n, const char *script) {
    frame_afters.push_back({SwapCount + n, script ? script : ""});
    wants_frame = 1;
}
extern "C" int frameAfterPending(void) {
    return (int)frame_afters.size();
}
extern "C" void frameGrabAdd(const char *script, int x, int y, int w, int h, int keep_alpha) {
    FrameGrab g;
    g.script = script ? script : "";
    g.x = x; g.y = y; g.w = w; g.h = h; g.keep_alpha = keep_alpha;
    g.captured = false;
    g.gw = 0; g.gh = 0;
    frame_grabs.push_back(std::move(g));
    wants_frame = 1;
}

/* doUpdate from stim2.cpp: a dynamic group steps to its next frame
   before the picture is drawn. */
static int do_update(OBJ_GROUP *g) {
    if (!g || !OBJList) return 0;
    if (OL_DYNAMIC(OBJList)) {
        if (OGL_VISIBLE(GList)) {
            int status = glistNextGroupFrame(GList, OGL_CURGROUP(GList));
            if (status == 0 && OG_REPEAT_MODE(g) != G_NORMAL) return 0;
        }
        if (OGL_VISIBLE(GList)) {
            for (int i = 0; i < OG_NOBJS(g); i++) {
                GR_OBJ *obj = OL_OBJ(OBJList, OG_OBJID(g, i));
                if (obj && GR_UPDATEFUNCP(obj)) GR_UPDATEFUNC(obj)(obj);
            }
        }
        return 1;
    }
    if (OG_DYNAMIC(g) == DYNAMIC_TIME_BASED) {
        int status = glistNextTimeFrame(g, (int)StimTime);
        if (!status) return 1;
        NextFrameTime = status;
    } else if (OG_DYNAMIC(g) == DYNAMIC_WAKEUP_BASED) {
        glistNextGroupFrame(GList, OGL_CURGROUP(GList));
        return 1;
    } else if (OG_DYNAMIC(g) == DYNAMIC_ALWAYS_UPDATE) {
        return 1;
    } else {
        NextFrameTime = -1;
    }
    return 0;
}

static void finish_visible(OBJ_GROUP *g, int swapped) {
    if (OGL_NEWLY_VISIBLE(GList)) {
        OGL_NEWLY_VISIBLE(GList) = 0;
        if (OG_START(g) == -1) {
            if (NextFrameTime != -1) NextFrameTime += (int)StimTime;
            resetStimTime();
            OG_START(g) = 0;
        }
    }
    advance_clock();
    if (swapped) {
        deliver_swap_tags();
        deliver_frame_work();
    }
    execute_group(g, STIM_POSTFRAME_SCRIPT);
    execute_group(g, STIM_THISFRAME_SCRIPT);
    glistPostFrameCmd(g);
    if (OL_DYNAMIC(OBJList) || OG_DYNAMIC(g) == DYNAMIC_TIME_BASED ||
        OG_DYNAMIC(g) == DYNAMIC_WAKEUP_BASED || OG_DYNAMIC(g) == DYNAMIC_ALWAYS_UPDATE) {
        wants_frame = 1;
    }
}

extern "C" int stim_frame(void) {
    wants_frame = 0;
    if (!GList || !OBJList || OGL_CURGROUP(GList) >= OGL_NGROUPS(GList) || !OGL_NGROUPS(GList)) {
        return stim_redraw();
    }
    OBJ_GROUP *g = OGL_GROUP(GList, OGL_CURGROUP(GList));
    if (!g) return stim_redraw();

    int again = do_update(g);
    if (again) wants_frame = 1;

    if (!OGL_VISIBLE(GList)) {
        stopAnimation();
        capture_grabs();
        SwapCount++;
        deliver_swap_tags();
        deliver_frame_work();
        return 0;
    }

    switch (OG_SWAPMODE(g)) {
    case SWAP_ONLY:
        swapsyncBeforeSwap();
        swapsyncAfterSwap();
        advance_clock();
        SwapCount++;
        deliver_swap_tags();
        deliver_frame_work();
        execute_group(g, STIM_POSTFRAME_SCRIPT);
        execute_group(g, STIM_THISFRAME_SCRIPT);
        glistPostFrameCmd(g);
        return 0;
    case NO_SWAP:
        if (OGL_NEWLY_VISIBLE(GList) && OG_START(g) == -1) resetStimTime();
        execute_group(g, STIM_PRE_SCRIPT);
        stim_redraw();
        execute_group(g, STIM_POST_SCRIPT);
        finish_visible(g, 0);
        break;
    case SWAP_NORMAL:
    default:
        if (OGL_NEWLY_VISIBLE(GList) && OG_START(g) == -1) resetStimTime();
        execute_group(g, STIM_PRE_SCRIPT);
        stim_redraw();
        execute_group(g, STIM_POST_SCRIPT);
        capture_grabs();
        swapsyncBeforeSwap();
        swapsyncAfterSwap();
        SwapCount++;
        finish_visible(g, 1);
        break;
    }
    return stim_ndraws();
}

extern "C" int stim_wants_frame(void) { return wants_frame; }

/* The live display runs the frame pass on every refresh while the
   current group is showing and marked dynamic (glistSetDynamic status
   other than NOT_DYNAMIC). The page uses the same mark to decide
   whether a quiet stretch of the tape is one picture. */
extern "C" int stim_scene_moves(void) {
    if (!GList || !OGL_NGROUPS(GList) || !OGL_VISIBLE(GList)) return 0;
    if (OGL_CURGROUP(GList) >= OGL_NGROUPS(GList)) return 0;
    OBJ_GROUP *g = OGL_GROUP(GList, OGL_CURGROUP(GList));
    if (!g) return 0;
    return OG_DYNAMIC(g) != NOT_DYNAMIC;
}

static int stim_playback_cmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc == 1) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(stim_playback));
        return TCL_OK;
    }
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?boolean?");
        return TCL_ERROR;
    }
    int v = 0;
    if (Tcl_GetBooleanFromObj(interp, objv[1], &v) != TCL_OK) return TCL_ERROR;
    stim_playback = v ? 1 : 0;
    return TCL_OK;
}

static int stim_time_set_cmd(ClientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "ms");
        return TCL_ERROR;
    }
    double ms = 0;
    if (Tcl_GetDoubleFromObj(interp, objv[1], &ms) != TCL_OK) return TCL_ERROR;
    StimTimeF = ms;
    StimTime = (unsigned int)ms;
    return TCL_OK;
}

extern "C" void stim_register_frame_commands(Tcl_Interp *interp) {
    Tcl_CreateObjCommand(interp, "stimPlayback", stim_playback_cmd, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "stimTimeSet", stim_time_set_cmd, nullptr, nullptr);
}
