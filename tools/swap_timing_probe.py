#!/usr/bin/env python3
"""Qualify a stim2 rig's swap-ack timing over the port-4610 Tcl interface.

Non-destructive: sends `redraw`, reads clocks, and (on stim2 >= 0.25.0)
queries `swapStats`.  Run it from any machine on the rig's network:

    python3 swap_timing_probe.py <host>

What good looks like (see the swap-sync audit for the full story):
  * sustained !redraw ack intervals quantized at the frame period
  * isolated-swap RTTs SPREAD ACROSS a full frame period -- that spread is
    the wait for scanout.  A flat, uniformly fast distribution means acks
    are submit-only and lead the photons (the pre-0.25.0 Wayland failure).
  * swapStats: lastflip/counter advance per swap; missed/discarded/timeouts
    stay 0 during a fullscreen kiosk session.
"""
import socket, sys, time, random, statistics

FRAMES_SUSTAINED = 300
FRAMES_ISOLATED = 200


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    s = socket.create_connection((host, 4610), timeout=5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    f = s.makefile("rwb")

    def cmd(c):
        f.write((c + "\n").encode())
        f.flush()
        return f.readline().decode(errors="replace").rstrip("\n")

    def stats(name, xs):
        xs = sorted(xs)
        n = len(xs)
        print(f"  {name}: n={n} med={statistics.median(xs):.3f} "
              f"p5={xs[int(.05 * n)]:.3f} p95={xs[int(.95 * n)]:.3f} "
              f"min={xs[0]:.3f} max={xs[-1]:.3f} ms")

    def hist(xs, lo, hi, nbins):
        w = (hi - lo) / nbins
        counts = [0] * nbins
        for x in xs:
            i = int((x - lo) / w)
            if 0 <= i < nbins:
                counts[i] += 1
        peak = max(counts) or 1
        for i, c in enumerate(counts):
            if c:
                print(f"    {lo + i * w:6.2f}-{lo + (i + 1) * w:6.2f} ms "
                      f"|{'#' * max(1, int(40 * c / peak))} {c}")

    print(f"host {host}: ping -> {cmd('ping')}")
    refresh = int(cmd("screen_set RefreshRate"))
    frame_ms = 1000.0 / refresh
    print(f"refresh {refresh} Hz ({frame_ms:.3f} ms/frame)")
    swapstats = cmd("catch {swapStats} ::_r; set ::_r")
    print(f"swapStats: {swapstats}")

    print(f"\nbaseline RTT (no swap), 100 reps")
    rtts = []
    for _ in range(100):
        t0 = time.perf_counter()
        cmd("set StimTimeF")
        rtts.append((time.perf_counter() - t0) * 1000)
    stats("plain RTT", rtts)

    print(f"\nsustained !redraw, {FRAMES_SUSTAINED} reps")
    acks = []
    for _ in range(FRAMES_SUSTAINED):
        cmd("!redraw")
        acks.append(time.perf_counter() * 1000)
    ivs = [b - a for a, b in zip(acks, acks[1:])]
    stats("ack interval", ivs)
    hit = sum(1 for x in ivs if abs(x - frame_ms) < 0.5)
    print(f"  within +-0.5 ms of frame period: {hit}/{len(ivs)}")

    print(f"\nisolated !redraw after 20-50 ms idle, {FRAMES_ISOLATED} reps")
    rtts = []
    for _ in range(FRAMES_ISOLATED):
        time.sleep(random.uniform(0.020, 0.050))
        t0 = time.perf_counter()
        cmd("!redraw")
        rtts.append((time.perf_counter() - t0) * 1000)
    stats("isolated RTT", rtts)
    hist(rtts, 0, 2 * frame_ms, 24)
    spread = sorted(rtts)[int(.95 * len(rtts))] - sorted(rtts)[int(.05 * len(rtts))]
    if spread > 0.6 * frame_ms:
        print(f"  p5-p95 spread {spread:.2f} ms ~ frame period: "
              "acks wait for scanout (GOOD)")
    else:
        print(f"  p5-p95 spread {spread:.2f} ms << frame period: "
              "acks look submit-only -- they lead the photons (BAD)")

    print(f"\nswapStats after runs: {cmd('catch {swapStats} ::_r; set ::_r')}")
    cmd("unset -nocomplain ::_r")
    s.close()


if __name__ == "__main__":
    main()
