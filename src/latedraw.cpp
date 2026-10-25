/*
 * latedraw.cpp -- pure scheduling/adaptation arithmetic for late-draw.
 * See latedraw.h.  No GL, no Tcl, no threads: render-thread plain data.
 */
#include "latedraw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void latedrawInit(latedraw_state_t *s, double budget_ms)
{
  memset(s, 0, sizeof(*s));
  if (budget_ms < 0.0) budget_ms = 0.0;
  s->floor_ms = budget_ms;
  s->effective_ms = budget_ms;
}

double latedrawTarget(double last_flip_s, double period_ms, double budget_ms)
{
  return last_flip_s + (period_ms - budget_ms) / 1000.0;
}

int latedrawWaitMs(double now_s, double target_s)
{
  double remain_ms = (target_s - now_s) * 1000.0;
  if (remain_ms < 1.0) return 0;
  return (int) std::floor(remain_ms);
}

static double cap_ms(double period_ms)
{
  double cap = period_ms - LATEDRAW_CAP_MARGIN_MS;
  return cap > 0.0 ? cap : 0.0;
}

double latedrawRaise(double effective_ms, double draw_ms, double floor_ms,
                     double period_ms)
{
  double v = std::max(effective_ms * LATEDRAW_RAISE_FACTOR,
                      draw_ms * LATEDRAW_DRAW_FACTOR);
  double cap = cap_ms(period_ms);
  double lo = std::min(floor_ms, cap);       /* floor above cap: cap wins */
  if (v > cap) v = cap;
  if (v < lo) v = lo;
  return v;
}

double latedrawLower(double effective_ms, double floor_ms)
{
  double v = effective_ms - LATEDRAW_DECAY_MS;
  return v < floor_ms ? floor_ms : v;
}

int latedrawIsLate(long missed_before, long missed_after,
                   double flip_interval_ms, double period_ms)
{
  if (missed_after > missed_before) return 1;
  if (period_ms > 0.0 && flip_interval_ms > LATEDRAW_LATE_INTERVAL * period_ms)
    return 1;
  return 0;
}

int latedrawOnFrame(latedraw_state_t *s, double draw_ms, int late,
                    double period_ms)
{
  s->draw_ring[s->draw_i] = draw_ms;
  s->draw_i = (s->draw_i + 1) % LATEDRAW_DRAW_RING;
  if (s->draw_n < LATEDRAW_DRAW_RING) s->draw_n++;
  s->draws++;

  if (late) {
    s->late++;
    s->clean_run = 0;
    s->effective_ms = latedrawRaise(s->effective_ms, draw_ms, s->floor_ms,
                                    period_ms);
    return 1;
  }

  if (++s->clean_run >= LATEDRAW_CLEAN_RUN) {
    s->clean_run = 0;
    s->effective_ms = latedrawLower(s->effective_ms, s->floor_ms);
  }
  return 0;
}

void latedrawNoteFlipInterval(latedraw_state_t *s, double interval_ms)
{
  if (interval_ms <= 0.0) return;
  s->flip_ring[s->flip_i] = interval_ms;
  s->flip_i = (s->flip_i + 1) % LATEDRAW_FLIP_RING;
  if (s->flip_n < LATEDRAW_FLIP_RING) s->flip_n++;
}

static double median_of(const double *ring, int n)
{
  if (n <= 0) return 0.0;
  double tmp[LATEDRAW_DRAW_RING];
  memcpy(tmp, ring, sizeof(double) * n);
  std::sort(tmp, tmp + n);
  return (n & 1) ? tmp[n / 2] : 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
}

double latedrawPeriodMs(const latedraw_state_t *s, double refresh_ms)
{
  if (refresh_ms > 0.0) return refresh_ms;
  if (s->flip_n < 4) return 0.0;
  return median_of(s->flip_ring, s->flip_n);
}

double latedrawDrawMedianMs(const latedraw_state_t *s)
{
  return median_of(s->draw_ring, s->draw_n);
}

double latedrawDrawMaxMs(const latedraw_state_t *s)
{
  double m = 0.0;
  for (int i = 0; i < s->draw_n; i++)
    if (s->draw_ring[i] > m) m = s->draw_ring[i];
  return m;
}
