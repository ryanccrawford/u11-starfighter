#!/system/bin/sh
# U11 Starfighter (3D) one-shot launcher: X11 + panfork/DRI3 + GPU clock pin + PulseAudio wait, then the game.
# usage: [GPU=panfork|virgl|soft] [DRI3=1|0] [GPU_MAX=1|0] [AUDIO=0] [GLENV="VAR=.."] run-shooter.sh
GPU=${GPU:-panfork}
x11_ready() {
  [ -S /data/data/com.termux/files/usr/tmp/.X11-unix/X0 ] || return 1
  /data/local/linux-enter.sh -c 'DISPLAY=:0 xwd -root -silent >/dev/null 2>&1'
}
if ! x11_ready; then /data/local/start-x11.sh >/dev/null 2>&1; fi
am start -n com.termux.x11/.MainActivity >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8 9 10; do
  x11_ready && break
  sleep 0.5
done
if ! x11_ready; then
  echo 'LAUNCHER x11=FAILED display=:0' >>/data/local/tmp/pulse-status.log
  exit 1
fi
pkill -x hello 2>/dev/null; pkill -x shooter 2>/dev/null; pkill -x demo3d 2>/dev/null; sleep 0.5
if [ "${GPU_MAX:-1}" = 1 ]; then /data/local/gpu-max.sh >/dev/null; else /data/local/gpu-max.sh restore >/dev/null 2>&1; fi
if [ "${AUDIO:-1}" = 1 ]; then
  # pulse-wait. Health = a real protocol round-trip from the chroot to the explicit TCP server, with a timeout.
  # (Explicit --server never autospawns; never run a bare Termux `pactl`, which autospawns an exit-idle default daemon.)
  # A wedged daemon can still hold :4713 (listen backlog full, clients hang) and pass `pulseaudio --check`,
  # so on failure kill every Termux pulseaudio and start one clean configured instance.
  pa_ok() { /data/local/linux-enter.sh -c "timeout 2 pactl --server=tcp:127.0.0.1:4713 info" >/dev/null 2>&1; }
  PA=FAILED
  for i in 1 2 3; do
    pa_ok && { PA=ok; break; }
    pkill -u 10217 -x pulseaudio 2>/dev/null; sleep 0.5; pkill -9 -u 10217 -x pulseaudio 2>/dev/null
    sh /data/local/start-pulse.sh >/dev/null 2>&1; sleep 1
  done
  echo "LAUNCHER pulse=$PA try=$i daemons=$(pgrep -u 10217 -x pulseaudio | wc -l)" >/data/local/tmp/pulse-status.log
fi
E="DISPLAY=:0 PULSE_SERVER=tcp:127.0.0.1:4713"
if [ "$GPU" = panfork ]; then L=/opt/panfork/lib/aarch64-linux-gnu; E="$E LD_LIBRARY_PATH=$L LIBGL_DRIVERS_PATH=$L/dri MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 PAN_KBASE_GL_DRI3=${DRI3:-1}"
elif [ "$GPU" = virgl ]; then /data/local/start-virgl.sh >/dev/null; E="$E GALLIUM_DRIVER=virpipe MESA_GL_VERSION_OVERRIDE=4.0"
else E="$E LIBGL_ALWAYS_SOFTWARE=1"; fi
/data/local/linux-enter.sh -c "cd /root/shooter; env $E $GLENV ${PIN:-taskset -c 6,7} ./shooter" >/data/local/tmp/shooter.log 2>&1 &
( sleep 4; grep -m1 "Backend:" /data/local/tmp/shooter.log >>/data/local/tmp/pulse-status.log ) &
