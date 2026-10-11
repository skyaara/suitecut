#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
task_root="$PWD/.suitecut/obs-deps/root"
task_logs="$PWD/benchmarks/obs-cef/2026-10-09/logs"
mkdir -p "$task_logs" .suitecut/obs-deps/apt/{lists/partial,archives/partial} "$task_root" .suitecut/runtime .suitecut/cef-obs-comparison
printf 'deb https://deb.debian.org/debian trixie main\n' > .suitecut/obs-deps/apt/sources.list
task_apt=(-o Debug::NoLocking=true -o "Dir::Etc::sourcelist=$PWD/.suitecut/obs-deps/apt/sources.list" -o Dir::Etc::sourceparts=- -o "Dir::State::lists=$PWD/.suitecut/obs-deps/apt/lists" -o "Dir::Cache::archives=$PWD/.suitecut/obs-deps/apt/archives")
/usr/bin/apt-get "${task_apt[@]}" update > "$task_logs/reproduce-apt-update.log" 2>&1
/usr/bin/apt-get "${task_apt[@]}" --download-only -y install obs-studio xvfb xdotool libx11-dev libxrandr-dev libxext-dev libxrender-dev pulseaudio pulseaudio-utils > "$task_logs/reproduce-apt-download.log" 2>&1
for pkg in .suitecut/obs-deps/apt/archives/*.deb; do dpkg-deb -x "$pkg" "$task_root"; done
ln -sf /usr/lib/x86_64-linux-gnu/libX11.so.6 "$task_root/usr/lib/x86_64-linux-gnu/libX11.so"
python3 -m pip install cmake ninja psutil numpy matplotlib obsws-python
pnpm build > "$task_logs/reproduce-typescript-build.log" 2>&1
PLAYWRIGHT_BROWSERS_PATH="$PWD/.suitecut/playwright-browsers" pnpm exec playwright install chromium > "$task_logs/reproduce-chromium-install.log" 2>&1
task_archive="$PWD/.suitecut/cef-obs-comparison/cef-linux64.tar.bz2"
if [[ ! -f "$task_archive" ]]; then
  curl --fail --location --show-error 'https://cef-builds.spotifycdn.com/cef_binary_152.0.8%2Bg1ce985c%2Bchromium-152.0.7977.134_linux64_minimal.tar.bz2' -o "$task_archive"
fi
printf '%s  %s\n' 7f47eeb1c297099ffb9e12d0dea6a0d4eabd255135d114ff0ace8780bdb3feeb "$task_archive" | sha256sum -c -
mkdir -p .suitecut/cef-sdk
if [[ ! -f .suitecut/cef-sdk/cmake/FindCEF.cmake ]]; then tar -xjf "$task_archive" --strip-components=1 -C .suitecut/cef-sdk; fi
if [[ ! -d .suitecut/libyuv/.git ]]; then git init .suitecut/libyuv; fi
git -C .suitecut/libyuv fetch --depth=1 https://chromium.googlesource.com/libyuv/libyuv 7c85a3a0820fab29abb502c207d8b9a394e352cc
git -C .suitecut/libyuv checkout --detach FETCH_HEAD
test "$(git -C .suitecut/libyuv rev-parse HEAD)" = 7c85a3a0820fab29abb502c207d8b9a394e352cc
python3 -m cmake -S native/browser -B .suitecut/native-build -G Ninja -DCEF_ROOT="$PWD/.suitecut/cef-sdk" -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" -DPROJECT_ARCH=x86_64 -DCMAKE_BUILD_TYPE=Release -DX11_X11_INCLUDE_PATH="$task_root/usr/include" -DX11_X11_LIB=/usr/lib/x86_64-linux-gnu/libX11.so.6 -DX11_Xrandr_INCLUDE_PATH="$task_root/usr/include" -DX11_Xrandr_LIB=/usr/lib/x86_64-linux-gnu/libXrandr.so.2 -DCMAKE_EXE_LINKER_FLAGS="-L$task_root/usr/lib/x86_64-linux-gnu" > "$task_logs/reproduce-cef-configure.log" 2>&1
python3 -m cmake --build .suitecut/native-build --parallel 4 > "$task_logs/reproduce-cef-build.log" 2>&1
python3 benchmarks/obs-cef/setup-obs.py
printf 'Setup complete. Run start-runtime.sh, then follow README.md.\n'
