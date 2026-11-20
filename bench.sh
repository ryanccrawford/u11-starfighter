#!/system/bin/sh
# Autoplay benchmark: per-scene FPS (+ PROF lines when PROFILE=1). usage: sh bench.sh [extra GLENV]
X="$*"
run() {  # level seconds window label
  GLENV="START_LEVEL=$1 AUTOPLAY=1 NOAUDIO=1 $X" sh /data/local/run-shooter.sh >/dev/null 2>&1
  sleep 3; for i in 1 2 3; do input tap 1200 60; sleep 1; done   # skip intro cutscene if any
  sleep $2
  F=$(grep '^FPS' /data/local/tmp/shooter.log | tail -$3 | awk '{s+=$2; if(min==""||$2<min)min=$2} END{if(NR)printf "avg=%.0f min=%d", s/NR, min}')
  echo "$4: $F"
  grep '^PROF' /data/local/tmp/shooter.log | tail -3
  grep -iE 'error|segm|abort' /data/local/tmp/shooter.log | head -3
}
run 1 22 10 "L1 combat (side)"
run 2 22 10 "L2 invaders (top-down)"
run 5 45 10 "L5 boss (side)"
screencap -p /sdcard/bench_last.png
pkill -x shooter
