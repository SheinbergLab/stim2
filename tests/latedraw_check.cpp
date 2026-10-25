/*
 * latedraw_check.cpp -- unit check of the draw-late scheduler arithmetic
 * (src/latedraw.cpp).  Needs no display, no Tcl, no GL:
 *
 *     cmake --build build --target latedraw_check && ./build/latedraw_check
 *     ctest --test-dir build -R latedraw
 *
 * Prints OK/FAIL lines and a summary; exits non-zero on any failure.
 */
#include "latedraw.h"

#include <cmath>
#include <cstdio>

static int passes = 0, fails = 0;

static void check(const char *name, bool cond, double got = NAN,
                  double want = NAN)
{
  if (cond) {
    passes++;
    printf("OK   %s\n", name);
  }
  else {
    fails++;
    if (std::isnan(got)) printf("FAIL %s\n", name);
    else printf("FAIL %s  got %.6f want %.6f\n", name, got, want);
  }
}

static bool near(double a, double b, double tol = 1e-9)
{
  return std::fabs(a - b) <= tol;
}

int main(void)
{
  const double P = 1000.0 / 120.0;            /* 8.333 ms, a 120 Hz rig */

  /* ---- target ---------------------------------------------------------- */
  {
    double t = latedrawTarget(10.0, P, 3.0);
    check("target = lastFlip + period - budget", near(t, 10.0 + (P - 3.0) / 1000.0),
          t, 10.0 + (P - 3.0) / 1000.0);
    t = latedrawTarget(10.0, P, P);
    check("budget == period -> target is the flip itself (no deferral)",
          near(t, 10.0), t, 10.0);
    t = latedrawTarget(10.0, P, 2 * P);
    check("budget > period -> target before the flip (no deferral)", t < 10.0);
  }

  /* ---- wait rounding --------------------------------------------------- */
  {
    check("5.9 ms remaining -> wait 5", latedrawWaitMs(0.0, 0.0059) == 5,
          latedrawWaitMs(0.0, 0.0059), 5);
    check("1.0 ms remaining -> wait 1", latedrawWaitMs(0.0, 0.0010) == 1,
          latedrawWaitMs(0.0, 0.0010), 1);
    check("0.9 ms remaining -> no wait", latedrawWaitMs(0.0, 0.0009) == 0);
    check("past the target -> no wait", latedrawWaitMs(1.0, 0.5) == 0);
  }

  /* ---- raise ----------------------------------------------------------- */
  {
    /* effective 3, draw 1: 1.5*3 = 4.5 beats 2*1 = 2 */
    double v = latedrawRaise(3.0, 1.0, 3.0, P);
    check("late frame raises to effective*1.5 when that dominates",
          near(v, 4.5), v, 4.5);
    /* effective 3, draw 4: 2*4 = 8 beats 4.5, but cap is P-1 = 7.333 */
    v = latedrawRaise(3.0, 4.0, 3.0, P);
    check("late frame raises to draw*2 when that dominates, capped at period-1",
          near(v, P - 1.0), v, P - 1.0);
    v = latedrawRaise(3.0, 2.5, 3.0, P);
    check("draw*2 = 5 > effective*1.5 = 4.5 -> 5", near(v, 5.0), v, 5.0);
    /* never above the cap even from a huge effective */
    v = latedrawRaise(100.0, 100.0, 3.0, P);
    check("raise never exceeds period - 1 ms", near(v, P - 1.0), v, P - 1.0);
    /* never below the floor */
    v = latedrawRaise(0.1, 0.05, 3.0, P);
    check("raise never goes below the floor", near(v, 3.0), v, 3.0);
    /* floor above the cap: cap wins */
    v = latedrawRaise(20.0, 1.0, 20.0, P);
    check("floor above period-1: result is the cap", near(v, P - 1.0), v, P - 1.0);
  }

  /* ---- lower ----------------------------------------------------------- */
  {
    double v = latedrawLower(4.5, 3.0);
    check("decay step lowers by 0.25 ms", near(v, 4.25), v, 4.25);
    v = latedrawLower(3.1, 3.0);
    check("decay never goes below the floor", near(v, 3.0), v, 3.0);
    v = latedrawLower(3.0, 3.0);
    check("decay at the floor stays at the floor", near(v, 3.0), v, 3.0);
  }

  /* ---- late detection -------------------------------------------------- */
  {
    check("missed counter advanced -> late", latedrawIsLate(5, 6, P, P) == 1);
    check("interval 1.4 periods, no miss -> not late",
          latedrawIsLate(5, 5, 1.4 * P, P) == 0);
    check("interval 1.6 periods -> late", latedrawIsLate(5, 5, 1.6 * P, P) == 1);
    check("interval 2 periods -> late", latedrawIsLate(5, 5, 2.0 * P, P) == 1);
    check("no new flip (interval 0) and no miss -> not late",
          latedrawIsLate(5, 5, 0.0, P) == 0);
    check("unknown period: only the missed counter decides",
          latedrawIsLate(5, 5, 100.0, 0.0) == 0);
  }

  /* ---- state machine: init, frames, decay, ring ------------------------ */
  {
    latedraw_state_t s;
    latedrawInit(&s, 3.0);
    check("init: effective == floor", near(s.effective_ms, 3.0));
    check("init: enabled left 0 for the caller", s.enabled == 0);
    check("init: no draws, no late", s.draws == 0 && s.late == 0);
    check("init: no period from an empty flip ring",
          near(latedrawPeriodMs(&s, 0.0), 0.0));
    check("refresh period wins when known",
          near(latedrawPeriodMs(&s, P), P));

    /* a late frame with draw 1 ms: 3 -> 4.5 */
    int late = latedrawOnFrame(&s, 1.0, 1, P);
    check("late frame counted", late == 1 && s.late == 1 && s.draws == 1);
    check("late frame raised effective to 4.5", near(s.effective_ms, 4.5),
          s.effective_ms, 4.5);
    check("late frame reset the clean run", s.clean_run == 0);

    /* 599 clean frames: no change yet */
    for (int i = 0; i < LATEDRAW_CLEAN_RUN - 1; i++) latedrawOnFrame(&s, 1.0, 0, P);
    check("599 clean frames: effective unchanged", near(s.effective_ms, 4.5),
          s.effective_ms, 4.5);
    /* the 600th: -0.25 */
    latedrawOnFrame(&s, 1.0, 0, P);
    check("600th clean frame lowers by 0.25 ms", near(s.effective_ms, 4.25),
          s.effective_ms, 4.25);
    check("clean run restarts after a decay step", s.clean_run == 0);
    /* 600 more x 6 -> would be 2.75 but floors at 3.0 */
    for (int k = 0; k < 6; k++)
      for (int i = 0; i < LATEDRAW_CLEAN_RUN; i++) latedrawOnFrame(&s, 1.0, 0, P);
    check("repeated decay never goes below the floor", near(s.effective_ms, 3.0),
          s.effective_ms, 3.0);

    /* a late frame mid-run resets the clean run */
    for (int i = 0; i < 100; i++) latedrawOnFrame(&s, 1.0, 0, P);
    latedrawOnFrame(&s, 1.0, 1, P);
    check("late frame mid-run resets the clean run", s.clean_run == 0);
    check("late count is 2", s.late == 2);

    /* repeated late frames climb geometrically but stop at the cap */
    for (int i = 0; i < 20; i++) latedrawOnFrame(&s, 1.0, 1, P);
    check("many late frames saturate at period - 1 ms",
          near(s.effective_ms, P - 1.0), s.effective_ms, P - 1.0);

    /* draws counter and ring statistics */
    latedraw_state_t r;
    latedrawInit(&r, 2.0);
    for (int i = 1; i <= 5; i++) latedrawOnFrame(&r, (double) i, 0, P);
    check("draw median over 1..5 is 3", near(latedrawDrawMedianMs(&r), 3.0),
          latedrawDrawMedianMs(&r), 3.0);
    check("draw max over 1..5 is 5", near(latedrawDrawMaxMs(&r), 5.0));
    check("draws counts every frame", r.draws == 5);
    /* overflow the ring: 300 frames of 0.5 then the ring holds only 0.5 */
    for (int i = 0; i < 300; i++) latedrawOnFrame(&r, 0.5, 0, P);
    check("ring keeps the last 256 only (median)", near(latedrawDrawMedianMs(&r), 0.5),
          latedrawDrawMedianMs(&r), 0.5);
    check("ring keeps the last 256 only (max)", near(latedrawDrawMaxMs(&r), 0.5),
          latedrawDrawMaxMs(&r), 0.5);
    check("draws keeps counting past the ring", r.draws == 305);

    /* period fallback: median of flip intervals */
    latedraw_state_t f;
    latedrawInit(&f, 3.0);
    latedrawNoteFlipInterval(&f, P);
    latedrawNoteFlipInterval(&f, P);
    latedrawNoteFlipInterval(&f, P);
    check("fewer than 4 intervals: period still unknown",
          near(latedrawPeriodMs(&f, 0.0), 0.0));
    latedrawNoteFlipInterval(&f, 2 * P);        /* one missed frame */
    latedrawNoteFlipInterval(&f, P);
    check("median of intervals ignores the odd double interval",
          near(latedrawPeriodMs(&f, 0.0), P), latedrawPeriodMs(&f, 0.0), P);
    latedrawNoteFlipInterval(&f, 0.0);
    latedrawNoteFlipInterval(&f, -1.0);
    check("non-positive intervals are ignored", f.flip_n == 5);
    for (int i = 0; i < 40; i++) latedrawNoteFlipInterval(&f, 16.7);
    check("flip ring keeps the last 16", near(latedrawPeriodMs(&f, 0.0), 16.7),
          latedrawPeriodMs(&f, 0.0), 16.7);
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
