/*
 * tlock_packet.h -- the CRS T-Lock digital-output packet (Bits++, Bits#,
 * Display++), as one row of RGB pixels.  Pure: no GL, no Tcl, so
 * tests/tlock_packet_check.cpp can check it without a display.
 * See stim2's docs/tlock.md.
 *
 * The device watches the video stream for an 8-pixel unlock key.  What
 * follows the key on that row is a list of (address, value) pairs, one
 * pixel in two, that load its digital-output memory:
 *
 *   pixel 0..7        the unlock key
 *   pixel 8           B = pairs - 1
 *   pixel 9           R = 6 (address), G = command, B = 2 (digital outputs)
 *   pixel 11          R = 7 (address), G = mask high byte, B = mask low byte
 *   pixel 13 + 2i     R = 8 + i (address), G = slot i high byte, B = low byte
 *
 * Each slot is the 16-bit output word for 100 microseconds of the frame,
 * slot 0 starting at the frame's start.  The packet takes effect on the
 * frame AFTER the one it is drawn in, and the device plays it again on
 * every frame until another packet replaces it.
 *
 * The layout is Psychtoolbox's BitsPlusDIO2Matrix.m, byte for byte at 248
 * slots (508 pixels).  Every pixel between and after those named is 0.
 */
#ifndef TLOCK_PACKET_H
#define TLOCK_PACKET_H

#include <vector>
#include <cstddef>
#include <cstdint>

namespace tlock {

constexpr int MAX_SLOTS = 248;          /* addresses 8..255 */
constexpr int SLOTS_PER_MS = 10;        /* a slot is 100 us */

inline int packet_width(int nslots) { return 12 + 2 * nslots; }

static const uint8_t UNLOCK_KEY[8][3] = {
  {  69,  33,  56 }, {  40, 230, 208 }, {  19, 190, 102 }, { 119,  84, 207 },
  {  52,  12, 192 }, { 233, 108, 172 }, {  41, 201,  80 }, { 183, 124, 221 }
};

/* The packet for `nslots` output words (slots[0..nslots-1]; a NULL slots
   is all `fill`), as packet_width(nslots) RGBA pixels, alpha 255.  Empty
   if nslots is out of range. */
inline std::vector<uint8_t> encode(const uint16_t *slots, int nslots,
                                   uint16_t mask, uint8_t command,
                                   uint16_t fill = 0)
{
  std::vector<uint8_t> px;
  if (nslots < 1 || nslots > MAX_SLOTS) return px;
  px.assign((std::size_t) packet_width(nslots) * 4, 0);
  auto set = [&](int i, int r, int g, int b) {
    px[(std::size_t) i * 4 + 0] = (uint8_t) r;
    px[(std::size_t) i * 4 + 1] = (uint8_t) g;
    px[(std::size_t) i * 4 + 2] = (uint8_t) b;
  };
  for (int i = 0; i < packet_width(nslots); i++) px[(std::size_t) i * 4 + 3] = 255;
  for (int i = 0; i < 8; i++) set(i, UNLOCK_KEY[i][0], UNLOCK_KEY[i][1], UNLOCK_KEY[i][2]);
  set(8, 0, 0, nslots + 1);                       /* pairs - 1 */
  set(9, 6, command, 2);
  set(11, 7, mask >> 8, mask & 255);
  for (int i = 0; i < nslots; i++) {
    uint16_t w = slots ? slots[i] : fill;
    set(13 + 2 * i, 8 + i, w >> 8, w & 255);
  }
  return px;
}

/* A pulse: `bits` from slot `on` for `dur` slots, `idle` in the others.
   The pulse is cut at the packet's end. */
inline std::vector<uint8_t> encode_pulse(int nslots, uint16_t bits, int on, int dur,
                                         uint16_t idle, uint16_t mask, uint8_t command)
{
  if (nslots < 1 || nslots > MAX_SLOTS) return std::vector<uint8_t>();
  uint16_t slots[MAX_SLOTS];
  for (int i = 0; i < nslots; i++)
    slots[i] = (i >= on && i < on + dur) ? bits : idle;
  return encode(slots, nslots, mask, command);
}

} /* namespace tlock */

#endif /* TLOCK_PACKET_H */
