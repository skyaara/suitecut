#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
task_root="$PWD/.suitecut/obs-deps/root"
task_logs="${RUNTIME_LOG_DIR:-$PWD/benchmarks/obs-cef/2026-10-09/logs}"
mkdir -p "$task_logs"
export DISPLAY=:99
export LD_LIBRARY_PATH="$task_root/usr/lib/x86_64-linux-gnu:$task_root/usr/lib/x86_64-linux-gnu/pulseaudio:$task_root/usr/lib/pulse-17.0+dfsg1/modules"
export PULSE_SERVER="unix:$PWD/.suitecut/runtime/pulse.sock"
export XDG_CONFIG_HOME="${OBS_CONFIG_ROOT:-$PWD/.suitecut/obs-profile}"
export QT_QPA_PLATFORM_PLUGIN_PATH="$task_root/usr/lib/x86_64-linux-gnu/qt6/plugins"
export QT_QPA_PLATFORM=xcb
task_pids=()
cleanup(){
  kill "${task_pids[@]}" 2>/dev/null || true
  for task_pid in "${task_pids[@]}"; do wait "$task_pid" 2>/dev/null || true; done
}
trap cleanup EXIT INT TERM
"$task_root/usr/bin/Xvfb" :99 -screen 0 3840x2300x24 -nolisten tcp -ac > "$task_logs/reproduce-xvfb.log" 2>&1 &
task_pids+=("$!")
"$task_root/usr/bin/pulseaudio" --daemonize=no --exit-idle-time=-1 --use-pid-file=no --dl-search-path="$task_root/usr/lib/pulse-17.0+dfsg1/modules" -n "--load=module-native-protocol-unix socket=$PWD/.suitecut/runtime/pulse.sock auth-anonymous=1" '--load=module-null-sink sink_name=benchmark rate=48000 channels=2' > "$task_logs/reproduce-pulse.log" 2>&1 &
task_pids+=("$!")
# Wait for the local X server before opening OBS.
for attempt in $(seq 1 40); do if "$task_root/usr/bin/xdotool" getdisplaygeometry >/dev/null 2>&1; then break; fi; sleep .25; done
"$task_root/usr/bin/obs" --disable-missing-files-check --disable-updater > "$task_logs/reproduce-obs.log" 2>&1 &
task_pids+=("$!")
printf 'Runtime PIDs: Xvfb=%s Pulse=%s OBS=%s\n' "${task_pids[0]}" "${task_pids[1]}" "${task_pids[2]}"
wait "${task_pids[2]}"
