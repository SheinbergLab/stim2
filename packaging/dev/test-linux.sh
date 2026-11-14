#!/bin/sh
# Run stim2's tests on the Linux dev build, in the stim2-dev:trixie image:
# headless, on Mesa's software renderer (llvmpipe), with dlsh.zip and the
# stimdlls where a rig has them.  It checks a Linux build without a rig --
# not a GPU driver, vsync or flip timing, which need the rig.
#
#   packaging/dev/build-linux.sh        # first: the build under test
#   packaging/dev/test-linux.sh         # every test below
#   packaging/dev/test-linux.sh test_gazewin test_shaderpass
#
# Exits non-zero if any test fails, errors or times out.
set -e
here=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT:-$here/build-dev-trixie}
[ -x "$out/stim2" ] && [ -d "$out/stimdlls" ] || {
  echo "no dev build in $out: run packaging/dev/build-linux.sh first" >&2; exit 2; }
docker run --rm --platform linux/amd64 \
  -e TESTS="$*" -v "$here":/src:ro -v "$out":/out:ro \
  stim2-dev:trixie sh -c '
    # the build, laid out as the .deb installs it
    mkdir -p /usr/local/stim2 /work
    cp -r /out/. /usr/local/stim2/
    cp -r /src/config /usr/local/stim2/config
    # the tests write files beside themselves; /src is read-only
    cp -r /src/tests /src/shaders /src/modules /work/
    export LD_LIBRARY_PATH=/usr/local/lib
    [ -f /usr/local/dlsh/dlsh.zip ] || {
      echo "the image has no /usr/local/dlsh/dlsh.zip: rebuild it (packaging/dev/README.md)" >&2; exit 2; }
    tclsh=$(ls /usr/local/bin/tclsh9* | head -1)
    stim2="/usr/local/stim2/stim2 --headless -w 320 -h 180 --port-base 5610"

    # run inside stim2, after the config a rig starts it with
    inproc="test_frame_grab test_headless_step test_dynlist_numeric test_gazewin test_gazewin_step test_shaderpass test_shader_fragment_only"
    # plain tclsh clients of a running stim2: host msgport ?dsport?
    clients="test_gazewin_client test_ack_header"
    # plain tclsh, no stim2
    plain="test_stimess"
    [ -n "$TESTS" ] || TESTS="$inproc $clients $plain"

    fails=0
    report() {  # name rc logfile
      sum=$(grep -o "[0-9]* ok, [0-9]* FAIL\|[0-9]* passed, [0-9]* failed\|ALL PASS ([0-9]* checks)\|[0-9]* FAILURE(S)" "$3" | tail -1)
      if [ "$2" = 0 ]; then echo "PASS  $1  ($sum)"; else
        fails=$((fails + 1))
        [ "$2" = 124 ] && sum="timed out"
        echo "FAIL  $1  rc=$2  ($sum)"
        grep "FAIL \|TEST ERROR\|rror:" "$3" | grep -v "ok, [0-9]* FAIL" | cut -c1-200 | head -8 | sed "s/^/      /"
      fi
    }
    cd /work
    for t in $TESTS; do
      log=/work/$t.log
      case " $inproc " in *" $t "*)
        printf "source /usr/local/stim2/config/linux.cfg\nif {[catch {source /work/tests/%s.tcl} e]} {puts \"TEST ERROR: \$e\"; exit 3}\n" $t > /work/run.tcl
        rc=0; timeout 180 $stim2 -f /work/run.tcl > $log 2>&1 || rc=$?
        report $t $rc $log; continue;; esac
      case " $clients " in *" $t "*)
        printf "source /usr/local/stim2/config/linux.cfg\n" > /work/run.tcl
        $stim2 -f /work/run.tcl > /work/stim2.log 2>&1 &
        pid=$!; sleep 4
        rc=0; timeout 180 $tclsh /work/tests/$t.tcl localhost 5612 5611 > $log 2>&1 || rc=$?
        kill $pid 2>/dev/null; wait $pid 2>/dev/null
        report $t $rc $log; continue;; esac
      case " $plain " in *" $t "*)
        rc=0; timeout 180 $tclsh /work/tests/$t.tcl > $log 2>&1 || rc=$?
        report $t $rc $log; continue;; esac
      echo "FAIL  $t  (not a test this script knows)"; fails=$((fails + 1))
    done
    grep -ho "renderer: .*" /work/*.log | head -1
    [ $fails = 0 ] && echo "all passed" || echo "$fails failed"
    exit $fails'
