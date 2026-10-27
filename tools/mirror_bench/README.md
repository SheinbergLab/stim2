# mirror_bench

Can a machine keep up as a live mirror of a rig, with stim2's scene drawn
again and streamed to a browser, alongside or instead of dserv?

For each size, rate and encoder, a headless stim2 plays a recorded session
in real time through `tools/replay/replay.tcl`, as a mirror would from dserv.
Meanwhile:

- every frame goes through the frame sink into an encoder writing MPEG-TS to
  `/dev/null`. `none` is readback only; `x264` is veryfast/zerolatency on
  the CPU; `vaapi` is the GPU's encoder (Intel Quick Sync, AMD VCN), with
  RGBA to NV12 done on the GPU when the driver allows it;
- `eye.tcl` sends synthetic eye datapoints, by default at 500 Hz, to stim2's
  dataserver port in dserv's line form. stimess dispatches them to a bound
  handler, which is the mirror's own eye subscription. Rig stim2s do not get
  eye data; `ess_control.html` does.

Each run reports:
- `frames` and `late`: frames that started before their deadline and
  finished after it;
- `busy` p50/p95/max: draw + readback + sink write per frame, which has to
  stay under the period;
- stim2's and the encoder's CPU (one core = 100%), and the whole machine's
  CPU;
- amdgpu's busy % (AMD only);
- eye datapoints dispatched out of those sent.

The same rows go to `results.csv`.

## On a candidate machine

It needs Docker and a GPU render node. Either build the image from a
directory holding `stim2/` and `dlsh/` (`docker/headless/Dockerfile`), or
copy one over (3 GB uncompressed):

    docker save stim2-headless | gzip > stim2-headless.tgz      # where it is built
    docker load < stim2-headless.tgz                            # on the candidate

Then, with a session file in the current directory:

    docker run --rm --device /dev/dri -v $PWD:/data \
      --entrypoint /src/stim2/tools/mirror_bench/mirror_bench.sh \
      stim2-headless /data/SESSION.ess -out /data/bench

| option | |
|---|---|
| `-trials A-B` | the part of the session to play (default `1-5`) |
| `-res "WxH ..."` | sizes (default `960x540 1280x720 1920x1080`) |
| `-fps "..."` | mirror rates (default `30 60`) |
| `-enc "..."` | `none x264 vaapi` (vaapi runs are skipped when no encoder works) |
| `-eye HZ` | synthetic eye rate (default 500; 0 turns it off) |
| `-out DIR` | logs per run and `results.csv` (default `/tmp/mirror_bench`) |

On Intel, `va-driver-all` brings the iHD driver. The probe tries
`-low_power 1`, which some Intel encoders need. `vainfo --display drm`
inside the image lists what the driver can do.

The files in `-out` belong to root, because the container runs as root.
Remove them through the container
(`docker run --rm -v $PWD:/data --entrypoint rm stim2-headless -rf /data/bench`)
or with sudo.
