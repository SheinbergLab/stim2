#!/bin/bash
# mirror_bench.sh -- can this machine keep up as a live mirror?
#
# For each size x rate x encoder, a headless stim2 plays a recorded session
# in real time (tools/replay/replay.tcl, paced), as a live mirror would from
# dserv, while
#   - every frame goes through the frame sink into an encoder writing an
#     MPEG-TS stream to /dev/null (none: readback only; x264; vaapi: the
#     GPU's encoder, Intel Quick Sync / AMD VCN), and
#   - a synthetic eye tracker sends datapoints at -eye Hz to stim2's
#     dataserver port, dispatched to a bound handler (the mirror's own eye
#     subscription; rig stim2s do not get eye data).
# It reports stim2's pacing (headlessStats: frames, late, busy ms), the CPU
# taken by stim2 and the encoder, the machine's total CPU (which includes
# the replay standing in for dserv), the AMD GPU's busy % where the driver
# exposes it, and eye datapoints sent and dispatched.
#
# Inside the stim2-headless image (docker/headless), with the GPU:
#   docker run --rm --device /dev/dri -v $PWD:/data \
#     --entrypoint /src/stim2/tools/mirror_bench/mirror_bench.sh \
#     stim2-headless /data/SESSION.ess
#
# Options: -trials A-B (default 1-5)  -res "960x540 1280x720 1920x1080"
#          -fps "30 60"  -enc "none x264 vaapi"  -eye HZ (default 500, 0 off)
#          -out DIR (default /tmp/mirror_bench: logs and results.csv)
set -u
here=$(cd "$(dirname "$0")" && pwd)
replay=$here/../replay/replay.tcl
file=${1:?usage: mirror_bench.sh SESSION.ess [options]}; shift
trials=1-5; res="960x540 1280x720 1920x1080"; fpss="30 60"; encs="none x264 vaapi"
eye=500; out=/tmp/mirror_bench
while [ $# -gt 0 ]; do
  case $1 in
    -trials) trials=$2; shift 2 ;;
    -res) res=$2; shift 2 ;;
    -fps) fpss=$2; shift 2 ;;
    -enc) encs=$2; shift 2 ;;
    -eye) eye=$2; shift 2 ;;
    -out) out=$2; shift 2 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done
mkdir -p "$out"
base=5610; msgport=$((base + 2)); dsport=$((base + 1))
dev=$(ls /dev/dri/renderD* 2>/dev/null | head -1)
tck=$(getconf CLK_TCK)
msg() { dlsh "$here/msg.tcl" $msgport "$1"; }

# --- the machine -----------------------------------------------------------
cpu=$(awk -F: '/model name/ {sub(/^ /,"",$2); print $2; exit}' /proc/cpuinfo)
ncpu=$(nproc)
echo "machine: $cpu, $ncpu threads; render node ${dev:-none}"

# --- which VAAPI encoder setting works here (Intel often wants low power) ---
vaapi_opts=""
if [ -n "$dev" ]; then
  for lp in "" "-low_power 1"; do
    if ffmpeg -hide_banner -loglevel error -vaapi_device "$dev" -f lavfi \
         -i testsrc=size=640x360:rate=30 -t 1 -vf format=nv12,hwupload \
         -c:v h264_vaapi $lp -f null - 2>/dev/null; then vaapi_opts="ok $lp"; break; fi
  done
fi
if [ -n "$vaapi_opts" ]; then echo "vaapi: h264_vaapi works${vaapi_opts#ok}"
else echo "vaapi: no working h264_vaapi here; vaapi runs are skipped"; fi

sink_target() {   # enc w h fps
  case $1 in
    none) echo /dev/null ;;
    x264) echo "|ffmpeg -loglevel error -f rawvideo -pix_fmt rgba -s $2x$3 -r $4 -i - -c:v libx264 -preset veryfast -tune zerolatency -pix_fmt yuv420p -b:v 4M -f mpegts /dev/null" ;;
    vaapi) echo "|ffmpeg -loglevel error -vaapi_device $dev -f rawvideo -pix_fmt rgba -s $2x$3 -r $4 -i - -vf format=nv12,hwupload -c:v h264_vaapi ${vaapi_opts#ok} -b:v 4M -f mpegts /dev/null" ;;
  esac
}
proc_ticks() {    # pids... -> utime+stime summed, in clock ticks
  local t=0 p
  for p in "$@"; do
    [ -r /proc/$p/stat ] && t=$((t + $(awk '{print $14 + $15}' /proc/$p/stat)))
  done
  echo $t
}
sys_ticks() { awk '/^cpu / {print $2+$3+$4+$5+$6+$7+$8, $5+$6}' /proc/stat; }  # total idle
gpu_busy_file=$(ls /sys/class/drm/card*/device/gpu_busy_percent 2>/dev/null | head -1)

csv=$out/results.csv
echo "size,fps,enc,frames,late,late_pct,worst_late_ms,busy_p50_ms,busy_p95_ms,busy_max_ms,period_ms,stim2_cpu_pct,enc_cpu_pct,system_cpu_pct,gpu_busy_pct,eye_hz,eye_sent,eye_dispatched" > "$csv"
printf "\n%-10s %3s %-5s | %6s %5s %6s | %6s %6s %6s | %6s %6s %6s | %5s | %s\n" \
  size fps enc frames late late% p50ms p95ms maxms stim2% enc% sys% gpu% "eye dispatched/sent"

for wh in $res; do
  w=${wh%x*}; h=${wh#*x}
  for fps in $fpss; do
    for enc in $encs; do
      [ "$enc" = vaapi ] && [ -z "$vaapi_opts" ] && continue
      tag=${wh}_${fps}_${enc}; log=$out/$tag
      # stim2, as a Linux rig starts it
      /usr/local/stim2/stim2 --headless -w $w -h $h -r $fps --port-base $base \
        -f /usr/local/stim2/config/linux.cfg > $log.stim2.log 2>&1 &
      spid=$!
      for i in $(seq 50); do msg 1 >/dev/null 2>&1 && break; sleep 0.2; done
      msg "frameSink open [list $(sink_target $enc $w $h $fps)]" > /dev/null || { echo "$tag: sink failed"; kill $spid; continue; }
      # the session, paced, in the background; setup is done once the
      # geometry line is out
      dlsh "$replay" "$file" -trials $trials > $log.replay.log 2>&1 &
      rpid=$!
      for i in $(seq 100); do grep -q "geometry:\|screen  no" $log.replay.log && break; sleep 0.1; done
      sleep 0.3
      # the mirror's own eye subscription (after replay's prelude, which
      # clears bindings)
      if [ "$eye" -gt 0 ]; then
        msg "proc ::bench_eye {x y} { set ::bench_eye_xy [list \$x \$y] }; stim::bind bench/eye {x y} -mode event -handler ::bench_eye" > /dev/null
        rm -f $log.fifo; mkfifo $log.fifo
        dlsh "$here/eye.tcl" $dsport $eye bench/eye < $log.fifo > $log.eye.log 2>&1 &
        epid=$!
        exec 4> $log.fifo
      fi
      msg "headlessStats reset" > /dev/null
      encpid=$(pgrep -n -f "ffmpeg.*rawvideo" || true)
      s0=$(proc_ticks $spid); e0=$(proc_ticks $encpid)
      read st0 si0 < <(sys_ticks); t0=$(date +%s.%N)
      gsum=0; gn=0
      while kill -0 $rpid 2>/dev/null; do
        if [ -n "$gpu_busy_file" ]; then gsum=$((gsum + $(cat $gpu_busy_file))); gn=$((gn + 1)); fi
        sleep 0.2
      done
      s1=$(proc_ticks $spid); e1=$(proc_ticks $encpid)
      read st1 si1 < <(sys_ticks); t1=$(date +%s.%N)
      stats=$(msg "headlessStats")
      arrived=0; sent=0
      if [ "$eye" -gt 0 ]; then
        exec 4>&-; wait $epid 2>/dev/null
        sleep 0.2      # let the last lines dispatch
        arrived=$(msg "stim::count bench/eye")
        sent=$(awk '/^eye:/ {print $2}' $log.eye.log)
      fi
      msg "frameSink close" > /dev/null
      kill $spid 2>/dev/null; wait $spid 2>/dev/null
      wait $rpid 2>/dev/null
      dt=$(awk -v a=$t0 -v b=$t1 'BEGIN {print b - a}')
      get() { echo "$stats" | tr ' ' '\n' | awk -v k=$1 'p {print; exit} $0 == k {p = 1}'; }
      frames=$(get frames); late=$(get late)
      row=$(awk -v f=$frames -v l=$late -v wl=$(get worst_late_ms) -v p50=$(get busy_p50_ms) \
        -v p95=$(get busy_p95_ms) -v mx=$(get busy_max_ms) -v per=$(get period_ms) \
        -v s=$((s1 - s0)) -v e=$((e1 - e0)) -v tck=$tck -v dt=$dt \
        -v st=$((st1 - st0)) -v si=$((si1 - si0)) -v gs=$gsum -v gn=$gn -v eye=$eye \
        -v se=${sent:-0} -v ar=${arrived:-0} \
        'BEGIN {
           lp = f ? 100 * l / f : 0
           printf "%d,%d,%.2f,%.1f,%.2f,%.2f,%.1f,%.2f,%.0f,%.0f,%.0f,%s,%d,%d,%d",
             f, l, lp, wl, p50, p95, mx, per,
             100 * s / tck / dt, 100 * e / tck / dt,
             st ? 100 * (st - si) / st : 0, gn ? sprintf("%.0f", gs / gn) : "", eye, se, ar }')
      echo "$wh,$fps,$enc,$row" >> "$csv"
      IFS=, read f l lp wl p50 p95 mx per sc ec sy gp eh se ar <<< "$row"
      printf "%-10s %3s %-5s | %6s %5s %5s%% | %6s %6s %6s | %5s%% %5s%% %5s%% | %4s%s | %s/%s\n" \
        $wh $fps $enc $f $l $lp $p50 $p95 $mx $sc $ec $sy "${gp:--}" "${gp:+%}" $ar $se
    done
  done
done
echo
echo "late: frames that started before their deadline and finished after it."
echo "busy: draw + readback + sink write per frame; it must stay under period_ms ($csv)."
echo "stim2%/enc%: one core = 100%. sys%: the whole machine. gpu%: amdgpu's busy % (AMD only)."
