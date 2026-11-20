#!/usr/bin/env python3
"""
dpp_serial.py -- talk to a CRS Display++ (or Bits#) over its USB serial port,
to check stim2's tlock pass (docs/tlock.md) without a scope.

    dpp_serial.py [-p /dev/ttyACM0] info
    dpp_serial.py scan                 which of rows 0..7 holds a T-Lock key
    dpp_serial.py line ROW [NPIX]      one video row as the monitor received it
    dpp_serial.py status [SECONDS]     log the monitor's INPUTS; print changes
    dpp_serial.py sweep                every code 0..255 through the display path
    dpp_serial.py video                back to Bits++ video mode
    dpp_serial.py raw '$Command'       send anything, print the reply

ROW is stim2's row, as `tlock set -row` takes it: 0 is the top of the
screen.  The monitor numbers its video lines from 1 ($GetVideoLine for line
0 gets no answer; stim2's row 2 is the monitor's line 3), so the tool adds 1.

What the serial port can and cannot show:

  - `line` / `scan` ask the monitor for the pixels of a video row
    ($GetVideoLine).  If the row stim2 drew comes back with the unlock key
    and the packet intact, the display path is clean: nothing between
    stim2 and the monitor changed a byte.  The packet is decoded and
    printed.  NOTE: the monitor switches to its status screen to answer;
    `video` puts it back.
  - `sweep` drives stim2 too (its message port, localhost:4612 by default):
    it arms a T-Lock pulse whose slot pixels carry the code under test,
    G = v and B = 255 - v, for every v, reads the row back each time and
    reports every code that arrived changed, per channel.  The slot
    pixels' red bytes are their addresses, 8 up to 7 + slots, so red is
    covered from 8 up.  A code that comes back as several values at
    different x positions is dithering.  stim2's tlock state is saved and
    restored; if something else changes it mid-sweep (an ESS system load
    runs `postpass reset`), the sweep stops and leaves it alone.  Run it
    with the rig idle: it takes a few minutes.  Exit status 0: every code
    exact; 1: codes altered; 2: the sweep could not finish.
  - The monitor does NOT report its digital OUTPUTS over serial.  `status`
    logs its INPUTS (trigger in, DIN 0..9) with the monitor's own
    timestamps.  So to see a tlock pulse or toggle arrive, loop an output
    back to an input (trigger out to trigger in, or a DOUT pin to a DIN pin
    on the DB25) and run `status` while stim2 pulses.

The commands and reply formats are from PsychoPy's psychopy_crs/bits.py.
Standard library only.  Run against a Display++ (firmware 24/06/2015).
"""
import argparse
import collections
import glob
import os
import re
import select
import socket
import struct
import sys
import termios
import time

KEY = [(69, 33, 56), (40, 230, 208), (19, 190, 102), (119, 84, 207),
       (52, 12, 192), (233, 108, 172), (41, 201, 80), (183, 124, 221)]

# $GetVideoLine counts lines from 1 at the top; stim2 counts rows from 0.
LINE_BASE = 1


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
    """npix (r, g, b) tuples of stim2 row `row` (0 = top), or [] if the
    monitor did not answer in full."""
    for _ in range(5):
        p.read(0.05)
        p.send("$GetVideoLine=[%d, %d]" % (row + LINE_BASE, npix))
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


def near_key_at(px, tol=4, need=6):
    """Index of something close to the unlock key -- at least `need` of its
    8 pixels within `tol` of the key in every channel -- or -1.  A key that
    arrives like this was drawn, then altered on the way (dithering, a
    gamma ramp that is not quite identity, limited-range RGB)."""
    for i in range(0, len(px) - 7):
        near = sum(1 for k in range(8)
                   if max(abs(px[i + k][c] - KEY[k][c]) for c in range(3)) <= tol)
        if near >= need:
            return i
    return -1


def key_errors(px, at):
    """(key pixel index, sent, received) for each key pixel that differs."""
    return [(k, KEY[k], px[at + k]) for k in range(8) if px[at + k] != KEY[k]]


def decode(px, at, intact=True):
    """Print the T-Lock digital-output packet that starts at pixel `at`."""
    p = px[at:]
    if len(p) < 13:
        print("  key found at pixel %d, but too few pixels read to decode" % at)
        return
    pairs = p[8][2] + 1
    if intact:
        print("  unlock key at pixel %d: intact" % at)
    else:
        print("  unlock key at pixel %d: DAMAGED, the monitor will not unlock" % at)
        for k, sent, got in key_errors(px, at):
            print("    key pixel %d: drew %d,%d,%d, received %d,%d,%d" % ((k,) + sent + got))
        print("  the rest, decoded as if the key were intact:")
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
    if intact and bad == 0 and odd == 0:
        print("  the packet is intact: the display path changed nothing")
    else:
        print("  the packet is DAMAGED: look at dithering, gamma, scaling, colour range")


def cmd_info(p, a):
    quiet(p)
    for c in ("$ProductType", "$SerialNumber", "$FirmwareDate", "$VideoFrameRate"):
        print("%-16s %s" % (c, p.ask(c)))


def cmd_line(p, a):
    quiet(p)
    if a.row < 0:
        sys.exit("ROW is stim2's row, from 0 at the top")
    px = video_line(p, a.row, a.npix)
    if not px:
        sys.exit("no answer to $GetVideoLine for row %d (monitor line %d)" % (a.row, a.row + LINE_BASE))
    print("row %d (monitor line %d), %d pixels (the monitor is now on its status screen: `video` to go back)"
          % (a.row, a.row + LINE_BASE, len(px)))
    at = key_at(px)
    if at >= 0:
        decode(px, at)
    elif near_key_at(px) >= 0:
        print("  the key arrived with small errors: something in the display path alters pixels")
        decode(px, near_key_at(px), intact=False)
    else:
        print("  no unlock key on this row.  First 16 pixels:")
        print("  " + " ".join("%d,%d,%d" % t for t in px[:16]))
    if a.dump:
        print(" ".join("%d %d %d" % t for t in px))


def cmd_scan(p, a):
    quiet(p)
    found = damaged = False
    for row in range(0, 8):
        px = video_line(p, row, 24)
        if not px:
            print("row %d: no answer" % row)
            continue
        at, near = key_at(px), near_key_at(px)
        if at >= 0:
            print("row %d: T-Lock key at pixel %d" % (row, at))
        elif near >= 0:
            bad = key_errors(px, near)
            print("row %d: T-Lock key at pixel %d, DAMAGED (%d of 8 key pixels altered)" % (row, near, len(bad)))
        else:
            print("row %d: %s ..." % (row, " ".join("%d,%d,%d" % t for t in px[:6])))
        found = found or at >= 0
        damaged = damaged or (at < 0 and near >= 0)
    print("(the monitor is now on its status screen: `video` to go back)")
    if damaged:
        print("a key that arrives altered was drawn but changed on the way: `line ROW` shows how;"
              " look at dithering, gamma, scaling, colour range")
    elif not found:
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


class Stim2:
    """stim2's framed message port: 4-byte big-endian length, then text."""

    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10)

    def __call__(self, cmd):
        b = cmd.encode()
        self.s.sendall(struct.pack(">I", len(b)) + b)
        n = struct.unpack(">I", self._read(4))[0]
        r = self._read(n).decode("latin-1")
        if r.startswith("!TCL_ERROR"):
            raise RuntimeError("stim2: %s: %s" % (cmd, r))
        return r

    def _read(self, n):
        d = b""
        while len(d) < n:
            x = self.s.recv(n - len(d))
            if not x:
                raise RuntimeError("stim2 closed the connection")
            d += x
        return d


def tlock_state(stim):
    """(on, toggle bits, row, x, slots) from `tlock status` and `tlock set`."""
    st = stim("tlock status")
    on = int(re.match(r"on (\d+)", st).group(1))
    toggle = int(re.search(r"(?:^| )toggle (\d+)", st).group(1))
    s = dict(re.findall(r"-(\w+) (\S+)", stim("tlock set")))
    return on, toggle, int(s["row"]), int(s["x"]), int(s["slots"])


def cmd_sweep(p, a):
    try:
        stim = Stim2(a.host, a.stim_port)
        on0, toggle0, row, x, slots = tlock_state(stim)
    except (OSError, RuntimeError) as e:
        print("cannot reach stim2's tlock at %s:%d: %s" % (a.host, a.stim_port, e))
        return 2
    npix = x + 12 + 2 * slots
    tol = 4                       # how far a code may move and still count as "arrived"
    vs = list(range(0, 256, a.step))
    if vs[-1] != 255:
        vs.append(255)
    seen = {c: collections.defaultdict(collections.Counter) for c in "RGB"}
    print("sweeping %d codes through row %d (monitor line %d), %d slots, %.1f s settle"
          % (len(vs), row, row + LINE_BASE, slots, a.settle))
    print("the monitor goes to its status screen; it is put back in video mode after")
    result, why = 0, ""
    restore = True
    quiet(p)
    try:
        stim("tlock toggle 0")
        stim("tlock on")
        for n, v in enumerate(vs):
            g, b = v, 255 - v
            stim("tlock pulse 0x%04X -on 0 -dur %.1f -frames always" % (g << 8 | b, slots / 10.0))
            deadline = time.time() + a.settle + 3.0
            time.sleep(a.settle)
            while True:
                px = video_line(p, row, npix)
                at = near_key_at(px[x:]) if px else -1
                if at >= 0:
                    pk = px[x + at:]
                    sl = [pk[13 + 2 * i] for i in range(slots) if 13 + 2 * i < len(pk)]
                    arrived = sum(1 for (_, rg, rb) in sl if abs(rg - g) <= tol and abs(rb - b) <= tol)
                    if arrived * 2 > len(sl):
                        break
                if time.time() > deadline:
                    break
                time.sleep(0.25)
            if at < 0:
                result, why = 2, "no T-Lock packet on the row at code %d" % v
                break
            if arrived * 2 <= len(sl):
                result, why = 2, "the pulse for code %d never showed on the row" % v
                break
            for i, (rr, rg, rb) in enumerate(sl):
                seen["R"][8 + i][rr] += 1
                seen["G"][g][rg] += 1
                seen["B"][b][rb] += 1
            if n % 16 == 15:
                o, t = tlock_state(stim)[:2]
                if (o, t) != (1, 0):
                    result, why = 2, ("stim2's tlock changed under the sweep (on %d, toggle %d): "
                                      "a system load? left as it is now" % (o, t))
                    restore = False
                    break
                print("  %d of %d codes" % (n + 1, len(vs)), file=sys.stderr)
    except (OSError, RuntimeError) as e:
        result, why = 2, str(e)
    finally:
        if restore:
            # a system load between checks shows first as a missing packet:
            # whatever set tlock since then owns it now
            try:
                o, t = tlock_state(stim)[:2]
                if (o, t) != (1, 0):
                    restore = False
                    print("stim2's tlock changed under the sweep (on %d, toggle %d): left as it is now" % (o, t))
            except (OSError, RuntimeError):
                pass
        if restore:
            try:
                stim("tlock stop")
                stim("tlock toggle %d" % toggle0)
                stim("tlock on" if on0 else "tlock off")
            except (OSError, RuntimeError) as e:
                print("could not restore tlock (on %d, toggle 0x%04X): %s" % (on0, toggle0, e))
        quiet(p)
        p.send("$BitsPlusPlus")
    if result == 2:
        print("sweep stopped: %s" % why)
        return 2
    altered = dithered = 0
    for c in "RGB":
        for code in sorted(seen[c]):
            got = seen[c][code]
            if set(got) != {code}:
                altered += 1
                dithered += len(got) > 1
                print("  %s %3d -> %s" % (c, code, ", ".join(
                    "%d x%d" % (k, m) for k, m in sorted(got.items()))))
    rcodes = sorted(seen["R"])
    span = "R %d..%d, G and B %s" % (rcodes[0], rcodes[-1],
                                     "0..255" if a.step == 1 else "every %dth code" % a.step)
    if altered == 0:
        print("every code arrived exactly (%s, at %d x positions each)" % (span, slots))
        return 0
    print("%d codes altered (%s); %d of them came back as more than one value at different"
          " x positions: dithering" % (altered, span, dithered))
    return 1


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
    s = sub.add_parser("sweep")
    s.add_argument("--host", default="127.0.0.1", help="stim2's host (default: this one)")
    s.add_argument("--stim-port", type=int, default=4612, help="stim2's message port")
    s.add_argument("--settle", type=float, default=0.5, help="seconds from a pulse to its read")
    s.add_argument("--step", type=int, default=1, help="test every Nth code (quicker, less complete)")
    s.set_defaults(fn=cmd_sweep)
    sub.add_parser("video").set_defaults(fn=cmd_video)
    s = sub.add_parser("raw")
    s.add_argument("command")
    s.add_argument("seconds", type=float, nargs="?", default=0.5)
    s.set_defaults(fn=cmd_raw)
    a = ap.parse_args()
    p = Port(a.port or find_port())
    rc = a.fn(p, a)
    sys.exit(rc if isinstance(rc, int) else 0)


if __name__ == "__main__":
    main()
