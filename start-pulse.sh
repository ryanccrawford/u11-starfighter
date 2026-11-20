#!/system/bin/sh
# Start Termux PulseAudio (AAudio sink, TCP on 127.0.0.1:4713 for the chroot) if it isn't already running.
# Runs as the Termux app uid via run-as (Termux is a debuggable build), so audio goes through Android's AAudio.
cat > /data/local/tmp/pulse-termux.sh <<'S'
export PREFIX=/data/data/com.termux/files/usr HOME=/data/data/com.termux/files/home
export PATH=$PREFIX/bin:$PATH TMPDIR=$PREFIX/tmp LD_LIBRARY_PATH=$PREFIX/lib
# NOTE: never probe with `pactl info` here: libpulse autospawns a *default* daemon (no TCP module),
# which then blocks our configured start. `pulseaudio --check` does not autospawn.
TCP="module-native-protocol-tcp auth-ip-acl=127.0.0.1 auth-anonymous=1 port=4713"
if pulseaudio --check 2>/dev/null; then
  pactl list short modules | grep -q native-protocol-tcp && exit 0
  pulseaudio -k; sleep 0.5   # an autospawned default daemon (no TCP, exits when idle): replace it with ours
fi
pulseaudio --start --exit-idle-time=-1 -n --load="module-aaudio-sink" \
  --load="$TCP" \
  --load="module-native-protocol-unix auth-anonymous=1"
S
chmod 644 /data/local/tmp/pulse-termux.sh
run-as com.termux sh /data/local/tmp/pulse-termux.sh 2>/dev/null || su 10217 -c "sh /data/local/tmp/pulse-termux.sh"
