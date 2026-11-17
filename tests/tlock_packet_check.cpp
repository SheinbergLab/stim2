/*
 * tlock_packet_check.cpp -- unit check of the CRS T-Lock digital-output
 * packet (stimdlls/src/tlock_packet.h) against Psychtoolbox's
 * BitsPlusDIO2Matrix.m, the reference encoder.  Needs no display, no Tcl,
 * no GL:
 *
 *     cmake --build build --target tlock_packet_check && ./build/tlock_packet_check
 *     ctest --test-dir build -R tlock
 *
 * Prints OK/FAIL lines and a summary; exits non-zero on any failure.
 */
#include "tlock_packet.h"

#include <cstdio>

static int passes = 0, fails = 0;

static void check(const char *name, bool cond)
{
  if (cond) passes++; else fails++;
  printf("%s %s\n", cond ? "OK  " : "FAIL", name);
}

/* BitsPlusDIO2Matrix(mask, data, command), written out again from the
   Matlab: a 1 x 508 x 3 uint8 matrix, 1-based pixel indices there */
static std::vector<uint8_t> reference(uint16_t mask, const uint16_t *data, uint8_t command)
{
  std::vector<uint8_t> m(508 * 3, 0);
  static const uint8_t key[3][8] = {
    {  69,  40,  19, 119,  52, 233,  41, 183 },
    {  33, 230, 190,  84,  12, 108, 201, 124 },
    {  56, 208, 102, 207, 192, 172,  80, 221 }
  };
  auto at = [&](int pix1, int chan1) -> uint8_t & { return m[(size_t) (pix1 - 1) * 3 + (chan1 - 1)]; };
  for (int p = 1; p <= 8; p++) for (int c = 1; c <= 3; c++) at(p, c) = key[c - 1][p - 1];
  at(9, 3) = 249;                 /* length of data packet = number + 1 */
  at(10, 3) = 2;                  /* the digital output group */
  at(10, 2) = command;
  at(10, 1) = 6;
  at(12, 3) = mask & 255;
  at(12, 2) = mask >> 8;
  at(12, 1) = 7;
  for (int i = 0; i < 248; i++) {
    at(14 + 2 * i, 3) = data[i] & 255;
    at(14 + 2 * i, 2) = (data[i] & 0xff00) >> 8;
    at(14 + 2 * i, 1) = (uint8_t) (8 + i);
  }
  return m;
}

static bool same_rgb(const std::vector<uint8_t> &rgba, const std::vector<uint8_t> &rgb)
{
  if (rgba.size() / 4 != rgb.size() / 3) return false;
  for (size_t i = 0; i < rgb.size() / 3; i++)
    for (int c = 0; c < 3; c++)
      if (rgba[i * 4 + c] != rgb[i * 3 + c]) return false;
  return true;
}

int main()
{
  using namespace tlock;

  uint16_t data[248];
  for (int i = 0; i < 248; i++) data[i] = (uint16_t) (i * 263 + 5);   /* both bytes vary */

  std::vector<uint8_t> p = encode(data, 248, 0xA55A, 3);
  check("248 slots: 508 pixels", p.size() == 508 * 4 && packet_width(248) == 508);
  check("248 slots: the reference packet, byte for byte", same_rgb(p, reference(0xA55A, data, 3)));
  bool opaque = true;
  for (size_t i = 3; i < p.size(); i += 4) if (p[i] != 255) opaque = false;
  check("alpha is 255 throughout", opaque);

  /* the pulse the module draws: bits from `on` for `dur` slots */
  uint16_t want[248];
  for (int i = 0; i < 248; i++) want[i] = (i >= 20 && i < 30) ? 0x0400 : 0;
  check("pulse: slots 20..29 carry the bits",
        same_rgb(encode_pulse(248, 0x0400, 20, 10, 0, 0xFFFF, 0), reference(0xFFFF, want, 0)));
  for (int i = 0; i < 248; i++) want[i] = i >= 240 ? 0x0001 : 0x8000;
  check("pulse: cut at the packet's end, idle word elsewhere",
        same_rgb(encode_pulse(248, 0x0001, 240, 100, 0x8000, 0xFFFF, 0), reference(0xFFFF, want, 0)));
  for (int i = 0; i < 248; i++) want[i] = 0x0102;
  check("no slots given: all the fill word",
        same_rgb(encode(nullptr, 248, 0xFFFF, 0, 0x0102), reference(0xFFFF, want, 0)));

  /* a shorter packet (84 slots: a 120 Hz frame) */
  std::vector<uint8_t> s = encode_pulse(84, 0x0400, 0, 10, 0, 0xFFFF, 0);
  check("84 slots: 180 pixels", s.size() == 180 * 4);
  check("84 slots: pairs - 1 = 85", s[8 * 4 + 2] == 85 && s[8 * 4 + 0] == 0 && s[8 * 4 + 1] == 0);
  check("84 slots: last slot at address 91, pixel 179", s[179 * 4 + 0] == 91);
  check("84 slots: slot 0 is the pulse", s[13 * 4 + 0] == 8 && s[13 * 4 + 1] == 0x04 && s[13 * 4 + 2] == 0x00);
  check("84 slots: slot 10 is idle", s[33 * 4 + 0] == 18 && s[33 * 4 + 1] == 0 && s[33 * 4 + 2] == 0);
  check("the pixels between pairs are black", s[10 * 4] == 0 && s[12 * 4] == 0 && s[14 * 4] == 0 && s[14 * 4 + 2] == 0);

  check("0 slots refused", encode(nullptr, 0, 0xFFFF, 0).empty());
  check("249 slots refused", encode(nullptr, 249, 0xFFFF, 0).empty() && encode_pulse(249, 1, 0, 1, 0, 0xFFFF, 0).empty());

  printf("%d ok, %d FAIL\n", passes, fails);
  return fails ? 1 : 0;
}
