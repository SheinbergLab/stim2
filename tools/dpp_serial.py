#!/usr/bin/env python3
"""
dpp_serial.py -- talk to a CRS Display++ (or Bits#) over its USB serial port,
to check stim2's tlock pass (docs/tlock.md) without a scope.

    dpp_serial.py [-p /dev/ttyACM0] info
    dpp_serial.py scan                 which of rows 0..7 holds a T-Lock key
    dpp_serial.py line ROW [NPIX]      one video row as the monitor received it
    dpp_serial.py status [SECONDS]     log the monitor's INPUTS; print changes
    dpp_serial.py video                back to Bits++ video mode
    dpp_serial.py raw '$Command'       send anything, print the reply

What the serial port can and cannot show:

  - `line` / `scan` ask the monitor for the pixels of a video row
    ($GetVideoLine).  If the row stim2 drew comes back with the unlock key
    and the packet intact, the display path is clean: nothing between
    stim2 and the monitor changed a byte.  The packet is decoded and
    printed.  NOTE: the monitor switches to its status screen to answer;
    `video` puts it back.
  - The monitor does NOT report its digital OUTPUTS over serial.  `status`
    logs its INPUTS (trigger in, DIN 0..9) with the monitor's own
    timestamps.  So to see a tlock pulse or toggle arrive, loop an output
    back to an input (trigger out to trigger in, or a DOUT pin to a DIN pin
    on the DB25) and run `status` while stim2 pulses.

The commands and reply formats are from PsychoPy's psychopy_crs/bits.py.
Standard library only.  Not yet run against a monitor.
"""
import argparse
import glob
import os
import select
import sys
import termios
import time

KEY = [(69, 33, 56), (40, 230, 208), (19, 190, 102), (119, 84, 207),
       (52, 12, 192), (233, 108, 172), (41, 201, 80), (183, 124, 221)]


class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = 0                                   # iflag: raw
        a[1] = 0                                   # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0                                   # lflag
        a[4] = a[5] = termios.B19200               # a CDC port ignores it
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def send(self, cmd):
        os.write(self.fd, cmd.encode() + b"\r")

    def read(self, seconds, until=None):
        """Everything that arrives in `seconds`; stops early once `until`
        (a function of the bytes so far) is true."""
        buf = b""
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try:
                    buf += os.read(self.fd, 65536)
                except BlockingIOError:
                    pass
                if until and until(buf):
                    break
        return buf

    def ask(self, cmd, seconds=0.3):
        self.send(cmd)
        return self.read(seconds).decode("latin-1").strip("\r\n\x00 ")


def find_port():
    c = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/serial/by-id/*CRS*") +
               glob.glob("/dev/serial/by-id/*Bits*") + glob.glob("/dev/serial/by-id/*Display*") +
               glob.glob("/dev/cu.usbmodem*"))
    # a by-id name and its ttyACM are one device: keep the first of each
    seen, uniq = set(), []
    for path in c:
        real = os.path.realpath(path)
        if real not in seen:
            seen.add(real)
            uniq.append(path)
    c = uniq
    if not c:
        sys.exit("no serial port found: name one with -p")
    if len(c) > 1:
        print("several ports (%s): using %s; name another with -p" % (" ".join(c), c[0]), file=sys.stderr)
    return c[0]


def quiet(p):
    p.send("$Stop")
    p.read(0.3)


def video_line(p, row, npix, timeout=5.0):
    """npix (r, g, b) tuples of video row `row`, or [] if the monitor did
    not answer in full."""
    for _ in range(5):
        p.read(0.05)
        p.send("$GetVideoLine=[%d, %d]" % (row, npix))
        raw = p.read(timeout, until=lambda b: b.count(b";") >= npix * 3 + 1).decode("latin-1")
        vals = raw.split(";")[1:-1]
        try:
            v = [int(x) for x in vals[:npix * 3]]
        except ValueError:
            continue
        if len(v) == npix * 3:
            return [tuple(v[i:i + 3]) for i in range(0, len(v), 3)]
    return []


def key_at(px):
    """Index of the unlock key in a row of pixels, or -1."""
    for i in range(0, len(px) - 7):
        if px[i:i + 8] == KEY:
            return i
    return -1


def decode(px, at):
    """Print the T-Lock digital-output packet that starts at pixel `at`."""
    p = px[at:]
    if len(p) < 13:
        print("  key found at pixel %d, but too few pixels read to decode" % at)
        return
    pairs = p[8][2] + 1
    print("  unlock key at pixel %d: intact" % at)
    print("  pixel 8: %s  -> %d address/value pairs (%d slots)" % (p[8], pairs, pairs - 2))
    print("  pixel 9: %s  -> address %d, command %d, group %d%s" %
          (p[9], p[9][0], p[9][1], p[9][2], "" if (p[9][0], p[9][2]) == (6, 2) else "   UNEXPECTED (want 6, x, 2)"))
    print("  pixel 11: %s -> address %d, mask 0x%04X%s" %
          (p[11], p[11][0], p[11][1] << 8 | p[11][2], "" if p[11][0] == 7 else "   UNEXPECTED (want address 7)"))
    slots, bad, odd = [], 0, 0
    i = 0
    while 13 + 2 * i < len(p) and i < pairs - 2:
        r, g, b = p[13 + 2 * i]
        if r != 8 + i:
            bad += 1
        slots.append(g << 8 | b)
        # the pixel between this slot and the next (none after the last)
        if i < pairs - 3 and 14 + 2 * i < len(p) and p[14 + 2 * i] != (0, 0, 0):
            odd += 1
        i += 1
    for q in (10, 12):
        if p[q] != (0, 0, 0):
            odd += 1
    print("  %d slots read; %d with a wrong address; %d in-between pixels not black" % (len(slots), bad, odd))
    # runs of equal words: the pulse as drawn
    runs, start = [], 0
    for j in range(1, len(slots) + 1):
        if j == len(slots) or slots[j] != slots[start]:
            runs.append((start, j - start, slots[start]))
            start = j
    for s, n, w in runs:
        print("    slots %3d..%3d (%5.1f ms from %4.1f ms): 0x%04X" % (s, s + n - 1, n / 10.0, s / 10.0, w))
    if bad == 0 and odd == 0:
        print("  the packet is intact: the display path changed nothing")
    else:
        print("  the packet is DAMAGED: look at dithering, gamma, scaling, colour range")


def cmd_info(p, a):
    quiet(p)
    for c in ("$ProductType", "$SerialNumber", "$FirmwareDate", "$VideoFrameRate"):
        print("%-16s %s" % (c, p.ask(c)))


def cmd_line(p, a):
    quiet(p)
    px = video_line(p, a.row, a.npix)
    if not px:
        sys.exit("no answer to $GetVideoLine for row %d" % a.row)
    print("row %d, %d pixels (the monitor is now on its status screen: `video` to go back)" % (a.row, len(px)))
    at = key_at(px)
    if at < 0:
        print("  no unlock key on this row.  First 16 pixels:")
        print("  " + " ".join("%d,%d,%d" % t for t in px[:16]))
        near = sum(1 for i in range(8) if i < len(px) and max(abs(px[i][c] - KEY[i][c]) for c in range(3)) <= 4)
        if near >= 6:
            print("  ...which is the key with small errors: something in the display path alters pixels")
    else:
        decode(px, at)
    if a.dump:
        print(" ".join("%d %d %d" % t for t in px))


def cmd_scan(p, a):
    quiet(p)
    found = False
    for row in range(0, 8):
        px = video_line(p, row, 24)
        if not px:
            print("row %d: no answer" % row)
            continue
        at = key_at(px)
        print("row %d: %s" % (row, "T-Lock key at pixel %d" % at if at >= 0 else
                              " ".join("%d,%d,%d" % t for t in px[:6]) + " ..."))
        found = found or at >= 0
    print("(the monitor is now on its status screen: `video` to go back)")
    if not found:
        print("no key on rows 0..7: is `tlock on` set, and was a frame drawn since?")


def cmd_status(p, a):
    quiet(p)
    p.send("$Start")
    raw = p.read(a.seconds).decode("latin-1")
    quiet(p)
    last, n, changes = None, 0, 0
    t0 = None
    for line in raw.split("\r"):
        v = line.strip("\n\x00 ").split(";")
        # "#sample" in PsychoPy's driver; this firmware (2015) echoes the
        # command's own "$" on its replies, so take either
        if len(v) < 14 or v[0].lstrip("#$").lower() != "sample":
            continue
        try:
            t = float(v[2])
            trig = int(float(v[3]))
            din = sum(int(float(v[4 + j])) << j for j in range(10))
        except ValueError:
            continue
        n += 1
        if t0 is None:
            t0 = t
        state = (trig, din)
        if state != last:
            if last is not None:
                changes += 1
            if changes <= a.max:
                print("t %10.4f s  trigIn %d  DIN 0x%03X" % (t - t0, trig, din))
            last = state
    print("%d status samples in %.1f s, %d changes of the inputs%s" %
          (n, a.seconds, changes, "" if changes <= a.max else " (first %d shown)" % a.max))
    if n == 0:
        print("no status lines parsed: the reply began %r" % raw[:300])


def cmd_video(p, a):
    quiet(p)
    p.send("$BitsPlusPlus")
    print(p.read(0.5).decode("latin-1").strip() or "sent $BitsPlusPlus")


def cmd_raw(p, a):
    print(repr(p.ask(a.command, a.seconds)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (default: the first /dev/ttyACM*)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info").set_defaults(fn=cmd_info)
    sub.add_parser("scan").set_defaults(fn=cmd_scan)
    s = sub.add_parser("line")
    s.add_argument("row", type=int)
    s.add_argument("npix", type=int, nargs="?", default=508)
    s.add_argument("--dump", action="store_true", help="also print every pixel, R G B")
    s.set_defaults(fn=cmd_line)
    s = sub.add_parser("status")
    s.add_argument("seconds", type=float, nargs="?", default=2.0)
    s.add_argument("--max", type=int, default=40, help="changes to print")
    s.set_defaults(fn=cmd_status)
    sub.add_parser("video").set_defaults(fn=cmd_video)
    s = sub.add_parser("raw")
    s.add_argument("command")
    s.add_argument("seconds", type=float, nargs="?", default=0.5)
    s.set_defaults(fn=cmd_raw)
    a = ap.parse_args()
    p = Port(a.port or find_port())
    a.fn(p, a)


if __name__ == "__main__":
    main()
