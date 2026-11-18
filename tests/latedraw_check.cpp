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

  /* ---- warm-up detection ----------------------------------------------- */
  {
    check("nothing governed yet (-1) -> warm-up", latedrawIsWarmup(100, -1) == 1);
    check("swap count where the governed frame left it -> scheduled",
          latedrawIsWarmup(100, 100) == 0);
    check("swap count moved since (foreign swap) -> warm-up",
          latedrawIsWarmup(101, 100) == 1);
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

    /* nothing measured yet: the budget is the cap, whatever the floor */
    latedrawOnFrame(&s, 1.0, 0.0, 0, 0, P);
    check("first frames: budget at the cap", near(s.effective_ms, P - 1.0), s.effective_ms, P - 1.0);
    int l0 = latedrawOnFrame(&s, 1.0, 0.0, 1, 0, P);
    check("a late frame before the scene is known is counted", l0 == 1 && s.late == 1);
    check("... and teaches nothing", near(s.margin_ms, LATEDRAW_MARGIN_MS) && s.late_excess == 0);
    for (int i = 2; i < LATEDRAW_MIN_SAMPLES; i++) latedrawOnFrame(&s, 1.0, 0.0, 0, 0, P);
    /* 32 frames of 1 ms work in: work 1.0 + margin 1.0 = 2.0, under the 3 ms floor */
    check("cheap frames: the floor holds", near(s.effective_ms, 3.0), s.effective_ms, 3.0);
    check("work is the ring's high quantile", near(latedrawWorkMs(&s), 1.0), latedrawWorkMs(&s), 1.0);

    /* the rig: draw 0.6 + gpu 2.6, a few frames to 3.6: budget 4.6, and it stays */
    latedraw_state_t g;
    latedrawInit(&g, 3.0);
    for (int i = 0; i < 300; i++)
      latedrawOnFrameSlack(&g, 0.6, (i % 40 == 0) ? 3.0 : 2.6, 0, 0, P, 1.2);
    check("budget = high-quantile work + margin (3.6 + 1.0)", near(g.effective_ms, 4.6), g.effective_ms, 4.6);
    double held = g.effective_ms;
    for (int i = 0; i < 3000; i++)
      latedrawOnFrameSlack(&g, 0.6, (i % 40 == 0) ? 3.0 : 2.6, 0, 0, P, 1.2);
    check("... and holds there: no decay toward a floor it cannot meet",
          near(g.effective_ms, held), g.effective_ms, held);
    check("gpu ring records the tail", near(latedrawGpuMaxMs(&g), 3.0), latedrawGpuMaxMs(&g), 3.0);

    /* one stalled frame (gpu 5 ms): late, counted, and nothing moves */
    int ls = latedrawOnFrameSlack(&g, 0.9, 5.0, 1, 0, P, -0.1);
    check("a stall is late and counted as over plan", ls == 1 && g.late == 1 && g.late_excess == 1);
    check("a single stall leaves the budget alone", near(g.effective_ms, held), g.effective_ms, held);
    check("... and the margin", near(g.margin_ms, LATEDRAW_MARGIN_MS));
    for (int i = 0; i < 300; i++) latedrawOnFrameSlack(&g, 0.6, 2.6, 0, 0, P, 1.2);
    check("stalls minutes apart never add up", near(g.effective_ms, held, 0.3), g.effective_ms, held);

    /* a heavier scene (the blur comes on): two late frames, then it fits */
    latedrawOnFrameSlack(&g, 0.6, 4.4, 1, 0, P, -0.3);
    latedrawOnFrameSlack(&g, 0.6, 4.4, 1, 0, P, -0.3);
    check("second over-plan frame in a row lifts the plan at once (5.0 + 1.0)",
          near(g.effective_ms, 6.0), g.effective_ms, 6.0);
    check("... without touching the margin", near(g.margin_ms, LATEDRAW_MARGIN_MS));
    for (int i = 0; i < 20; i++) latedrawOnFrameSlack(&g, 0.6, 4.4, 0, 0, P, 1.0);
    check("and the quantile carries it from there", near(g.effective_ms, 6.0), g.effective_ms, 6.0);

    /* back to the lighter scene: the plan comes down slowly, 0.25 ms per 600 frames */
    for (int i = 0; i < 256; i++) latedrawOnFrameSlack(&g, 0.6, 2.6, 0, 0, P, 2.0);
    check("while the heavy frames are still in the ring, nothing is given back",
          near(g.effective_ms, 6.0, 0.01), g.effective_ms, 6.0);
    for (int i = 0; i < 600; i++) latedrawOnFrameSlack(&g, 0.6, 2.6, 0, 0, P, 2.0);
    check("then 600 lighter frames give back 0.25 ms", near(g.effective_ms, 5.75, 0.02), g.effective_ms, 5.75);
    for (int i = 0; i < 6000; i++) latedrawOnFrameSlack(&g, 0.6, 2.6, 0, 0, P, 2.0);
    check("... and it settles on the lighter scene's work, not below",
          near(g.effective_ms, 4.2), g.effective_ms, 4.2);

    /* work as planned and late anyway, with little slack: the margin was short */
    latedraw_state_t m;
    latedrawInit(&m, 3.0);
    for (int i = 0; i < 300; i++) latedrawOnFrameSlack(&m, 0.6, 2.6, 0, 0, P, 1.0);
    check("planned 3.2 + 1.0", near(m.effective_ms, 4.2), m.effective_ms, 4.2);
    latedrawOnFrameSlack(&m, 0.6, 2.7, 1, 0, P, 0.4);
    check("late within plan: margin grows by 0.25", near(m.margin_ms, 1.25), m.margin_ms, 1.25);
    check("... counted in late, not as over plan", m.late == 1 && m.late_excess == 0);
    for (int i = 0; i < 6000; i++) latedrawOnFrameSlack(&m, 0.6, 2.6, 0, 0, P, 1.2);
    check("the margin does not come back down", near(m.margin_ms, 1.25), m.margin_ms, 1.25);
    for (int i = 0; i < 40; i++) latedrawOnFrameSlack(&m, 0.6, 2.6, 1, 0, P, 0.1);
    check("a machine that keeps missing ends at the cap", near(m.effective_ms, P - 1.0), m.effective_ms, P - 1.0);
    latedrawOnFrame(&m, 0.6, -1.0, 0, 0, P);
    check("negative gpu tail (unmeasured) is recorded as 0", latedrawGpuMaxMs(&m) < 3.0);

    /* ready in time and late all the same: counted apart, nothing learned */
    latedraw_state_t b;
    latedrawInit(&b, 3.0);
    for (int i = 0; i < 300; i++) latedrawOnFrameSlack(&b, 0.6, 2.6, 0, 0, P, 3.0);
    double be = b.effective_ms;
    int bl = latedrawOnFrameSlack(&b, 0.6, 2.6, 1, 0, P, 3.7);
    check("late with 3.7 ms of slack reports late", bl == 1);
    check("... is counted in late_blameless, not late", b.late_blameless == 1 && b.late == 0);
    check("... moves neither budget nor margin", near(b.effective_ms, be) && near(b.margin_ms, LATEDRAW_MARGIN_MS));
    latedrawOnFrameSlack(&b, 0.6, 2.6, 1, 0, P, LATEDRAW_BLAMELESS_SLACK_MS);
    check("slack exactly at the threshold is blameless", b.late_blameless == 2 && b.late == 0);
    latedrawOnFrameSlack(&b, 0.6, 2.6, 1, 0, P, -1.0);
    check("unknown slack is the budget's", b.late == 1 && near(b.margin_ms, 1.25));

    /* a floor the machine can meet is honoured; one above the cap is the cap */
    latedraw_state_t f5;
    latedrawInit(&f5, 5.0);
    for (int i = 0; i < 300; i++) latedrawOnFrameSlack(&f5, 0.6, 2.6, 0, 0, P, 1.8);
    check("floor 5 ms over work 3.2 + 1.0: the floor", near(f5.effective_ms, 5.0), f5.effective_ms, 5.0);
    latedrawInit(&f5, 20.0);
    for (int i = 0; i < 300; i++) latedrawOnFrameSlack(&f5, 0.6, 2.6, 0, 0, P, 1.8);
    check("floor above the cap: the cap", near(f5.effective_ms, P - 1.0), f5.effective_ms, P - 1.0);
    check("unknown period: the floor", near(latedrawBudgetMs(&f5, 0.0), 20.0));

    /* warm-up frames: counted, recorded, never adapt */
    latedraw_state_t w;
    latedrawInit(&w, 3.0);
    for (int i = 0; i < 300; i++) latedrawOnFrameSlack(&w, 0.6, 2.6, 0, 0, P, 1.0);
    double we = w.effective_ms;
    int wl = latedrawOnFrameSlack(&w, 0.9, 6.0, 1, 1, P, -1.0);
    check("late warm-up frame reports late", wl == 1);
    check("late warm-up frame counted in warmups / warmup_late", w.warmups == 1 && w.warmup_late == 1);
    check("late warm-up frame not counted in late", w.late == 0 && w.late_excess == 0);
    check("late warm-up frame moves neither budget nor margin",
          near(w.effective_ms, we) && near(w.margin_ms, LATEDRAW_MARGIN_MS));
    check("warm-up frame still counts as a draw", w.draws == 301);
    check("warm-up gpu tail still lands in the ring", near(latedrawGpuMaxMs(&w), 6.0), latedrawGpuMaxMs(&w), 6.0);
    latedrawOnFrameSlack(&w, 0.9, 2.6, 0, 1, P, -1.0);
    check("clean warm-up frame counted in warmups only", w.warmups == 2 && w.warmup_late == 1);

    /* draws counter and ring statistics */
    latedraw_state_t r;
    latedrawInit(&r, 2.0);
    for (int i = 1; i <= 5; i++) latedrawOnFrame(&r, (double) i, 0.0, 0, 0, P);
    check("draw median over 1..5 is 3", near(latedrawDrawMedianMs(&r), 3.0),
          latedrawDrawMedianMs(&r), 3.0);
    check("draw max over 1..5 is 5", near(latedrawDrawMaxMs(&r), 5.0));
    check("draws counts every frame", r.draws == 5);
    /* overflow the ring: 300 frames of 0.5 then the ring holds only 0.5 */
    for (int i = 0; i < 300; i++) latedrawOnFrame(&r, 0.5, 0.0, 0, 0, P);
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
