#!/usr/bin/env python3
"""
mp_pulsed_verify.py - Compare the forward design (mp_pulsed_design_dg)
with per-frame motionpatch logs recorded during one or two trials.
Produces a multi-panel verification figure suitable for methods
documentation.

Two modes of use:

    Single recording (any mode):
        --design  D.dgz --target T.dgz --bg B.dgz

    Both modes overlaid (the comparison the methods figure wants):
        --design  D.dgz \
        --continuous CT.dgz CB.dgz \
        --pulsed     PT.dgz PB.dgz

When both --continuous and --pulsed are supplied the script overlays
the two recordings on the same axes with distinct markers/colors,
so a single figure shows that the same parameters yield (1) a flat
coherence baseline at base_coh in continuous mode, and (2) the
trough-matched gating in pulsed mode.

Time alignment is approximate: each recording's first captured
frame is shifted to t=0. With ~16 ms frame intervals that's good to
about one frame.
"""

import argparse
from pathlib import Path

import dgread
import matplotlib.pyplot as plt
import numpy as np


def scalar(d, key):
    return float(np.asarray(d[key]).reshape(-1)[0])


def string_scalar(d, key):
    if key not in d:
        return ""
    arr = np.asarray(d[key]).reshape(-1)
    if len(arr) == 0:
        return ""
    v = arr[0]
    return v.decode() if isinstance(v, bytes) else str(v)


def rel_deg(theta_rad, ref_rad):
    """Angle(s) relative to a reference, wrapped to [-180, 180] degrees.
    Used to plot direction relative to the trial's seed θ₀ so the seed
    pulses sit near 0 and the probe sits at ±90 (or off-axis)."""
    d = np.asarray(theta_rad) - ref_rad
    return np.degrees(np.arctan2(np.sin(d), np.cos(d)))


def load_recording(target_path, bg_path, patch_dva):
    """Load a (target, bg) recording pair into a flat dict of arrays
    aligned so that t=0 corresponds to the moment of drop, not to
    the first captured frame. Logging starts a few frames BEFORE drop
    (the record action calls motionpatch_logBegin then mp_pulsed_trigger
    drop in sequence), so naive t = stim_time - stim_time[0] would
    leave the recording phase-shifted vs the design by however many
    pre-drop frames got captured. We detect the drop by the first
    frame where the mask offset starts changing -- pre-drop the mask
    is held at the trajectory start, post-drop it begins translating.
    Speeds are converted from patch-units/sec to dva/sec for direct
    comparison with the design vectors."""
    T = dgread.dgread(target_path)
    B = dgread.dgread(bg_path)
    t_t_ms = np.asarray(T['stim_time_ms']).astype(float)
    b_t_ms = np.asarray(B['stim_time_ms']).astype(float)
    mox = np.asarray(T['mask_offset_x'])
    moy = np.asarray(T['mask_offset_y'])
    dt_ms = np.asarray(T['dt']).astype(float) * 1000.0

    # Align display t=0 with the moment of drop, which corresponds to
    # the design's play_t origin. mp_pulsed_update increments play_t
    # by dt before evaluating the envelope, so the first captured
    # frame's coherence reflects play_t = dt[0]. Subtracting dt[0]
    # from the first captured stim_time gives the moment of drop --
    # i.e. the recording's display t equals the design's play_t to
    # within sub-frame precision.
    drop_idx = 0
    t0_ms = float(t_t_ms[0]) - float(dt_ms[0])

    # Filter out pre-drop frames. The patch is held at speed=0 during
    # setup until the prescript starts running, so any captured frame
    # with inside speed ~0 is a transient before the trial took over.
    # Post-drop the minimum inside speed is the surround floor (>=
    # surround_speed_dva), so a small threshold cleanly separates the
    # two regimes without dropping legitimate trough samples.
    speed_arr = np.asarray(T['speed']) * patch_dva
    keep = speed_arr > 0.5      # dva/sec

    # Direction channels. dir_cmd = per-frame commanded patch direction.
    #
    # dir_realized = the INDEPENDENT realized motion direction, recovered
    # from the dots' frame-to-frame displacement (dot_x/dot_y). Note that
    # `dot_theta` in the log is stored RELATIVE to the commanded direction
    # (coherent dots = 0), so a circular mean of dot_theta would trivially
    # reproduce the command — not an independent check. Displacement is
    # the genuine measurement: coherent dots physically translate in the
    # commanded direction (~speed·dt per frame), while respawns (lifetime
    # turnover) jump O(patch) and are rejected by a global magnitude
    # cutoff. Frames with too few surviving coherent displacements (troughs,
    # 0%-coherence probes) are left NaN — direction is only carried during
    # coherent pulses.
    dir_cmd = np.asarray(T['direction'])[keep] if 'direction' in T else None
    if ('dot_x' in T) and ('dot_y' in T) and ('dot_coherent' in T):
        X = np.asarray(T['dot_x']); Y = np.asarray(T['dot_y'])
        C = np.asarray(T['dot_coherent']) > 0
        dX = X[1:] - X[:-1]; dY = Y[1:] - Y[:-1]    # (nframes-1, ndots)
        both = C[1:] & C[:-1]                       # coherent across the pair
        mag  = np.hypot(dX, dY)
        valid_mag = mag[both & (mag > 0)]
        thr = 5.0 * float(np.median(valid_mag)) if valid_mag.size else np.inf
        use = both & (mag > 0) & (mag < thr)        # drop respawn jumps
        ang = np.arctan2(dY, dX)
        ss = np.where(use, np.sin(ang), 0.0).sum(axis=1)
        cs = np.where(use, np.cos(ang), 0.0).sum(axis=1)
        nuse = use.sum(axis=1)
        realized = np.arctan2(ss, cs)
        realized[nuse < 5] = np.nan
        realized = np.concatenate([realized, [np.nan]])   # align to nframes
        dir_realized = realized[keep]
        dir_ncoh     = C.sum(axis=1)[keep]
    else:
        dir_realized = None
        dir_ncoh     = None

    return {
        't':            ((t_t_ms - t0_ms) / 1000.0)[keep],
        'coh':          np.asarray(T['coherence'])[keep],
        'speed':        speed_arr[keep],
        'life':         np.asarray(T['lifetime_s'])[keep],
        'mox_dva':      (mox * patch_dva)[keep],
        'moy_dva':      (moy * patch_dva)[keep],
        'b_t':          ((b_t_ms - t0_ms) / 1000.0)[keep],
        'b_speed':      (np.asarray(B['speed']) * patch_dva)[keep],
        'b_life':       np.asarray(B['lifetime_s'])[keep],
        'dir_cmd':      dir_cmd,
        'dir_realized': dir_realized,
        'dir_ncoh':     dir_ncoh,
        'drop_idx':     drop_idx,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    # --design is shared by --continuous/--pulsed/--target+--bg (same
    # parameters, only the recording differs). For --peak/--trough,
    # each condition has its own design dg because the bounce phase
    # (and therefore bounce_t) differs between them; pass a 3-tuple
    # of (design, target, bg) for those cases.
    p.add_argument('--design',
                   help='design dg shared by --continuous/--pulsed (smooth curves)')
    p.add_argument('--target', help='single-recording inside-mask log')
    p.add_argument('--bg',     help='single-recording surround log')
    p.add_argument('--continuous', nargs=2,
                   metavar=('TARGET', 'BG'),
                   help='continuous-mode (target, bg) dgz pair')
    p.add_argument('--pulsed', nargs=2,
                   metavar=('TARGET', 'BG'),
                   help='pulsed-mode (target, bg) dgz pair')
    p.add_argument('--peak', nargs=3,
                   metavar=('DESIGN', 'TARGET', 'BG'),
                   help='peak-aligned bounce condition (design, target, bg)')
    p.add_argument('--trough', nargs=3,
                   metavar=('DESIGN', 'TARGET', 'BG'),
                   help='trough-aligned bounce condition (design, target, bg)')
    p.add_argument('--out', default='./mp_pulsed_verify.png')
    args = p.parse_args()

    # Pick the master design for smooth-curve plotting. Priority:
    # explicit --design > --peak's design > --trough's design.
    if args.design is not None:
        master_design_path = args.design
    elif args.peak is not None:
        master_design_path = args.peak[0]
    elif args.trough is not None:
        master_design_path = args.trough[0]
    else:
        p.error('provide --design (or one of --peak/--trough with its own design)')

    if not Path(master_design_path).exists():
        raise FileNotFoundError(f"design not found at {master_design_path}")

    D = dgread.dgread(master_design_path)

    d_t      = np.asarray(D['t'])
    d_x      = np.asarray(D['x'])
    d_y      = np.asarray(D['y'])
    d_coh_p  = np.asarray(D['coh_pulsed'])
    d_coh_c  = np.asarray(D['coh_continuous'])
    d_sp_p   = np.asarray(D['speed_pulsed'])
    d_sp_c   = np.asarray(D['speed_continuous'])
    d_li_p   = np.asarray(D['life_pulsed'])
    d_li_c   = np.asarray(D['life_continuous'])
    d_cumE_p = np.asarray(D['cum_energy_pulsed'])
    d_cumE_c = np.asarray(D['cum_energy_continuous'])
    d_tiles  = np.asarray(D['tile_times'])

    # Direction reference (mp_pulsed designs): per-frame commanded
    # direction + the trial's seed θ₀, used by the direction panel.
    d_dir  = np.asarray(D['direction']) if 'direction' in D else None
    theta0 = scalar(D, 'seed_direction_rad') if 'seed_direction_rad' in D else 0.0

    patch_dva    = scalar(D, 'patch_size_dva')
    surround_dva = scalar(D, 'surround_speed_dva')
    bg_lifetime  = scalar(D, 'bg_lifetime')
    energy_ratio = scalar(D, 'energy_ratio')
    base_coh     = scalar(D, 'base_coh')
    n_snapshots  = int(scalar(D, 'n_snapshots'))
    sigma_s      = scalar(D, 'sigma_s')
    preset       = string_scalar(D, 'preset')
    # Bounce metadata (added when a synthetic bounce is configured;
    # missing in older recordings that predate the bounce extension).
    bounce_phase = string_scalar(D, 'bounce_phase') or 'none'
    bounce_t     = scalar(D, 'bounce_t')           if 'bounce_t'           in D else 0.0
    bounce_x     = scalar(D, 'bounce_x')           if 'bounce_x'           in D else 0.0
    bounce_y     = scalar(D, 'bounce_y')           if 'bounce_y'           in D else 0.0
    bounce_angle = scalar(D, 'bounce_angle_deg')   if 'bounce_angle_deg'   in D else 0.0
    bounce_rest  = scalar(D, 'bounce_restitution') if 'bounce_restitution' in D else 1.0
    has_bounce   = bounce_phase != 'none' and bounce_t > 0.0

    # Collect recordings: an ordered list of (label, color, marker,
    # data, bounce_t, bounce_phase). For --continuous/--pulsed/--target
    # the bounce metadata comes from the master design dg. For --peak
    # /--trough each condition brings its own design dg, so the
    # bounce_t / bounce_phase are pulled from there -- this is what
    # lets us draw two distinct bounce-time vertical lines on the same
    # figure when comparing peak vs trough conditions.
    recordings = []

    def _add_simple(label, color, marker, two_args):
        recordings.append((label, color, marker,
                           load_recording(two_args[0], two_args[1], patch_dva),
                           bounce_t, bounce_phase))

    def _add_with_design(label, color, marker, three_args):
        cond_design = dgread.dgread(three_args[0])
        cond_bt    = scalar(cond_design, 'bounce_t')   if 'bounce_t'    in cond_design else 0.0
        cond_bp    = string_scalar(cond_design, 'bounce_phase') or 'none'
        cond_data  = load_recording(three_args[1], three_args[2], patch_dva)
        recordings.append((label, color, marker, cond_data, cond_bt, cond_bp))

    if args.continuous:
        _add_simple('continuous', '#666666', 's', args.continuous)
    if args.pulsed:
        _add_simple('pulsed',     '#1b7837', 'o', args.pulsed)
    if args.peak:
        _add_with_design('peak',   '#1b3eb7', 'o', args.peak)
    if args.trough:
        _add_with_design('trough', '#cc0033', '^', args.trough)
    if not recordings and args.target and args.bg:
        _add_simple('recorded', '#1b7837', 'o', [args.target, args.bg])
    if not recordings:
        p.error('provide either (--target --bg) or one/both of '
                '(--continuous T B) (--pulsed T B) (--peak D T B) (--trough D T B)')

    # Add a direction panel (F) when direction data is available — either
    # the design's commanded trace or any recording's per-dot channels.
    want_dir = (d_dir is not None) or any(
        (r.get('dir_cmd') is not None) or (r.get('dir_realized') is not None)
        for (_l, _c, _m, r, _bt, _bp) in recordings)
    n_panels = 6 if want_dir else 5
    fig, axes = plt.subplots(n_panels, 1,
                             figsize=(13, 14 if n_panels == 5 else 16.5),
                             sharex=True)
    # Build a list of distinct (bounce_t, color, label) entries so the
    # vertical-line annotation can show one per condition, color-matched
    # to the recording markers.
    bounce_marks = []
    seen_bts = set()
    for (label, color, marker, r, bt, bp) in recordings:
        if bt > 0.0 and bp != 'none' and bt not in seen_bts:
            seen_bts.add(bt)
            bounce_marks.append((bt, color, f'{bp} bounce'))
    if not bounce_marks and has_bounce:
        bounce_marks.append((bounce_t, '#cc0033', f'{bounce_phase} bounce'))

    line1 = (f'mp_pulsed verification  |  preset={preset}  |  '
             f'N={n_snapshots}, σ={sigma_s*1000:.0f} ms, '
             f'base_coh={base_coh:.2f}  |  '
             f'pulsed energy_ratio = {energy_ratio:.3f}')
    if bounce_marks:
        parts = [f'{lbl} @ t={bt:.3f} s' for (bt, _, lbl) in bounce_marks]
        line2 = 'bounce conditions: ' + '   |   '.join(parts)
        title = line1 + '\n' + line2
    else:
        title = line1
    fig.suptitle(title, fontsize=11)

    def mark_pulses(ax, alpha=0.10):
        for ti in d_tiles:
            ax.axvline(ti, color='C2', alpha=alpha, linewidth=0.7)
        # One vertical dashed line per bounce condition, color-matched
        # to the corresponding recording's markers so the reader can
        # tell at a glance which bounce belongs to which condition.
        for (bt, bcolor, _bl) in bounce_marks:
            ax.axvline(bt, color=bcolor, linestyle='--',
                       linewidth=1.4, alpha=0.85)

    # A: mask trajectory. Mode-independent shape, but if multiple
    # recordings are given we plot each one's mask offsets in its
    # own color so the reader can see how trial-to-trial position
    # randomization shifted the absolute trajectory.
    ax = axes[0]
    ax.plot(d_t, d_x, color='C0', label='design x', alpha=0.5, linewidth=1)
    ax.plot(d_t, d_y, color='C1', label='design y', alpha=0.5, linewidth=1)
    for (label, color, marker, r, bt, bp) in recordings:
        ax.plot(r['t'], r['mox_dva'], '.', markersize=2, color=color,
                alpha=0.5, label=f'rec x ({label})')
        ax.plot(r['t'], r['moy_dva'], 'x', markersize=2, color=color,
                alpha=0.5, label=f'rec y ({label})')
    mark_pulses(ax)
    ax.set_ylabel('mask offset (dva)')
    ax.legend(loc='upper right', fontsize='x-small', ncol=2)
    ax.set_title('A. mask trajectory continuous; '
                 'object segmentation gated by motion-energy (panels B–D)',
                 fontsize=10)

    # B: coherence. Both designs as background; recorded points overlaid
    # per recording.
    ax = axes[1]
    ax.plot(d_t, d_coh_c, '--', color='gray', alpha=0.7,
            label='continuous design')
    ax.plot(d_t, d_coh_p, color='C2', linewidth=2, label='pulsed design')
    for (label, color, marker, r, bt, bp) in recordings:
        ax.plot(r['t'], r['coh'], marker, markersize=3, color=color,
                alpha=0.7, label=f'recorded {label}', linestyle='')
    mark_pulses(ax)
    ax.set_ylabel('coherence (motion energy)')
    ax.set_ylim(-0.05, max(1.05, base_coh * 1.05))
    ax.legend(loc='center right', fontsize='x-small')
    ax.set_title('B. motion-energy gating (the manipulation)',
                 fontsize=10)

    # C: effective speed. Same overlay treatment.
    ax = axes[2]
    ax.plot(d_t, d_sp_c, '--', color='gray', alpha=0.7,
            label='continuous design')
    ax.plot(d_t, d_sp_p, color='C3', linewidth=2, label='pulsed design')
    for (label, color, marker, r, bt, bp) in recordings:
        ax.plot(r['t'], r['speed'], marker, markersize=3, color=color,
                alpha=0.7, label=f'recorded {label}', linestyle='')
    ax.axhline(surround_dva, color='C7', linestyle=':', alpha=0.6,
               label=f'surround = {surround_dva:g} dva/s')
    mark_pulses(ax)
    ax.set_ylabel('inside dot speed (dva/sec)')
    ax.legend(loc='upper left', fontsize='x-small', ncol=2)
    ax.set_title('C. trough-matched speed: between pulses, inside dots '
                 'run at surround speed', fontsize=10)

    # D: effective lifetime.
    ax = axes[3]
    ax.plot(d_t, d_li_c, '--', color='gray', alpha=0.7,
            label='continuous design')
    ax.plot(d_t, d_li_p, color='C4', linewidth=2, label='pulsed design')
    for (label, color, marker, r, bt, bp) in recordings:
        ax.plot(r['t'], r['life'], marker, markersize=3, color=color,
                alpha=0.7, label=f'recorded {label}', linestyle='')
    ax.axhline(bg_lifetime, color='C7', linestyle=':', alpha=0.6,
               label=f'surround = {bg_lifetime:g} s')
    mark_pulses(ax)
    ax.set_ylabel('inside dot lifetime (sec)')
    ax.legend(loc='center right', fontsize='x-small')
    ax.set_title('D. trough-matched lifetime: between pulses, inside '
                 'turnover matches surround', fontsize=10)

    # E: cumulative motion-energy. Design only.
    ax = axes[4]
    ax.plot(d_t, d_cumE_c, '--', color='gray', alpha=0.7,
            label='continuous')
    ax.plot(d_t, d_cumE_p, color='C5', linewidth=2,
            label=f'pulsed (final = {energy_ratio*100:.1f}% of continuous)')
    mark_pulses(ax)
    ax.set_ylabel('cumulative motion-energy (s)')
    ax.legend(loc='upper left', fontsize='x-small')
    ax.set_title('E. delivered motion-energy budget vs continuous',
                 fontsize=10)

    # F: direction, relative to the trial's seed θ₀. Shows the commanded
    # schedule (design + recorded) and the REALIZED motion direction
    # recovered from dot displacement (independent of the command). Seeds
    # should wobble near 0 (within ±seed_jitter); the probe sits at +90
    # (predicted) / −90 (violation) / off-axis (uniform_other). Realized
    # points appear only during coherent pulses — troughs and
    # 0%-coherence probes leave gaps, confirming direction is carried
    # only when coherence is up.
    if want_dir:
        ax = axes[5]
        for yref in (0.0, 90.0, -90.0):
            ax.axhline(yref, color='C7', linestyle=':', alpha=0.45,
                       linewidth=0.8)
        if d_dir is not None:
            ax.plot(d_t, rel_deg(d_dir, theta0), color='C8', linewidth=1.2,
                    alpha=0.7, label='design (commanded)')
        for (label, color, marker, r, bt, bp) in recordings:
            if r.get('dir_cmd') is not None:
                ax.plot(r['t'], rel_deg(r['dir_cmd'], theta0), '-',
                        color=color, alpha=0.35, linewidth=0.8,
                        label=f'cmd ({label})')
            if r.get('dir_realized') is not None:
                ax.plot(r['t'], rel_deg(r['dir_realized'], theta0), marker,
                        markersize=3, color=color, alpha=0.85,
                        label=f'realized (dot displacement, {label})')
        mark_pulses(ax)
        ax.set_ylabel('direction re seed (deg)')
        ax.set_ylim(-180, 180)
        ax.set_yticks([-180, -90, 0, 90, 180])
        ax.legend(loc='upper right', fontsize='x-small', ncol=2)
        ax.set_title('F. direction: commanded vs realized (dot displacement) '
                     '— seeds wobble ~0; probe at ±90 or off-axis',
                     fontsize=10)

    # x-label only on the bottom panel (sharex).
    axes[-1].set_xlabel('time (s)')

    fig.tight_layout(rect=[0, 0, 1, 0.97])
    fig.savefig(args.out, dpi=150)
    print(f'wrote {args.out}')

    # ---- numerical verification per recording -------------------------
    # Trough check verifies the manipulation's FLOOR rather than an
    # arbitrary band: select frames in the bottom decile of recorded
    # coherence -- those are the deepest part of each trough, where the
    # driver's interpolation puts inside-dot stats closest to surround.
    # Skip recordings whose coherence is essentially flat (continuous
    # mode), since there's no trough to verify.
    bottom_frac = 0.10
    for (label, _, _, r, _bt, _bp) in recordings:
        coh_range = float(np.ptp(r['coh']))
        if coh_range < 0.1:
            print(f'\n[{label}] coherence is flat (range={coh_range:.3f}); '
                  f'no trough to verify (expected for continuous mode).')
            continue
        trough_thresh = float(np.quantile(r['coh'], bottom_frac))
        trough_mask = r['coh'] <= trough_thresh
        n_trough = int(trough_mask.sum())
        if n_trough < 5:
            print(f'\n[{label}] WARNING: only {n_trough} bottom-decile '
                  f'frames; verification may be noisy.')
        if n_trough > 0:
            inside_speed_med = float(np.median(r['speed'][trough_mask]))
            inside_life_med  = float(np.median(r['life'][trough_mask]))
            if len(r['b_speed']) == len(r['speed']):
                bg_speed_med = float(np.median(r['b_speed'][trough_mask]))
                bg_life_med  = float(np.median(r['b_life'][trough_mask]))
            else:
                bg_speed_med = float(np.median(r['b_speed']))
                bg_life_med  = float(np.median(r['b_life']))
            print(f'\n[{label}] trough-floor verification '
                  f'({n_trough} frames in bottom {bottom_frac*100:.0f}% of '
                  f'coherence, threshold = {trough_thresh:.3f}):')
            print(f'  inside  median speed    = {inside_speed_med:.3f} dva/s '
                  f'(target: {surround_dva:.3f})')
            print(f'  surround median speed   = {bg_speed_med:.3f} dva/s')
            print(f'  inside  median lifetime = {inside_life_med:.4f} s '
                  f'(target: {bg_lifetime:.4f})')
            print(f'  surround median lifetime= {bg_life_med:.4f} s')


if __name__ == '__main__':
    main()
