#!/bin/sh
set -eu
host_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir="$host_dir/ClawDock.app"
app="$app_dir/Contents/MacOS/ClawDock"
rebuild=false
if [ ! -x "$app" ] || [ "$host_dir/Info.plist" -nt "$app" ]; then rebuild=true; fi
for source in "$host_dir"/Sources/*.swift; do
    if [ "$source" -nt "$app" ]; then rebuild=true; fi
done
if [ "$rebuild" = true ]; then "$host_dir/build-mouse-bridge.sh"; fi
if [ "$#" -gt 0 ]; then
    echo '请在 ClawDock 菜单栏的“设备串口”菜单中选择设备。' >&2
    exit 1
fi
open "$app_dir"
echo 'ClawDock 已启动，请查看屏幕右上角的菜单栏图标。'
