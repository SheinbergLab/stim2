/*
 * swapsync.h
 *  Frame-presentation synchronization: block after a buffer swap until the
 *  frame is actually on glass (where the platform can tell us), and record
 *  when that happened.
 *
 *  Strategies, chosen once at startup:
 *    oml           X11/GLX: glXWaitForSbcOML blocks until the flip completes
 *                  and returns its hardware timestamp (UST) + vblank counter.
 *    presentation  Wayland: wp_presentation feedback delivers the compositor's
 *                  flip timestamp, refresh period, and vsync/zero-copy flags.
 *    glfinish      Everything else: glFinish() after the swap.  On some stacks
 *                  (NVIDIA/X11, current Mesa/X11) this happens to block until
 *                  the flip; on Wayland it only proves the GPU finished, so an
 *                  ack under this strategy can lead the photons by a frame.
 */

#ifndef SWAPSYNC_H
#define SWAPSYNC_H

typedef struct GLFWwindow GLFWwindow;

#ifdef __cplusplus
extern "C" {
#endif

/* Call once, after the GL context is current and the video mode is known. */
void swapsyncInit(GLFWwindow *window, int refresh_rate_hz, int verbose);

/* Call immediately BEFORE glfwSwapBuffers() (requests Wayland feedback for
   the commit the swap is about to make; no-op for other strategies). */
void swapsyncBeforeSwap(void);

/* Call immediately AFTER glfwSwapBuffers(); blocks per the active strategy. */
void swapsyncAfterSwap(void);

const char *swapsyncStrategyName(void);

/* Timestamp of the last confirmed flip in the glfwGetTime() domain (seconds);
   0 if the strategy has no flip timestamps (glfinish) or none seen yet. */
double swapsyncLastFlipTime(void);

/* The same flip on the wall clock (CLOCK_REALTIME microseconds since the
   epoch), for placing frames on a chrony/PTP-disciplined lab timeline.  The
   monotonic-to-wall offset is sampled as each flip is recorded, so NTP slews
   don't accumulate.  -1 if no flip timestamp is available. */
long long swapsyncLastFlipWallUs(void);

/* Refresh period reported with the last flip (ms); 0 if unknown. */
double swapsyncLastRefreshMs(void);

/* Hardware frame counter at the last flip (X11 MSC / Wayland seq); -1 n/a. */
long long swapsyncLastCounter(void);

/* Frames the display advanced past us between back-to-back swaps. */
long swapsyncMissedFrames(void);

/* Wayland only: commits the compositor never showed / feedback waits that
   timed out.  Nonzero values mean acks are not tracking scanout. */
long swapsyncDiscardedFrames(void);
long swapsyncTimeouts(void);

#ifdef __cplusplus
}
#endif

#endif /* SWAPSYNC_H */
